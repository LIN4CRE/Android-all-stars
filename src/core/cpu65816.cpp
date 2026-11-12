// 65C816 interpreter. Timing is driven by the bus: every memory access and
// internal operation reports to Snes, which advances the master clock.
#include "cpu65816.h"
#include "snes.h"

inline uint8_t Cpu65816::read(uint32_t adr) { return bus_->cpuRead(adr); }
inline void Cpu65816::write(uint32_t adr, uint8_t val) { bus_->cpuWrite(adr, val); }
inline void Cpu65816::idle() { bus_->cpuIdle(); }
inline void Cpu65816::idleWait() { bus_->cpuIdle(); }

void Cpu65816::reset(bool hard) {
  if (hard) {
    r.a = r.x = r.y = 0;
    r.dp = 0;
    r.db = 0;
    r.c = r.z = r.v = r.n = false;
  }
  r.pb = 0;
  r.sp = 0x100 | (r.sp & 0xff);
  r.dp = 0;
  r.db = 0;
  r.e = true;
  r.mf = r.xf = true;
  r.i = true;
  r.d = false;
  r.x &= 0xff;
  r.y &= 0xff;
  r.waiting = r.stopped = false;
  r.nmiWanted = r.irqWanted = r.intPending = false;
  r.resetWanted = true;
}

uint8_t Cpu65816::getP() const {
  uint8_t v = 0;
  v |= r.n ? 0x80 : 0;
  v |= r.v ? 0x40 : 0;
  v |= r.mf ? 0x20 : 0;
  v |= r.xf ? 0x10 : 0;
  v |= r.d ? 0x08 : 0;
  v |= r.i ? 0x04 : 0;
  v |= r.z ? 0x02 : 0;
  v |= r.c ? 0x01 : 0;
  return v;
}

void Cpu65816::setP(uint8_t v) {
  r.n = v & 0x80;
  r.v = v & 0x40;
  r.mf = v & 0x20;
  r.xf = v & 0x10;
  r.d = v & 0x08;
  r.i = v & 0x04;
  r.z = v & 0x02;
  r.c = v & 0x01;
  if (r.e) {
    r.mf = r.xf = true;
    r.sp = 0x100 | (r.sp & 0xff);
  }
  if (r.xf) {
    r.x &= 0xff;
    r.y &= 0xff;
  }
}

uint8_t Cpu65816::readOpcode() {
  uint8_t v = read((uint32_t(r.pb) << 16) | r.pc);
  r.pc++;
  return v;
}

uint16_t Cpu65816::readOpcodeWord() {
  uint8_t lo = readOpcode();
  return lo | (uint16_t(readOpcode()) << 8);
}

uint16_t Cpu65816::readWord(uint32_t lo, uint32_t hi) {
  uint8_t l = read(lo);
  return l | (uint16_t(read(hi)) << 8);
}

void Cpu65816::writeWord(uint32_t lo, uint32_t hi, uint16_t val, bool reversed) {
  if (reversed) {
    write(hi, val >> 8);
    write(lo, val & 0xff);
  } else {
    write(lo, val & 0xff);
    write(hi, val >> 8);
  }
}

void Cpu65816::pushByte(uint8_t v) {
  write(r.sp, v);
  r.sp--;
  if (r.e) r.sp = 0x100 | (r.sp & 0xff);
}

uint8_t Cpu65816::pullByte() {
  r.sp++;
  if (r.e) r.sp = 0x100 | (r.sp & 0xff);
  return read(r.sp);
}

void Cpu65816::pushWord(uint16_t v) {
  pushByte(v >> 8);
  pushByte(v & 0xff);
}

uint16_t Cpu65816::pullWord() {
  uint8_t lo = pullByte();
  return lo | (uint16_t(pullByte()) << 8);
}

void Cpu65816::setZN(uint16_t v, bool byte) {
  if (byte) {
    r.z = (v & 0xff) == 0;
    r.n = v & 0x80;
  } else {
    r.z = v == 0;
    r.n = v & 0x8000;
  }
}

void Cpu65816::doBranch(bool cond) {
  if (!cond) {
    r.pc++;
    return;
  }
  int8_t off = (int8_t)readOpcode();
  idle();
  uint16_t np = r.pc + off;
  if (r.e && ((np & 0xff00) != (r.pc & 0xff00))) idle();
  r.pc = np;
}

void Cpu65816::doInterrupt(uint16_t nativeVec, uint16_t emuVec, bool isBrk) {
  // For hardware interrupts two idle cycles replace the opcode fetch.
  if (!r.e) pushByte(r.pb);
  pushWord(r.pc);
  uint8_t p = getP();
  if (r.e) p = (p & ~0x30) | 0x20 | (isBrk ? 0x10 : 0);
  pushByte(p);
  r.i = true;
  r.d = false;
  r.pb = 0;
  uint16_t vec = r.e ? emuVec : nativeVec;
  r.pc = readWord(vec, vec + 1);
}

void Cpu65816::step() {
  if (r.resetWanted) {
    r.resetWanted = false;
    // reset sequence: dummy reads then fetch vector
    for (int i = 0; i < 5; i++) idle();
    r.pc = readWord(0xfffc, 0xfffd);
    return;
  }
  if (r.stopped) {
    idleWait();
    return;
  }
  if (r.waiting) {
    if (r.nmiWanted || r.irqWanted) {
      r.waiting = false;
      idle();
      idle();
      r.intPending = r.nmiWanted || (r.irqWanted && !r.i);
    } else {
      idleWait();
      return;
    }
  }
  // Interrupts sampled at the end of the previous instruction.
  if (r.intPending) {
    r.intPending = false;
    if (r.nmiWanted) {
      r.nmiWanted = false;
      read((uint32_t(r.pb) << 16) | r.pc);
      idle();
      doInterrupt(0xffea, 0xfffa, false);
      r.intPending = r.nmiWanted || (r.irqWanted && !r.i);
      return;
    } else if (r.irqWanted && !r.i) {
      read((uint32_t(r.pb) << 16) | r.pc);
      idle();
      doInterrupt(0xffee, 0xfffe, false);
      r.intPending = r.nmiWanted || (r.irqWanted && !r.i);
      return;
    }
  }
  lastPc = (uint32_t(r.pb) << 16) | r.pc;
  if (bus_->anyExecMarks_ && bus_->execCheck(lastPc)) return;
  uint8_t op = readOpcode();
  execOpcode(op);
  r.intPending = r.nmiWanted || (r.irqWanted && !r.i);
}

// ---------------------------------------------------------------------------
// Addressing modes

uint32_t Cpu65816::adrImm(uint32_t* hi, bool xFlag) {
  bool byte = xFlag ? r.xf : r.mf;
  uint32_t lo = (uint32_t(r.pb) << 16) | r.pc;
  r.pc++;
  if (!byte) {
    *hi = (uint32_t(r.pb) << 16) | r.pc;
    r.pc++;
  } else {
    *hi = lo;
  }
  return lo;
}

uint32_t Cpu65816::adrDp(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  *hi = (r.dp + adr + 1) & 0xffff;
  return (r.dp + adr) & 0xffff;
}

uint32_t Cpu65816::adrDpx(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  idle();
  if (r.e && (r.dp & 0xff) == 0) {
    uint16_t base = r.dp & 0xff00;
    *hi = base | ((adr + r.x + 1) & 0xff);
    return base | ((adr + r.x) & 0xff);
  }
  *hi = (r.dp + adr + r.x + 1) & 0xffff;
  return (r.dp + adr + r.x) & 0xffff;
}

uint32_t Cpu65816::adrDpy(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  idle();
  if (r.e && (r.dp & 0xff) == 0) {
    uint16_t base = r.dp & 0xff00;
    *hi = base | ((adr + r.y + 1) & 0xff);
    return base | ((adr + r.y) & 0xff);
  }
  *hi = (r.dp + adr + r.y + 1) & 0xffff;
  return (r.dp + adr + r.y) & 0xffff;
}

uint32_t Cpu65816::adrIdp(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  uint16_t ptr = readWord((r.dp + adr) & 0xffff, (r.dp + adr + 1) & 0xffff);
  uint32_t lo = (uint32_t(r.db) << 16) + ptr;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrIdx(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  idle();
  uint16_t ptr = readWord((r.dp + adr + r.x) & 0xffff, (r.dp + adr + r.x + 1) & 0xffff);
  uint32_t lo = (uint32_t(r.db) << 16) + ptr;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrIdy(uint32_t* hi, bool wr) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  uint16_t ptr = readWord((r.dp + adr) & 0xffff, (r.dp + adr + 1) & 0xffff);
  if (wr || !r.xf || ((ptr >> 8) != ((ptr + r.y) >> 8))) idle();
  uint32_t lo = ((uint32_t(r.db) << 16) + ptr + r.y) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrIdl(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  uint32_t ptr = readWord((r.dp + adr) & 0xffff, (r.dp + adr + 1) & 0xffff);
  ptr |= uint32_t(read((r.dp + adr + 2) & 0xffff)) << 16;
  *hi = (ptr + 1) & 0xffffff;
  return ptr;
}

uint32_t Cpu65816::adrIly(uint32_t* hi) {
  uint8_t adr = readOpcode();
  if (r.dp & 0xff) idle();
  uint32_t ptr = readWord((r.dp + adr) & 0xffff, (r.dp + adr + 1) & 0xffff);
  ptr |= uint32_t(read((r.dp + adr + 2) & 0xffff)) << 16;
  uint32_t lo = (ptr + r.y) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrSr(uint32_t* hi) {
  uint8_t adr = readOpcode();
  idle();
  *hi = (r.sp + adr + 1) & 0xffff;
  return (r.sp + adr) & 0xffff;
}

uint32_t Cpu65816::adrIsy(uint32_t* hi) {
  uint8_t adr = readOpcode();
  idle();
  uint16_t ptr = readWord((r.sp + adr) & 0xffff, (r.sp + adr + 1) & 0xffff);
  idle();
  uint32_t lo = ((uint32_t(r.db) << 16) + ptr + r.y) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrAbs(uint32_t* hi) {
  uint16_t adr = readOpcodeWord();
  uint32_t lo = (uint32_t(r.db) << 16) + adr;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrAbx(uint32_t* hi, bool wr) {
  uint16_t adr = readOpcodeWord();
  if (wr || !r.xf || ((adr >> 8) != ((adr + r.x) >> 8))) idle();
  uint32_t lo = ((uint32_t(r.db) << 16) + adr + r.x) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrAby(uint32_t* hi, bool wr) {
  uint16_t adr = readOpcodeWord();
  if (wr || !r.xf || ((adr >> 8) != ((adr + r.y) >> 8))) idle();
  uint32_t lo = ((uint32_t(r.db) << 16) + adr + r.y) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

uint32_t Cpu65816::adrAbl(uint32_t* hi) {
  uint32_t adr = readOpcodeWord();
  adr |= uint32_t(readOpcode()) << 16;
  *hi = (adr + 1) & 0xffffff;
  return adr;
}

uint32_t Cpu65816::adrAlx(uint32_t* hi) {
  uint32_t adr = readOpcodeWord();
  adr |= uint32_t(readOpcode()) << 16;
  uint32_t lo = (adr + r.x) & 0xffffff;
  *hi = (lo + 1) & 0xffffff;
  return lo;
}

// ---------------------------------------------------------------------------
// Operations

void Cpu65816::opAnd(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    r.a = (r.a & 0xff00) | ((r.a & v) & 0xff);
    setZN(r.a, true);
  } else {
    r.a &= readWord(lo, hi);
    setZN(r.a, false);
  }
}

void Cpu65816::opOra(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    r.a = (r.a & 0xff00) | ((r.a | v) & 0xff);
    setZN(r.a, true);
  } else {
    r.a |= readWord(lo, hi);
    setZN(r.a, false);
  }
}

void Cpu65816::opEor(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    r.a = (r.a & 0xff00) | ((r.a ^ v) & 0xff);
    setZN(r.a, true);
  } else {
    r.a ^= readWord(lo, hi);
    setZN(r.a, false);
  }
}

void Cpu65816::opAdc(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t val = read(lo);
    int res;
    if (r.d) {
      res = (r.a & 0x0f) + (val & 0x0f) + (r.c ? 1 : 0);
      if (res > 0x09) res = ((res + 0x06) & 0x0f) + 0x10;
      res = (r.a & 0xf0) + (val & 0xf0) + res;
    } else {
      res = (r.a & 0xff) + val + (r.c ? 1 : 0);
    }
    r.v = ((r.a & 0x80) == (val & 0x80)) && ((val & 0x80) != (res & 0x80));
    if (r.d && res > 0x9f) res += 0x60;
    r.c = res > 0xff;
    r.a = (r.a & 0xff00) | (res & 0xff);
    setZN(r.a, true);
  } else {
    uint16_t val = readWord(lo, hi);
    int res;
    if (r.d) {
      res = (r.a & 0x000f) + (val & 0x000f) + (r.c ? 1 : 0);
      if (res > 0x0009) res = ((res + 0x0006) & 0x000f) + 0x0010;
      res = (r.a & 0x00f0) + (val & 0x00f0) + res;
      if (res > 0x009f) res = ((res + 0x0060) & 0x00ff) + 0x0100;
      res = (r.a & 0x0f00) + (val & 0x0f00) + res;
      if (res > 0x09ff) res = ((res + 0x0600) & 0x0fff) + 0x1000;
      res = (r.a & 0xf000) + (val & 0xf000) + res;
    } else {
      res = r.a + val + (r.c ? 1 : 0);
    }
    r.v = ((r.a & 0x8000) == (val & 0x8000)) && ((val & 0x8000) != (res & 0x8000));
    if (r.d && res > 0x9fff) res += 0x6000;
    r.c = res > 0xffff;
    r.a = res & 0xffff;
    setZN(r.a, false);
  }
}

void Cpu65816::opSbc(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t val = read(lo) ^ 0xff;
    int res;
    if (r.d) {
      res = (r.a & 0x0f) + (val & 0x0f) + (r.c ? 1 : 0);
      if (res < 0x10) res = (res - 0x06) & ((res - 0x06 < 0) ? 0x0f : 0x1f);
      res = (r.a & 0xf0) + (val & 0xf0) + res;
    } else {
      res = (r.a & 0xff) + val + (r.c ? 1 : 0);
    }
    r.v = ((r.a & 0x80) == (val & 0x80)) && ((val & 0x80) != (res & 0x80));
    if (r.d && res < 0x100) res -= 0x60;
    r.c = res > 0xff;
    r.a = (r.a & 0xff00) | (res & 0xff);
    setZN(r.a, true);
  } else {
    uint16_t val = readWord(lo, hi) ^ 0xffff;
    int res;
    if (r.d) {
      res = (r.a & 0x000f) + (val & 0x000f) + (r.c ? 1 : 0);
      if (res < 0x0010) res = (res - 0x0006) & ((res - 0x0006 < 0) ? 0x000f : 0x001f);
      res = (r.a & 0x00f0) + (val & 0x00f0) + res;
      if (res < 0x0100) res = (res - 0x0060) & ((res - 0x0060 < 0) ? 0x00ff : 0x01ff);
      res = (r.a & 0x0f00) + (val & 0x0f00) + res;
      if (res < 0x1000) res = (res - 0x0600) & ((res - 0x0600 < 0) ? 0x0fff : 0x1fff);
      res = (r.a & 0xf000) + (val & 0xf000) + res;
    } else {
      res = r.a + val + (r.c ? 1 : 0);
    }
    r.v = ((r.a & 0x8000) == (val & 0x8000)) && ((val & 0x8000) != (res & 0x8000));
    if (r.d && res < 0x10000) res -= 0x6000;
    r.c = res > 0xffff;
    r.a = res & 0xffff;
    setZN(r.a, false);
  }
}

void Cpu65816::opCmp(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    int res = (r.a & 0xff) - read(lo);
    r.c = res >= 0;
    setZN(res & 0xff, true);
  } else {
    int res = r.a - readWord(lo, hi);
    r.c = res >= 0;
    setZN(res & 0xffff, false);
  }
}

void Cpu65816::opCpx(uint32_t lo, uint32_t hi) {
  if (r.xf) {
    int res = (r.x & 0xff) - read(lo);
    r.c = res >= 0;
    setZN(res & 0xff, true);
  } else {
    int res = r.x - readWord(lo, hi);
    r.c = res >= 0;
    setZN(res & 0xffff, false);
  }
}

void Cpu65816::opCpy(uint32_t lo, uint32_t hi) {
  if (r.xf) {
    int res = (r.y & 0xff) - read(lo);
    r.c = res >= 0;
    setZN(res & 0xff, true);
  } else {
    int res = r.y - readWord(lo, hi);
    r.c = res >= 0;
    setZN(res & 0xffff, false);
  }
}

void Cpu65816::opBit(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    r.z = ((r.a & 0xff) & v) == 0;
    r.n = v & 0x80;
    r.v = v & 0x40;
  } else {
    uint16_t v = readWord(lo, hi);
    r.z = (r.a & v) == 0;
    r.n = v & 0x8000;
    r.v = v & 0x4000;
  }
}

void Cpu65816::opLda(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    r.a = (r.a & 0xff00) | read(lo);
    setZN(r.a, true);
  } else {
    r.a = readWord(lo, hi);
    setZN(r.a, false);
  }
}

void Cpu65816::opLdx(uint32_t lo, uint32_t hi) {
  if (r.xf) {
    r.x = read(lo);
    setZN(r.x, true);
  } else {
    r.x = readWord(lo, hi);
    setZN(r.x, false);
  }
}

void Cpu65816::opLdy(uint32_t lo, uint32_t hi) {
  if (r.xf) {
    r.y = read(lo);
    setZN(r.y, true);
  } else {
    r.y = readWord(lo, hi);
    setZN(r.y, false);
  }
}

void Cpu65816::opSta(uint32_t lo, uint32_t hi) {
  if (r.mf) write(lo, r.a & 0xff);
  else writeWord(lo, hi, r.a, false);
}

void Cpu65816::opStx(uint32_t lo, uint32_t hi) {
  if (r.xf) write(lo, r.x & 0xff);
  else writeWord(lo, hi, r.x, false);
}

void Cpu65816::opSty(uint32_t lo, uint32_t hi) {
  if (r.xf) write(lo, r.y & 0xff);
  else writeWord(lo, hi, r.y, false);
}

void Cpu65816::opStz(uint32_t lo, uint32_t hi) {
  if (r.mf) write(lo, 0);
  else writeWord(lo, hi, 0, false);
}

void Cpu65816::opRor(uint32_t lo, uint32_t hi) {
  bool carry;
  if (r.mf) {
    uint8_t v = read(lo);
    idle();
    carry = v & 1;
    v = (v >> 1) | (r.c ? 0x80 : 0);
    setZN(v, true);
    write(lo, v);
  } else {
    uint16_t v = readWord(lo, hi);
    idle();
    carry = v & 1;
    v = (v >> 1) | (r.c ? 0x8000 : 0);
    setZN(v, false);
    writeWord(lo, hi, v, true);
  }
  r.c = carry;
}

void Cpu65816::opRol(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    int v = (read(lo) << 1) | (r.c ? 1 : 0);
    idle();
    r.c = v & 0x100;
    setZN(v & 0xff, true);
    write(lo, v & 0xff);
  } else {
    int v = (readWord(lo, hi) << 1) | (r.c ? 1 : 0);
    idle();
    r.c = v & 0x10000;
    setZN(v & 0xffff, false);
    writeWord(lo, hi, v & 0xffff, true);
  }
}

void Cpu65816::opLsr(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    idle();
    r.c = v & 1;
    v >>= 1;
    setZN(v, true);
    write(lo, v);
  } else {
    uint16_t v = readWord(lo, hi);
    idle();
    r.c = v & 1;
    v >>= 1;
    setZN(v, false);
    writeWord(lo, hi, v, true);
  }
}

void Cpu65816::opAsl(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    idle();
    r.c = v & 0x80;
    v <<= 1;
    setZN(v, true);
    write(lo, v);
  } else {
    uint16_t v = readWord(lo, hi);
    idle();
    r.c = v & 0x8000;
    v <<= 1;
    setZN(v, false);
    writeWord(lo, hi, v, true);
  }
}

void Cpu65816::opInc(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo) + 1;
    idle();
    setZN(v, true);
    write(lo, v);
  } else {
    uint16_t v = readWord(lo, hi) + 1;
    idle();
    setZN(v, false);
    writeWord(lo, hi, v, true);
  }
}

void Cpu65816::opDec(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo) - 1;
    idle();
    setZN(v, true);
    write(lo, v);
  } else {
    uint16_t v = readWord(lo, hi) - 1;
    idle();
    setZN(v, false);
    writeWord(lo, hi, v, true);
  }
}

void Cpu65816::opTsb(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    idle();
    r.z = ((r.a & 0xff) & v) == 0;
    write(lo, v | (r.a & 0xff));
  } else {
    uint16_t v = readWord(lo, hi);
    idle();
    r.z = (r.a & v) == 0;
    writeWord(lo, hi, v | r.a, true);
  }
}

void Cpu65816::opTrb(uint32_t lo, uint32_t hi) {
  if (r.mf) {
    uint8_t v = read(lo);
    idle();
    r.z = ((r.a & 0xff) & v) == 0;
    write(lo, v & ~(r.a & 0xff));
  } else {
    uint16_t v = readWord(lo, hi);
    idle();
    r.z = (r.a & v) == 0;
    writeWord(lo, hi, v & ~r.a, true);
  }
}

// ---------------------------------------------------------------------------

void Cpu65816::execOpcode(uint8_t op) {
  uint32_t lo = 0, hi = 0;
  switch (op) {
    // ---- ORA / AND / EOR / ADC / STA / LDA / CMP / SBC groups ----
#define ALU_GROUP(base, fn)                                   \
  case base + 0x01: lo = adrIdx(&hi); fn(lo, hi); break;      \
  case base + 0x03: lo = adrSr(&hi); fn(lo, hi); break;       \
  case base + 0x05: lo = adrDp(&hi); fn(lo, hi); break;       \
  case base + 0x07: lo = adrIdl(&hi); fn(lo, hi); break;      \
  case base + 0x09: lo = adrImm(&hi, false); fn(lo, hi); break; \
  case base + 0x0d: lo = adrAbs(&hi); fn(lo, hi); break;      \
  case base + 0x0f: lo = adrAbl(&hi); fn(lo, hi); break;      \
  case base + 0x11: lo = adrIdy(&hi, false); fn(lo, hi); break; \
  case base + 0x12: lo = adrIdp(&hi); fn(lo, hi); break;      \
  case base + 0x13: lo = adrIsy(&hi); fn(lo, hi); break;      \
  case base + 0x15: lo = adrDpx(&hi); fn(lo, hi); break;      \
  case base + 0x17: lo = adrIly(&hi); fn(lo, hi); break;      \
  case base + 0x19: lo = adrAby(&hi, false); fn(lo, hi); break; \
  case base + 0x1d: lo = adrAbx(&hi, false); fn(lo, hi); break; \
  case base + 0x1f: lo = adrAlx(&hi); fn(lo, hi); break;

    ALU_GROUP(0x00, opOra)
    ALU_GROUP(0x20, opAnd)
    ALU_GROUP(0x40, opEor)
    ALU_GROUP(0x60, opAdc)
    ALU_GROUP(0xa0, opLda)
    ALU_GROUP(0xc0, opCmp)
    ALU_GROUP(0xe0, opSbc)
#undef ALU_GROUP

    // STA (no immediate)
    case 0x81: lo = adrIdx(&hi); opSta(lo, hi); break;
    case 0x83: lo = adrSr(&hi); opSta(lo, hi); break;
    case 0x85: lo = adrDp(&hi); opSta(lo, hi); break;
    case 0x87: lo = adrIdl(&hi); opSta(lo, hi); break;
    case 0x8d: lo = adrAbs(&hi); opSta(lo, hi); break;
    case 0x8f: lo = adrAbl(&hi); opSta(lo, hi); break;
    case 0x91: lo = adrIdy(&hi, true); opSta(lo, hi); break;
    case 0x92: lo = adrIdp(&hi); opSta(lo, hi); break;
    case 0x93: lo = adrIsy(&hi); opSta(lo, hi); break;
    case 0x95: lo = adrDpx(&hi); opSta(lo, hi); break;
    case 0x97: lo = adrIly(&hi); opSta(lo, hi); break;
    case 0x99: lo = adrAby(&hi, true); opSta(lo, hi); break;
    case 0x9d: lo = adrAbx(&hi, true); opSta(lo, hi); break;
    case 0x9f: lo = adrAlx(&hi); opSta(lo, hi); break;

    // BIT
    case 0x24: lo = adrDp(&hi); opBit(lo, hi); break;
    case 0x2c: lo = adrAbs(&hi); opBit(lo, hi); break;
    case 0x34: lo = adrDpx(&hi); opBit(lo, hi); break;
    case 0x3c: lo = adrAbx(&hi, false); opBit(lo, hi); break;
    case 0x89: {  // BIT imm only affects Z
      lo = adrImm(&hi, false);
      if (r.mf) r.z = ((r.a & 0xff) & read(lo)) == 0;
      else r.z = (r.a & readWord(lo, hi)) == 0;
      break;
    }

    // LDX / LDY / STX / STY / CPX / CPY
    case 0xa2: lo = adrImm(&hi, true); opLdx(lo, hi); break;
    case 0xa6: lo = adrDp(&hi); opLdx(lo, hi); break;
    case 0xae: lo = adrAbs(&hi); opLdx(lo, hi); break;
    case 0xb6: lo = adrDpy(&hi); opLdx(lo, hi); break;
    case 0xbe: lo = adrAby(&hi, false); opLdx(lo, hi); break;
    case 0xa0: lo = adrImm(&hi, true); opLdy(lo, hi); break;
    case 0xa4: lo = adrDp(&hi); opLdy(lo, hi); break;
    case 0xac: lo = adrAbs(&hi); opLdy(lo, hi); break;
    case 0xb4: lo = adrDpx(&hi); opLdy(lo, hi); break;
    case 0xbc: lo = adrAbx(&hi, false); opLdy(lo, hi); break;
    case 0x86: lo = adrDp(&hi); opStx(lo, hi); break;
    case 0x8e: lo = adrAbs(&hi); opStx(lo, hi); break;
    case 0x96: lo = adrDpy(&hi); opStx(lo, hi); break;
    case 0x84: lo = adrDp(&hi); opSty(lo, hi); break;
    case 0x8c: lo = adrAbs(&hi); opSty(lo, hi); break;
    case 0x94: lo = adrDpx(&hi); opSty(lo, hi); break;
    case 0xe0: lo = adrImm(&hi, true); opCpx(lo, hi); break;
    case 0xe4: lo = adrDp(&hi); opCpx(lo, hi); break;
    case 0xec: lo = adrAbs(&hi); opCpx(lo, hi); break;
    case 0xc0: lo = adrImm(&hi, true); opCpy(lo, hi); break;
    case 0xc4: lo = adrDp(&hi); opCpy(lo, hi); break;
    case 0xcc: lo = adrAbs(&hi); opCpy(lo, hi); break;

    // STZ
    case 0x64: lo = adrDp(&hi); opStz(lo, hi); break;
    case 0x74: lo = adrDpx(&hi); opStz(lo, hi); break;
    case 0x9c: lo = adrAbs(&hi); opStz(lo, hi); break;
    case 0x9e: lo = adrAbx(&hi, true); opStz(lo, hi); break;

    // Shifts / rotates / inc / dec (memory)
    case 0x06: lo = adrDp(&hi); opAsl(lo, hi); break;
    case 0x0e: lo = adrAbs(&hi); opAsl(lo, hi); break;
    case 0x16: lo = adrDpx(&hi); opAsl(lo, hi); break;
    case 0x1e: lo = adrAbx(&hi, true); opAsl(lo, hi); break;
    case 0x26: lo = adrDp(&hi); opRol(lo, hi); break;
    case 0x2e: lo = adrAbs(&hi); opRol(lo, hi); break;
    case 0x36: lo = adrDpx(&hi); opRol(lo, hi); break;
    case 0x3e: lo = adrAbx(&hi, true); opRol(lo, hi); break;
    case 0x46: lo = adrDp(&hi); opLsr(lo, hi); break;
    case 0x4e: lo = adrAbs(&hi); opLsr(lo, hi); break;
    case 0x56: lo = adrDpx(&hi); opLsr(lo, hi); break;
    case 0x5e: lo = adrAbx(&hi, true); opLsr(lo, hi); break;
    case 0x66: lo = adrDp(&hi); opRor(lo, hi); break;
    case 0x6e: lo = adrAbs(&hi); opRor(lo, hi); break;
    case 0x76: lo = adrDpx(&hi); opRor(lo, hi); break;
    case 0x7e: lo = adrAbx(&hi, true); opRor(lo, hi); break;
    case 0xe6: lo = adrDp(&hi); opInc(lo, hi); break;
    case 0xee: lo = adrAbs(&hi); opInc(lo, hi); break;
    case 0xf6: lo = adrDpx(&hi); opInc(lo, hi); break;
    case 0xfe: lo = adrAbx(&hi, true); opInc(lo, hi); break;
    case 0xc6: lo = adrDp(&hi); opDec(lo, hi); break;
    case 0xce: lo = adrAbs(&hi); opDec(lo, hi); break;
    case 0xd6: lo = adrDpx(&hi); opDec(lo, hi); break;
    case 0xde: lo = adrAbx(&hi, true); opDec(lo, hi); break;
    case 0x04: lo = adrDp(&hi); opTsb(lo, hi); break;
    case 0x0c: lo = adrAbs(&hi); opTsb(lo, hi); break;
    case 0x14: lo = adrDp(&hi); opTrb(lo, hi); break;
    case 0x1c: lo = adrAbs(&hi); opTrb(lo, hi); break;

    // Accumulator shifts
    case 0x0a:
      idle();
      if (r.mf) {
        r.c = r.a & 0x80;
        r.a = (r.a & 0xff00) | ((r.a << 1) & 0xff);
        setZN(r.a, true);
      } else {
        r.c = r.a & 0x8000;
        r.a <<= 1;
        setZN(r.a, false);
      }
      break;
    case 0x2a: {
      idle();
      bool c = r.c;
      if (r.mf) {
        r.c = r.a & 0x80;
        r.a = (r.a & 0xff00) | (((r.a << 1) | (c ? 1 : 0)) & 0xff);
        setZN(r.a, true);
      } else {
        r.c = r.a & 0x8000;
        r.a = (r.a << 1) | (c ? 1 : 0);
        setZN(r.a, false);
      }
      break;
    }
    case 0x4a:
      idle();
      if (r.mf) {
        r.c = r.a & 1;
        r.a = (r.a & 0xff00) | ((r.a & 0xff) >> 1);
        setZN(r.a, true);
      } else {
        r.c = r.a & 1;
        r.a >>= 1;
        setZN(r.a, false);
      }
      break;
    case 0x6a: {
      idle();
      bool c = r.c;
      if (r.mf) {
        r.c = r.a & 1;
        r.a = (r.a & 0xff00) | ((r.a & 0xff) >> 1) | (c ? 0x80 : 0);
        setZN(r.a, true);
      } else {
        r.c = r.a & 1;
        r.a = (r.a >> 1) | (c ? 0x8000 : 0);
        setZN(r.a, false);
      }
      break;
    }
    case 0x1a:
      idle();
      if (r.mf) {
        r.a = (r.a & 0xff00) | ((r.a + 1) & 0xff);
        setZN(r.a, true);
      } else {
        r.a++;
        setZN(r.a, false);
      }
      break;
    case 0x3a:
      idle();
      if (r.mf) {
        r.a = (r.a & 0xff00) | ((r.a - 1) & 0xff);
        setZN(r.a, true);
      } else {
        r.a--;
        setZN(r.a, false);
      }
      break;

    // Index inc/dec
    case 0xe8: idle(); r.x = r.xf ? ((r.x + 1) & 0xff) : uint16_t(r.x + 1); setZN(r.x, r.xf); break;
    case 0xc8: idle(); r.y = r.xf ? ((r.y + 1) & 0xff) : uint16_t(r.y + 1); setZN(r.y, r.xf); break;
    case 0xca: idle(); r.x = r.xf ? ((r.x - 1) & 0xff) : uint16_t(r.x - 1); setZN(r.x, r.xf); break;
    case 0x88: idle(); r.y = r.xf ? ((r.y - 1) & 0xff) : uint16_t(r.y - 1); setZN(r.y, r.xf); break;

    // Branches
    case 0x10: doBranch(!r.n); break;
    case 0x30: doBranch(r.n); break;
    case 0x50: doBranch(!r.v); break;
    case 0x70: doBranch(r.v); break;
    case 0x90: doBranch(!r.c); break;
    case 0xb0: doBranch(r.c); break;
    case 0xd0: doBranch(!r.z); break;
    case 0xf0: doBranch(r.z); break;
    case 0x80: doBranch(true); break;
    case 0x82: {  // BRL
      uint16_t off = readOpcodeWord();
      idle();
      r.pc += off;
      break;
    }

    // Flags
    case 0x18: idle(); r.c = false; break;
    case 0x38: idle(); r.c = true; break;
    case 0x58: idle(); r.i = false; break;
    case 0x78: idle(); r.i = true; break;
    case 0xb8: idle(); r.v = false; break;
    case 0xd8: idle(); r.d = false; break;
    case 0xf8: idle(); r.d = true; break;
    case 0xc2: {  // REP
      uint8_t v = readOpcode();
      idle();
      setP(getP() & ~v);
      break;
    }
    case 0xe2: {  // SEP
      uint8_t v = readOpcode();
      idle();
      setP(getP() | v);
      break;
    }
    case 0xfb: {  // XCE
      idle();
      bool t = r.c;
      r.c = r.e;
      r.e = t;
      if (r.e) {
        r.mf = r.xf = true;
        r.x &= 0xff;
        r.y &= 0xff;
        r.sp = 0x100 | (r.sp & 0xff);
      }
      break;
    }

    // Transfers
    case 0xaa: idle(); r.x = r.xf ? (r.a & 0xff) : r.a; setZN(r.x, r.xf); break;  // TAX
    case 0xa8: idle(); r.y = r.xf ? (r.a & 0xff) : r.a; setZN(r.y, r.xf); break;  // TAY
    case 0x8a:  // TXA
      idle();
      if (r.mf) { r.a = (r.a & 0xff00) | (r.x & 0xff); setZN(r.a, true); }
      else { r.a = r.x; setZN(r.a, false); }
      break;
    case 0x98:  // TYA
      idle();
      if (r.mf) { r.a = (r.a & 0xff00) | (r.y & 0xff); setZN(r.a, true); }
      else { r.a = r.y; setZN(r.a, false); }
      break;
    case 0x9b: idle(); r.y = r.xf ? (r.x & 0xff) : r.x; setZN(r.y, r.xf); break;  // TXY
    case 0xbb: idle(); r.x = r.xf ? (r.y & 0xff) : r.y; setZN(r.x, r.xf); break;  // TYX
    case 0xba: idle(); r.x = r.xf ? (r.sp & 0xff) : r.sp; setZN(r.x, r.xf); break;  // TSX
    case 0x9a:  // TXS
      idle();
      r.sp = r.e ? (0x100 | (r.x & 0xff)) : r.x;
      break;
    case 0x1b:  // TCS
      idle();
      r.sp = r.e ? (0x100 | (r.a & 0xff)) : r.a;
      break;
    case 0x3b: idle(); r.a = r.sp; setZN(r.a, false); break;  // TSC
    case 0x5b: idle(); r.dp = r.a; setZN(r.dp, false); break;  // TCD
    case 0x7b: idle(); r.a = r.dp; setZN(r.a, false); break;  // TDC
    case 0xeb: {  // XBA
      idle();
      idle();
      r.a = (r.a >> 8) | (r.a << 8);
      setZN(r.a & 0xff, true);
      break;
    }

    // Stack
    case 0x48:  // PHA
      idle();
      if (r.mf) pushByte(r.a & 0xff);
      else pushWord(r.a);
      break;
    case 0xda:  // PHX
      idle();
      if (r.xf) pushByte(r.x & 0xff);
      else pushWord(r.x);
      break;
    case 0x5a:  // PHY
      idle();
      if (r.xf) pushByte(r.y & 0xff);
      else pushWord(r.y);
      break;
    case 0x68:  // PLA
      idle();
      idle();
      if (r.mf) { r.a = (r.a & 0xff00) | pullByte(); setZN(r.a, true); }
      else { r.a = pullWord(); setZN(r.a, false); }
      break;
    case 0xfa:  // PLX
      idle();
      idle();
      if (r.xf) { r.x = pullByte(); setZN(r.x, true); }
      else { r.x = pullWord(); setZN(r.x, false); }
      break;
    case 0x7a:  // PLY
      idle();
      idle();
      if (r.xf) { r.y = pullByte(); setZN(r.y, true); }
      else { r.y = pullWord(); setZN(r.y, false); }
      break;
    case 0x08: idle(); pushByte(r.e ? (getP() | 0x30) : getP()); break;  // PHP
    case 0x28: idle(); idle(); setP(pullByte()); break;                  // PLP
    case 0x8b: idle(); pushByte(r.db); break;                           // PHB
    case 0xab: idle(); idle(); r.db = pullByte(); setZN(r.db, true); break;  // PLB
    case 0x0b: idle(); pushWord(r.dp); break;                           // PHD
    case 0x2b: idle(); idle(); r.dp = pullWord(); setZN(r.dp, false); break;  // PLD
    case 0x4b: idle(); pushByte(r.pb); break;                           // PHK
    case 0xf4: pushWord(readOpcodeWord()); break;                       // PEA
    case 0xd4: {  // PEI
      uint8_t adr = readOpcode();
      if (r.dp & 0xff) idle();
      pushWord(readWord((r.dp + adr) & 0xffff, (r.dp + adr + 1) & 0xffff));
      break;
    }
    case 0x62: {  // PER
      uint16_t off = readOpcodeWord();
      idle();
      pushWord(r.pc + off);
      break;
    }

    // Jumps / calls
    case 0x4c: r.pc = readOpcodeWord(); break;  // JMP abs
    case 0x5c: {  // JML long
      uint16_t a = readOpcodeWord();
      r.pb = readOpcode();
      r.pc = a;
      break;
    }
    case 0x6c: {  // JMP (abs)
      uint16_t a = readOpcodeWord();
      r.pc = readWord(a, (a + 1) & 0xffff);
      break;
    }
    case 0x7c: {  // JMP (abs,X)
      uint16_t a = readOpcodeWord();
      idle();
      uint32_t p = (uint32_t(r.pb) << 16) | uint16_t(a + r.x);
      r.pc = readWord(p, (uint32_t(r.pb) << 16) | uint16_t(a + r.x + 1));
      break;
    }
    case 0xdc: {  // JML [abs]
      uint16_t a = readOpcodeWord();
      r.pc = readWord(a, (a + 1) & 0xffff);
      r.pb = read((a + 2) & 0xffff);
      break;
    }
    case 0x20: {  // JSR abs
      uint16_t a = readOpcodeWord();
      idle();
      pushWord(r.pc - 1);
      r.pc = a;
      break;
    }
    case 0xfc: {  // JSR (abs,X)
      uint8_t alo = readOpcode();
      pushWord(r.pc);  // pc points at high byte
      uint8_t ahi = readOpcode();
      uint16_t a = alo | (ahi << 8);
      idle();
      uint32_t p = (uint32_t(r.pb) << 16) | uint16_t(a + r.x);
      r.pc = readWord(p, (uint32_t(r.pb) << 16) | uint16_t(a + r.x + 1));
      break;
    }
    case 0x22: {  // JSL long
      uint16_t a = readOpcodeWord();
      pushByte(r.pb);
      idle();
      uint8_t b = readOpcode();
      pushWord(r.pc - 1);
      r.pb = b;
      r.pc = a;
      break;
    }
    case 0x60:  // RTS
      idle();
      idle();
      r.pc = pullWord() + 1;
      idle();
      break;
    case 0x6b:  // RTL
      idle();
      idle();
      r.pc = pullWord() + 1;
      r.pb = pullByte();
      break;
    case 0x40:  // RTI
      idle();
      idle();
      setP(pullByte());
      r.pc = pullWord();
      if (!r.e) r.pb = pullByte();
      break;

    // Interrupt instructions
    case 0x00:  // BRK
      readOpcode();
      doInterrupt(0xffe6, 0xfffe, true);
      break;
    case 0x02:  // COP
      readOpcode();
      doInterrupt(0xffe4, 0xfff4, true);
      break;
    case 0xcb:  // WAI
      r.waiting = true;
      idle();
      idle();
      break;
    case 0xdb:  // STP
      r.stopped = true;
      idle();
      idle();
      break;
    case 0x42: {  // WDM -> native trap
      uint8_t id = readOpcode();
      if (trapFn) trapFn(trapCtx, id);
      break;
    }
    case 0xea: idle(); break;  // NOP

    // Block moves
    case 0x54: {  // MVN
      uint8_t dst = readOpcode();
      uint8_t src = readOpcode();
      r.db = dst;
      uint8_t v = read((uint32_t(src) << 16) | r.x);
      write((uint32_t(dst) << 16) | r.y, v);
      r.a--;
      r.x++;
      r.y++;
      if (r.xf) { r.x &= 0xff; r.y &= 0xff; }
      idle();
      idle();
      if (r.a != 0xffff) r.pc -= 3;
      break;
    }
    case 0x44: {  // MVP
      uint8_t dst = readOpcode();
      uint8_t src = readOpcode();
      r.db = dst;
      uint8_t v = read((uint32_t(src) << 16) | r.x);
      write((uint32_t(dst) << 16) | r.y, v);
      r.a--;
      r.x--;
      r.y--;
      if (r.xf) { r.x &= 0xff; r.y &= 0xff; }
      idle();
      idle();
      if (r.a != 0xffff) r.pc -= 3;
      break;
    }

    default:
      break;
  }
}
