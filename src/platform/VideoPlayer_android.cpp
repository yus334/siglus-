#include "platform/VideoPlayer.h"

#if defined(__ANDROID__)

#include <jni.h>
#include <string>

#include <SDL.h>
#include <SDL_system.h>

#include "core/Core.h"

namespace siglus {

static const char* kActivityClass = "com/siglus/port/SiglusActivity";

class AndroidVideoPlayer : public IVideoPlayer {
 public:
  bool Play(const std::string& path) override {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
      SIGLUS_LOGE("playMovie: 取不到 JNIEnv/Activity");
      return false;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = env->GetMethodID(cls, "playMovie", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(cls);
    if (!mid) {
      SIGLUS_LOGE("playMovie: 找不到 Java 方法 SiglusActivity.playMovie(String)");
      return false;
    }
    jstring jpath = env->NewStringUTF(path.c_str());
    env->CallVoidMethod(activity, mid, jpath);
    env->DeleteLocalRef(jpath);
    playing_ = true;
    return true;
  }

  bool IsPlaying() override { return playing_; }
  void Stop() override { playing_ = false; }

  void NotifyFinished() { playing_ = false; }

 private:
  bool playing_ = false;
};

static AndroidVideoPlayer g_player;

IVideoPlayer& VideoPlayer() { return g_player; }

}  // namespace siglus

extern "C" JNIEXPORT void JNICALL
Java_com_siglus_port_SiglusActivity_nativeMovieFinished(JNIEnv*, jclass) {
  siglus::g_player.NotifyFinished();
}

#else

#include "core/Core.h"

namespace siglus {
namespace {
class StubVideoPlayer : public IVideoPlayer {
 public:
  bool Play(const std::string& path) override {
    SIGLUS_LOGW("视频播放（非安卓）未实现：%s", path.c_str());
    return false;
  }
  bool IsPlaying() override { return false; }
  void Stop() override {}
};
}  // namespace

IVideoPlayer& VideoPlayer() {
  static StubVideoPlayer p;
  return p;
}

}  // namespace siglus

#endif
