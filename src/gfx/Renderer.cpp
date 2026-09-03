#include "gfx/Renderer.h"

#include <algorithm>

namespace siglus {

#if SDL_VERSION_ATLEAST(2, 0, 10)
#define SIGLUS_HAS_COPY_F 1
#endif

Texture::~Texture() {
  if (tex && owner) SDL_DestroyTexture(tex);
}

static SDL_BlendMode ToSdlBlend(BlendMode m) {
  switch (m) {
    case BlendMode::Normal: return SDL_BLENDMODE_BLEND;
    case BlendMode::Add:    return SDL_BLENDMODE_ADD;
    case BlendMode::Mod:    return SDL_BLENDMODE_MOD;
    case BlendMode::Mul:    return SDL_BLENDMODE_MUL;
    case BlendMode::None:   return SDL_BLENDMODE_NONE;
  }
  return SDL_BLENDMODE_BLEND;
}

bool Renderer::Init(SDL_Window* window) {
  r_ = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
  if (!r_) {
    SIGLUS_LOGE("SDL_CreateRenderer 失败: %s", SDL_GetError());
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  return true;
}

void Renderer::Shutdown() {
  rtA_.reset();
  rtB_.reset();
  if (r_) { SDL_DestroyRenderer(r_); r_ = nullptr; }
}

void Renderer::SetLogicalSize(int w, int h) {
  lw_ = w;
  lh_ = h;
  if (r_) SDL_RenderSetLogicalSize(r_, w, h);
}

TexturePtr Renderer::CreateTexture(int w, int h, const uint8_t* rgba) {
  if (!r_ || w <= 0 || h <= 0) return nullptr;
  SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!t) {
    SIGLUS_LOGE("创建纹理失败: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  if (rgba) SDL_UpdateTexture(t, nullptr, rgba, w * 4);
  auto out = std::make_shared<Texture>();
  out->tex = t;
  out->owner = r_;
  out->w = w;
  out->h = h;
  return out;
}

TexturePtr Renderer::CreateTextureFromBitmap(const Bitmap& bmp) {
  if (!bmp.valid()) return nullptr;
  return CreateTexture(bmp.w, bmp.h, bmp.rgba.data());
}

TexturePtr Renderer::CreateTarget(int w, int h) {
  if (!r_ || w <= 0 || h <= 0) return nullptr;
  SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!t) {
    SIGLUS_LOGE("创建 render target 失败: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  auto out = std::make_shared<Texture>();
  out->tex = t;
  out->owner = r_;
  out->w = w;
  out->h = h;
  return out;
}

void Renderer::EnsureTargets() {
  if (lw_ <= 0 || lh_ <= 0) return;
  if (!rtA_ || rtA_->w != lw_ || rtA_->h != lh_) {
    rtA_ = CreateTarget(lw_, lh_);
    rtB_ = CreateTarget(lw_, lh_);
  }
}

void Renderer::BeginFrame(Color clear) {
  if (!r_) return;
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r_, clear.r, clear.g, clear.b, clear.a);
  SDL_RenderClear(r_);
}

void Renderer::DrawSprite(const Sprite& s) {
  if (!r_ || !s.tex || !s.tex->tex || s.alpha <= 0.f) return;

  RectF dst = s.dst;
  if (dst.w <= 0 && s.tex->w > 0) dst.w = static_cast<float>(s.tex->w);
  if (dst.h <= 0 && s.tex->h > 0) dst.h = static_cast<float>(s.tex->h);

  SDL_SetTextureBlendMode(s.tex->tex, ToSdlBlend(s.blend));
  SDL_SetTextureAlphaMod(s.tex->tex, static_cast<Uint8>(std::clamp(s.alpha, 0.f, 1.f) * 255.f));
  SDL_SetTextureColorMod(s.tex->tex, s.tint.r, s.tint.g, s.tint.b);

  RectF src = s.src;
  if (src.w <= 0 || src.h <= 0) {
    SDL_FRect sfd{dst.x, dst.y, dst.w, dst.h};
#ifdef SIGLUS_HAS_COPY_F
    SDL_RenderCopyExF(r_, s.tex->tex, nullptr, &sfd, 0.0, nullptr, s.flip);
#else
    SDL_Rect d{dst.x, dst.y, dst.w, dst.h};
    SDL_RenderCopyEx(r_, s.tex->tex, nullptr, &d, 0.0, nullptr, s.flip);
#endif
    return;
  }
  SDL_FRect sfs{src.x, src.y, src.w, src.h};
  SDL_FRect sfd{dst.x, dst.y, dst.w, dst.h};
#ifdef SIGLUS_HAS_COPY_F
  SDL_RenderCopyExF(r_, s.tex->tex, &sfs, &sfd, 0.0, nullptr, s.flip);
#else
  SDL_Rect ss{src.x, src.y, src.w, src.h};
  SDL_Rect dd{dst.x, dst.y, dst.w, dst.h};
  SDL_RenderCopyEx(r_, s.tex->tex, &ss, &dd, 0.0, nullptr, s.flip);
#endif
}

void Renderer::FillRect(const RectF& r, Color c) {
  if (!r_) return;
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
  SDL_FRect fr{r.x, r.y, r.w, r.h};
#ifdef SIGLUS_HAS_COPY_F
  SDL_RenderFillRectF(r_, &fr);
#else
  SDL_Rect ir{r.x, r.y, r.w, r.h};
  SDL_RenderFillRect(r_, &ir);
#endif
}

void Renderer::DrawTransition(TransitionType type, const TexturePtr& from, const TexturePtr& to,
                              float progress, const TexturePtr& mask) {
  if (!r_) return;
  float p = std::clamp(progress, 0.f, 1.f);

  auto full = [&](const TexturePtr& t) {
    Sprite s;
    s.tex = t;
    s.dst = RectF{0, 0, static_cast<float>(lw_), static_cast<float>(lh_)};
    return s;
  };

  switch (type) {
    case TransitionType::CrossFade: {
      if (from) DrawSprite(full(from));
      if (to) {
        Sprite s = full(to);
        s.alpha = p;
        DrawSprite(s);
      }
      return;
    }
    case TransitionType::Fade: {
      if (p < 0.5f) {
        if (from) DrawSprite(full(from));
        FillRect(RectF{0, 0, static_cast<float>(lw_), static_cast<float>(lh_)},
                 Color{0, 0, 0, static_cast<uint8_t>(p * 2.f * 255.f)});
      } else {
        if (to) DrawSprite(full(to));
        FillRect(RectF{0, 0, static_cast<float>(lw_), static_cast<float>(lh_)},
                 Color{0, 0, 0, static_cast<uint8_t>((1.f - p) * 2.f * 255.f)});
      }
      return;
    }
    case TransitionType::MaskBlend: {
      if (!from || !to) {
        if (to) DrawSprite(full(to));
        return;
      }
      EnsureTargets();
      if (!rtA_ || !rtB_) {
        Sprite s = full(to);
        s.alpha = p;
        DrawSprite(s);
        return;
      }

      // 关闭逻辑缩放，render target 用原始像素坐标
      SDL_RenderSetLogicalSize(r_, 0, 0);

      // rtB = from 的颜色 + mask 的 alpha（mask 整体再乘以 progress 做近似阈值）
      SDL_SetRenderTarget(r_, rtB_->tex);
      SDL_SetRenderDrawColor(r_, 0, 0, 0, 0);
      SDL_RenderClear(r_);
      DrawSprite(full(from));
      if (mask && mask->tex) {
        static const SDL_BlendMode kKeepColorTakeAlpha = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,   // RGB 保持目标
            SDL_BLENDFACTOR_ONE,  SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);  // A 取源
        SDL_SetTextureBlendMode(mask->tex, kKeepColorTakeAlpha);
        SDL_SetTextureAlphaMod(mask->tex, static_cast<Uint8>(p * 255.f));
        DrawSprite(full(mask));
        SDL_SetTextureBlendMode(mask->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(mask->tex, 255);
      } else {
        // 没有遮罩图时退化为交叉淡化：直接把 alpha 乘在 from 上是不对的，
        // 这里用全局 alpha 近似
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
      }

      // rtA = to，然后把 rtB 按 alpha 合成上去
      SDL_SetRenderTarget(r_, rtA_->tex);
      SDL_SetRenderDrawColor(r_, 0, 0, 0, 0);
      SDL_RenderClear(r_);
      DrawSprite(full(to));
      DrawSprite(full(rtB_));

      SDL_SetRenderTarget(r_, nullptr);
      SDL_RenderSetLogicalSize(r_, lw_, lh_);
      DrawSprite(full(rtA_));
      return;
    }
    case TransitionType::None:
    default:
      if (to) DrawSprite(full(to));
      else if (from) DrawSprite(full(from));
      return;
  }
}

void Renderer::EndFrame() {
  if (r_) SDL_RenderPresent(r_);
}

}  // namespace siglus
