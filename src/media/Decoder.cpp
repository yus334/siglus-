#include "media/Decoder.h"

#include <algorithm>
#include <cstring>

#if defined(SIGLUS_HAVE_STB_IMAGE) && SIGLUS_HAVE_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
#define STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.h>
#endif

namespace siglus {

// ============================== 图像 ==============================
bool StbImageDecoder::Decode(const uint8_t* data, size_t len, Bitmap& out) {
#if defined(SIGLUS_HAVE_STB_IMAGE) && SIGLUS_HAVE_STB_IMAGE
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(len), &w, &h, &comp, 4);
  if (!px) {
    SIGLUS_LOGW("stb_image: 解码失败");
    return false;
  }
  out.w = w;
  out.h = h;
  out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
  stbi_image_free(px);
  return true;
#else
  (void)data; (void)len; (void)out;
  SIGLUS_LOGW("未启用 stb_image，无法解码位图（把 stb_image.h 放到 thirdparty/stb 并开启 SIGLUS_STB）");
  return false;
#endif
}

// g00 的头部结构存在多个版本，这里只做"识别 + 转储"，真正的解压算法
// 需要在 tools/probe 拿到样本头部后按版本分支实现。
bool G00Decoder::Decode(const uint8_t* data, size_t len, Bitmap& out) {
  (void)out;
  if (len < 16) {
    SIGLUS_LOGE("g00: 文件过小");
    return false;
  }
  uint32_t w = Rd32(data + 0);
  uint32_t h = Rd32(data + 4);
  uint32_t flag = Rd32(data + 8);
  SIGLUS_LOGW("g00: 疑似 w=%u h=%u flag=0x%08X len=%zu（解压算法尚未实现，请在 G00Decoder 中补充）",
              w, h, flag, len);
  SIGLUS_LOGD("g00 头部:\n%s", HexDump(data, std::min<size_t>(len, 64)).c_str());
  return false;
}

// ============================== 图像注册表 ==============================
ImageRegistry& ImageRegistry::Get() {
  static ImageRegistry r;
  static bool inited = false;
  if (!inited) {
    r.RegisterDefault();
    inited = true;
  }
  return r;
}

void ImageRegistry::Register(std::unique_ptr<IImageDecoder> d) {
  decoders_.push_back(std::move(d));
}

void ImageRegistry::RegisterDefault() {
  Register(std::make_unique<G00Decoder>());
  Register(std::make_unique<StbImageDecoder>());
}

bool ImageRegistry::Decode(const std::string& fileName, const uint8_t* data, size_t len,
                           Bitmap& out) {
  const std::string ext = Extension(fileName);
  for (auto& d : decoders_) {
    for (const auto& e : d->Extensions()) {
      if (e != ext) continue;
      if (d->Decode(data, len, out)) return true;
      SIGLUS_LOGW("解码器 %s 处理 %s 失败", d->Name(), fileName.c_str());
    }
  }
  return false;
}

// ============================== 音频：WAV ==============================
bool WavDecoder::Open(const uint8_t* data, size_t len, PcmFormat& fmt) {
  if (len < 44 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
    SIGLUS_LOGW("wav: 不是 RIFF/WAVE");
    return false;
  }
  size_t p = 12;
  int    channels = 0, rate = 0, bits = 0;
  const uint8_t* pcm = nullptr;
  size_t         pcmLen = 0;
  while (p + 8 <= len) {
    uint32_t chunk = 0;
    std::memcpy(&chunk, data + p, 4);
    uint32_t csize = Rd32(data + p + 4);
    size_t   body = p + 8;
    if (body > len) break;
    if (chunk == 0x20746D66 /* 'fmt ' */ && body + 16 <= len) {
      channels = static_cast<int>(Rd16(data + body + 2));
      rate     = static_cast<int>(Rd32(data + body + 4));
      bits     = static_cast<int>(Rd16(data + body + 14));
    } else if (chunk == 0x61746164 /* 'data' */) {
      pcm    = data + body;
      pcmLen = std::min<size_t>(csize, len - body);
    }
    p = body + csize + (csize & 1);
  }
  if (!pcm || channels == 0 || rate == 0 || bits != 16) {
    SIGLUS_LOGW("wav: 只支持 16bit PCM（ch=%d rate=%d bits=%d）", channels, rate, bits);
    return false;
  }
  fmt.channels = channels;
  fmt.rate     = rate;
  channels_    = channels;
  samples_.assign(reinterpret_cast<const int16_t*>(pcm),
                  reinterpret_cast<const int16_t*>(pcm) + pcmLen / 2);
  totalFrames_ = static_cast<uint32_t>(samples_.size() / static_cast<size_t>(channels));
  pos_         = 0;
  return true;
}

uint32_t WavDecoder::Decode(int16_t* dst, uint32_t frames) {
  uint32_t avail = totalFrames_ - pos_;
  uint32_t n = std::min(frames, avail);
  if (n) std::memcpy(dst, samples_.data() + static_cast<size_t>(pos_) * channels_,
                     static_cast<size_t>(n) * channels_ * sizeof(int16_t));
  pos_ += n;
  return n;
}

// ============================== 音频：OGG ==============================
struct OggDecoder::Impl {
#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
  stb_vorbis* v = nullptr;
#endif
};

OggDecoder::~OggDecoder() {
#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
  if (impl_ && impl_->v) stb_vorbis_close(impl_->v);
#endif
}

bool OggDecoder::Open(const uint8_t* data, size_t len, PcmFormat& fmt) {
  impl_ = std::make_unique<Impl>();
#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
  int err = 0;
  impl_->v = stb_vorbis_open_memory(data, static_cast<int>(len), &err, nullptr);
  if (!impl_->v) {
    SIGLUS_LOGW("ogg: stb_vorbis 打开失败 err=%d", err);
    return false;
  }
  stb_vorbis_info info = stb_vorbis_get_info(impl_->v);
  fmt.channels = info.channels;
  fmt.rate     = static_cast<int>(info.sample_rate);
  totalFrames_ = stb_vorbis_stream_length_in_samples(impl_->v);
  return true;
#else
  (void)data; (void)len; (void)fmt;
  SIGLUS_LOGW("未启用 stb_vorbis，无法解码 ogg");
  return false;
#endif
}

uint32_t OggDecoder::Decode(int16_t* dst, uint32_t frames) {
#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
  if (!impl_ || !impl_->v) return 0;
  int channels = stb_vorbis_get_info(impl_->v).channels;
  int got = stb_vorbis_get_samples_short_interleaved(impl_->v, channels, dst,
                                                     static_cast<int>(frames * channels));
  return got > 0 ? static_cast<uint32_t>(got) : 0;
#else
  (void)dst; (void)frames; return 0;
#endif
}

void OggDecoder::Seek(uint32_t frame) {
#if defined(SIGLUS_HAVE_STB_VORBIS) && SIGLUS_HAVE_STB_VORBIS
  if (impl_ && impl_->v) stb_vorbis_seek(impl_->v, frame);
#endif
}

// ============================== 音频：NWA 槽位 ==============================
bool NwaDecoder::Open(const uint8_t* data, size_t len, PcmFormat& fmt) {
  (void)fmt;
  if (len < 44) {
    SIGLUS_LOGE("nwa: 文件过小");
    return false;
  }
  SIGLUS_LOGW("nwa: 解码算法尚未实现（len=%zu），请先补齐 NwaDecoder", len);
  SIGLUS_LOGD("nwa 头部:\n%s", HexDump(data, std::min<size_t>(len, 64)).c_str());
  return false;
}

// ============================== 音频注册表 ==============================
AudioRegistry& AudioRegistry::Get() {
  static AudioRegistry r;
  static bool inited = false;
  if (!inited) {
    r.RegisterDefault();
    inited = true;
  }
  return r;
}

void AudioRegistry::Register(std::unique_ptr<IAudioDecoder> d) {
  decoders_.push_back(std::move(d));
}

void AudioRegistry::RegisterDefault() {
  Register(std::make_unique<NwaDecoder>());
  Register(std::make_unique<OggDecoder>());
  Register(std::make_unique<WavDecoder>());
}

IAudioDecoder* AudioRegistry::Create(const std::string& fileName) {
  const std::string ext = Extension(fileName);
  for (auto& d : decoders_) {
    for (const auto& e : d->Extensions())
      if (e == ext) return d.get();
  }
  return nullptr;
}

}  // namespace siglus
