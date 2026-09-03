#include "input/Input.h"

#include <cmath>

namespace siglus {

static Gesture SwipeTo(float dx, float dy, float threshold) {
  if (std::fabs(dx) < threshold && std::fabs(dy) < threshold) return Gesture::None;
  if (std::fabs(dx) > std::fabs(dy)) return dx > 0 ? Gesture::SwipeRight : Gesture::SwipeLeft;
  return dy > 0 ? Gesture::SwipeDown : Gesture::SwipeUp;
}

Gesture GestureDetector::Feed(const SDL_Event& e) {
  auto normX = [&](int px) { return vw_ > 0 ? static_cast<float>(px) / vw_ : 0.f; };
  auto normY = [&](int py) { return vh_ > 0 ? static_cast<float>(py) / vh_ : 0.f; };

  switch (e.type) {
    case SDL_FINGERDOWN: {
      ++fingers_;
      if (fingers_ >= 2) {
        down_ = false;
        return Gesture::TwoFingerTap;
      }
      down_     = true;
      moved_    = false;
      downTime_ = SDL_GetTicks();
      startX_ = x_ = e.tfinger.x;
      startY_ = y_ = e.tfinger.y;
      return Gesture::None;
    }
    case SDL_FINGERMOTION: {
      x_ = e.tfinger.x;
      y_ = e.tfinger.y;
      if (down_ && (std::fabs(x_ - startX_) > cfg_.swipeDist ||
                    std::fabs(y_ - startY_) > cfg_.swipeDist))
        moved_ = true;
      return Gesture::None;
    }
    case SDL_FINGERUP: {
      if (fingers_ > 0) --fingers_;
      if (!down_) return Gesture::None;
      down_ = false;
      Uint32 dt = SDL_GetTicks() - downTime_;
      if (moved_) return SwipeTo(x_ - startX_, y_ - startY_, cfg_.swipeDist);
      if (dt >= static_cast<Uint32>(cfg_.longPressMs)) return Gesture::LongPress;
      if (dt <= static_cast<Uint32>(cfg_.tapMaxMs)) {
        Uint32 now = SDL_GetTicks();
        if (now - lastTapTime_ <= static_cast<Uint32>(cfg_.doubleTapMs))
          return Gesture::DoubleTap;
        lastTapTime_ = now;
      }
      return Gesture::Tap;
    }

    // 桌面端：鼠标
    case SDL_MOUSEBUTTONDOWN:
      down_     = true;
      moved_    = false;
      downTime_ = SDL_GetTicks();
      startX_ = x_ = normX(e.button.x);
      startY_ = y_ = normY(e.button.y);
      return Gesture::None;
    case SDL_MOUSEBUTTONUP: {
      if (!down_) return Gesture::None;
      down_ = false;
      Uint32 dt = SDL_GetTicks() - downTime_;
      if (moved_) return SwipeTo(x_ - startX_, y_ - startY_, cfg_.swipeDist);
      if (dt >= static_cast<Uint32>(cfg_.longPressMs)) return Gesture::LongPress;
      return Gesture::Tap;
    }
    case SDL_MOUSEMOTION:
      x_ = normX(e.motion.x);
      y_ = normY(e.motion.y);
      if (down_ && (std::fabs(x_ - startX_) > cfg_.swipeDist ||
                    std::fabs(y_ - startY_) > cfg_.swipeDist))
        moved_ = true;
      return Gesture::None;
    case SDL_MOUSEWHEEL:
      return e.wheel.y > 0 ? Gesture::WheelUp : Gesture::WheelDown;

    case SDL_KEYDOWN: {
      switch (e.key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:   return Gesture::Tap;
        case SDLK_ESCAPE:
        case SDLK_AC_BACK: return Gesture::Back;
        case SDLK_a:       return Gesture::DoubleTap;
        case SDLK_s:       return Gesture::LongPress;
        case SDLK_h:       return Gesture::SwipeUp;
        case SDLK_d:       return Gesture::SwipeDown;
        case SDLK_m:       return Gesture::Menu;
        case SDLK_F5:      return Gesture::QuickSave;
        case SDLK_F9:      return Gesture::QuickLoad;
        default:           return Gesture::None;
      }
    }
    default:
      return Gesture::None;
  }
}

}  // namespace siglus
