// Zip archives for save backups: see zipfile.h.
#include "core/zipfile.h"
#include <cstring>
#include <ctime>
#include "core/crc32.h"

namespace {
void put16(std::vector<uint8_t>& o, unsigned v) {
  o.push_back((uint8_t)v);
  o.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
  put16(o, v & 0xffff);
  put16(o, v >> 16);
}
unsigned get16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t get32(const uint8_t* p) { return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16; }

// MS-DOS date and time, as zip keeps them (local time, two-second steps).
void dosTime(long long when, unsigned* date, unsigned* time) {
  time_t t = (time_t)(when > 0 ? when : std::time(nullptr));
  tm lt{};
  localtime_r(&t, &lt);
  int year = lt.tm_year + 1900;
  if (year < 1980) year = 1980, lt.tm_mon = 0, lt.tm_mday = 1, lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
  if (year > 2107) year = 2107;
  *date = (unsigned)((year - 1980) << 9 | (lt.tm_mon + 1) << 5 | lt.tm_mday);
  *time = (unsigned)(lt.tm_hour << 11 | lt.tm_min << 5 | lt.tm_sec / 2);
}

long long unixTime(unsigned date, unsigned time) {
  tm lt{};
  lt.tm_year = (int)(date >> 9) + 80;
  lt.tm_mon = (int)((date >> 5) & 15) - 1;
  lt.tm_mday = (int)(date & 31);
  lt.tm_hour = (int)(time >> 11);
  lt.tm_min = (int)((time >> 5) & 63);
  lt.tm_sec = (int)(time & 31) * 2;
  lt.tm_isdst = -1;
  time_t t = mktime(&lt);
  return t == (time_t)-1 ? 0 : (long long)t;
}

// A name that is only ever a relative path inside the archive.
bool safeName(const std::string& n) {
  if (n.empty() || n.size() > 240 || n[0] == '/' || n.find('\\') != std::string::npos ||
      n.find(':') != std::string::npos)
    return false;
  for (unsigned char c : n)
    if (c < 0x20) return false;
  size_t start = 0;
  while (start <= n.size()) {
    size_t end = n.find('/', start);
    if (end == std::string::npos) end = n.size();
    const std::string part = n.substr(start, end - start);
    if (part == ".." || part == ".") return false;
    start = end + 1;
  }
  return true;
}
}  // namespace

std::vector<uint8_t> zipWrite(const std::vector<ZipEntry>& entries) {
  std::vector<uint8_t> o, central;
  for (const ZipEntry& e : entries) {
    unsigned date, time;
    dosTime(e.time, &date, &time);
    const uint32_t crc = crc32(e.data.data(), e.data.size()), size = (uint32_t)e.data.size(), at = (uint32_t)o.size();
    const unsigned flags = 0x0800;  // (names in UTF-8)
    // local file header
    put32(o, 0x04034b50);
    put16(o, 20);  // version needed: 2.0
    put16(o, flags);
    put16(o, 0);  // stored
    put16(o, time);
    put16(o, date);
    put32(o, crc);
    put32(o, size);
    put32(o, size);
    put16(o, (unsigned)e.name.size());
    put16(o, 0);
    o.insert(o.end(), e.name.begin(), e.name.end());
    o.insert(o.end(), e.data.begin(), e.data.end());
    // its entry in the central directory
    put32(central, 0x02014b50);
    put16(central, 20);  // made by: 2.0, MS-DOS attributes
    put16(central, 20);
    put16(central, flags);
    put16(central, 0);
    put16(central, time);
    put16(central, date);
    put32(central, crc);
    put32(central, size);
    put32(central, size);
    put16(central, (unsigned)e.name.size());
    put16(central, 0);  // extra
    put16(central, 0);  // comment
    put16(central, 0);  // disk
    put16(central, 0);  // internal attributes
    put32(central, 0);  // external attributes
    put32(central, at);
    central.insert(central.end(), e.name.begin(), e.name.end());
  }
  const uint32_t dirAt = (uint32_t)o.size(), dirSize = (uint32_t)central.size();
  o.insert(o.end(), central.begin(), central.end());
  put32(o, 0x06054b50);  // end of central directory
  put16(o, 0);
  put16(o, 0);
  put16(o, (unsigned)entries.size());
  put16(o, (unsigned)entries.size());
  put32(o, dirSize);
  put32(o, dirAt);
  put16(o, 0);
  return o;
}

bool zipRead(const std::vector<uint8_t>& f, std::vector<ZipEntry>* entries, std::string* err, const ZipLimits& lim) {
  auto fail = [&](const char* why) {
    if (err) *err = why;
    return false;
  };
  entries->clear();
  const size_t n = f.size();
  if (n < 22) return fail("the file is too short to be a zip archive");
  if (get32(f.data()) != 0x04034b50 && get32(f.data()) != 0x06054b50) return fail("the file is not a zip archive");
  // The end record: within the last 64 KB (it may be followed by a comment).
  size_t eocd = n;
  for (size_t back = 22; back <= n && back <= 22 + 0xffff; back++) {
    const size_t at = n - back;
    if (get32(&f[at]) == 0x06054b50 && (size_t)get16(&f[at + 20]) == back - 22) {
      eocd = at;
      break;
    }
  }
  if (eocd == n) return fail("the archive's end record is missing (the file is incomplete)");
  const unsigned disk = get16(&f[eocd + 4]), dirDisk = get16(&f[eocd + 6]);
  const unsigned count = get16(&f[eocd + 10]), countHere = get16(&f[eocd + 8]);
  const uint32_t dirSize = get32(&f[eocd + 12]), dirAt = get32(&f[eocd + 16]);
  if (disk != 0 || dirDisk != 0 || count != countHere) return fail("split archives are not supported");
  if (count == 0xffff || dirSize == 0xffffffffu || dirAt == 0xffffffffu) return fail("zip64 archives are not supported");
  if ((int)count > lim.maxEntries) return fail("the archive has too many entries to be a backup");
  if ((uint64_t)dirAt + dirSize > eocd) return fail("the archive's directory is damaged");
  size_t p = dirAt, total = 0;
  for (unsigned i = 0; i < count; i++) {
    if (p + 46 > (size_t)dirAt + dirSize || get32(&f[p]) != 0x02014b50) return fail("the archive's directory is damaged");
    const unsigned flags = get16(&f[p + 8]), method = get16(&f[p + 10]);
    const unsigned time = get16(&f[p + 12]), date = get16(&f[p + 14]);
    const uint32_t crc = get32(&f[p + 16]), csize = get32(&f[p + 20]), usize = get32(&f[p + 24]);
    const unsigned nameLen = get16(&f[p + 28]), extraLen = get16(&f[p + 30]), commentLen = get16(&f[p + 32]);
    const uint32_t localAt = get32(&f[p + 42]);
    if (p + 46 + nameLen + extraLen + commentLen > (size_t)dirAt + dirSize) return fail("the archive's directory is damaged");
    ZipEntry e;
    e.name.assign((const char*)&f[p + 46], nameLen);
    e.time = unixTime(date, time);
    p += 46 + nameLen + extraLen + commentLen;
    if (!e.name.empty() && e.name.back() == '/') continue;  // (a folder: nothing in it)
    if (!safeName(e.name)) return fail("the archive has an entry with an unsafe name");
    if (flags & 0x0041) return fail("encrypted archives are not supported");
    if (csize == 0xffffffffu || usize == 0xffffffffu || localAt == 0xffffffffu) return fail("zip64 archives are not supported");
    if (usize > lim.maxEntryBytes) return fail("the archive has an entry too large to be a save");
    total += usize;
    if (total > lim.maxTotalBytes) return fail("the archive is too large to be a backup");
    // Its data: after the local header (whose name and extra lengths may differ from the directory's).
    if ((uint64_t)localAt + 30 > dirAt || get32(&f[localAt]) != 0x04034b50) return fail("an entry's header is damaged");
    const size_t dataAt = (size_t)localAt + 30 + get16(&f[localAt + 26]) + get16(&f[localAt + 28]);
    if ((uint64_t)dataAt + csize > dirAt) return fail("an entry's data runs past the end of the archive");
    e.data.resize(usize);
    if (method == 0) {
      if (csize != usize) return fail("an entry's length is inconsistent");
      if (usize) std::memcpy(e.data.data(), &f[dataAt], usize);
    } else if (method == 8) {
      if (usize && !inflateRaw(&f[dataAt], csize, e.data.data(), usize)) return fail("an entry could not be decompressed");
    } else {
      return fail("the archive uses a compression method that is not supported");
    }
    if (crc32(e.data.data(), e.data.size()) != crc) return fail("an entry is damaged (its checksum does not match)");
    entries->push_back(std::move(e));
  }
  return true;
}
