#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Core.h"
#include "platform/VideoPlayer.h"

namespace siglus {

// ============================== 图像 ==============================
struct Bitmap {
  int                  w = 0, h = 0;
  std::vector<uint8_t> rgba;  // 统一 RGBA8
  bool valid() const { return w > 0 && h > 0 && rgba.size() == static_cast<size_t>(w) * h * 4; }
};

class IImageDecoder {
 public:
  virtual ~IImageDecoder() = default;
  virtual const char*              Name() const = 0;
  virtual std::vector<std::string> Extensions() const = 0;
  virtual bool Decode(const uint8_t* data, size_t len, Bitmap& out) = 0;
};

// stb_image 后端（png/jpg/bmp/tga）。没有 stb 时该解码器始终失败。
class StbImageDecoder : public IImageDecoder {
 public:
  const char* Name() const override { return "stb_image"; }
  std::vector<std::string> Extensions() const override { return {"png", "jpg", "jpeg", "bmp", "tga"}; }
  bool Decode(const uint8_t* data, size_t len, Bitmap& out) override;
};

// .g00 解码槽位。Visual Art's 的 g00 有多个版本（不同压缩方式），
// 必须拿实机样本对照实现。当前实现：读取并打印头部，返回 false。
class G00Decoder : public IImageDecoder {
 public:
  const char* Name() const override { return "g00(slot)"; }
  std::vector<std::string> Extensions() const override { return {"g00"}; }
  bool Decode(const uint8_t* data, size_t len, Bitmap& out) override;
};

class ImageRegistry {
 public:
  static ImageRegistry& Get();
  void RegisterDefault();
  void Register(std::unique_ptr<IImageDecoder> d);
  bool Decode(const std::string& fileName, const uint8_t* data, size_t len, Bitmap& out);

 private:
  std::vector<std::unique_ptr<IImageDecoder>> decoders_;
};

// ============================== 音频 ==============================
struct PcmFormat {
  int channels = 2;
  int rate     = 44100;
};

class IAudioDecoder {
 public:
  virtual ~IAudioDecoder() = default;
  virtual const char*              Name() const = 0;
  virtual std::vector<std::string> Extensions() const = 0;
  virtual bool     Open(const uint8_t* data, size_t len, PcmFormat& fmt) = 0;
  virtual uint32_t Decode(int16_t* dst, uint32_t frames) = 0;  // 返回实际写入帧数
  virtual void     Seek(uint32_t frame) = 0;
  virtual uint32_t TotalFrames() const = 0;
  virtual uint32_t LoopStartFrames() const { return 0; }
  virtual uint32_t LoopLengthFrames() const { return 0; }
};

class WavDecoder : public IAudioDecoder {
 public:
  const char* Name() const override { return "wav"; }
  std::vector<std::string> Extensions() const override { return {"wav"}; }
  bool     Open(const uint8_t* data, size_t len, PcmFormat& fmt) override;
  uint32_t Decode(int16_t* dst, uint32_t frames) override;
  void     Seek(uint32_t frame) override { pos_ = frame; }
  uint32_t TotalFrames() const override { return totalFrames_; }

 private:
  std::vector<int16_t> samples_;
  uint32_t             pos_         = 0;
  uint32_t             totalFrames_ = 0;
  int                  channels_    = 2;
};

class OggDecoder : public IAudioDecoder {
 public:
  ~OggDecoder() override;
  const char* Name() const override { return "ogg"; }
  std::vector<std::string> Extensions() const override { return {"ogg"}; }
  bool     Open(const uint8_t* data, size_t len, PcmFormat& fmt) override;
  uint32_t Decode(int16_t* dst, uint32_t frames) override;
  void     Seek(uint32_t frame) override;
  uint32_t TotalFrames() const override { return totalFrames_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  uint32_t              totalFrames_ = 0;
};

// .nwa 解码槽位。NWA 是有损压缩（DPCM/ADPCM 系），需按真实样本实现。
class NwaDecoder : public IAudioDecoder {
 public:
  const char* Name() const override { return "nwa(slot)"; }
  std::vector<std::string> Extensions() const override { return {"nwa"}; }
  bool     Open(const uint8_t* data, size_t len, PcmFormat& fmt) override;
  uint32_t Decode(int16_t* dst, uint32_t frames) override { (void)dst; (void)frames; return 0; }
  void     Seek(uint32_t frame) override { (void)frame; }
  uint32_t TotalFrames() const override { return 0; }
};

class AudioRegistry {
 public:
  static AudioRegistry& Get();
  void RegisterDefault();
  void Register(std::unique_ptr<IAudioDecoder> d);
  IAudioDecoder* Create(const std::string& fileName);  // 失败返回 nullptr
};

}  // namespace siglus
