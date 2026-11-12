// 65C816 CPU interpreter (SNES S-CPU core).
#pragma once
#include <cstdint>

class Snes;

struct CpuRegs {
  uint16_t a = 0, x = 0, y = 0, sp = 0x1ff, dp = 0, pc = 0;
  uint8_t db = 0, pb = 0;
  bool c = false, z = false, v = false, n = false;
  bool i = true, d = false, xf = true, mf = true, e = true;
  bool waiting = false, stopped = false;
  bool nmiWanted = false;  // edge latched by system
  bool irqWanted = false;  // level from system
  bool intPending = false; // sampled at end of previous instruction
  bool resetWanted = true;
};

class Cpu65816 {
public:
  explicit Cpu65816(Snes* bus) : bus_(bus) {}
  CpuRegs r;

  void reset(bool hard);
  // Execute one instruction (or service an interrupt / idle when waiting).
  void step();

  uint8_t getP() const;
  void setP(uint8_t v);

  // Native trap hook: WDM #imm calls this if set. Return true if handled.
  using TrapFn = bool (*)(void* ctx, uint8_t id);
  TrapFn trapFn = nullptr;
  void* trapCtx = nullptr;

  // Last executed opcode address (for debugging).
  uint32_t lastPc = 0;

private:
  Snes* bus_;

  inline uint8_t read(uint32_t adr);
  inline void write(uint32_t adr, uint8_t val);
  inline void idle();
  inline void idleWait();

  uint8_t readOpcode();
  uint16_t readOpcodeWord();
  uint16_t readWord(uint32_t lo, uint32_t hi);
  void writeWord(uint32_t lo, uint32_t hi, uint16_t val, bool reversed);
  void pushByte(uint8_t v);
  uint8_t pullByte();
  void pushWord(uint16_t v);
  uint16_t pullWord();
  void setZN(uint16_t v, bool byte);
  void doBranch(bool cond);
  void doInterrupt(uint16_t nativeVec, uint16_t emuVec, bool isBrk);
  void execOpcode(uint8_t op);

  // addressing modes: return low address, set *hi to high address
  uint32_t adrImm(uint32_t* hi, bool xFlag);
  uint32_t adrDp(uint32_t* hi);
  uint32_t adrDpx(uint32_t* hi);
  uint32_t adrDpy(uint32_t* hi);
  uint32_t adrIdp(uint32_t* hi);
  uint32_t adrIdx(uint32_t* hi);
  uint32_t adrIdy(uint32_t* hi, bool write);
  uint32_t adrIdl(uint32_t* hi);
  uint32_t adrIly(uint32_t* hi);
  uint32_t adrSr(uint32_t* hi);
  uint32_t adrIsy(uint32_t* hi);
  uint32_t adrAbs(uint32_t* hi);
  uint32_t adrAbx(uint32_t* hi, bool write);
  uint32_t adrAby(uint32_t* hi, bool write);
  uint32_t adrAbl(uint32_t* hi);
  uint32_t adrAlx(uint32_t* hi);

  // operations
  void opAnd(uint32_t lo, uint32_t hi);
  void opOra(uint32_t lo, uint32_t hi);
  void opEor(uint32_t lo, uint32_t hi);
  void opAdc(uint32_t lo, uint32_t hi);
  void opSbc(uint32_t lo, uint32_t hi);
  void opCmp(uint32_t lo, uint32_t hi);
  void opCpx(uint32_t lo, uint32_t hi);
  void opCpy(uint32_t lo, uint32_t hi);
  void opBit(uint32_t lo, uint32_t hi);
  void opLda(uint32_t lo, uint32_t hi);
  void opLdx(uint32_t lo, uint32_t hi);
  void opLdy(uint32_t lo, uint32_t hi);
  void opSta(uint32_t lo, uint32_t hi);
  void opStx(uint32_t lo, uint32_t hi);
  void opSty(uint32_t lo, uint32_t hi);
  void opStz(uint32_t lo, uint32_t hi);
  void opRor(uint32_t lo, uint32_t hi);
  void opRol(uint32_t lo, uint32_t hi);
  void opLsr(uint32_t lo, uint32_t hi);
  void opAsl(uint32_t lo, uint32_t hi);
  void opInc(uint32_t lo, uint32_t hi);
  void opDec(uint32_t lo, uint32_t hi);
  void opTsb(uint32_t lo, uint32_t hi);
  void opTrb(uint32_t lo, uint32_t hi);
};
