#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_main.h>

#if defined(__ANDROID__)
#include <SDL_system.h>
#endif

#include "app/Engine.h"
#include "core/Core.h"
#include "core/FileSystem.h"

namespace {

// 安卓端默认数据目录：<filesDir>/game
// 若存在 <filesDir>/siglus_root.txt，则以该文件内容为数据目录（由 SiglusActivity 写入）
std::string AndroidDefaultRoot() {
#if defined(__ANDROID__)
  const char* internal = SDL_AndroidGetInternalStoragePath();
  if (!internal) return "game";
  std::string rootFile = std::string(internal) + "/siglus_root.txt";
  std::vector<uint8_t> buf;
  if (siglus::FileSystem::Exists(rootFile)) {
    // FileSystem::ReadFile 会相对数据根解析，这里直接读绝对路径
    std::ifstream f(rootFile, std::ios::binary);
    if (f) {
      std::string line;
      std::getline(f, line);
      if (!line.empty()) return line;
    }
  }
  return std::string(internal) + "/game";
#else
  return "assets";
#endif
}

}  // namespace

int main(int argc, char** argv) {
  siglus::EngineConfig cfg;
#if defined(__ANDROID__)
  cfg.dataRoot = AndroidDefaultRoot();
#endif

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if ((a == "--data" || a == "-d") && i + 1 < argc) {
      cfg.dataRoot = argv[++i];
    } else if ((a == "--ini" || a == "-c") && i + 1 < argc) {
      cfg.gameIni = argv[++i];
    } else if (a == "--fullscreen" || a == "-f") {
      cfg.fullscreen = true;
    } else if (a == "--portrait") {
      cfg.landscape = false;
    } else if (a == "--help" || a == "-h") {
      std::printf("用法: siglus_pc [--data <目录>] [--ini <game.ini>] [--fullscreen] [--portrait]\n");
      return 0;
    }
  }

  siglus::Engine engine;
  if (!engine.Init(cfg)) {
    siglus::SIGLUS_LOGE("Engine 初始化失败（数据目录: %s）", cfg.dataRoot.c_str());
    return 1;
  }
  int code = engine.Run();
  engine.Shutdown();
  return code;
}
