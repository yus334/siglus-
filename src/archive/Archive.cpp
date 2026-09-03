#include "archive/Archive.h"

#include <algorithm>
#include <cstdio>

#include "core/FileSystem.h"

namespace siglus {

// ------------------------------------------------------------- DirArchive
bool DirArchive::Open(const std::string& path) {
  root_ = path;
  entries_.clear();
  if (!FileSystem::IsDir(root_)) {
    SIGLUS_LOGE("DirArchive: 不是目录 %s", path.c_str());
    return false;
  }
  for (const std::string& rel : FileSystem::ListFiles(root_, true)) {
    ArchiveEntry e;
    e.name = rel;
    std::vector<uint8_t> tmp;
    if (FileSystem::ReadFile(rel, tmp)) e.size = tmp.size();
    entries_.push_back(std::move(e));
  }
  SIGLUS_LOGI("DirArchive: %s -> %zu 个文件", path.c_str(), entries_.size());
  return true;
}

bool DirArchive::Has(const std::string& name) const {
  for (const auto& e : entries_) if (e.name == name) return true;
  return false;
}

bool DirArchive::Read(const std::string& name, std::vector<uint8_t>& out) {
  return FileSystem::ReadFile(FileSystem::Join(root_, name), out);
}

// ------------------------------------------------------------- PkgLayout
PkgLayout LoadPkgLayout(const IniDoc& ini, const std::string& section) {
  PkgLayout l;
  l.countOffset = static_cast<uint32_t>(IniGetInt(ini, section, "count_offset", 0));
  l.entryBase   = static_cast<uint32_t>(IniGetInt(ini, section, "entry_base", 4));
  l.entryStride = IniGetInt(ini, section, "entry_stride", 32);
  l.nameOffset  = IniGetInt(ini, section, "name_offset", 0);
  l.nameLen     = IniGetInt(ini, section, "name_len", 0);
  l.offsetField = IniGetInt(ini, section, "offset_field", 16);
  l.sizeField   = IniGetInt(ini, section, "size_field", 20);
  l.fieldSize   = IniGetInt(ini, section, "field_size", 4);
  l.dataBase    = static_cast<uint64_t>(std::atoll(IniGet(ini, section, "data_base", "0").c_str()));
  l.countAdjust = static_cast<uint64_t>(std::atoll(IniGet(ini, section, "count_adjust", "0").c_str()));
  return l;
}

// ------------------------------------------------------------- PkgArchive
bool PkgArchive::Open(const std::string& path) {
  path_ = path;
  if (!FileSystem::ReadFile(path, file_)) {
    SIGLUS_LOGE("PkgArchive: 读取失败 %s", path.c_str());
    return false;
  }
  if (!ParseIndex()) return false;
  SIGLUS_LOGI("PkgArchive: %s -> %zu 条", path.c_str(), entries_.size());
  return true;
}

bool PkgArchive::Has(const std::string& name) const {
  for (const auto& e : entries_) if (e.name == name) return true;
  return false;
}

bool PkgArchive::ParseIndex() {
  entries_.clear();
  const PkgLayout& L = layout_;
  if (file_.size() < L.countOffset + 4) {
    SIGLUS_LOGE("PkgArchive: 文件太小");
    return false;
  }
  uint32_t count = Rd32(file_.data() + L.countOffset);
  count = static_cast<uint32_t>(static_cast<uint64_t>(count) + L.countAdjust);
  if (count == 0 || count > 200000u) {
    SIGLUS_LOGE("PkgArchive: 条目数异常 count=%u（检查 count_offset/data_base）", count);
    return false;
  }
  const size_t stride = static_cast<size_t>(std::max(1, L.entryStride));
  if (L.entryBase + stride * static_cast<size_t>(count) > file_.size()) {
    SIGLUS_LOGE("PkgArchive: 条目表越界（检查 entry_base/entry_stride）");
    return false;
  }

  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = file_.data() + L.entryBase + stride * i;
    ArchiveEntry ent;

    if (L.nameLen > 0) {
      ent.name.assign(reinterpret_cast<const char*>(e + L.nameOffset),
                      static_cast<size_t>(L.nameLen));
      // 去掉尾部 '\0'
      while (!ent.name.empty() && ent.name.back() == '\0') ent.name.pop_back();
    } else {
      ent.name.assign(reinterpret_cast<const char*>(e + L.nameOffset));
    }

    uint64_t off = 0, sz = 0;
    if (L.fieldSize == 8) {
      off = 0;
      for (int b = 7; b >= 0; --b) off = (off << 8) | e[L.offsetField + b];
      for (int b = 7; b >= 0; --b) sz = (sz << 8) | e[L.sizeField + b];
    } else {
      off = Rd32(e + L.offsetField);
      sz  = Rd32(e + L.sizeField);
    }
    ent.offset = off + L.dataBase;
    ent.size   = sz;
    entries_.push_back(std::move(ent));
  }
  return true;
}

bool PkgArchive::Read(const std::string& name, std::vector<uint8_t>& out) {
  for (const auto& e : entries_) {
    if (e.name != name) continue;
    if (e.offset + e.size > file_.size()) {
      SIGLUS_LOGE("PkgArchive: %s 越界 (off=%llu size=%llu)", name.c_str(),
                  static_cast<unsigned long long>(e.offset),
                  static_cast<unsigned long long>(e.size));
      return false;
    }
    out.assign(file_.begin() + static_cast<long>(e.offset),
               file_.begin() + static_cast<long>(e.offset + e.size));
    return true;
  }
  return false;
}

// ------------------------------------------------------------- 启发式探测
std::vector<PkgLayout> GuessPkgLayouts(const std::vector<uint8_t>& data, int maxCandidates) {
  struct Cand { PkgLayout layout; int score; };
  std::vector<Cand> cands;
  const size_t n = data.size();
  if (n < 32) return {};

  static const uint32_t kCountOffsets[] = {0, 4, 8, 12, 16};
  static const int      kStrides[]      = {16, 20, 24, 28, 32, 36, 40, 48, 64};

  for (uint32_t co : kCountOffsets) {
    if (co + 4 > n) continue;
    uint32_t count = Rd32(data.data() + co);
    if (count == 0 || count > 50000u) continue;
    for (int stride : kStrides) {
      uint64_t tableEnd = static_cast<uint64_t>(co) + 4ull + static_cast<uint64_t>(stride) * count;
      if (tableEnd > n) continue;

      // 检查前若干个条目的 offset/size 是否落在文件范围内
      int plausible = 0;
      int checked = 0;
      for (uint32_t i = 0; i < count && checked < 16; ++i, ++checked) {
        const uint8_t* e = data.data() + co + 4 + static_cast<size_t>(stride) * i;
        uint64_t off = Rd32(e + stride - 8);
        uint64_t sz  = Rd32(e + stride - 4);
        if (off <= n && sz <= n && off + sz <= n && sz > 0) ++plausible;
      }
      if (checked == 0) continue;
      int score = plausible * 100 / checked;
      if (score >= 60 && tableEnd * 100 / n < 60) {  // 条目表不应占据大半个文件
        PkgLayout l;
        l.countOffset = co;
        l.entryBase   = co + 4;
        l.entryStride = stride;
        l.nameOffset  = 0;
        l.nameLen     = 0;
        l.offsetField = stride - 8;
        l.sizeField   = stride - 4;
        l.fieldSize   = 4;
        cands.push_back({l, score});
      }
    }
  }

  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    return a.score > b.score;
  });
  std::vector<PkgLayout> out;
  for (const auto& c : cands) {
    out.push_back(c.layout);
    if (static_cast<int>(out.size()) >= maxCandidates) break;
  }
  return out;
}

}  // namespace siglus
