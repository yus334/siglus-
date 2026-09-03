#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Core.h"

namespace siglus {

struct ArchiveEntry {
  std::string name;
  uint64_t    offset = 0;
  uint64_t    size   = 0;
};

class IArchive {
 public:
  virtual ~IArchive() = default;
  virtual bool Open(const std::string& path) = 0;
  virtual const std::vector<ArchiveEntry>& Entries() const = 0;
  virtual bool Read(const std::string& name, std::vector<uint8_t>& out) = 0;
  virtual bool Has(const std::string& name) const = 0;
};

// 目录型"封包"：直接用解包后的目录，开发期最方便，也用于安卓端从 SAF 拷进来的目录。
class DirArchive : public IArchive {
 public:
  bool Open(const std::string& path) override;
  const std::vector<ArchiveEntry>& Entries() const override { return entries_; }
  bool Read(const std::string& name, std::vector<uint8_t>& out) override;
  bool Has(const std::string& name) const override;

 private:
  std::string              root_;
  std::vector<ArchiveEntry> entries_;
};

// .pkg 条目布局。真实布局必须用 siglus_probe 对实机样本校准后写入
// assets/pkg_layout.ini，不要凭猜测填。
struct PkgLayout {
  uint32_t countOffset = 0;    // 条目数量字段的文件偏移
  uint32_t entryBase   = 4;    // 条目表起始偏移
  int      entryStride = 32;   // 每条字节数
  int      nameOffset  = 0;    // 条目内文件名字段偏移
  int      nameLen     = 0;    // <=0 表示以 '\0' 结尾
  int      offsetField = 16;   // 条目内数据偏移字段
  int      sizeField   = 20;   // 条目内数据长度字段
  int      fieldSize   = 4;    // 偏移/长度字段宽度（4 或 8）
  uint64_t dataBase    = 0;    // 数据区基址，最终 offset += dataBase
  uint64_t countAdjust = 0;    // 某些包 count 字段需要偏移修正
};

PkgLayout LoadPkgLayout(const IniDoc& ini, const std::string& section = "pkg");

class PkgArchive : public IArchive {
 public:
  explicit PkgArchive(PkgLayout layout = PkgLayout()) : layout_(layout) {}
  bool Open(const std::string& path) override;
  const std::vector<ArchiveEntry>& Entries() const override { return entries_; }
  bool Read(const std::string& name, std::vector<uint8_t>& out) override;
  bool Has(const std::string& name) const override;

 private:
  bool ParseIndex();

  PkgLayout               layout_;
  std::string             path_;
  std::vector<uint8_t>    file_;
  std::vector<ArchiveEntry> entries_;
};

// 启发式扫描：枚举 (count 偏移, 条目步长) 组合，挑出结构上说得通的候选布局。
// 只是"缩小范围"，最终仍要人工核对条目名是否为可打印文件名。
std::vector<PkgLayout> GuessPkgLayouts(const std::vector<uint8_t>& data, int maxCandidates = 12);

}  // namespace siglus
