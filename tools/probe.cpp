// siglus_probe：资源探测 / 反汇编 / 封包索引猜测工具。
// 校准二进制格式（.pkg / .g00 / .nwa / .ss）时，这是第一步。
#include <cstdio>
#include <string>
#include <vector>

#include "archive/Archive.h"
#include "core/Core.h"
#include "core/FileSystem.h"
#include "media/Decoder.h"
#include "script/Script.h"

using namespace siglus;

static void PrintUsage() {
  std::printf(
      "用法: siglus_probe <文件或目录> [选项]\n"
      "  --data <dir>        数据根目录（默认当前目录）\n"
      "  --dump <n>          打印前 n 字节 hex（默认 256）\n"
      "  --strings [min]     扫描可打印字符串（默认最少 4 字符）\n"
      "  --ss <opcodes.csv>  用 opcode 表反汇编并输出\n"
      "  --ss-out <file>     反汇编结果写文件\n"
      "  --pkg               尝试猜测 .pkg 条目布局\n"
      "  --pkg-layout <ini>  用指定布局解析 .pkg 并列出条目\n"
      "  --pkg-extract <dir> 按布局解包到目录\n");
}

static std::string ReadAll(const std::string& path) {
  std::vector<uint8_t> data;
  if (!FileSystem::ReadFile(path, data)) return {};
  return std::string(data.begin(), data.end());
}

static void ScanStrings(const std::vector<uint8_t>& data, size_t minLen) {
  std::string cur;
  for (size_t i = 0; i < data.size(); ++i) {
    unsigned char c = data[i];
    if (c >= 0x20 && c < 0x7F) {
      cur.push_back(static_cast<char>(c));
    } else {
      if (cur.size() >= minLen) std::printf("%08X  %s\n", static_cast<unsigned>(i - cur.size()), cur.c_str());
      cur.clear();
    }
  }
  if (cur.size() >= minLen) std::printf("%08X  %s\n", static_cast<unsigned>(data.size() - cur.size()), cur.c_str());
}

int main(int argc, char** argv) {
  if (argc < 2) { PrintUsage(); return 1; }

  std::string target;
  std::string dataRoot = ".";
  std::string csvPath, ssOut, layoutIni, extractDir;
  size_t dumpBytes = 256;
  bool doDump = false, doStrings = false, doPkg = false;
  size_t strMin = 4;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--data" && i + 1 < argc) dataRoot = argv[++i];
    else if (a == "--dump" && i + 1 < argc) { dumpBytes = static_cast<size_t>(std::atol(argv[++i])); doDump = true; }
    else if (a == "--strings") {
      doStrings = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') strMin = static_cast<size_t>(std::atol(argv[++i]));
    }
    else if (a == "--ss" && i + 1 < argc) csvPath = argv[++i];
    else if (a == "--ss-out" && i + 1 < argc) ssOut = argv[++i];
    else if (a == "--pkg") doPkg = true;
    else if (a == "--pkg-layout" && i + 1 < argc) layoutIni = argv[++i];
    else if (a == "--pkg-extract" && i + 1 < argc) extractDir = argv[++i];
    else if (!a.empty() && a[0] != '-' && target.empty()) target = a;
  }

  FileSystem::SetRoot(dataRoot);
  if (target.empty()) { PrintUsage(); return 1; }

  if (FileSystem::IsDir(target)) {
    auto files = FileSystem::ListFiles(target, true);
    std::printf("目录 %s：%zu 个文件\n\n", target.c_str(), files.size());
    for (const auto& f : files) std::printf("  %s\n", f.c_str());
    return 0;
  }

  std::vector<uint8_t> data;
  if (!FileSystem::ReadFile(target, data)) {
    std::printf("读取失败: %s\n", target.c_str());
    return 1;
  }
  std::printf("文件 %s：%zu 字节\n\n", target.c_str(), data.size());

  if (doDump || (!doStrings && csvPath.empty() && !doPkg && layoutIni.empty())) {
    std::printf("%s", HexDump(data.data(), std::min(dumpBytes, data.size())).c_str());
    std::printf("\n");
  }
  if (doStrings) { ScanStrings(data, strMin); std::printf("\n"); }

  if (doPkg) {
    auto cands = GuessPkgLayouts(data);
    std::printf("启发式候选布局 %zu 个（需人工核对条目名是否为文件名）：\n", cands.size());
    for (const auto& l : cands) {
      std::printf("  count_offset=%u entry_base=%u entry_stride=%d offset_field=%d size_field=%d\n",
                  l.countOffset, l.entryBase, l.entryStride, l.offsetField, l.sizeField);
    }
    std::printf("\n把确认后的布局写进 pkg_layout.ini 的 [pkg] 段即可。\n");
  }

  if (!layoutIni.empty()) {
    std::string iniText = ReadAll(layoutIni);
    if (iniText.empty()) {
      std::printf("读取布局失败: %s\n", layoutIni.c_str());
      return 1;
    }
    IniDoc ini = IniParse(iniText);
    PkgArchive arc(LoadPkgLayout(ini, "pkg"));
    if (!arc.Open(target)) {
      std::printf("按该布局解析失败\n");
      return 1;
    }
    std::printf("条目 %zu 个：\n", arc.Entries().size());
    for (const auto& e : arc.Entries())
      std::printf("  %-40s off=%-12llu size=%llu\n", e.name.c_str(),
                  static_cast<unsigned long long>(e.offset),
                  static_cast<unsigned long long>(e.size));

    if (!extractDir.empty()) {
      FileSystem::MakeDirs(extractDir);
      size_t ok = 0;
      for (const auto& e : arc.Entries()) {
        std::vector<uint8_t> out;
        if (!arc.Read(e.name, out)) continue;
        std::string safe = e.name;
        for (char& c : safe)
          if (c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
        if (FileSystem::WriteFile(FileSystem::Join(extractDir, safe), out)) ++ok;
      }
      std::printf("已解出 %zu 个文件到 %s\n", ok, extractDir.c_str());
    }
  }

  if (!csvPath.empty()) {
    std::string csv = ReadAll(csvPath);
    if (csv.empty()) {
      std::printf("读取 opcode 表失败: %s\n", csvPath.c_str());
      return 1;
    }
    OpcodeTable tbl;
    if (!tbl.LoadCsv(csv)) {
      std::printf("opcode 表为空\n");
      return 1;
    }
    auto insns = Disassemble(data.data(), data.size(), tbl);
    std::string out;
    size_t bad = 0;
    for (const Insn& in : insns) {
      char line[512];
      int p = std::snprintf(line, sizeof(line), "%08X: %-12s", in.addr, in.name.c_str());
      out.append(line, p);
      for (size_t k = 0; k < in.args.size(); ++k)
        out += " " + std::to_string(in.args[k]);
      for (const std::string& s : in.strs) out += " \"" + s + "\"";
      out += "\n";
      if (!in.valid) ++bad;
    }
    std::printf("反汇编 %zu 条（无法识别 %zu 条）\n\n", insns.size(), bad);
    if (ssOut.empty()) std::printf("%s", out.c_str());
    else FileSystem::WriteText(ssOut, out);
  }

  return 0;
}
