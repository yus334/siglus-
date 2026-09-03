#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"

namespace siglus {

// ============================================================ opcode 表
enum class OperandKind { U8, U16, U32, I32, Ptr32, StrZ, Bytes };

struct Operand {
  OperandKind kind = OperandKind::U32;
  int         size = 4;  // 仅 Bytes 有效
};

struct OpDef {
  uint32_t             code = 0;
  std::string          name;
  std::vector<Operand> ops;
};

// CSV 单行示例：  0x12,TEXT,PTR
//                 0x20,LAYER,U8,PTR,U16,U16
// 首行可写       codesize=2      表示操作码占 2 字节（默认 1）
class OpcodeTable {
 public:
  bool        LoadCsv(const std::string& text);
  const OpDef* Find(uint32_t code) const;
  size_t      Size() const { return defs_.size(); }

  std::string dialect;
  int         codeSize = 1;

 private:
  std::unordered_map<uint32_t, OpDef> defs_;
};

// ============================================================ 宿主接口
class IScriptHost {
 public:
  virtual ~IScriptHost() = default;
  virtual void SetSpeaker(const std::string& name)                 = 0;
  virtual void ShowText(const std::string& text)                   = 0;
  virtual void SetBackground(const std::string& image)             = 0;
  virtual void SetLayer(int layer, const std::string& image)       = 0;
  virtual void SetLayerPos(int layer, float x, float y)            = 0;
  virtual void SetLayerAlpha(int layer, float a)                   = 0;   // 0..1
  virtual void SetLayerBlend(int layer, int mode)                  = 0;
  virtual void ClearLayer(int layer)                               = 0;
  virtual void ClearLayers()                                       = 0;
  virtual void PlayBgm(const std::string& name, bool loop)         = 0;
  virtual void StopBgm()                                           = 0;
  virtual void PlaySe(const std::string& name)                     = 0;
  virtual void PlayVoice(const std::string& name)                  = 0;
  virtual void PlayMovie(const std::string& name)                  = 0;
  virtual void Select(const std::vector<std::string>& options)     = 0;
  virtual void Transition(int type, int ms)                        = 0;
};

// ============================================================ 指令
struct Insn {
  uint32_t                addr   = 0;
  uint32_t                code   = 0;
  size_t                  length = 1;
  std::string             name;
  std::vector<int64_t>    args;    // 数值操作数
  std::vector<std::string> strs;   // 字符串操作数（按出现顺序）
  bool                    valid = true;
};

inline int64_t Arg(const Insn& in, size_t i, int64_t def = 0) {
  return i < in.args.size() ? in.args[i] : def;
}
inline std::string Str(const Insn& in, size_t i) {
  return i < in.strs.size() ? in.strs[i] : std::string();
}

std::vector<Insn> Disassemble(const uint8_t* data, size_t len, const OpcodeTable& tbl,
                              size_t start = 0, size_t end = static_cast<size_t>(-1),
                              size_t maxInsns = 500000);

// ============================================================ 虚拟机
class VM {
 public:
  enum class Status { Running, WaitClick, WaitSelect, WaitTimer, WaitMovie, End, Error };

  VM();

  void         SetHost(IScriptHost* h) { host_ = h; }
  IScriptHost* Host() const { return host_; }

  // demo/自测用文本方言（assets/*.asm）
  bool LoadAsm(const std::string& text);
  // 真实 .ss：需要先用 OpcodeTable 描述指令编码
  bool LoadBinary(const uint8_t* data, size_t len, const OpcodeTable& tbl, size_t start = 0);

  Status Run(uint32_t maxSteps = 4096);
  Status Step();
  void   Tick(float dtSeconds);
  void   NotifyClick();
  void   NotifySelect(int index);
  void   NotifyMovieEnd();

  Status status() const { return status_; }
  const std::vector<std::string>& PendingOptions() const { return pendingOptions_; }

  int  GetVar(int idx) const;
  void SetVar(int idx, int v);
  const std::unordered_map<int, int>& Vars() const { return vars_; }
  void SetVars(const std::unordered_map<int, int>& v) { vars_ = v; }

  uint32_t Pc() const { return pc_; }
  bool     GotoAddr(uint32_t addr);
  bool     GotoLabel(const std::string& label);
  size_t   InsnCount() const { return insns_.size(); }

  void SetWaitTimer(float ms) { timerMs_ = ms; }
  void SetPendingOptions(const std::vector<std::string>& opts) { pendingOptions_ = opts; }

  void        PushCall(uint32_t retAddr);
  bool        PopCall(uint32_t& retAddr);

  std::string Serialize() const;
  bool        Deserialize(const std::string& text);

  using Handler = std::function<Status(VM&, const Insn&)>;
  void RegisterHandler(const std::string& name, Handler h);
  void RegisterBuiltinHandlers();

 private:
  bool Dispatch(const Insn& in);

  IScriptHost* host_ = nullptr;
  std::vector<Insn>  insns_;
  std::unordered_map<std::string, uint32_t> labels_;
  std::unordered_map<int, int>              vars_;
  std::unordered_map<std::string, Handler>  handlers_;
  std::vector<uint32_t>                     callStack_;
  std::vector<std::string>                  pendingOptions_;

  uint32_t pc_     = 0;
  Status   status_ = Status::End;
  float    timerMs_ = 0.f;
};

const char* ToString(VM::Status s);

}  // namespace siglus
