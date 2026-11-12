// CRC-32 (the zip / PNG one: polynomial 0xEDB88320), for checking that saved
// files are whole. `crc` chains calls: crc32(b, nb, crc32(a, na)).
#pragma once
#include <cstddef>
#include <cstdint>

inline uint32_t crc32(const void* data, size_t n, uint32_t crc = 0) {
  struct Table {
    uint32_t v[256];
    Table() {
      for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        v[i] = c;
      }
    }
  };
  static const Table table;
  const uint8_t* p = (const uint8_t*)data;
  uint32_t c = ~crc;
  for (size_t i = 0; i < n; i++) c = table.v[(c ^ p[i]) & 0xff] ^ (c >> 8);
  return ~c;
}
