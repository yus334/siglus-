#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"
#include "gfx/Renderer.h"

namespace siglus {

// 字体与字形图集。
//  - 有 thirdparty/stb 时走 stb_truetype（单头文件、无额外依赖，安卓可直接编）
//  - 没有 stb 时 Font::Load 会失败，文字不显示（其余功能不受影响）
class Font {
 public:
  struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // 归一化图集 UV
    int   w  = 0, h = 0;
    int   offX = 0, offY = 0;              // 相对基线左上偏移
    float advance = 0;
    bool  valid = false;
  };

  Font();
  ~Font();

  // faceIndex：.ttc 字体集合里选第几个字体（game.ini 的 [text] face_index，默认 0）
  bool  Load(const std::string& path, int pixelSize, int faceIndex = 0);
  bool  loaded() const { return loaded_; }
  int   pixelSize() const { return px_; }
  float lineHeight() const { return lineHeight_; }
  float ascent() const { return ascent_; }

  const Glyph& Get(uint32_t codepoint);
  TexturePtr   Atlas(Renderer& r);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  bool  Rasterize(uint32_t cp, Glyph& g);

  bool                loaded_ = false;
  int                 faceIndex_ = 0;
  int                 px_ = 28;
  float               lineHeight_ = 0.f;
  float               ascent_ = 0.f;
  std::vector<uint8_t> atlas_;
  int                 atlasW_ = 1024, atlasH_ = 1024;
  int                 penX_ = 1, penY_ = 1, rowH_ = 0;
  bool                dirty_ = true;
  TexturePtr          atlasTex_;
  std::unordered_map<uint32_t, Glyph> cache_;
};

// 文本绘制。支持：
//  - 横排 / 纵排（vertical）
//  - 行宽自动折行（maxWidth，按字断行，CJK 友好）
//  - 简单的 ruby 注音语法：[漢字](かんじ)
class TextRenderer {
 public:
  void SetFont(Font* f) { font_ = f; }
  Font* font() const { return font_; }

  void Draw(Renderer& r, const std::string& utf8, float x, float y, Color c,
            bool vertical = false, float maxWidth = 0.f, float lineGap = 0.f);
  void Measure(const std::string& utf8, bool vertical, float& outW, float& outH,
               float maxWidth = 0.f, float lineGap = 0.f);

 private:
  Font* font_ = nullptr;
};

}  // namespace siglus
