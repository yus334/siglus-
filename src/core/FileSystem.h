#pragma once

#include <string>
#include <vector>

#include "core/Core.h"

namespace siglus {

// 统一的数据根。PC 上指向 assets/ 或游戏目录；安卓上指向
// getFilesDir()/game（由 SiglusActivity 在 JNI 层设置）。
class FileSystem {
 public:
  static void        SetRoot(const std::string& root);
  static const std::string& Root();
  static std::string Join(const std::string& a, const std::string& b);
  static std::string DirName(const std::string& path);

  static bool Exists(const std::string& rel);
  static bool IsDir(const std::string& rel);
  static bool ReadFile(const std::string& rel, std::vector<uint8_t>& out);
  static bool WriteFile(const std::string& rel, const std::vector<uint8_t>& data);
  static bool WriteText(const std::string& rel, const std::string& text);
  static bool MakeDirs(const std::string& rel);
  static std::vector<std::string> ListFiles(const std::string& relDir, bool recursive);
};

}  // namespace siglus
