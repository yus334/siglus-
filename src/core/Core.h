#pragma once
// 基础层：日志 / 字节序 / 流 / 字符串与 UTF-8 / 极简 INI。全部 header-only。

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace siglus {

enum class LogLevel { Debug = 0, Info, Warn, Error };

inline void Log(LogLevel lv, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
#if defined(__ANDROID__)
  int prio = ANDROID_LOG_DEBUG;
  switch (lv) {
    case LogLevel::Debug: prio = ANDROID_LOG_DEBUG; break;
    case LogLevel::Info:  prio = ANDROID_LOG_INFO;  break;
    case LogLevel::Warn:  prio = ANDROID_LOG_WARN;  break;
    case LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
  }
  __android_log_print(prio, "siglus", "%s", buf);
#else
  static const char* kTags[] = {"D", "I", "W", "E"};
  std::fprintf(stderr, "[%s] %s\n", kTags[static_cast<int>(lv)], buf);
#endif
}

#define SIGLUS_LOGD(...) ::siglus::Log(::siglus::LogLevel::Debug, __VA_ARGS__)
#define SIGLUS_LOGI(...) ::siglus::Log(::siglus::LogLevel::Info, __VA_ARGS__)
#define SIGLUS_LOGW(...) ::siglus::Log(::siglus::LogLevel::Warn, __VA_ARGS__)
#define SIGLUS_LOGE(...) ::siglus::Log(::siglus::LogLevel::Error, __VA_ARGS__)

// ---------------------------------------------------------------- 字节序
inline uint16_t Rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------- 流
class Stream {
 public:
  virtual ~Stream() = default;
  virtual bool    Seek(int64_t off) = 0;
  virtual int64_t Tell() const = 0;
  virtual int64_t Size() const = 0;
  virtual size_t  Read(void* dst, size_t n) = 0;

  bool ReadExact(void* dst, size_t n) { return Read(dst, n) == n; }
  uint8_t  U8()  { uint8_t v = 0; Read(&v, 1); return v; }
  uint16_t U16() { uint8_t b[2] = {0, 0}; ReadExact(b, 2); return Rd16(b); }
  uint32_t U32() { uint8_t b[4] = {0, 0, 0, 0}; ReadExact(b, 4); return Rd32(b); }
  int32_t  I32() { return static_cast<int32_t>(U32()); }
  std::string CString() {
    std::string s;
    char c = 1;
    while (Read(&c, 1) == 1 && c != 0) s.push_back(c);
    return s;
  }
  std::vector<uint8_t> Rest() {
    std::vector<uint8_t> out(static_cast<size_t>(std::max<int64_t>(0, Size() - Tell())));
    if (!out.empty()) Read(out.data(), out.size());
    return out;
  }
};

class MemoryStream : public Stream {
 public:
  MemoryStream(const uint8_t* data, size_t len) : data_(data), size_(len) {}
  bool    Seek(int64_t off) override { pos_ = (off < 0) ? 0 : (off > size_ ? size_ : static_cast<size_t>(off)); return true; }
  int64_t Tell() const override { return static_cast<int64_t>(pos_); }
  int64_t Size() const override { return static_cast<int64_t>(size_); }
  size_t  Read(void* dst, size_t n) override {
    size_t avail = size_ - pos_;
    size_t take = n < avail ? n : avail;
    if (take) std::memcpy(dst, data_ + pos_, take);
    pos_ += take;
    return take;
  }

 private:
  const uint8_t* data_;
  size_t         size_;
  size_t         pos_ = 0;
};

class FileStream : public Stream {
 public:
  FileStream() = default;
  ~FileStream() override { Close(); }
  bool Open(const std::string& path) {
    Close();
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) return false;
    std::fseek(f_, 0, SEEK_END);
    size_ = std::ftell(f_);
    std::fseek(f_, 0, SEEK_SET);
    return true;
  }
  void Close() { if (f_) { std::fclose(f_); f_ = nullptr; } }
  bool IsOpen() const { return f_ != nullptr; }

  bool    Seek(int64_t off) override { return f_ && std::fseek(f_, static_cast<long>(off), SEEK_SET) == 0; }
  int64_t Tell() const override { return f_ ? std::ftell(f_) : 0; }
  int64_t Size() const override { return size_; }
  size_t  Read(void* dst, size_t n) override { return f_ ? std::fread(dst, 1, n, f_) : 0; }

 private:
  FILE*   f_ = nullptr;
  int64_t size_ = 0;
};

// ---------------------------------------------------------------- 字符串
inline std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}
inline std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
inline std::string Extension(const std::string& path) {
  size_t p = path.find_last_of('.');
  if (p == std::string::npos) return {};
  return ToLower(path.substr(p + 1));
}
inline std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

// ---------------------------------------------------------------- UTF-8
inline size_t Utf8Count(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
  return n;
}
inline std::vector<uint32_t> Utf8Decode(const std::string& s) {
  std::vector<uint32_t> out;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = 0;
    int extra = 0;
    if (c < 0x80)              { cp = c;        extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else                        { cp = c;        extra = 0; }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    out.push_back(cp);
  }
  return out;
}
inline std::string Utf8Encode(uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return s;
}

// ---------------------------------------------------------------- 极简 INI
using IniDoc = std::unordered_map<std::string,
                                  std::unordered_map<std::string, std::string>>;

inline IniDoc IniParse(const std::string& text) {
  IniDoc doc;
  std::string section;
  size_t i = 0;
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(i, eol - i);
    i = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#') continue;
    if (t.front() == '[' && t.back() == ']') {
      section = Trim(t.substr(1, t.size() - 2));
      continue;
    }
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Trim(t.substr(0, eq));
    std::string val = Trim(t.substr(eq + 1));
    // 去掉配对引号
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
      val = val.substr(1, val.size() - 2);
    doc[section][key] = val;
  }
  return doc;
}

inline std::string IniGet(const IniDoc& doc, const std::string& section,
                          const std::string& key, const std::string& def = {}) {
  auto s = doc.find(section);
  if (s == doc.end()) return def;
  auto k = s->second.find(key);
  return k == s->second.end() ? def : k->second;
}
inline int IniGetInt(const IniDoc& doc, const std::string& section,
                     const std::string& key, int def) {
  std::string v = IniGet(doc, section, key);
  return v.empty() ? def : std::atoi(v.c_str());
}
inline float IniGetFloat(const IniDoc& doc, const std::string& section,
                         const std::string& key, float def) {
  std::string v = IniGet(doc, section, key);
  return v.empty() ? def : static_cast<float>(std::atof(v.c_str()));
}

// ---------------------------------------------------------------- 头部转储
inline std::string HexDump(const uint8_t* data, size_t len, size_t base = 0) {
  std::string out;
  char line[128];
  for (size_t i = 0; i < len; i += 16) {
    int n = static_cast<int>(std::min<size_t>(16, len - i));
    int p = std::snprintf(line, sizeof(line), "%08X  ", static_cast<unsigned>(base + i));
    out.append(line, p);
    for (int k = 0; k < 16; ++k) {
      if (k < n) p = std::snprintf(line, sizeof(line), "%02X ", data[i + k]);
      else       p = std::snprintf(line, sizeof(line), "   ");
      out.append(line, p);
    }
    out += " |";
    for (int k = 0; k < n; ++k) {
      unsigned char c = data[i + k];
      out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
    }
    out += "|\n";
  }
  return out;
}

}  // namespace siglus
