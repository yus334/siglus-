#include "platform/VideoPlayer.h"

#include "core/Core.h"

namespace siglus {
namespace {

class DesktopVideoPlayer : public IVideoPlayer {
 public:
  bool Play(const std::string& path) override {
    SIGLUS_LOGW("桌面端未实现视频播放（.omv 需先转码；此处视为已播放结束）：%s", path.c_str());
    return false;
  }
  bool IsPlaying() override { return false; }
  void Stop() override {}
};

}  // namespace

IVideoPlayer& VideoPlayer() {
  static DesktopVideoPlayer p;
  return p;
}

}  // namespace siglus
