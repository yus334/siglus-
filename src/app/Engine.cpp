#include "app/Engine.h"

#include <algorithm>
#include <cmath>

#include "core/FileSystem.h"
#include "media/Decoder.h"
#include "platform/VideoPlayer.h"

namespace siglus {
namespace {

size_t Utf8SeqLen(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  if ((u & 0xE0) == 0xC0) return 2;
  if ((u & 0xF0) == 0xE0) return 3;
  if ((u & 0xF8) == 0xF0) return 4;
  return 1;
}

std::string Utf8Prefix(const std::string& s, size_t cpCount) {
  size_t n = 0, i = 0;
  while (i < s.size() && n < cpCount) {
    i += Utf8SeqLen(s[i]);
    ++n;
  }
  return s.substr(0, i);
}

std::string Escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else o.push_back(c);
  }
  return o;
}

std::string Unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[i + 1];
      if (n == 'n') { o.push_back('\n'); ++i; continue; }
      if (n == 'r') { o.push_back('\r'); ++i; continue; }
    }
    o.push_back(s[i]);
  }
  return o;
}

}  // namespace

Engine::Engine() = default;
Engine::~Engine() { Shutdown(); }

// ================================================================== 初始化
bool Engine::Init(const EngineConfig& cfg) {
  cfg_ = cfg;
  FileSystem::SetRoot(cfg.dataRoot);

  SDL_SetHint(SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1");
  SDL_SetHint(SDL_HINT_ORIENTATIONS, cfg.landscape ? "LandscapeLeft LandscapeRight"
                                                   : "Portrait");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
    SIGLUS_LOGE("SDL_Init 失败: %s", SDL_GetError());
    return false;
  }

  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  window_ = SDL_CreateWindow("SiglusPort", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             cfg.windowW, cfg.windowH, flags);
  if (!window_) {
    SIGLUS_LOGE("创建窗口失败: %s", SDL_GetError());
    return false;
  }

  if (!renderer_.Init(window_)) return false;

  if (!LoadGameIni()) return false;
  renderer_.SetLogicalSize(designW_, designH_);

  int vw = 0, vh = 0;
  SDL_GetWindowSize(window_, &vw, &vh);
  gestures_.SetViewportSize(vw, vh);

  if (!audio_.Init()) SIGLUS_LOGW("音频初始化失败，继续以静音运行");
  audio_.SetLoader([this](const std::string& n, std::vector<uint8_t>& o) {
    return LoadMedia(n, o);
  });

  if (!fontPath_.empty()) {
    int faceIndex = IniGetInt(ini_, "text", "face_index", 0);
    if (font_.Load(fontPath_, fontSize_, faceIndex)) text_.SetFont(&font_);
  }

  // 资源
  if (!archivePath_.empty()) {
    if (archiveType_ == "pkg") {
      auto a = std::make_unique<PkgArchive>(LoadPkgLayout(ini_, "pkg"));
      if (a->Open(archivePath_)) archive_ = std::move(a);
    } else {
      auto a = std::make_unique<DirArchive>();
      if (a->Open(archivePath_)) archive_ = std::move(a);
    }
  }

  // 脚本
  vm_.SetHost(this);
  std::vector<uint8_t> script;
  if (!LoadMedia(scriptPath_, script)) {
    SIGLUS_LOGE("找不到脚本: %s", scriptPath_.c_str());
    return false;
  }
  if (dialect_ == "ss") {
    std::vector<uint8_t> csv;
    if (!opcodesPath_.empty() && LoadMedia(opcodesPath_, csv)) {
      opcodes_.LoadCsv(std::string(csv.begin(), csv.end()));
    } else {
      SIGLUS_LOGE("dialect=ss 需要提供 opcode 表: %s", opcodesPath_.c_str());
      return false;
    }
    if (!vm_.LoadBinary(script.data(), script.size(), opcodes_)) return false;
  } else {
    if (!vm_.LoadAsm(std::string(script.begin(), script.end()))) return false;
  }

  SIGLUS_LOGI("Engine 初始化完成（%dx%d 逻辑分辨率）", designW_, designH_);
  return true;
}

bool Engine::LoadGameIni() {
  std::vector<uint8_t> raw;
  if (!FileSystem::ReadFile(cfg_.gameIni, raw)) {
    SIGLUS_LOGW("没有 %s，使用默认配置", cfg_.gameIni.c_str());
    return true;
  }
  ini_ = IniParse(std::string(raw.begin(), raw.end()));

  designW_ = IniGetInt(ini_, "game", "design_width", 1280);
  designH_ = IniGetInt(ini_, "game", "design_height", 720);
  scriptPath_  = IniGet(ini_, "game", "script", "script/demo.asm");
  dialect_     = ToLower(IniGet(ini_, "game", "dialect", "asm"));
  opcodesPath_ = IniGet(ini_, "game", "opcodes", "opcodes/ss_opcodes.csv");
  archivePath_ = IniGet(ini_, "game", "archive", "game");
  archiveType_ = ToLower(IniGet(ini_, "game", "archive_type", "dir"));
  imageExts_   = Split(IniGet(ini_, "game", "image_ext", "png,g00"), ',');

  fontPath_ = IniGet(ini_, "text", "font", "font/default.ttf");
  fontSize_ = IniGetInt(ini_, "text", "font_size", 28);
  charsPerSec_ = IniGetFloat(ini_, "text", "chars_per_sec", 40.f);
  textBox_.x = IniGetFloat(ini_, "text", "box_x", 80.f);
  textBox_.y = IniGetFloat(ini_, "text", "box_y", 520.f);
  textBox_.w = IniGetFloat(ini_, "text", "box_w", static_cast<float>(designW_) - 160.f);
  textBox_.h = IniGetFloat(ini_, "text", "box_h", 160.f);
  return true;
}

void Engine::Shutdown() {
  audio_.Shutdown();
  texCache_.clear();
  renderer_.Shutdown();
  if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
  SDL_Quit();
}

// ================================================================== 资源
bool Engine::LoadMedia(const std::string& name, std::vector<uint8_t>& out) {
  if (archive_ && archive_->Read(name, out)) return true;
  if (FileSystem::ReadFile(name, out)) return true;
  if (Extension(name).empty()) {
    for (const std::string& ext : imageExts_) {
      std::string candidate = name + "." + ext;
      if (LoadMedia(candidate, out)) return true;
    }
  }
  return false;
}

TexturePtr Engine::LoadTexture(const std::string& name) {
  if (name.empty()) return nullptr;
  auto it = texCache_.find(name);
  if (it != texCache_.end()) return it->second;

  std::vector<uint8_t> data;
  if (!LoadMedia(name, data)) {
    SIGLUS_LOGW("缺少图像资源: %s", name.c_str());
    texCache_[name] = nullptr;
    return nullptr;
  }
  Bitmap bmp;
  if (!ImageRegistry::Get().Decode(name, data.data(), data.size(), bmp)) {
    SIGLUS_LOGW("图像解码失败: %s", name.c_str());
    texCache_[name] = nullptr;
    return nullptr;
  }
  TexturePtr tex = renderer_.CreateTextureFromBitmap(bmp);
  texCache_[name] = tex;
  return tex;
}

// ================================================================== 宿主实现
void Engine::SetSpeaker(const std::string& name) { speaker_ = name; }

void Engine::ShowText(const std::string& text) {
  fullText_ = text;
  totalCp_  = Utf8Count(text);
  shownCp_  = 0;
  textTimer_ = 0.f;
  textActive_ = true;
  autoWait_ = 0.f;
  if (!text.empty()) {
    std::string line = speaker_.empty() ? text : (speaker_ + "：「" + text + "」");
    history_.push_back(line);
    if (history_.size() > 200) history_.erase(history_.begin());
  }
  if (skipMode_) shownCp_ = totalCp_;
}

void Engine::SetBackground(const std::string& image) {
  TexturePtr old = bgTex_;
  if (image == bgName_ && bgTex_) return;
  bgName_ = image;
  bgTex_  = LoadTexture(image);
  if (old && bgTex_ && pendingTransMs_ > 0) {
    trans_.active = true;
    trans_.from   = old;
    trans_.t      = 0.f;
    trans_.dur    = static_cast<float>(pendingTransMs_);
    trans_.type   = pendingTransType_ == 2 ? TransitionType::Fade
                  : pendingTransType_ == 3 ? TransitionType::MaskBlend
                                           : TransitionType::CrossFade;
    trans_.mask   = trans_.type == TransitionType::MaskBlend
                        ? LoadTexture(IniGet(ini_, "game", "mask_image", "mask.png"))
                        : nullptr;
  }
}

void Engine::SetLayer(int layer, const std::string& image) {
  if (layer < 0 || layer >= kMaxLayers) return;
  Layer& L = layers_[layer];
  bool changed = (L.image != image);
  L.image   = image;
  L.tex     = LoadTexture(image);
  L.visible = true;
  if (changed) {
    if (L.fadeTo <= 0.f) L.fadeTo = 1.f;
    L.fadeFrom = 0.f;
    L.alpha    = 0.f;
    L.fadeT    = 0.f;
    L.fadeDur  = static_cast<float>(pendingTransMs_);
    if (L.fadeDur <= 0.f) { L.alpha = L.fadeTo; L.fadeT = 1.f; }
  }
}

void Engine::SetLayerPos(int layer, float x, float y) {
  if (layer < 0 || layer >= kMaxLayers) return;
  layers_[layer].x = x;
  layers_[layer].y = y;
}

void Engine::SetLayerAlpha(int layer, float a) {
  if (layer < 0 || layer >= kMaxLayers) return;
  Layer& L = layers_[layer];
  L.fadeFrom = L.alpha;
  L.fadeTo   = a;
  L.fadeT    = 0.f;
  L.fadeDur  = static_cast<float>(pendingTransMs_);
  if (L.fadeDur <= 0.f) { L.alpha = a; L.fadeT = 1.f; }
}

void Engine::SetLayerBlend(int layer, int mode) {
  if (layer < 0 || layer >= kMaxLayers) return;
  layers_[layer].blend = mode;
}

void Engine::ClearLayer(int layer) {
  if (layer < 0 || layer >= kMaxLayers) return;
  layers_[layer] = Layer{};
}

void Engine::ClearLayers() {
  for (Layer& L : layers_) L = Layer{};
}

void Engine::PlayBgm(const std::string& name, bool loop) {
  uint32_t loopStart = static_cast<uint32_t>(IniGetInt(ini_, "loop", name + "_start", 0));
  uint32_t loopLen   = static_cast<uint32_t>(IniGetInt(ini_, "loop", name + "_length", 0));
  audio_.PlayBgm(name, loop, loopStart, loopLen);
}

void Engine::StopBgm() { audio_.StopBgm(); }
void Engine::PlaySe(const std::string& name) { audio_.PlaySe(name, 0); }
void Engine::PlayVoice(const std::string& name) { audio_.PlayVoice(name); }

void Engine::PlayMovie(const std::string& name) {
  std::string rel = "movie/" + name + ".mp4";
  if (!FileSystem::Exists(rel)) {
    std::vector<uint8_t> data;
    if (LoadMedia(rel, data)) {
      FileSystem::MakeDirs("movie");
      FileSystem::WriteFile(rel, data);
    } else {
      SIGLUS_LOGW("找不到影片（需预先把 .omv 转成 mp4 放到 movie/）：%s", name.c_str());
      vm_.NotifyMovieEnd();
      return;
    }
  }
  VideoPlayer().Play(FileSystem::Join(FileSystem::Root(), rel));
}

void Engine::Select(const std::vector<std::string>& options) { options_ = options; }

void Engine::Transition(int type, int ms) {
  pendingTransType_ = type;
  pendingTransMs_   = ms;
}

// ================================================================== 主循环
int Engine::Run() {
  running_ = true;
  Uint32 last = SDL_GetTicks();
  while (running_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) { running_ = false; }
      else if (e.type == SDL_APP_WILLENTERBACKGROUND) { paused_ = true; }
      else if (e.type == SDL_APP_DIDENTERFOREGROUND) { paused_ = false; last = SDL_GetTicks(); }
      else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
        int vw = 0, vh = 0;
        SDL_GetWindowSize(window_, &vw, &vh);
        gestures_.SetViewportSize(vw, vh);
      } else {
        Gesture g = gestures_.Feed(e);
        if (g != Gesture::None) OnGesture(g);
      }
    }

    Uint32 now = SDL_GetTicks();
    float  dt  = std::min((now - last) / 1000.f, 0.1f);
    last = now;

    if (!paused_) Update(dt);
    Render();
    SDL_Delay(1);
  }
  return 0;
}

void Engine::Update(float dt) {
  audio_.Update(dt);
  vm_.Tick(dt);

  if (trans_.active) {
    trans_.t += dt * 1000.f;
    if (trans_.t >= trans_.dur) trans_.active = false;
  }
  for (Layer& L : layers_) {
    if (L.fadeDur > 0.f && L.fadeT < 1.f) {
      L.fadeT = std::min(1.f, L.fadeT + dt * 1000.f / L.fadeDur);
      L.alpha = L.fadeFrom + (L.fadeTo - L.fadeFrom) * L.fadeT;
    }
  }

  if (textActive_) {
    textTimer_ += dt;
    shownCp_ = std::min(totalCp_, static_cast<size_t>(textTimer_ * charsPerSec_));
  }

  if (VideoPlayer().IsPlaying()) return;

  VM::Status st = vm_.Run(512);
  if (st == VM::Status::WaitMovie && !VideoPlayer().IsPlaying()) vm_.NotifyMovieEnd();

  if (st == VM::Status::WaitClick) {
    if (skipMode_) {
      vm_.NotifyClick();
    } else if (autoMode_ && shownCp_ >= totalCp_) {
      autoWait_ += dt;
      if (autoWait_ > 0.8f) { autoWait_ = 0.f; vm_.NotifyClick(); }
    }
  } else if (st == VM::Status::End) {
    SIGLUS_LOGI("脚本执行结束");
    running_ = false;
  }
}

// ================================================================== 渲染
void Engine::Render() {
  renderer_.BeginFrame();

  if (trans_.active) {
    renderer_.DrawTransition(trans_.type, trans_.from, bgTex_,
                             trans_.dur > 0 ? trans_.t / trans_.dur : 1.f, trans_.mask);
  } else if (bgTex_) {
    Sprite s;
    s.tex = bgTex_;
    s.dst = RectF{0, 0, static_cast<float>(designW_), static_cast<float>(designH_)};
    renderer_.DrawSprite(s);
  }

  for (int i = 0; i < kMaxLayers; ++i) {
    const Layer& L = layers_[i];
    if (!L.visible || !L.tex || L.alpha <= 0.f) continue;
    float y = L.y > 0.f ? L.y : static_cast<float>(designH_);
    Sprite s;
    s.tex   = L.tex;
    s.dst   = RectF{L.x - static_cast<float>(L.tex->w) * 0.5f,
                    y - static_cast<float>(L.tex->h),
                    static_cast<float>(L.tex->w), static_cast<float>(L.tex->h)};
    s.alpha = L.alpha;
    s.blend = L.blend == 1 ? BlendMode::Add
            : L.blend == 2 ? BlendMode::Mod
            : L.blend == 3 ? BlendMode::Mul
                           : BlendMode::Normal;
    renderer_.DrawSprite(s);
  }

  if (!uiHidden_) {
    RenderTextWindow();
    RenderSelect();
  }
  if (historyOpen_) RenderHistory();
  if (menuOpen_) RenderMenu();
  RenderHud();

  renderer_.EndFrame();
}

void Engine::RenderTextWindow() {
  if (!textActive_ && fullText_.empty()) return;
  renderer_.FillRect(textBox_, textBoxColor_);

  std::string shown = Utf8Prefix(fullText_, shownCp_);
  text_.Draw(renderer_, shown, textBox_.x + 32.f, textBox_.y + 40.f, textColor_, false,
             textBox_.w - 64.f, 8.f);

  if (!speaker_.empty()) {
    RectF nameBox{textBox_.x + 24.f, textBox_.y - 44.f, 320.f, 40.f};
    renderer_.FillRect(nameBox, textBoxColor_);
    text_.Draw(renderer_, speaker_, nameBox.x + 12.f, nameBox.y + 4.f, textColor_, false,
               nameBox.w - 24.f, 0.f);
  }

  // 等待点击的小三角
  if (shownCp_ >= totalCp_ && vm_.status() == VM::Status::WaitClick) {
    float cx = textBox_.x + textBox_.w - 40.f;
    float cy = textBox_.y + textBox_.h - 32.f;
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() / 250.f);
    renderer_.FillRect(RectF{cx - 12.f, cy - 8.f + pulse * 6.f, 24.f, 16.f},
                       Color{255, 255, 255, 200});
  }
}

void Engine::RenderSelect() {
  if (options_.empty()) return;
  optionRects_.clear();

  const float boxW = designW_ * 0.6f;
  const float rowH = 64.f;
  const float totalH = rowH * static_cast<float>(options_.size()) + 40.f;
  const float top = (static_cast<float>(designH_) - totalH) * 0.5f;
  const float left = (static_cast<float>(designW_) - boxW) * 0.5f;

  renderer_.FillRect(RectF{left, top, boxW, totalH}, Color{0, 0, 0, 200});
  for (size_t i = 0; i < options_.size(); ++i) {
    RectF r{left + 20.f, top + 20.f + rowH * static_cast<float>(i), boxW - 40.f, rowH - 8.f};
    renderer_.FillRect(r, Color{60, 60, 80, 220});
    text_.Draw(renderer_, options_[i], r.x + 16.f, r.y + 8.f, textColor_, false, r.w - 32.f, 0.f);
    optionRects_.push_back(r);
  }
}

void Engine::RenderHistory() {
  renderer_.FillRect(RectF{0, 0, static_cast<float>(designW_), static_cast<float>(designH_)},
                     Color{0, 0, 0, 230});
  float y = 40.f;
  const size_t maxLines = 14;
  size_t start = history_.size() > maxLines ? history_.size() - maxLines : 0;
  for (size_t i = start; i < history_.size(); ++i) {
    text_.Draw(renderer_, history_[i], 60.f, y, textColor_, false,
               static_cast<float>(designW_) - 120.f, 6.f);
    y += font_.lineHeight() + 8.f;
  }
}

void Engine::RenderMenu() {
  const char* items[] = {"存档", "读档", "返回"};
  const float boxW = 320.f;
  const float rowH = 72.f;
  const float left = (static_cast<float>(designW_) - boxW) * 0.5f;
  const float top = (static_cast<float>(designH_) - rowH * 3.f) * 0.5f;
  renderer_.FillRect(RectF{left, top, boxW, rowH * 3.f}, Color{20, 20, 30, 240});
  for (int i = 0; i < 3; ++i) {
    RectF r{left + 10.f, top + 10.f + rowH * static_cast<float>(i), boxW - 20.f, rowH - 10.f};
    renderer_.FillRect(r, Color{70, 70, 90, 230});
    text_.Draw(renderer_, items[i], r.x + 20.f, r.y + 14.f, textColor_, false, r.w - 40.f, 0.f);
  }
}

void Engine::RenderHud() {
  if (autoMode_ || skipMode_) {
    renderer_.FillRect(RectF{static_cast<float>(designW_) - 60.f, 20.f, 40.f, 16.f},
                       skipMode_ ? Color{255, 120, 60, 220} : Color{80, 200, 255, 220});
  }
}

// ================================================================== 交互
void Engine::PointerToLogical(float nx, float ny, float& lx, float& ly) {
  int ww = 0, wh = 0;
  SDL_GetWindowSize(window_, &ww, &wh);
  if (ww <= 0 || wh <= 0) { lx = ly = 0.f; return; }
  const float wx = nx * static_cast<float>(ww);
  const float wy = ny * static_cast<float>(wh);
  const float scale = std::min(static_cast<float>(ww) / designW_,
                               static_cast<float>(wh) / designH_);
  const float vw = designW_ * scale;
  const float vh = designH_ * scale;
  lx = (wx - (static_cast<float>(ww) - vw) * 0.5f) / scale;
  ly = (wy - (static_cast<float>(wh) - vh) * 0.5f) / scale;
}

bool Engine::HitOption(float lx, float ly, int& index) {
  for (size_t i = 0; i < optionRects_.size(); ++i) {
    const RectF& r = optionRects_[i];
    if (lx >= r.x && lx <= r.x + r.w && ly >= r.y && ly <= r.y + r.h) {
      index = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

void Engine::OnGesture(Gesture g) {
  float lx = 0.f, ly = 0.f;
  PointerToLogical(gestures_.x(), gestures_.y(), lx, ly);

  switch (g) {
    case Gesture::Tap: {
      if (menuOpen_) {
        const float boxW = 320.f, rowH = 72.f;
        const float left = (static_cast<float>(designW_) - boxW) * 0.5f;
        const float top = (static_cast<float>(designH_) - rowH * 3.f) * 0.5f;
        for (int i = 0; i < 3; ++i) {
          float y0 = top + 10.f + rowH * static_cast<float>(i);
          if (lx >= left && lx <= left + boxW && ly >= y0 && ly <= y0 + rowH - 10.f) {
            if (i == 0) SaveSlot(0);
            else if (i == 1) LoadSlot(0);
            menuOpen_ = false;
            return;
          }
        }
        menuOpen_ = false;
        return;
      }
      if (historyOpen_) { historyOpen_ = false; return; }
      int idx = 0;
      if (!options_.empty() && HitOption(lx, ly, idx)) {
        options_.clear();
        optionRects_.clear();
        vm_.NotifySelect(idx);
        return;
      }
      // 首次点击先补全整行文本，再点才推进
      if (shownCp_ < totalCp_) shownCp_ = totalCp_;
      else vm_.NotifyClick();
      return;
    }
    case Gesture::LongPress:
      skipMode_ = !skipMode_;
      SIGLUS_LOGI("快进: %s", skipMode_ ? "开" : "关");
      return;
    case Gesture::DoubleTap:
      autoMode_ = !autoMode_;
      SIGLUS_LOGI("自动: %s", autoMode_ ? "开" : "关");
      return;
    case Gesture::TwoFingerTap:
    case Gesture::Menu:
      menuOpen_ = !menuOpen_;
      return;
    case Gesture::SwipeUp:
      historyOpen_ = !historyOpen_;
      return;
    case Gesture::SwipeDown:
      uiHidden_ = !uiHidden_;
      return;
    case Gesture::SwipeRight:
      menuOpen_ = !menuOpen_;
      return;
    case Gesture::SwipeLeft:
    case Gesture::Back:
      if (historyOpen_) historyOpen_ = false;
      else if (menuOpen_) menuOpen_ = false;
      else running_ = false;
      return;
    case Gesture::QuickSave:
      SaveSlot(0);
      return;
    case Gesture::QuickLoad:
      LoadSlot(0);
      return;
    case Gesture::WheelUp:
      historyOpen_ = true;
      return;
    case Gesture::WheelDown:
      historyOpen_ = false;
      return;
    default:
      return;
  }
}

// ================================================================== 存档
std::string Engine::SlotPath(int slot) const {
  return "save/save" + std::to_string(slot) + ".sav";
}

void Engine::SaveSlot(int slot) {
  std::string s = "; siglus save v1\n";
  s += "bg=" + Escape(bgName_) + "\n";
  for (int i = 0; i < kMaxLayers; ++i) {
    const Layer& L = layers_[i];
    if (!L.visible && L.image.empty()) continue;
    s += "layer=" + std::to_string(i) + "," + Escape(L.image) + "," +
         std::to_string(L.x) + "," + std::to_string(L.y) + "," +
         std::to_string(L.alpha) + "," + std::to_string(L.blend) + "," +
         (L.visible ? "1" : "0") + "\n";
  }
  s += "speaker=" + Escape(speaker_) + "\n";
  s += "text=" + Escape(fullText_) + "\n";
  s += "shown=" + std::to_string(shownCp_) + "\n";
  s += "--vm--\n";
  s += vm_.Serialize();

  FileSystem::MakeDirs("save");
  if (FileSystem::WriteText(SlotPath(slot), s))
    SIGLUS_LOGI("已存档 -> %s", SlotPath(slot).c_str());
  else
    SIGLUS_LOGE("存档失败: %s", SlotPath(slot).c_str());
}

bool Engine::LoadSlot(int slot) {
  std::vector<uint8_t> raw;
  if (!FileSystem::ReadFile(SlotPath(slot), raw)) {
    SIGLUS_LOGW("没有存档: %s", SlotPath(slot).c_str());
    return false;
  }
  std::string text(raw.begin(), raw.end());
  size_t vmPos = text.find("--vm--\n");
  std::string head = (vmPos == std::string::npos) ? text : text.substr(0, vmPos);
  std::string vmPart = (vmPos == std::string::npos) ? std::string()
                                                    : text.substr(vmPos + 7);

  ClearLayers();
  bgName_.clear();
  bgTex_ = nullptr;
  speaker_.clear();
  fullText_.clear();
  shownCp_ = totalCp_ = 0;
  trans_.active = false;
  options_.clear();

  size_t i = 0;
  while (i < head.size()) {
    size_t eol = head.find('\n', i);
    if (eol == std::string::npos) eol = head.size();
    std::string line = Trim(head.substr(i, eol - i));
    i = eol + 1;
    if (line.empty() || line[0] == ';') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);

    if (key == "bg") {
      if (!val.empty()) { bgName_ = val; bgTex_ = LoadTexture(val); }
    } else if (key == "layer") {
      std::vector<std::string> f = Split(val, ',');
      if (f.size() >= 7) {
        int idx = std::atoi(f[0].c_str());
        if (idx >= 0 && idx < kMaxLayers) {
          Layer& L = layers_[idx];
          L.image   = Unescape(f[1]);
          L.x       = static_cast<float>(std::atof(f[2].c_str()));
          L.y       = static_cast<float>(std::atof(f[3].c_str()));
          L.alpha   = static_cast<float>(std::atof(f[4].c_str()));
          L.blend   = std::atoi(f[5].c_str());
          L.visible = f[6] == "1";
          L.tex     = L.image.empty() ? nullptr : LoadTexture(L.image);
          L.fadeTo  = L.alpha;
          L.fadeT   = 1.f;
          L.fadeDur = 0.f;
        }
      }
    } else if (key == "speaker") {
      speaker_ = Unescape(val);
    } else if (key == "text") {
      fullText_ = Unescape(val);
      totalCp_  = Utf8Count(fullText_);
      textActive_ = true;
    } else if (key == "shown") {
      shownCp_ = static_cast<size_t>(std::atol(val.c_str()));
    }
  }

  if (!vmPart.empty()) vm_.Deserialize(vmPart);
  SIGLUS_LOGI("已读档 <- %s", SlotPath(slot).c_str());
  return true;
}

}  // namespace siglus
