#pragma once

#include <functional>
#include <string>
#include <vector>

#include <SDL.h>

#include "media/Decoder.h"

namespace siglus {

using MediaLoader = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;

enum class AudioBus { Bgm = 0, Se = 1, Voice = 2 };

// 简易混音器：BGM 1 路 + SE 8 路 + 语音 1 路。
// 循环点（loopStart / loopLength）是 galgame BGM 的关键，缺失会导致循环处"咔哒"一声。
class AudioSystem {
 public:
  bool Init(int rate = 44100, int channels = 2, int bufferFrames = 1024);
  void Shutdown();
  void SetLoader(MediaLoader loader);

  bool PlayBgm(const std::string& name, bool loop = true, uint32_t loopStartFrame = 0,
               uint32_t loopLengthFrames = 0);
  void StopBgm(float fadeMs = 300.f);
  bool PlaySe(const std::string& name, int channel = 0);
  bool PlayVoice(const std::string& name);
  void StopVoice();
  void SetVolume(AudioBus bus, float v);
  float Volume(AudioBus bus) const;
  void Update(float dtSeconds);

  bool IsReady() const { return dev_ != 0; }

 private:
  struct Track {
    std::vector<int16_t> pcm;
    uint32_t total     = 0;  // 帧数
    uint32_t pos       = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd   = 0;  // 0 表示 total
    int      channels  = 2;
    float    volume    = 1.f;
    float    targetVol = 1.f;
    float    fadeRate  = 0.f;  // 每秒变化量
    bool     playing   = false;
    bool     loop      = false;
  };

  static void Callback(void* userdata, Uint8* stream, int len);
  void MixTrack(Track& t, int16_t* out, uint32_t frames, float busVol);
  bool DecodeToTrack(const std::string& name, Track& t, bool loop, uint32_t loopStart,
                     uint32_t loopLen);

  SDL_AudioDeviceID dev_ = 0;
  int               specChannels_ = 2;
  int               specRate_ = 44100;

  Track              bgm_;
  std::vector<Track> ses_;
  Track              voice_;
  MediaLoader        loader_;
  float              volumes_[3] = {0.8f, 0.9f, 1.0f};
};

}  // namespace siglus
