#include "disasm.h"
#include <cstdio>

namespace {
enum Mode {
  IMP, IMM_M, IMM_X, IMM8, DP, DPX, DPY, IDP, IDX, IDY, IDL, ILY, SR, ISY,
  ABS, ABX, ABY, ABL, ALX, IND, IAX, IAL, REL, RELL, MOVE, ACC
};
struct Op {
  const char* name;
  Mode mode;
};
const Op kOps[256] = {
    {"BRK", IMM8}, {"ORA", IDX}, {"COP", IMM8}, {"ORA", SR}, {"TSB", DP}, {"ORA", DP}, {"ASL", DP}, {"ORA", IDL},
    {"PHP", IMP}, {"ORA", IMM_M}, {"ASL", ACC}, {"PHD", IMP}, {"TSB", ABS}, {"ORA", ABS}, {"ASL", ABS}, {"ORA", ABL},
    {"BPL", REL}, {"ORA", IDY}, {"ORA", IDP}, {"ORA", ISY}, {"TRB", DP}, {"ORA", DPX}, {"ASL", DPX}, {"ORA", ILY},
    {"CLC", IMP}, {"ORA", ABY}, {"INC", ACC}, {"TCS", IMP}, {"TRB", ABS}, {"ORA", ABX}, {"ASL", ABX}, {"ORA", ALX},
    {"JSR", ABS}, {"AND", IDX}, {"JSL", ABL}, {"AND", SR}, {"BIT", DP}, {"AND", DP}, {"ROL", DP}, {"AND", IDL},
    {"PLP", IMP}, {"AND", IMM_M}, {"ROL", ACC}, {"PLD", IMP}, {"BIT", ABS}, {"AND", ABS}, {"ROL", ABS}, {"AND", ABL},
    {"BMI", REL}, {"AND", IDY}, {"AND", IDP}, {"AND", ISY}, {"BIT", DPX}, {"AND", DPX}, {"ROL", DPX}, {"AND", ILY},
    {"SEC", IMP}, {"AND", ABY}, {"DEC", ACC}, {"TSC", IMP}, {"BIT", ABX}, {"AND", ABX}, {"ROL", ABX}, {"AND", ALX},
    {"RTI", IMP}, {"EOR", IDX}, {"WDM", IMM8}, {"EOR", SR}, {"MVP", MOVE}, {"EOR", DP}, {"LSR", DP}, {"EOR", IDL},
    {"PHA", IMP}, {"EOR", IMM_M}, {"LSR", ACC}, {"PHK", IMP}, {"JMP", ABS}, {"EOR", ABS}, {"LSR", ABS}, {"EOR", ABL},
    {"BVC", REL}, {"EOR", IDY}, {"EOR", IDP}, {"EOR", ISY}, {"MVN", MOVE}, {"EOR", DPX}, {"LSR", DPX}, {"EOR", ILY},
    {"CLI", IMP}, {"EOR", ABY}, {"PHY", IMP}, {"TCD", IMP}, {"JML", ABL}, {"EOR", ABX}, {"LSR", ABX}, {"EOR", ALX},
    {"RTS", IMP}, {"ADC", IDX}, {"PER", RELL}, {"ADC", SR}, {"STZ", DP}, {"ADC", DP}, {"ROR", DP}, {"ADC", IDL},
    {"PLA", IMP}, {"ADC", IMM_M}, {"ROR", ACC}, {"RTL", IMP}, {"JMP", IND}, {"ADC", ABS}, {"ROR", ABS}, {"ADC", ABL},
    {"BVS", REL}, {"ADC", IDY}, {"ADC", IDP}, {"ADC", ISY}, {"STZ", DPX}, {"ADC", DPX}, {"ROR", DPX}, {"ADC", ILY},
    {"SEI", IMP}, {"ADC", ABY}, {"PLY", IMP}, {"TDC", IMP}, {"JMP", IAX}, {"ADC", ABX}, {"ROR", ABX}, {"ADC", ALX},
    {"BRA", REL}, {"STA", IDX}, {"BRL", RELL}, {"STA", SR}, {"STY", DP}, {"STA", DP}, {"STX", DP}, {"STA", IDL},
    {"DEY", IMP}, {"BIT", IMM_M}, {"TXA", IMP}, {"PHB", IMP}, {"STY", ABS}, {"STA", ABS}, {"STX", ABS}, {"STA", ABL},
    {"BCC", REL}, {"STA", IDY}, {"STA", IDP}, {"STA", ISY}, {"STY", DPX}, {"STA", DPX}, {"STX", DPY}, {"STA", ILY},
    {"TYA", IMP}, {"STA", ABY}, {"TXS", IMP}, {"TXY", IMP}, {"STZ", ABS}, {"STA", ABX}, {"STZ", ABX}, {"STA", ALX},
    {"LDY", IMM_X}, {"LDA", IDX}, {"LDX", IMM_X}, {"LDA", SR}, {"LDY", DP}, {"LDA", DP}, {"LDX", DP}, {"LDA", IDL},
    {"TAY", IMP}, {"LDA", IMM_M}, {"TAX", IMP}, {"PLB", IMP}, {"LDY", ABS}, {"LDA", ABS}, {"LDX", ABS}, {"LDA", ABL},
    {"BCS", REL}, {"LDA", IDY}, {"LDA", IDP}, {"LDA", ISY}, {"LDY", DPX}, {"LDA", DPX}, {"LDX", DPY}, {"LDA", ILY},
    {"CLV", IMP}, {"LDA", ABY}, {"TSX", IMP}, {"TYX", IMP}, {"LDY", ABX}, {"LDA", ABX}, {"LDX", ABY}, {"LDA", ALX},
    {"CPY", IMM_X}, {"CMP", IDX}, {"REP", IMM8}, {"CMP", SR}, {"CPY", DP}, {"CMP", DP}, {"DEC", DP}, {"CMP", IDL},
    {"INY", IMP}, {"CMP", IMM_M}, {"DEX", IMP}, {"WAI", IMP}, {"CPY", ABS}, {"CMP", ABS}, {"DEC", ABS}, {"CMP", ABL},
    {"BNE", REL}, {"CMP", IDY}, {"CMP", IDP}, {"CMP", ISY}, {"PEI", DP}, {"CMP", DPX}, {"DEC", DPX}, {"CMP", ILY},
    {"CLD", IMP}, {"CMP", ABY}, {"PHX", IMP}, {"STP", IMP}, {"JML", IAL}, {"CMP", ABX}, {"DEC", ABX}, {"CMP", ALX},
    {"CPX", IMM_X}, {"SBC", IDX}, {"SEP", IMM8}, {"SBC", SR}, {"CPX", DP}, {"SBC", DP}, {"INC", DP}, {"SBC", IDL},
    {"INX", IMP}, {"SBC", IMM_M}, {"NOP", IMP}, {"XBA", IMP}, {"CPX", ABS}, {"SBC", ABS}, {"INC", ABS}, {"SBC", ABL},
    {"BEQ", REL}, {"SBC", IDY}, {"SBC", IDP}, {"SBC", ISY}, {"PEA", ABS}, {"SBC", DPX}, {"INC", DPX}, {"SBC", ILY},
    {"SED", IMP}, {"SBC", ABY}, {"PLX", IMP}, {"XCE", IMP}, {"JSR", IAX}, {"SBC", ABX}, {"INC", ABX}, {"SBC", ALX},
};
}  // namespace

int disasm65816(uint32_t pc, bool mflag, bool xflag, const std::function<uint8_t(uint32_t)>& rd,
                std::string& out) {
  auto at = [&](int i) { return rd((pc & 0xff0000) | ((pc + i) & 0xffff)); };
  uint8_t op = at(0);
  const Op& o = kOps[op];
  char buf[64];
  int len = 1;
  uint32_t b1 = at(1), b2 = at(2), b3 = at(3);
  switch (o.mode) {
    case IMP: snprintf(buf, sizeof buf, "%s", o.name); break;
    case ACC: snprintf(buf, sizeof buf, "%s A", o.name); break;
    case IMM8: snprintf(buf, sizeof buf, "%s #$%02X", o.name, b1); len = 2; break;
    case IMM_M:
      if (mflag) { snprintf(buf, sizeof buf, "%s #$%02X", o.name, b1); len = 2; }
      else { snprintf(buf, sizeof buf, "%s #$%04X", o.name, b1 | (b2 << 8)); len = 3; }
      break;
    case IMM_X:
      if (xflag) { snprintf(buf, sizeof buf, "%s #$%02X", o.name, b1); len = 2; }
      else { snprintf(buf, sizeof buf, "%s #$%04X", o.name, b1 | (b2 << 8)); len = 3; }
      break;
    case DP: snprintf(buf, sizeof buf, "%s $%02X", o.name, b1); len = 2; break;
    case DPX: snprintf(buf, sizeof buf, "%s $%02X,X", o.name, b1); len = 2; break;
    case DPY: snprintf(buf, sizeof buf, "%s $%02X,Y", o.name, b1); len = 2; break;
    case IDP: snprintf(buf, sizeof buf, "%s ($%02X)", o.name, b1); len = 2; break;
    case IDX: snprintf(buf, sizeof buf, "%s ($%02X,X)", o.name, b1); len = 2; break;
    case IDY: snprintf(buf, sizeof buf, "%s ($%02X),Y", o.name, b1); len = 2; break;
    case IDL: snprintf(buf, sizeof buf, "%s [$%02X]", o.name, b1); len = 2; break;
    case ILY: snprintf(buf, sizeof buf, "%s [$%02X],Y", o.name, b1); len = 2; break;
    case SR: snprintf(buf, sizeof buf, "%s $%02X,S", o.name, b1); len = 2; break;
    case ISY: snprintf(buf, sizeof buf, "%s ($%02X,S),Y", o.name, b1); len = 2; break;
    case ABS: snprintf(buf, sizeof buf, "%s $%04X", o.name, b1 | (b2 << 8)); len = 3; break;
    case ABX: snprintf(buf, sizeof buf, "%s $%04X,X", o.name, b1 | (b2 << 8)); len = 3; break;
    case ABY: snprintf(buf, sizeof buf, "%s $%04X,Y", o.name, b1 | (b2 << 8)); len = 3; break;
    case IND: snprintf(buf, sizeof buf, "%s ($%04X)", o.name, b1 | (b2 << 8)); len = 3; break;
    case IAX: snprintf(buf, sizeof buf, "%s ($%04X,X)", o.name, b1 | (b2 << 8)); len = 3; break;
    case IAL: snprintf(buf, sizeof buf, "%s [$%04X]", o.name, b1 | (b2 << 8)); len = 3; break;
    case ABL: snprintf(buf, sizeof buf, "%s $%06X", o.name, b1 | (b2 << 8) | (b3 << 16)); len = 4; break;
    case ALX: snprintf(buf, sizeof buf, "%s $%06X,X", o.name, b1 | (b2 << 8) | (b3 << 16)); len = 4; break;
    case REL: {
      uint16_t t = (pc + 2 + (int8_t)b1) & 0xffff;
      snprintf(buf, sizeof buf, "%s $%04X", o.name, t);
      len = 2;
      break;
    }
    case RELL: {
      uint16_t t = (pc + 3 + (int16_t)(b1 | (b2 << 8))) & 0xffff;
      snprintf(buf, sizeof buf, "%s $%04X", o.name, t);
      len = 3;
      break;
    }
    case MOVE: snprintf(buf, sizeof buf, "%s $%02X,$%02X", o.name, b2, b1); len = 3; break;
  }
  out = buf;
  return len;
}
