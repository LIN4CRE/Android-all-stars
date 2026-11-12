// Helpers for loading save states: data from a file is not trusted until it
// has been brought into range.
#pragma once
#include <cstdint>
#include <cstring>

// A bool read as raw bytes may hold any value: make it 0 or 1.
inline void fixBool(bool& b) {
  uint8_t t;
  std::memcpy(&t, &b, 1);
  b = t != 0;
}
