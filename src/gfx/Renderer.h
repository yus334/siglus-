#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

#include "core/Core.h"
#include "media/Decoder.h"

namespace siglus {

struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
};

struct Color {
  uint8_t r = 255, g = 255, b = 255, a = 255;
};

enum class BlendMode { Normal, Add, Mod, Mul, None };

struct Texture {
  SDL_Texture* tex = nullptr;
  SDL_Renderer* owner = nullptr;
  int w = 0, h = 0;
  ~Texture();
};
using TexturePtr = std::shared_ptr<Texture>;

struct Sprite {
  TexturePtr       tex;
  RectF            dst;
  RectF            src;  // w/h<=0 表示整张纹理
  float            alpha = 1.f;
  Color            tint{255, 255, 255, 255};  // 仅 RGB 生效（用于文字着色）
  BlendMode        blend = BlendMode::Normal;
  SDL_RendererFlip flip  = SDL_FLIP_NONE;
};

enum class TransitionType { None, Fade, CrossFade, MaskBlend };

// 基于 SDL_Renderer 的 2D 后端：
//  - 桌面端自动走 D3D/OpenGL，安卓端走 GLES2，无需自己写着色器
//  - 混合模式覆盖 galgame 常用的 normal / add / mod / mul
//  - 转场用 render target 合成，避免引入 FBO/着色器复杂度
// 若将来需要减法混合或自定义 shader，再增加 RendererGL 后端即可。
class Renderer {
 public:
  bool Init(SDL_Window* window);
  void Shutdown();

  void SetLogicalSize(int w, int h);
  int  LogicalWidth() const { return lw_; }
  int  LogicalHeight() const { return lh_; }

  TexturePtr CreateTexture(int w, int h, const uint8_t* rgba);
  TexturePtr CreateTextureFromBitmap(const Bitmap& bmp);
  TexturePtr CreateTarget(int w, int h);

  void BeginFrame(Color clear = Color{0, 0, 0, 255});
  void DrawSprite(const Sprite& s);
  void FillRect(const RectF& r, Color c);
  void DrawTransition(TransitionType type, const TexturePtr& from, const TexturePtr& to,
                      float progress, const TexturePtr& mask);
  void EndFrame();

  SDL_Renderer* Raw() const { return r_; }

 private:
  void EnsureTargets();

  SDL_Renderer* r_  = nullptr;
  int           lw_ = 0, lh_ = 0;
  TexturePtr    rtA_, rtB_;
};

}  // namespace siglus
