#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "archive/Archive.h"
#include "audio/Audio.h"
#include "gfx/Renderer.h"
#include "input/Input.h"
#include "script/Script.h"
#include "text/Text.h"

namespace siglus {

constexpr int kMaxLayers = 16;

struct EngineConfig {
  std::string dataRoot  = "assets";
  std::string gameIni   = "game.ini";
  int         windowW   = 1280;
  int         windowH   = 720;
  bool        fullscreen = false;
  bool        landscape  = true;
};

class Engine : public IScriptHost {
 public:
  Engine();
  ~Engine() override;

  bool Init(const EngineConfig& cfg);
  int  Run();
  void Shutdown();

  // ---- IScriptHost ----
  void SetSpeaker(const std::string& name) override;
  void ShowText(const std::string& text) override;
  void SetBackground(const std::string& image) override;
  void SetLayer(int layer, const std::string& image) override;
  void SetLayerPos(int layer, float x, float y) override;
  void SetLayerAlpha(int layer, float a) override;
  void SetLayerBlend(int layer, int mode) override;
  void ClearLayer(int layer) override;
  void ClearLayers() override;
  void PlayBgm(const std::string& name, bool loop) override;
  void StopBgm() override;
  void PlaySe(const std::string& name) override;
  void PlayVoice(const std::string& name) override;
  void PlayMovie(const std::string& name) override;
  void Select(const std::vector<std::string>& options) override;
  void Transition(int type, int ms) override;

 private:
  struct Layer {
    std::string image;
    float       x = 0, y = 0;
    float       alpha = 1.f;
    int         blend = 0;
    bool        visible = false;
    TexturePtr  tex;
    float       fadeFrom = 1.f, fadeTo = 1.f, fadeT = 1.f, fadeDur = 0.f;
  };

  struct Trans {
    bool          active = false;
    TransitionType type  = TransitionType::CrossFade;
    float         t = 0, dur = 0;
    TexturePtr    from;
    TexturePtr    mask;
  };

  bool LoadGameIni();
  bool LoadMedia(const std::string& name, std::vector<uint8_t>& out);
  TexturePtr LoadTexture(const std::string& name);

  void Update(float dt);
  void Render();
  void RenderTextWindow();
  void RenderSelect();
  void RenderHistory();
  void RenderMenu();
  void RenderHud();
  void OnGesture(Gesture g);
  void PointerToLogical(float nx, float ny, float& lx, float& ly);
  bool HitOption(float lx, float ly, int& index);

  void SaveSlot(int slot);
  bool LoadSlot(int slot);
  std::string SlotPath(int slot) const;

  // 配置
  EngineConfig cfg_;
  IniDoc       ini_;
  int          designW_ = 1280, designH_ = 720;
  std::string  scriptPath_;
  std::string  fontPath_;
  int          fontSize_ = 28;
  std::string  dialect_ = "asm";
  std::string  opcodesPath_;
  std::string  archivePath_;
  std::string  archiveType_ = "dir";
  std::vector<std::string> imageExts_{"png", "g00"};

  // 文本窗口
  RectF  textBox_{80, 520, 1120, 160};
  Color  textBoxColor_{0, 0, 0, 160};
  Color  textColor_{255, 255, 255, 255};
  float  charsPerSec_ = 40.f;

  // 运行时
  SDL_Window*   window_ = nullptr;
  Renderer      renderer_;
  AudioSystem   audio_;
  Font          font_;
  TextRenderer  text_;
  GestureDetector gestures_;
  VM            vm_;
  OpcodeTable   opcodes_;
  std::unique_ptr<IArchive> archive_;

  std::unordered_map<std::string, TexturePtr> texCache_;
  std::array<Layer, kMaxLayers> layers_;
  std::string bgName_;
  TexturePtr  bgTex_;
  Trans       trans_;
  int         pendingTransType_ = 1;  // 1=CrossFade 2=Fade 3=MaskBlend
  int         pendingTransMs_ = 400;

  std::string speaker_;
  std::string fullText_;
  size_t      shownCp_ = 0;
  size_t      totalCp_ = 0;
  float       textTimer_ = 0.f;
  bool        textActive_ = false;

  std::vector<std::string> options_;
  std::vector<RectF>       optionRects_;

  std::vector<std::string> history_;
  bool  historyOpen_ = false;
  bool  menuOpen_ = false;
  bool  uiHidden_ = false;
  bool  autoMode_ = false;
  bool  skipMode_ = false;
  bool  running_ = false;
  bool  paused_ = false;
  float autoWait_ = 0.f;
};

}  // namespace siglus
