// 65816 disassembler (debugging aid).
#pragma once
#include <cstdint>
#include <string>
#include <functional>

// Disassembles one instruction at `pc` (24-bit). `rd` reads a byte.
// Returns the instruction length. mflag/xflag: accumulator/index are 8-bit.
int disasm65816(uint32_t pc, bool mflag, bool xflag, const std::function<uint8_t(uint32_t)>& rd,
                std::string& out);
