// Plain field-by-field serialisation for the game layer's save-state block
// (SmasGame::saveExt). Readers fail softly: a short or damaged block is
// ignored and the game layer falls back to its defaults.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

struct ExtWriter {
  std::vector<uint8_t>& out;
  template <typename T>
  void v(const T& x) {
    const uint8_t* p = (const uint8_t*)&x;
    out.insert(out.end(), p, p + sizeof(T));
  }
};

struct ExtReader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;
  template <typename T>
  void v(T& x) {
    if (!ok || p + sizeof(T) > end) {
      ok = false;
      return;
    }
    std::memcpy(&x, p, sizeof(T));
    p += sizeof(T);
  }
  void b(bool& x) {  // stored as a byte
    uint8_t t = 0;
    v(t);
    x = t != 0;
  }
};
