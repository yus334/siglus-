#pragma once

#include <string>

namespace siglus {

class IVideoPlayer {
 public:
  virtual ~IVideoPlayer() = default;
  virtual bool Play(const std::string& path) = 0;
  virtual bool IsPlaying() = 0;
  virtual void Stop() = 0;
};

IVideoPlayer& VideoPlayer();

}  // namespace siglus
