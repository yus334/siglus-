#pragma once

#include <SDL.h>

namespace siglus {

enum class Gesture {
  None,
  Tap,            // 点击：推进文本 / 确认
  LongPress,      // 长按：切换快进
  DoubleTap,      // 双击（桌面 A 键）：切换自动播放
  TwoFingerTap,   // 双指点击：打开菜单
  SwipeLeft,      // 左滑：返回
  SwipeRight,     // 右滑：显示/隐藏对话框
  SwipeUp,        // 上滑（桌面 H 键）：历史记录
  SwipeDown,      // 下滑（桌面 D 键）：隐藏 UI
  Back,           // 系统返回键
  Menu,           // 打开系统菜单
  QuickSave,
  QuickLoad,
  WheelUp,
  WheelDown,
};

// 把 SDL 的触摸/鼠标/键盘事件统一成游戏手势。
// x()/y() 恒为归一化坐标（0..1），调用方负责换算到逻辑分辨率。
class GestureDetector {
 public:
  struct Config {
    int   tapMaxMs    = 250;
    int   longPressMs = 600;
    float swipeDist   = 0.08f;  // 归一化距离
    int   doubleTapMs = 300;
  };

  void SetViewportSize(int w, int h) { vw_ = w; vh_ = h; }

  Gesture Feed(const SDL_Event& e);

  float x() const { return x_; }
  float y() const { return y_; }
  Config& config() { return cfg_; }

 private:
  Config cfg_;
  int    vw_ = 0, vh_ = 0;
  bool   down_ = false;
  int    fingers_ = 0;
  Uint32 downTime_ = 0;
  Uint32 lastTapTime_ = 0;
  float  startX_ = 0, startY_ = 0;
  float  x_ = 0, y_ = 0;
  bool   moved_ = false;
};

}  // namespace siglus
