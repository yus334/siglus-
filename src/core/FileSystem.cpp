#include "core/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace siglus {
namespace fs = std::filesystem;

static std::string g_root = ".";

void FileSystem::SetRoot(const std::string& root) {
  g_root = root.empty() ? "." : root;
}

const std::string& FileSystem::Root() { return g_root; }

std::string FileSystem::Join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (b.front() == '/' || b.front() == '\\') return b;
  char last = a.back();
  if (last == '/' || last == '\\') return a + b;
  return a + "/" + b;
}

std::string FileSystem::DirName(const std::string& path) {
  size_t p = path.find_last_of("/\\");
  return p == std::string::npos ? std::string() : path.substr(0, p);
}

static fs::path Abs(const std::string& rel) {
  if (rel.empty()) return fs::path(g_root);
  fs::path p(rel);
  if (p.is_absolute()) return p;
  return fs::path(g_root) / p;
}

bool FileSystem::Exists(const std::string& rel) {
  std::error_code ec;
  return fs::exists(Abs(rel), ec);
}

bool FileSystem::IsDir(const std::string& rel) {
  std::error_code ec;
  return fs::is_directory(Abs(rel), ec);
}

bool FileSystem::ReadFile(const std::string& rel, std::vector<uint8_t>& out) {
  std::ifstream f(Abs(rel), std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  auto n = static_cast<size_t>(f.tellg());
  f.seekg(0, std::ios::beg);
  out.resize(n);
  if (n) f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
  return true;
}

bool FileSystem::WriteFile(const std::string& rel, const std::vector<uint8_t>& data) {
  std::error_code ec;
  fs::create_directories(Abs(rel).parent_path(), ec);
  std::ofstream f(Abs(rel), std::ios::binary);
  if (!f) return false;
  if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
  return true;
}

bool FileSystem::WriteText(const std::string& rel, const std::string& text) {
  return WriteFile(rel, std::vector<uint8_t>(text.begin(), text.end()));
}

bool FileSystem::MakeDirs(const std::string& rel) {
  std::error_code ec;
  return fs::create_directories(Abs(rel), ec);
}

std::vector<std::string> FileSystem::ListFiles(const std::string& relDir, bool recursive) {
  std::vector<std::string> out;
  std::error_code ec;
  fs::path base = Abs(relDir);
  if (!fs::exists(base, ec)) return out;

  if (recursive) {
    for (fs::recursive_directory_iterator it(base, ec), end; it != end; it.increment(ec)) {
      if (ec) break;
      if (!it->is_directory())
        out.push_back(fs::relative(it->path(), fs::path(g_root)).generic_string());
    }
  } else {
    for (fs::directory_iterator it(base, ec), end; it != end; it.increment(ec)) {
      if (ec) break;
      out.push_back(fs::relative(it->path(), fs::path(g_root)).generic_string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace siglus
