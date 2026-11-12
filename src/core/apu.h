// SNES APU: SPC700 CPU + timers + S-DSP + 64KB audio RAM.
// The boot ROM is implemented as a high-level state machine (no ROM image).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "dsp.h"

class Apu {
public:
  Apu();
  void reset(bool hard);

  // Run until the given SPC cycle count.
  void runTo(uint64_t spcCycle);
  uint64_t cycles() const { return cycles_; }
  void setCycles(uint64_t c) { cycles_ = c; }  // resynchronise (damaged state files)

  // CPU side port access
  uint8_t cpuReadPort(int p) const { return outPorts[p & 3]; }
  void cpuWritePort(int p, uint8_t v) { inPorts[p & 3] = v; }

  // Audio output (32040 Hz stereo interleaved)
  std::vector<int16_t> samples;
  bool generateAudio = true;

  uint8_t ram[0x10000];
  uint8_t inPorts[4] = {};   // CPU -> SPC
  uint8_t outPorts[4] = {};  // SPC -> CPU
  Dsp dsp;

  // SPC700 registers
  struct Regs {
    uint8_t a = 0, x = 0, y = 0, sp = 0xef;
    uint16_t pc = 0xffc0;
    bool n = false, v = false, p = false, b = false, h = false, i = false, z = false, c = false;
    bool stopped = false;
  } r;

  // Debug trace (instructions logged when non-null)
  void* traceFile = nullptr;
  int traceLimit = 0;

  // Serialization
  std::vector<uint8_t> saveState() const;
  bool loadState(const uint8_t* data, size_t size);

private:
  uint64_t cycles_ = 0;
  int sampleCounter_ = 0;
  uint8_t control_ = 0x80;
  uint8_t dspAddr_ = 0;
  uint8_t aux_[2] = {};
  struct Timer {
    uint8_t target = 0;
    uint8_t internal = 0;
    uint8_t out = 0;
    int divider = 0;
    bool enabled = false;
  } timers_[3];

  // IPL HLE state
  int iplState_ = 0;
  uint8_t iplY_ = 0;
  bool inIpl_ = true;

  int step();          // execute one instruction; returns cycles
  int iplStep();       // HLE boot ROM
  void tickTimers(int c);

  uint8_t read(uint16_t adr);
  void write(uint16_t adr, uint8_t v);
  uint8_t readDp(uint8_t a) { return read((r.p ? 0x100 : 0) | a); }
  void writeDp(uint8_t a, uint8_t v) { write((r.p ? 0x100 : 0) | a, v); }
  uint16_t dpAdr(uint8_t a) const { return (r.p ? 0x100 : 0) | a; }
  uint8_t fetch() { return read(r.pc++); }
  uint16_t fetch16() { uint8_t l = fetch(); return l | (fetch() << 8); }
  void push(uint8_t v) { write(0x100 | r.sp, v); r.sp--; }
  uint8_t pop() { r.sp++; return read(0x100 | r.sp); }
  uint8_t getPsw() const;
  void setPsw(uint8_t v);
  void setNZ(uint8_t v) { r.n = v & 0x80; r.z = v == 0; }

  // ALU
  uint8_t aluOr(uint8_t a, uint8_t b) { uint8_t v = a | b; setNZ(v); return v; }
  uint8_t aluAnd(uint8_t a, uint8_t b) { uint8_t v = a & b; setNZ(v); return v; }
  uint8_t aluEor(uint8_t a, uint8_t b) { uint8_t v = a ^ b; setNZ(v); return v; }
  uint8_t aluCmp(uint8_t a, uint8_t b) { int v = a - b; r.c = v >= 0; setNZ((uint8_t)v); return a; }
  uint8_t aluAdc(uint8_t a, uint8_t b);
  uint8_t aluSbc(uint8_t a, uint8_t b) { return aluAdc(a, b ^ 0xff); }
  uint8_t aluAsl(uint8_t v) { r.c = v & 0x80; v <<= 1; setNZ(v); return v; }
  uint8_t aluLsr(uint8_t v) { r.c = v & 1; v >>= 1; setNZ(v); return v; }
  uint8_t aluRol(uint8_t v) { bool c = r.c; r.c = v & 0x80; v = (v << 1) | (c ? 1 : 0); setNZ(v); return v; }
  uint8_t aluRor(uint8_t v) { bool c = r.c; r.c = v & 1; v = (v >> 1) | (c ? 0x80 : 0); setNZ(v); return v; }
  uint8_t aluInc(uint8_t v) { v++; setNZ(v); return v; }
  uint8_t aluDec(uint8_t v) { v--; setNZ(v); return v; }
  int branch(bool cond);
};
