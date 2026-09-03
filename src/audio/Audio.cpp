#include "audio/Audio.h"

#include <algorithm>
#include <cmath>

namespace siglus {

bool AudioSystem::Init(int rate, int channels, int bufferFrames) {
  SDL_AudioSpec want{};
  want.freq     = rate;
  want.format   = AUDIO_S16SYS;
  want.channels = static_cast<Uint8>(channels);
  want.samples  = static_cast<Uint16>(bufferFrames);
  want.callback = &AudioSystem::Callback;
  want.userdata = this;

  SDL_AudioSpec got{};
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
  if (dev_ == 0) {
    SIGLUS_LOGE("打开音频设备失败: %s", SDL_GetError());
    return false;
  }
  specChannels_ = got.channels;
  specRate_     = got.freq;
  ses_.resize(8);
  SDL_PauseAudioDevice(dev_, 0);
  SIGLUS_LOGI("音频就绪: %dHz %dch", specRate_, specChannels_);
  return true;
}

void AudioSystem::Shutdown() {
  if (dev_) {
    SDL_CloseAudioDevice(dev_);
    dev_ = 0;
  }
}

void AudioSystem::SetLoader(MediaLoader loader) { loader_ = std::move(loader); }

bool AudioSystem::DecodeToTrack(const std::string& name, Track& t, bool loop, uint32_t loopStart,
                                uint32_t loopLen) {
  if (!loader_) {
    SIGLUS_LOGW("AudioSystem: 未设置 MediaLoader");
    return false;
  }
  std::vector<uint8_t> file;
  if (!loader_(name, file) || file.empty()) {
    SIGLUS_LOGW("音频资源不存在: %s", name.c_str());
    return false;
  }
  IAudioDecoder* dec = AudioRegistry::Get().Create(name);
  if (!dec) {
    SIGLUS_LOGW("没有 %s 对应的音频解码器", name.c_str());
    return false;
  }
  PcmFormat fmt;
  if (!dec->Open(file.data(), file.size(), fmt)) {
    SIGLUS_LOGW("音频解码失败: %s", name.c_str());
    return false;
  }
  if (fmt.rate != specRate_) {
    SIGLUS_LOGW("采样率不匹配（%d != %d），暂不做重采样，播放速度会异常: %s",
                fmt.rate, specRate_, name.c_str());
  }

  const uint32_t total = dec->TotalFrames();
  t.pcm.clear();
  t.pcm.reserve(static_cast<size_t>(total) * fmt.channels);
  std::vector<int16_t> chunk(static_cast<size_t>(4096) * fmt.channels);
  uint32_t gotTotal = 0;
  for (;;) {
    uint32_t got = dec->Decode(chunk.data(), 4096);
    if (got == 0) break;
    t.pcm.insert(t.pcm.end(), chunk.begin(),
                 chunk.begin() + static_cast<long>(got) * fmt.channels);
    gotTotal += got;
    if (gotTotal >= total) break;
  }

  t.total     = static_cast<uint32_t>(t.pcm.size() / static_cast<size_t>(fmt.channels));
  t.channels  = fmt.channels;
  t.pos       = 0;
  t.loop      = loop;
  t.loopStart = dec->LoopStartFrames() ? dec->LoopStartFrames() : loopStart;
  uint32_t lenFrames = dec->LoopLengthFrames() ? dec->LoopLengthFrames() : loopLen;
  t.loopEnd = (lenFrames > 0 && t.loopStart + lenFrames <= t.total) ? t.loopStart + lenFrames
                                                                    : t.total;
  if (t.loopEnd <= t.loopStart) t.loopEnd = t.total;
  t.playing   = true;
  t.volume    = 1.f;
  t.targetVol = 1.f;
  t.fadeRate  = 0.f;
  SIGLUS_LOGI("音频载入: %s (%u 帧, loop %u-%u)", name.c_str(), t.total, t.loopStart, t.loopEnd);
  return t.total > 0;
}

bool AudioSystem::PlayBgm(const std::string& name, bool loop, uint32_t loopStartFrame,
                          uint32_t loopLengthFrames) {
  Track t;
  if (!DecodeToTrack(name, t, loop, loopStartFrame, loopLengthFrames)) return false;
  if (dev_) SDL_LockAudioDevice(dev_);
  bgm_ = std::move(t);
  if (dev_) SDL_UnlockAudioDevice(dev_);
  return true;
}

void AudioSystem::StopBgm(float fadeMs) {
  if (!bgm_.playing) return;
  if (fadeMs <= 0.f) {
    if (dev_) SDL_LockAudioDevice(dev_);
    bgm_.playing = false;
    if (dev_) SDL_UnlockAudioDevice(dev_);
    return;
  }
  bgm_.targetVol = 0.f;
  bgm_.fadeRate  = 1.f / (fadeMs / 1000.f);
}

bool AudioSystem::PlaySe(const std::string& name, int channel) {
  if (channel < 0 || channel >= static_cast<int>(ses_.size())) channel = 0;
  Track t;
  if (!DecodeToTrack(name, t, false, 0, 0)) return false;
  if (dev_) SDL_LockAudioDevice(dev_);
  ses_[static_cast<size_t>(channel)] = std::move(t);
  if (dev_) SDL_UnlockAudioDevice(dev_);
  return true;
}

bool AudioSystem::PlayVoice(const std::string& name) {
  Track t;
  if (!DecodeToTrack(name, t, false, 0, 0)) return false;
  if (dev_) SDL_LockAudioDevice(dev_);
  voice_ = std::move(t);
  if (dev_) SDL_UnlockAudioDevice(dev_);
  return true;
}

void AudioSystem::StopVoice() {
  if (dev_) SDL_LockAudioDevice(dev_);
  voice_.playing = false;
  if (dev_) SDL_UnlockAudioDevice(dev_);
}

void AudioSystem::SetVolume(AudioBus bus, float v) {
  volumes_[static_cast<int>(bus)] = std::clamp(v, 0.f, 1.f);
}

float AudioSystem::Volume(AudioBus bus) const { return volumes_[static_cast<int>(bus)]; }

void AudioSystem::Update(float dt) {
  auto tick = [dt](Track& t) {
    if (t.fadeRate > 0.f) {
      if (t.volume > t.targetVol) {
        t.volume = std::max(t.targetVol, t.volume - t.fadeRate * dt);
        if (t.volume <= t.targetVol && t.targetVol <= 0.f) t.playing = false;
      } else if (t.volume < t.targetVol) {
        t.volume = std::min(t.targetVol, t.volume + t.fadeRate * dt);
      }
    }
  };
  tick(bgm_);
  for (auto& s : ses_) tick(s);
  tick(voice_);
}

void AudioSystem::MixTrack(Track& t, int16_t* out, uint32_t frames, float busVol) {
  if (!t.playing || t.pcm.empty() || t.total == 0) return;
  const int ch = t.channels;
  const int dstCh = specChannels_;
  const float vol = t.volume * busVol;

  for (uint32_t f = 0; f < frames; ++f) {
    if (t.pos >= t.total) {
      if (!t.loop) { t.playing = false; return; }
      t.pos = (t.loopStart < t.total) ? t.loopStart : 0;
    }
    const int16_t* src = t.pcm.data() + static_cast<size_t>(t.pos) * ch;
    for (int c = 0; c < dstCh; ++c) {
      int16_t v = (ch == 1) ? src[0] : src[c < ch ? c : ch - 1];
      int32_t mixed = static_cast<int32_t>(out[f * dstCh + c]) +
                      static_cast<int32_t>(v * vol);
      if (mixed > 32767) mixed = 32767;
      if (mixed < -32768) mixed = -32768;
      out[f * dstCh + c] = static_cast<int16_t>(mixed);
    }
    ++t.pos;
    if (t.loop && t.pos >= t.loopEnd) t.pos = t.loopStart < t.total ? t.loopStart : 0;
  }
}

void AudioSystem::Callback(void* userdata, Uint8* stream, int len) {
  auto* self = static_cast<AudioSystem*>(userdata);
  int16_t* out = reinterpret_cast<int16_t*>(stream);
  uint32_t frames = static_cast<uint32_t>(len / (2 * self->specChannels_));
  if (frames == 0) return;
  std::fill(out, out + static_cast<size_t>(frames) * self->specChannels_, int16_t{0});

  self->MixTrack(self->bgm_, out, frames, self->volumes_[0]);
  for (Track& s : self->ses_) self->MixTrack(s, out, frames, self->volumes_[1]);
  self->MixTrack(self->voice_, out, frames, self->volumes_[2]);
}

}  // namespace siglus
