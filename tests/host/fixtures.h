// Things the tests make in memory. Nothing here is game data, and nothing
// here is ever written into the repository: files go to a temporary folder
// that is removed when the test ends.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "app/sys.h"
#include "check.h"
#include "core/crc32.h"
#include "core/fileio.h"

namespace fx {

inline std::vector<uint8_t> pattern(size_t n, uint32_t seed) {
  std::vector<uint8_t> v(n);
  uint32_t x = seed * 2654435761u + 12345u;
  for (size_t i = 0; i < n; i++) {
    x = x * 1664525u + 1013904223u;
    v[i] = (uint8_t)(x >> 24);
  }
  return v;
}

// The cartridge's 8 KB of battery memory, with something recognisable in it.
inline std::vector<uint8_t> sram(uint32_t seed) { return pattern(0x2000, seed); }

inline void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}

// Bytes with the outward form of a save state (the core's magic number, a
// version, a checksum at the end) around arbitrary contents: enough for the
// code that stores and checks states. States the console actually loads are
// made by the console itself (test_core.cpp, test_app.cpp).
inline std::vector<uint8_t> stateShaped(size_t payload, uint32_t seed, uint32_t version = 3) {
  std::vector<uint8_t> d;
  put32(d, 0x53534d53u);
  put32(d, version);
  const std::vector<uint8_t> p = pattern(payload, seed);
  d.insert(d.end(), p.begin(), p.end());
  put32(d, crc32(d.data(), d.size()));
  return d;
}

inline std::vector<uint8_t> slurp(const std::wstring& p) {
  std::vector<uint8_t> v;
  CHECK_MSG(readFile(p, v), "cannot read " + check::show(p));
  return v;
}

inline void spit(const std::wstring& p, const std::vector<uint8_t>& d) {
  CHECK_MSG(writeFileSafely(p, d.data(), d.size()), "cannot write " + check::show(p));
}

// A file as a power cut or a failing storage device leaves it.
inline void flipBit(const std::wstring& p, size_t byte, int bit = 0) {
  std::vector<uint8_t> d = slurp(p);
  REQUIRE(byte < d.size());
  d[byte] ^= (uint8_t)(1u << bit);
  spit(p, d);
}
inline void truncateTo(const std::wstring& p, size_t n) {
  std::vector<uint8_t> d = slurp(p);
  REQUIRE(n <= d.size());
  d.resize(n);
  spit(p, d);
}

// The names of the files in a folder, sorted (for "nothing changed" checks).
inline std::vector<std::wstring> names(const std::wstring& dir) {
  std::vector<std::wstring> n;
  listFiles(dir, n);
  std::sort(n.begin(), n.end());
  return n;
}
inline bool hasFileStarting(const std::wstring& dir, const std::wstring& prefix) {
  for (const std::wstring& n : names(dir))
    if (n.compare(0, prefix.size(), prefix) == 0) return true;
  return false;
}

// Everything in a folder, name and contents (for "exactly as it was" checks).
inline std::vector<std::pair<std::wstring, std::vector<uint8_t>>> snapshot(const std::wstring& dir) {
  std::vector<std::pair<std::wstring, std::vector<uint8_t>>> out;
  for (const std::wstring& n : names(dir)) out.push_back({n, slurp(pathJoin(dir, n))});
  return out;
}

// Installs write faults for the life of the object (core/fileio.h).
struct Faults {
  FileIoFaults f;
  Faults() { g_fileIoFaults = &f; }
  ~Faults() { g_fileIoFaults = nullptr; }
  Faults(const Faults&) = delete;
  Faults& operator=(const Faults&) = delete;
};

}  // namespace fx

namespace fx {

// Four bytes at `at` chosen so that the whole buffer's CRC-32 is `want`.
// (A CRC identifies a file; it is not a signature, and the app does not treat
// it as one. This lets the tests exercise the "exactly the supported version"
// path with a buffer that contains no game data at all.)
inline void forceCrc(std::vector<uint8_t>& d, size_t at, uint32_t want) {
  REQUIRE(at + 4 <= d.size());
  uint32_t table[256];
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
    table[i] = c;
  }
  // the register after everything before the four bytes
  uint32_t a = 0xffffffffu;
  for (size_t i = 0; i < at; i++) a = table[(a ^ d[i]) & 0xff] ^ (a >> 8);
  // the register needed after them: the wanted end, run backwards over what follows
  auto back = [&](uint32_t reg, uint8_t byte) {
    uint32_t idx = 0;
    while ((table[idx] >> 24) != (reg >> 24)) idx++;
    return ((reg ^ table[idx]) << 8) | (idx ^ byte);
  };
  uint32_t b = ~want;
  for (size_t i = d.size(); i > at + 4; i--) b = back(b, d[i - 1]);
  for (int i = 0; i < 4; i++) b = back(b, 0);
  const uint32_t x = b ^ a;
  for (int i = 0; i < 4; i++) d[at + i] = (uint8_t)(x >> (8 * i));
  REQUIRE(crc32(d.data(), d.size()) == want);
}

}  // namespace fx
