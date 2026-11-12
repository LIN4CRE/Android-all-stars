#include "png.h"
#include "fileio.h"
#include <string>
#include <vector>

namespace {
uint32_t crcTable[256];
bool crcInit = false;
uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
  if (!crcInit) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
      crcTable[i] = v;
    }
    crcInit = true;
  }
  c = ~c;
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ d[i]) & 0xff] ^ (c >> 8);
  return ~c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24);
  v.push_back(x >> 16);
  v.push_back(x >> 8);
  v.push_back(x);
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  size_t start = out.size();
  be32(out, (uint32_t)data.size());
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  uint32_t c = crc32(out.data() + start + 4, out.size() - start - 4);
  be32(out, c);
}
}  // namespace

// `path` in UTF-8 (the app converts its wide paths); the ANSI code page if it
// isn't valid UTF-8 (e.g. smastool arguments).
bool writePng(const std::string& path, const uint32_t* px, int w, int h, int stride) {
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // RGB
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(png, "IHDR", ihdr);

  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 3 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      uint32_t c = px[y * stride + x];
      raw.push_back((c >> 16) & 0xff);
      raw.push_back((c >> 8) & 0xff);
      raw.push_back(c & 0xff);
    }
  }
  // zlib stream with stored blocks
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  while (pos < raw.size() || raw.empty()) {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xff);
    z.push_back(n >> 8);
    z.push_back(~n & 0xff);
    z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  be32(z, (b << 16) | a);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  return writeFileSafely(path, png.data(), png.size());
}
