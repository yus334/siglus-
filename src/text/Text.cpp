#include "text/Text.h"

#include <algorithm>
#include <cmath>

#include "core/FileSystem.h"

#if defined(SIGLUS_HAVE_STB_TRUETYPE) && SIGLUS_HAVE_STB_TRUETYPE
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#endif

namespace siglus {

struct Font::Impl {
#if defined(SIGLUS_HAVE_STB_TRUETYPE) && SIGLUS_HAVE_STB_TRUETYPE
  stbtt_fontinfo font{};
  float          scale = 1.f;
#endif
  std::vector<uint8_t> data;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::Load(const std::string& path, int pixelSize, int faceIndex) {
  px_ = pixelSize;
  faceIndex_ = faceIndex;
  if (!FileSystem::ReadFile(path, impl_->data) || impl_->data.empty()) {
    SIGLUS_LOGE("字体加载失败: %s", path.c_str());
    return false;
  }
#if defined(SIGLUS_HAVE_STB_TRUETYPE) && SIGLUS_HAVE_STB_TRUETYPE
  // Windows 上大量字体是 .ttc 集合（msyh.ttc / simsun.ttc / YuGothM.ttc / meiryo.ttc），
  // 直接用 offset 0 初始化会失败，需要先取出第 N 个字体的偏移。
  int faceOffset = 0;
  if (impl_->data.size() > 4 && std::memcmp(impl_->data.data(), "ttcf", 4) == 0) {
    int index = faceIndex_ < 0 ? 0 : faceIndex_;
    faceOffset = stbtt_GetFontOffsetForIndex(impl_->data.data(), index);
    if (faceOffset < 0) {
      SIGLUS_LOGE("TTC 字体里取不到第 %d 个字体: %s", index, path.c_str());
      return false;
    }
    SIGLUS_LOGI("检测到字体集合 %s，使用第 %d 个字体（偏移 %d）", path.c_str(), index, faceOffset);
  }
  if (!stbtt_InitFont(&impl_->font, impl_->data.data(), faceOffset)) {
    SIGLUS_LOGE("stbtt_InitFont 失败: %s", path.c_str());
    return false;
  }
  impl_->scale = stbtt_ScaleForPixelHeight(&impl_->font, static_cast<float>(px_));
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&impl_->font, &ascent, &descent, &gap);
  ascent_     = static_cast<float>(ascent) * impl_->scale;
  lineHeight_ = static_cast<float>(ascent - descent + gap) * impl_->scale;
#else
  SIGLUS_LOGW("未启用 stb_truetype，文字将无法显示");
  return false;
#endif
  loaded_ = true;
  atlas_.assign(static_cast<size_t>(atlasW_) * atlasH_ * 4, 0);
  penX_ = 1;
  penY_ = 1;
  rowH_ = 0;
  dirty_ = true;
  SIGLUS_LOGI("字体已载入: %s (%dpx, 行高 %.1f)", path.c_str(), px_, lineHeight_);
  return true;
}

bool Font::Rasterize(uint32_t cp, Glyph& g) {
#if defined(SIGLUS_HAVE_STB_TRUETYPE) && SIGLUS_HAVE_STB_TRUETYPE
  int w = 0, h = 0, x0 = 0, y0 = 0;
  unsigned char* bmp = stbtt_GetCodepointBitmap(&impl_->font, impl_->scale, impl_->scale,
                                                static_cast<int>(cp), &w, &h, &x0, &y0);
  if (!bmp || w <= 0 || h <= 0) {
    if (bmp) stbtt_FreeBitmap(bmp, nullptr);
    return false;
  }

  // 简易 shelf 打包
  if (penX_ + w + 1 > atlasW_) {
    penX_ = 1;
    penY_ += rowH_ + 1;
    rowH_ = 0;
  }
  if (penY_ + h + 1 > atlasH_) {
    stbtt_FreeBitmap(bmp, nullptr);
    SIGLUS_LOGE("字形图集已满，无法缓存 U+%04X", cp);
    return false;
  }

  for (int yy = 0; yy < h; ++yy) {
    uint8_t* dst = atlas_.data() + (static_cast<size_t>(penY_ + yy) * atlasW_ + penX_) * 4;
    const unsigned char* src = bmp + static_cast<size_t>(yy) * w;
    for (int xx = 0; xx < w; ++xx) {
      dst[xx * 4 + 0] = 255;
      dst[xx * 4 + 1] = 255;
      dst[xx * 4 + 2] = 255;
      dst[xx * 4 + 3] = src[xx];
    }
  }

  g.u0 = static_cast<float>(penX_) / atlasW_;
  g.v0 = static_cast<float>(penY_) / atlasH_;
  g.u1 = static_cast<float>(penX_ + w) / atlasW_;
  g.v1 = static_cast<float>(penY_ + h) / atlasH_;
  g.w = w;
  g.h = h;
  g.offX = x0;
  g.offY = y0;
  int adv = 0, lsb = 0;
  stbtt_GetCodepointHMetrics(&impl_->font, static_cast<int>(cp), &adv, &lsb);
  g.advance = static_cast<float>(adv) * impl_->scale;
  g.valid = true;

  penX_ += w + 1;
  rowH_ = std::max(rowH_, h);
  dirty_ = true;
  stbtt_FreeBitmap(bmp, nullptr);
  return true;
#else
  (void)cp; (void)g;
  return false;
#endif
}

const Font::Glyph& Font::Get(uint32_t cp) {
  static Glyph kEmpty;
  if (!loaded_) return kEmpty;
  auto it = cache_.find(cp);
  if (it != cache_.end()) return it->second;
  Glyph g;
  Rasterize(cp, g);
  auto res = cache_.emplace(cp, g);
  return res.first->second;
}

TexturePtr Font::Atlas(Renderer& r) {
  if (!loaded_) return nullptr;
  if (dirty_ || !atlasTex_) {
    atlasTex_ = r.CreateTexture(atlasW_, atlasH_, atlas_.data());
    dirty_ = false;
  }
  return atlasTex_;
}

// ------------------------------------------------------------------ 排版
namespace {

struct Run {
  std::string base;
  std::string ruby;
};

std::vector<Run> ParseRuns(const std::string& s) {
  std::vector<Run> runs;
  std::string cur;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '[') {
      size_t close = s.find(']', i);
      if (close == std::string::npos) { cur.push_back(s[i++]); continue; }
      std::string base = s.substr(i + 1, close - i - 1);
      std::string ruby;
      size_t j = close + 1;
      if (j < s.size() && s[j] == '(') {
        size_t rp = s.find(')', j);
        if (rp != std::string::npos) {
          ruby = s.substr(j + 1, rp - j - 1);
          j = rp + 1;
        }
      }
      if (!cur.empty()) { runs.push_back({cur, ""}); cur.clear(); }
      runs.push_back({base, ruby});
      i = j;
      continue;
    }
    unsigned char c = static_cast<unsigned char>(s[i]);
    int len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    cur.append(s, i, static_cast<size_t>(len));
    i += static_cast<size_t>(len);
  }
  if (!cur.empty()) runs.push_back({cur, ""});
  return runs;
}

}  // namespace

void TextRenderer::Measure(const std::string& utf8, bool vertical, float& outW, float& outH,
                           float maxWidth, float lineGap) {
  outW = outH = 0.f;
  if (!font_ || !font_->loaded()) return;

  const float lh = font_->lineHeight() + lineGap;
  if (vertical) {
    float colH = 0.f;
    float x = 0.f;
    for (const Run& run : ParseRuns(utf8)) {
      for (uint32_t cp : Utf8Decode(run.base)) {
        const Font::Glyph& g = font_->Get(cp);
        float step = g.advance > 0.f ? g.advance : static_cast<float>(font_->pixelSize());
        colH += step;
        if (maxWidth > 0.f && colH > maxWidth) { colH = step; x += lh; }
      }
      if (!run.ruby.empty()) x += static_cast<float>(font_->pixelSize()) * 0.15f;
    }
    outW = x + lh;
    outH = std::max(outH, maxWidth > 0.f ? maxWidth : colH);
    return;
  }

  float lineW = 0.f;
  float lines = 1.f;
  for (const Run& run : ParseRuns(utf8)) {
    for (uint32_t cp : Utf8Decode(run.base)) {
      const Font::Glyph& g = font_->Get(cp);
      if (maxWidth > 0.f && lineW + g.advance > maxWidth && lineW > 0.f) {
        outW = std::max(outW, lineW);
        lineW = 0.f;
        lines += 1.f;
      }
      lineW += g.advance;
    }
  }
  outW = std::max(outW, lineW);
  outH = lines * lh;
}

void TextRenderer::Draw(Renderer& r, const std::string& utf8, float x, float y, Color c,
                        bool vertical, float maxWidth, float lineGap) {
  if (!font_ || !font_->loaded()) return;
  TexturePtr atlas = font_->Atlas(r);
  if (!atlas) return;

  const float lh = font_->lineHeight() + lineGap;
  const float aw = static_cast<float>(atlas->w);
  const float ah = static_cast<float>(atlas->h);

  auto drawCp = [&](uint32_t cp, float dx, float dy, float scale, float alpha) {
    const Font::Glyph& g = font_->Get(cp);
    if (!g.valid) return;
    Sprite s;
    s.tex   = atlas;
    s.src   = RectF{g.u0 * aw, g.v0 * ah, (g.u1 - g.u0) * aw, (g.v1 - g.v0) * ah};
    s.dst   = RectF{dx + static_cast<float>(g.offX) * scale,
                    dy + static_cast<float>(g.offY) * scale,
                    static_cast<float>(g.w) * scale,
                    static_cast<float>(g.h) * scale};
    s.alpha = alpha;
    s.tint  = c;
    r.DrawSprite(s);
  };

  if (vertical) {
    float penY = y;
    float penX = x;
    for (const Run& run : ParseRuns(utf8)) {
      for (uint32_t cp : Utf8Decode(run.base)) {
        const Font::Glyph& g = font_->Get(cp);
        float step = g.advance > 0.f ? g.advance : static_cast<float>(font_->pixelSize());
        if (maxWidth > 0.f && (penY - y) + step > maxWidth) {
          penY = y;
          penX -= lh;
        }
        drawCp(cp, penX - static_cast<float>(g.w) * 0.5f, penY, 1.f, c.a / 255.f);
        penY += step;
      }
      if (!run.ruby.empty()) penX -= static_cast<float>(font_->pixelSize()) * 0.15f;
    }
    return;
  }

  float penX = x;
  float penY = y;
  for (const Run& run : ParseRuns(utf8)) {
    float runStartX = penX;
    float runW = 0.f;
    for (uint32_t cp : Utf8Decode(run.base)) {
      const Font::Glyph& g = font_->Get(cp);
      if (maxWidth > 0.f && (penX - x) + g.advance > maxWidth && penX > x) {
        penX = x;
        penY += lh;
        runStartX = penX;
        runW = 0.f;
      }
      drawCp(cp, penX, penY, 1.f, c.a / 255.f);
      penX += g.advance;
      runW += g.advance;
    }
    if (!run.ruby.empty()) {
      float rw = 0.f;
      for (uint32_t cp : Utf8Decode(run.ruby)) rw += font_->Get(cp).advance;
      rw *= 0.5f;
      float rx = runStartX + runW * 0.5f - rw * 0.5f;
      float ry = penY - font_->ascent() - static_cast<float>(font_->pixelSize()) * 0.15f;
      for (uint32_t cp : Utf8Decode(run.ruby)) {
        const Font::Glyph& g = font_->Get(cp);
        drawCp(cp, rx, ry, 0.5f, c.a / 255.f);
        rx += g.advance * 0.5f;
      }
    }
  }
}

}  // namespace siglus
