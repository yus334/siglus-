#include "script/Script.h"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace siglus {

const char* ToString(VM::Status s) {
  switch (s) {
    case VM::Status::Running:    return "Running";
    case VM::Status::WaitClick:  return "WaitClick";
    case VM::Status::WaitSelect: return "WaitSelect";
    case VM::Status::WaitTimer:  return "WaitTimer";
    case VM::Status::WaitMovie:  return "WaitMovie";
    case VM::Status::End:        return "End";
    case VM::Status::Error:      return "Error";
  }
  return "?";
}

// ============================================================ opcode 表
static OperandKind ParseOperandKind(const std::string& tok, int& bytesSize) {
  std::string t = ToLower(tok);
  bytesSize = 0;
  if (t == "u8")  return OperandKind::U8;
  if (t == "u16") return OperandKind::U16;
  if (t == "u32") return OperandKind::U32;
  if (t == "i32") return OperandKind::I32;
  if (t == "ptr" || t == "ptr32" || t == "stroptr") return OperandKind::Ptr32;
  if (t == "strz" || t == "str") return OperandKind::StrZ;
  if (t.compare(0, 2, "b:") == 0) {
    bytesSize = std::atoi(t.c_str() + 2);
    return OperandKind::Bytes;
  }
  // 形如 "4" 视为 4 个原始字节
  if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) {
    bytesSize = std::atoi(t.c_str());
    return OperandKind::Bytes;
  }
  return OperandKind::U32;
}

bool OpcodeTable::LoadCsv(const std::string& text) {
  defs_.clear();
  size_t i = 0;
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(i, eol - i);
    i = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = Trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') continue;

    // 形如 codesize=2 / dialect=ss 的元信息
    size_t eq = t.find('=');
    if (eq != std::string::npos && t.compare(0, 2, "0x") != 0) {
      std::string k = ToLower(Trim(t.substr(0, eq)));
      std::string v = Trim(t.substr(eq + 1));
      if (k == "codesize") codeSize = std::atoi(v.c_str());
      else if (k == "dialect") dialect = v;
      continue;
    }

    std::vector<std::string> f = Split(t, ',');
    if (f.size() < 2) continue;
    OpDef def;
    def.code = static_cast<uint32_t>(std::strtoul(Trim(f[0]).c_str(), nullptr, 0));
    def.name = ToLower(Trim(f[1]));
    for (size_t k = 2; k < f.size(); ++k) {
      int byteSize = 0;
      Operand op;
      op.kind = ParseOperandKind(f[k], byteSize);
      op.size = byteSize > 0 ? byteSize : 4;
      def.ops.push_back(op);
    }
    defs_[def.code] = std::move(def);
  }
  SIGLUS_LOGI("OpcodeTable: 载入 %zu 条指令（codesize=%d）", defs_.size(), codeSize);
  return !defs_.empty();
}

const OpDef* OpcodeTable::Find(uint32_t code) const {
  auto it = defs_.find(code);
  return it == defs_.end() ? nullptr : &it->second;
}

// ============================================================ 反汇编
static std::string CStringAt(const uint8_t* data, size_t len, size_t off) {
  if (off >= len) return {};
  std::string s;
  for (size_t i = off; i < len && data[i] != 0; ++i) s.push_back(static_cast<char>(data[i]));
  return s;
}

std::vector<Insn> Disassemble(const uint8_t* data, size_t len, const OpcodeTable& tbl,
                              size_t start, size_t end, size_t maxInsns) {
  std::vector<Insn> out;
  if (!data || len == 0) return out;
  size_t stop = end < len ? end : len;
  size_t p = start;
  size_t unknownRun = 0;
  const size_t codeSize = static_cast<size_t>(tbl.codeSize < 1 ? 1 : tbl.codeSize);

  while (p + codeSize <= stop && out.size() < maxInsns) {
    uint32_t code = 0;
    for (size_t b = 0; b < codeSize; ++b) code |= static_cast<uint32_t>(data[p + b]) << (8 * b);

    Insn in;
    in.addr = static_cast<uint32_t>(p);
    in.code = code;

    const OpDef* def = tbl.Find(code);
    if (!def) {
      in.name  = "db";
      in.valid = false;
      in.length = codeSize;
      out.push_back(in);
      ++unknownRun;
      p += codeSize;
      if (unknownRun > 128) break;
      continue;
    }
    unknownRun = 0;

    in.name = def->name;
    size_t q = p + codeSize;
    bool truncated = false;
    for (const Operand& op : def->ops) {
      if (q >= stop && op.kind != OperandKind::StrZ) { truncated = true; break; }
      switch (op.kind) {
        case OperandKind::U8:  in.args.push_back(data[q]); q += 1; break;
        case OperandKind::U16: in.args.push_back(Rd16(data + q)); q += 2; break;
        case OperandKind::U32: in.args.push_back(Rd32(data + q)); q += 4; break;
        case OperandKind::I32: in.args.push_back(static_cast<int32_t>(Rd32(data + q))); q += 4; break;
        case OperandKind::Ptr32: {
          uint32_t off = Rd32(data + q);
          q += 4;
          in.args.push_back(off);
          in.strs.push_back(CStringAt(data, len, off));
          break;
        }
        case OperandKind::StrZ: {
          std::string s = CStringAt(data, len, q);
          q += s.size() + 1;
          in.args.push_back(static_cast<int64_t>(s.size()));
          in.strs.push_back(s);
          break;
        }
        case OperandKind::Bytes: q += static_cast<size_t>(op.size); break;
      }
    }
    if (truncated) { in.valid = false; }
    in.length = q > p ? q - p : codeSize;
    out.push_back(in);
    p += in.length;
  }
  return out;
}

// ============================================================ 汇编器（demo 方言）
static bool IsNumberToken(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.size()) return false;
  for (; i < s.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  return true;
}

using Tokens = std::vector<std::pair<std::string, bool>>;  // {文本, 是否为字符串字面量}

static Tokens TokenizeLine(const std::string& line) {
  Tokens out;
  std::string cur;
  bool inQuote = false;
  bool has = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (inQuote) {
      if (c == '\\' && i + 1 < line.size()) { cur.push_back(line[++i]); continue; }
      if (c == '"') { out.emplace_back(cur, true); cur.clear(); has = false; inQuote = false; continue; }
      cur.push_back(c);
      continue;
    }
    if (c == '#' || c == ';') break;
    if (c == '"') {
      if (has) { out.emplace_back(cur, false); cur.clear(); has = false; }
      inQuote = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (has) { out.emplace_back(cur, false); cur.clear(); has = false; }
      continue;
    }
    cur.push_back(c);
    has = true;
  }
  if (has) out.emplace_back(cur, false);
  return out;
}

// ============================================================ 内置指令
namespace {

using S = VM::Status;

S H_text(VM& vm, const Insn& in) {
  IScriptHost* h = vm.Host();
  if (!h) return S::Error;
  if (in.strs.size() >= 2) { h->SetSpeaker(in.strs[0]); h->ShowText(in.strs[1]); }
  else if (in.strs.size() == 1) { h->ShowText(in.strs[0]); }
  else { h->ShowText({}); }
  return S::WaitClick;
}

S H_speaker(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->SetSpeaker(Str(in, 0));
  return S::Running;
}

S H_bg(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->SetBackground(Str(in, 0));
  return S::Running;
}

S H_layer(VM& vm, const Insn& in) {
  if (!vm.Host()) return S::Error;
  int idx = static_cast<int>(Arg(in, 0));
  vm.Host()->SetLayer(idx, Str(in, 0));
  if (in.args.size() >= 3)
    vm.Host()->SetLayerPos(idx, static_cast<float>(Arg(in, 1)), static_cast<float>(Arg(in, 2)));
  return S::Running;
}

S H_pos(VM& vm, const Insn& in) {
  if (vm.Host())
    vm.Host()->SetLayerPos(static_cast<int>(Arg(in, 0)), static_cast<float>(Arg(in, 1)),
                           static_cast<float>(Arg(in, 2)));
  return S::Running;
}

S H_alpha(VM& vm, const Insn& in) {
  if (!vm.Host()) return S::Error;
  float a = static_cast<float>(Arg(in, 1, 255)) / 255.f;
  if (a < 0.f) a = 0.f;
  if (a > 1.f) a = 1.f;
  vm.Host()->SetLayerAlpha(static_cast<int>(Arg(in, 0)), a);
  return S::Running;
}

S H_blend(VM& vm, const Insn& in) {
  if (vm.Host())
    vm.Host()->SetLayerBlend(static_cast<int>(Arg(in, 0)), static_cast<int>(Arg(in, 1)));
  return S::Running;
}

S H_clear(VM& vm, const Insn& in) {
  if (!vm.Host()) return S::Error;
  if (in.args.empty()) { vm.Host()->ClearLayers(); return S::Running; }
  for (int64_t a : in.args) vm.Host()->ClearLayer(static_cast<int>(a));
  return S::Running;
}

S H_bgm(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->PlayBgm(Str(in, 0), Arg(in, 1, 1) != 0);
  return S::Running;
}

S H_stopBgm(VM& vm, const Insn&) {
  if (vm.Host()) vm.Host()->StopBgm();
  return S::Running;
}

S H_se(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->PlaySe(Str(in, 0));
  return S::Running;
}

S H_voice(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->PlayVoice(Str(in, 0));
  return S::Running;
}

S H_movie(VM& vm, const Insn& in) {
  if (vm.Host()) vm.Host()->PlayMovie(Str(in, 0));
  return S::WaitMovie;
}

S H_wait(VM& vm, const Insn& in) {
  vm.SetWaitTimer(static_cast<float>(Arg(in, 0)));
  return S::WaitTimer;
}

S H_select(VM& vm, const Insn& in) {
  vm.SetPendingOptions(in.strs);
  if (vm.Host()) vm.Host()->Select(in.strs);
  return S::WaitSelect;
}

S H_set(VM& vm, const Insn& in) {
  vm.SetVar(static_cast<int>(Arg(in, 0)), static_cast<int>(Arg(in, 1)));
  return S::Running;
}

S H_add(VM& vm, const Insn& in) {
  int k = static_cast<int>(Arg(in, 0));
  vm.SetVar(k, vm.GetVar(k) + static_cast<int>(Arg(in, 1)));
  return S::Running;
}

static bool DoGoto(VM& vm, const Insn& in, size_t addrArgIndex) {
  if (!in.strs.empty()) return vm.GotoLabel(in.strs.back());
  if (addrArgIndex < in.args.size()) return vm.GotoAddr(static_cast<uint32_t>(in.args[addrArgIndex]));
  return false;
}

S H_jmp(VM& vm, const Insn& in) {
  return DoGoto(vm, in, 0) ? S::Running : S::Error;
}

S H_jz(VM& vm, const Insn& in) {
  int k = static_cast<int>(Arg(in, 0));
  if (vm.GetVar(k) == 0) return DoGoto(vm, in, 1) ? S::Running : S::Error;
  return S::Running;
}

S H_jnz(VM& vm, const Insn& in) {
  int k = static_cast<int>(Arg(in, 0));
  if (vm.GetVar(k) != 0) return DoGoto(vm, in, 1) ? S::Running : S::Error;
  return S::Running;
}

S H_call(VM& vm, const Insn& in) {
  vm.PushCall(vm.Pc());
  return DoGoto(vm, in, 0) ? S::Running : S::Error;
}

S H_ret(VM& vm, const Insn&) {
  uint32_t ret = 0;
  if (!vm.PopCall(ret)) return S::End;
  return vm.GotoAddr(ret) ? S::Running : S::Error;
}

S H_fade(VM& vm, const Insn& in) {
  if (vm.Host())
    vm.Host()->Transition(static_cast<int>(Arg(in, 0)), static_cast<int>(Arg(in, 1, 400)));
  return S::Running;
}

S H_end(VM&, const Insn&) { return S::End; }
S H_nop(VM&, const Insn&) { return S::Running; }

}  // namespace

// ============================================================ VM
VM::VM() { RegisterBuiltinHandlers(); }

void VM::RegisterHandler(const std::string& name, Handler h) {
  handlers_[ToLower(name)] = std::move(h);
}

void VM::RegisterBuiltinHandlers() {
  RegisterHandler("text", H_text);
  RegisterHandler("msg", H_text);
  RegisterHandler("message", H_text);
  RegisterHandler("mes", H_text);
  RegisterHandler("speaker", H_speaker);
  RegisterHandler("name", H_speaker);
  RegisterHandler("bg", H_bg);
  RegisterHandler("background", H_bg);
  RegisterHandler("layer", H_layer);
  RegisterHandler("ld", H_layer);
  RegisterHandler("fg", H_layer);
  RegisterHandler("pos", H_pos);
  RegisterHandler("alpha", H_alpha);
  RegisterHandler("blend", H_blend);
  RegisterHandler("mode", H_blend);
  RegisterHandler("clear", H_clear);
  RegisterHandler("cl", H_clear);
  RegisterHandler("bgm", H_bgm);
  RegisterHandler("playbgm", H_bgm);
  RegisterHandler("stopbgm", H_stopBgm);
  RegisterHandler("se", H_se);
  RegisterHandler("sfx", H_se);
  RegisterHandler("voice", H_voice);
  RegisterHandler("movie", H_movie);
  RegisterHandler("playmovie", H_movie);
  RegisterHandler("wait", H_wait);
  RegisterHandler("delay", H_wait);
  RegisterHandler("select", H_select);
  RegisterHandler("choice", H_select);
  RegisterHandler("set", H_set);
  RegisterHandler("let", H_set);
  RegisterHandler("add", H_add);
  RegisterHandler("jmp", H_jmp);
  RegisterHandler("jump", H_jmp);
  RegisterHandler("jz", H_jz);
  RegisterHandler("je", H_jz);
  RegisterHandler("jnz", H_jnz);
  RegisterHandler("jne", H_jnz);
  RegisterHandler("call", H_call);
  RegisterHandler("ret", H_ret);
  RegisterHandler("fade", H_fade);
  RegisterHandler("trans", H_fade);
  RegisterHandler("transition", H_fade);
  RegisterHandler("end", H_end);
  RegisterHandler("halt", H_end);
  RegisterHandler("stop", H_end);
  RegisterHandler("nop", H_nop);
}

bool VM::LoadAsm(const std::string& text) {
  insns_.clear();
  labels_.clear();
  vars_.clear();
  callStack_.clear();
  pendingOptions_.clear();
  pc_ = 0;
  status_ = Status::Running;

  size_t i = 0;
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(i, eol - i);
    i = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string t = Trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') continue;

    if (t.back() == ':') {
      labels_[ToLower(Trim(t.substr(0, t.size() - 1)))] = static_cast<uint32_t>(insns_.size());
      continue;
    }

    Tokens toks = TokenizeLine(t);
    if (toks.empty()) continue;
    std::string op = ToLower(toks[0].first);
    if (op == "label") {
      if (toks.size() >= 2) labels_[ToLower(toks[1].first)] = static_cast<uint32_t>(insns_.size());
      continue;
    }

    Insn in;
    in.addr   = static_cast<uint32_t>(insns_.size());
    in.name   = op;
    in.length = 1;

    if (op == "select") {
      size_t sp = t.find(' ');
      std::string rest = (sp == std::string::npos) ? std::string() : t.substr(sp + 1);
      for (const std::string& raw : Split(rest, '|')) {
        std::string p = Trim(raw);
        if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
        if (!p.empty()) in.strs.push_back(p);
      }
    } else {
      for (size_t k = 1; k < toks.size(); ++k) {
        const std::string& v = toks[k].first;
        if (toks[k].second) {  // 显式引号 → 字符串
          in.strs.push_back(v);
        } else if (IsNumberToken(v)) {
          in.args.push_back(std::atoll(v.c_str()));
        } else {
          in.strs.push_back(v);
        }
      }
    }
    insns_.push_back(std::move(in));
  }

  SIGLUS_LOGI("VM: 汇编载入 %zu 条指令, %zu 个标签", insns_.size(), labels_.size());
  return !insns_.empty();
}

bool VM::LoadBinary(const uint8_t* data, size_t len, const OpcodeTable& tbl, size_t start) {
  insns_ = Disassemble(data, len, tbl, start);
  if (insns_.empty()) {
    SIGLUS_LOGE("VM: 反汇编结果为空");
    status_ = Status::Error;
    return false;
  }
  // 反汇编得到的"指令"其实是线性扫描结果，pc_ 用指令序号寻址
  pc_ = 0;
  status_ = Status::Running;
  SIGLUS_LOGI("VM: 反汇编 %zu 条指令", insns_.size());
  return true;
}

VM::Status VM::Step() {
  if (status_ != Status::Running) return status_;
  if (pc_ >= insns_.size()) {
    status_ = Status::End;
    return status_;
  }
  Insn in = insns_[pc_++];
  if (!in.valid) {
    // 无法识别的字节：跳过，继续尝试
    return status_;
  }
  auto it = handlers_.find(ToLower(in.name));
  if (it == handlers_.end()) {
    static std::unordered_map<std::string, bool> reported;
    if (!reported.count(in.name)) {
      reported[in.name] = true;
      SIGLUS_LOGW("VM: 指令 '%s' 没有对应实现（code=0x%X）。请用 RegisterHandler 补上。",
                  in.name.c_str(), in.code);
    }
    return status_;
  }
  status_ = it->second(*this, in);
  return status_;
}

VM::Status VM::Run(uint32_t maxSteps) {
  uint32_t n = 0;
  while (status_ == Status::Running && n < maxSteps) {
    Step();
    ++n;
  }
  return status_;
}

void VM::Tick(float dtSeconds) {
  if (status_ != Status::WaitTimer) return;
  timerMs_ -= dtSeconds * 1000.f;
  if (timerMs_ <= 0.f) status_ = Status::Running;
}

void VM::NotifyClick() {
  if (status_ == Status::WaitClick) status_ = Status::Running;
}

void VM::NotifySelect(int index) {
  if (status_ != Status::WaitSelect) return;
  SetVar(0, index);
  pendingOptions_.clear();
  status_ = Status::Running;
}

void VM::NotifyMovieEnd() {
  if (status_ == Status::WaitMovie) status_ = Status::Running;
}

int VM::GetVar(int idx) const {
  auto it = vars_.find(idx);
  return it == vars_.end() ? 0 : it->second;
}

void VM::SetVar(int idx, int v) { vars_[idx] = v; }

bool VM::GotoAddr(uint32_t addr) {
  if (addr >= insns_.size()) {
    SIGLUS_LOGE("VM: 跳转地址越界 %u / %zu", addr, insns_.size());
    return false;
  }
  pc_ = addr;
  return true;
}

bool VM::GotoLabel(const std::string& label) {
  auto it = labels_.find(ToLower(label));
  if (it == labels_.end()) {
    SIGLUS_LOGE("VM: 找不到标签 '%s'", label.c_str());
    return false;
  }
  pc_ = it->second;
  return true;
}

void VM::PushCall(uint32_t retAddr) { callStack_.push_back(retAddr); }

bool VM::PopCall(uint32_t& retAddr) {
  if (callStack_.empty()) return false;
  retAddr = callStack_.back();
  callStack_.pop_back();
  return true;
}

std::string VM::Serialize() const {
  std::string s = "; siglus vm state v1\n";
  s += "pc=" + std::to_string(pc_) + "\n";
  for (const auto& kv : vars_)
    s += "var=" + std::to_string(kv.first) + "=" + std::to_string(kv.second) + "\n";
  for (uint32_t a : callStack_) s += "call=" + std::to_string(a) + "\n";
  return s;
}

bool VM::Deserialize(const std::string& text) {
  vars_.clear();
  callStack_.clear();
  size_t i = 0;
  bool ok = false;
  while (i < text.size()) {
    size_t eol = text.find('\n', i);
    if (eol == std::string::npos) eol = text.size();
    std::string line = Trim(text.substr(i, eol - i));
    i = eol + 1;
    if (line.empty() || line[0] == ';') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string rest = line.substr(eq + 1);
    if (key == "pc") {
      pc_ = static_cast<uint32_t>(std::atol(rest.c_str()));
      ok = true;
    } else if (key == "var") {
      size_t sep = rest.find('=');
      if (sep != std::string::npos)
        vars_[std::atoi(rest.substr(0, sep).c_str())] = std::atoi(rest.substr(sep + 1).c_str());
    } else if (key == "call") {
      callStack_.push_back(static_cast<uint32_t>(std::atol(rest.c_str())));
    }
  }
  status_ = Status::Running;
  return ok;
}

}  // namespace siglus
