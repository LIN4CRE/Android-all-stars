// Save backups (app/backup.h) and the zip archives they are (core/zipfile.h):
// that a backup restores exactly, that an unsound one changes nothing, and
// that nothing in an archive can write anywhere but the app's own save files.
#include <cstring>
#include "app/backup.h"
#include "app/savestore.h"
#include "check.h"
#include "core/zipfile.h"
#include "fixtures.h"

using fx::Faults;

namespace {
// ---- archives made by hand, to say exactly what is in each field -----------
void put16(std::vector<uint8_t>& o, unsigned v) {
  o.push_back((uint8_t)v);
  o.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
  put16(o, v & 0xffff);
  put16(o, v >> 16);
}
struct RawEntry {
  std::string name;
  std::vector<uint8_t> stored;  // the bytes in the archive
  unsigned method = 0;
  unsigned flags = 0;
  uint32_t crc = 0;
  uint32_t usize = 0;
  std::vector<uint8_t> localExtra;  // (a local header's extra field can differ from the directory's)
};
RawEntry rawStored(const std::string& name, const std::vector<uint8_t>& data) {
  RawEntry e;
  e.name = name;
  e.stored = data;
  e.crc = crc32(data.data(), data.size());
  e.usize = (uint32_t)data.size();
  return e;
}
std::vector<uint8_t> rawZip(const std::vector<RawEntry>& entries, const std::string& comment = "") {
  std::vector<uint8_t> o, dir;
  for (const RawEntry& e : entries) {
    const uint32_t at = (uint32_t)o.size();
    put32(o, 0x04034b50);
    put16(o, 20);
    put16(o, e.flags);
    put16(o, e.method);
    put16(o, 0);
    put16(o, 0x5b6c);
    put32(o, e.crc);
    put32(o, (uint32_t)e.stored.size());
    put32(o, e.usize);
    put16(o, (unsigned)e.name.size());
    put16(o, (unsigned)e.localExtra.size());
    o.insert(o.end(), e.name.begin(), e.name.end());
    o.insert(o.end(), e.localExtra.begin(), e.localExtra.end());
    o.insert(o.end(), e.stored.begin(), e.stored.end());
    put32(dir, 0x02014b50);
    put16(dir, 20);
    put16(dir, 20);
    put16(dir, e.flags);
    put16(dir, e.method);
    put16(dir, 0);
    put16(dir, 0x5b6c);
    put32(dir, e.crc);
    put32(dir, (uint32_t)e.stored.size());
    put32(dir, e.usize);
    put16(dir, (unsigned)e.name.size());
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put32(dir, 0);
    put32(dir, at);
    dir.insert(dir.end(), e.name.begin(), e.name.end());
  }
  const uint32_t dirAt = (uint32_t)o.size();
  o.insert(o.end(), dir.begin(), dir.end());
  put32(o, 0x06054b50);
  put16(o, 0);
  put16(o, 0);
  put16(o, (unsigned)entries.size());
  put16(o, (unsigned)entries.size());
  put32(o, (uint32_t)dir.size());
  put32(o, dirAt);
  put16(o, (unsigned)comment.size());
  o.insert(o.end(), comment.begin(), comment.end());
  return o;
}
std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// Text and its "deflate" form as zlib (an independent implementation) made
// it: once with dynamic Huffman codes, once with the fixed ones.
std::string deflateText() {
  std::string t;
  for (int i = 0; i < 120; i++) {
    char line[80];
    snprintf(line, sizeof line, "line %03d: the quick brown fox jumps over the lazy dog\n", i);
    t += line;
  }
  return t;
}
const uint8_t kDeflateDynamic[] = {
    0x9d, 0xd8, 0xc7, 0x51, 0x03, 0x40, 0x14, 0x44, 0xc1, 0x3b, 0x51, 0x6c, 0x08, 0xfa, 0xeb, 0x97, 0x6c, 0x30, 0x02, 0x04,
    0x42, 0xc2, 0x09, 0x17, 0x3d, 0x55, 0x10, 0x01, 0x7d, 0x9e, 0x9a, 0x5b, 0x9f, 0xde, 0x7e, 0x77, 0xd8, 0xa6, 0xcd, 0x66,
    0x73, 0x9e, 0xde, 0xee, 0xb6, 0xe9, 0xf9, 0xb4, 0xbb, 0x7a, 0x48, 0x97, 0x2f, 0xc7, 0x8f, 0x43, 0xba, 0x39, 0x7e, 0xa6,
    0xfb, 0xd3, 0xe3, 0xd3, 0x6b, 0x3a, 0xbe, 0x6f, 0x5f, 0x7e, 0xe7, 0xfd, 0xc5, 0xf7, 0x57, 0xba, 0x3e, 0xde, 0x9e, 0xed,
    0xff, 0x5e, 0x41, 0xaf, 0x4c, 0xaf, 0x42, 0xaf, 0x4a, 0xaf, 0x46, 0xaf, 0x4e, 0xaf, 0x41, 0xaf, 0x49, 0xaf, 0x25, 0xaf,
    0x20, 0x1b, 0x41, 0x36, 0x82, 0x6c, 0x04, 0xd9, 0x08, 0xb2, 0x11, 0x64, 0x23, 0xc8, 0x46, 0x90, 0x8d, 0x20, 0x1b, 0x41,
    0x36, 0x32, 0xd9, 0xc8, 0x64, 0x23, 0x93, 0x8d, 0x4c, 0x36, 0x32, 0xd9, 0xc8, 0x64, 0x23, 0x93, 0x8d, 0x4c, 0x36, 0x32,
    0xd9, 0xc8, 0x64, 0xa3, 0x90, 0x8d, 0x42, 0x36, 0x0a, 0xd9, 0x28, 0x64, 0xa3, 0x90, 0x8d, 0x42, 0x36, 0x0a, 0xd9, 0x28,
    0x64, 0xa3, 0x90, 0x8d, 0x42, 0x36, 0x2a, 0xd9, 0xa8, 0x64, 0xa3, 0x92, 0x8d, 0x4a, 0x36, 0x2a, 0xd9, 0xa8, 0x64, 0xa3,
    0x92, 0x8d, 0x4a, 0x36, 0x2a, 0xd9, 0xa8, 0x64, 0xa3, 0x91, 0x8d, 0x46, 0x36, 0x1a, 0xd9, 0x68, 0x64, 0xa3, 0x91, 0x8d,
    0x46, 0x36, 0x1a, 0xd9, 0x68, 0x64, 0xa3, 0x91, 0x8d, 0x46, 0x36, 0x3a, 0xd9, 0xe8, 0x64, 0xa3, 0x93, 0x8d, 0x4e, 0x36,
    0x3a, 0xd9, 0xe8, 0x64, 0xa3, 0x93, 0x8d, 0x4e, 0x36, 0x3a, 0xd9, 0xe8, 0x64, 0x63, 0x90, 0x8d, 0x41, 0x36, 0x06, 0xd9,
    0x18, 0x64, 0x63, 0x90, 0x8d, 0x41, 0x36, 0x06, 0xd9, 0x18, 0x64, 0x63, 0x90, 0x8d, 0x41, 0x36, 0x26, 0xd9, 0x98, 0x64,
    0x63, 0x92, 0x8d, 0x49, 0x36, 0x26, 0xd9, 0x98, 0x64, 0x63, 0x92, 0x8d, 0x49, 0x36, 0x26, 0xd9, 0x98, 0x64, 0x63, 0x91,
    0x8d, 0x45, 0x36, 0x16, 0xd9, 0x58, 0x64, 0x63, 0x91, 0x8d, 0x45, 0x36, 0x16, 0xd9, 0x58, 0x64, 0x63, 0x91, 0x8d, 0x25,
    0x36, 0x82, 0xba, 0x68, 0x50, 0x17, 0x0d, 0xea, 0xa2, 0x41, 0x5d, 0x34, 0xa8, 0x8b, 0x06, 0x75, 0xd1, 0xa0, 0x2e, 0x1a,
    0xd4, 0x45, 0x83, 0xba, 0x68, 0x50, 0x17, 0x0d, 0xea, 0xa2, 0x41, 0x5d, 0x34, 0xa8, 0x8b, 0x06, 0x75, 0xd1, 0xa0, 0x2e,
    0x1a, 0xd4, 0x45, 0x83, 0xba, 0x68, 0x50, 0x17, 0x0d, 0xea, 0xa2, 0xf1, 0xdf, 0x2e, 0xfa, 0x03,
};
const uint8_t kDeflateFixed[] = {
    0xcb, 0xc9, 0xcc, 0x4b, 0x55, 0x30, 0x30, 0x30, 0xb0, 0x52, 0x28, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56,
    0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a, 0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d,
    0x02, 0x4b, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0x73, 0xe5, 0x80, 0x75, 0x01, 0x00,
};

bool reads(const std::vector<uint8_t>& zip, std::string* err = nullptr) {
  std::vector<ZipEntry> entries;
  std::string why;
  const bool ok = zipRead(zip, &entries, &why);
  if (err) *err = why;
  if (!ok) CHECK_MSG(!why.empty(), "a refusal with no reason");
  return ok;
}
}  // namespace

// ---- zip --------------------------------------------------------------------

TEST(zip_round_trip) {
  std::vector<ZipEntry> in(4);
  in[0].name = "smas-mobile-backup.txt";
  in[0].data = bytesOf("hello\n");
  in[1].name = "saves/smas.srm";
  in[1].data = fx::sram(1);
  in[1].time = 1794500000LL;
  in[2].name = "saves/empty";
  in[3].name = "settings/\xc3\xa9t\xc3\xa9.ini";  // (UTF-8 names pass through)
  in[3].data = fx::pattern(100000, 3);
  const std::vector<uint8_t> zip = zipWrite(in);
  std::vector<ZipEntry> out;
  std::string err;
  REQUIRE(zipRead(zip, &out, &err));
  REQUIRE(out.size() == in.size());
  for (size_t i = 0; i < in.size(); i++) {
    CHECK_EQ(out[i].name, in[i].name);
    CHECK(out[i].data == in[i].data);
  }
  // (zip keeps times to two seconds)
  CHECK(out[1].time >= 1794500000LL - 2 && out[1].time <= 1794500000LL + 2);
  // An archive of nothing is still an archive.
  REQUIRE(zipRead(zipWrite({}), &out, &err));
  CHECK(out.empty());
}

TEST(zip_reads_deflated_entries) {
  const std::string text = deflateText();
  RawEntry dyn;
  dyn.name = "dynamic.txt";
  dyn.method = 8;
  dyn.stored.assign(kDeflateDynamic, kDeflateDynamic + sizeof kDeflateDynamic);
  dyn.usize = (uint32_t)text.size();
  dyn.crc = crc32(text.data(), text.size());
  RawEntry fix;
  fix.name = "fixed.txt";
  fix.method = 8;
  fix.stored.assign(kDeflateFixed, kDeflateFixed + sizeof kDeflateFixed);
  fix.usize = 60;
  fix.crc = crc32(text.data(), 60);
  // (a "stored" block inside a deflate stream: final, type 00, length, its complement, the bytes)
  RawEntry blk;
  blk.name = "block.bin";
  blk.method = 8;
  const std::vector<uint8_t> payload = fx::pattern(300, 8);
  blk.stored = {0x01, 0x2c, 0x01, 0xd3, 0xfe};
  blk.stored.insert(blk.stored.end(), payload.begin(), payload.end());
  blk.usize = 300;
  blk.crc = crc32(payload.data(), payload.size());
  std::vector<ZipEntry> out;
  std::string err;
  REQUIRE(zipRead(rawZip({dyn, fix, blk}), &out, &err));
  REQUIRE(out.size() == 3);
  CHECK(std::string(out[0].data.begin(), out[0].data.end()) == text);
  CHECK(std::string(out[1].data.begin(), out[1].data.end()) == text.substr(0, 60));
  CHECK(out[2].data == payload);

  // Compressed data that is damaged, cut short, or longer or shorter than it says: refused.
  RawEntry bad = dyn;
  bad.stored[40] ^= 0x10;
  CHECK(!reads(rawZip({bad})));
  bad = dyn;
  bad.stored.resize(bad.stored.size() / 2);
  CHECK(!reads(rawZip({bad})));
  bad = dyn;
  bad.usize = (uint32_t)text.size() - 1;  // (it would decompress to more than it claims: never written past the end)
  CHECK(!reads(rawZip({bad})));
  bad = dyn;
  bad.usize = (uint32_t)text.size() + 1;
  CHECK(!reads(rawZip({bad})));
}

TEST(zip_inflate_raw) {
  const std::string text = deflateText();
  std::vector<uint8_t> out(text.size());
  CHECK(inflateRaw(kDeflateDynamic, sizeof kDeflateDynamic, out.data(), out.size()));
  CHECK(std::memcmp(out.data(), text.data(), text.size()) == 0);
  // The output must come out exactly as long as asked: too short or too long is a failure.
  std::vector<uint8_t> small(text.size() - 10);
  CHECK(!inflateRaw(kDeflateDynamic, sizeof kDeflateDynamic, small.data(), small.size()));
  std::vector<uint8_t> large(text.size() + 10);
  CHECK(!inflateRaw(kDeflateDynamic, sizeof kDeflateDynamic, large.data(), large.size()));
  CHECK(!inflateRaw(kDeflateDynamic, 0, out.data(), out.size()));
}

TEST(zip_tolerates_what_other_tools_add) {
  // A comment after the end record, a folder entry, an extra field in a local header.
  RawEntry a = rawStored("saves/smas.srm", fx::sram(1));
  a.localExtra = {0x55, 0x54, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  RawEntry folder = rawStored("saves/", {});
  std::vector<ZipEntry> out;
  std::string err;
  REQUIRE(zipRead(rawZip({folder, a}, "made by some file manager"), &out, &err));
  REQUIRE(out.size() == 1);
  CHECK_EQ(out[0].name, std::string("saves/smas.srm"));
  CHECK(out[0].data == fx::sram(1));
}

TEST(zip_refuses_what_is_not_a_sound_archive) {
  std::string err;
  CHECK(!reads({}, &err));
  CHECK(!reads(fx::pattern(10, 1), &err));
  CHECK(!reads(fx::pattern(5000, 1), &err));
  CHECK(err.find("not a zip") != std::string::npos);
  const std::vector<uint8_t> good = rawZip({rawStored("a.txt", bytesOf("some text in a file"))});
  REQUIRE(reads(good));

  // A checksum that does not match its data.
  RawEntry e = rawStored("a.txt", bytesOf("some text in a file"));
  e.crc ^= 1;
  CHECK(!reads(rawZip({e}), &err));
  CHECK(err.find("checksum") != std::string::npos);
  // Encrypted.
  e = rawStored("a.txt", bytesOf("x"));
  e.flags = 1;
  CHECK(!reads(rawZip({e}), &err));
  CHECK(err.find("encrypted") != std::string::npos);
  // A compression method that isn't supported (bzip2, LZMA...).
  e = rawStored("a.txt", bytesOf("x"));
  e.method = 12;
  CHECK(!reads(rawZip({e}), &err));
  CHECK(err.find("compression method") != std::string::npos);
  // Stored, but the two lengths disagree.
  e = rawStored("a.txt", bytesOf("xyz"));
  e.usize = 2;
  CHECK(!reads(rawZip({e}), &err));
  // zip64 and split archives.
  e = rawStored("a.txt", bytesOf("x"));
  e.usize = 0xffffffffu;
  CHECK(!reads(rawZip({e}), &err));
  std::vector<uint8_t> split = good;
  split[split.size() - 22 + 4] = 1;  // "this is disk 1"
  CHECK(!reads(split, &err));
  CHECK(err.find("split") != std::string::npos);
  std::vector<uint8_t> z64 = good;
  for (int i = 0; i < 4; i++) z64[z64.size() - 22 + 16 + i] = 0xff;
  CHECK(!reads(z64, &err));
  // A directory that points outside the file, or an entry that does.
  std::vector<uint8_t> far = good;
  far[far.size() - 22 + 16] ^= 0x40;
  CHECK(!reads(far, &err));
  std::vector<uint8_t> farEntry = rawZip({rawStored("a.txt", bytesOf("some text in a file"))});
  const size_t dirAt = farEntry.size() - 22 - (46 + 5);
  farEntry[dirAt + 42] = 0xf0;  // (the entry's offset)
  CHECK(!reads(farEntry, &err));
}

TEST(zip_refuses_unsafe_names) {
  for (const char* name : {"../evil", "/absolute", "a/../../b", "a/./b", "..", "back\\slash", "c:drive", "ctrl\x01", ""}) {
    std::string err;
    CHECK_MSG(!reads(rawZip({rawStored(name, bytesOf("x"))}), &err), name);
    if (*name) CHECK_MSG(err.find("unsafe name") != std::string::npos, err);
  }
  std::string longName(300, 'a');
  CHECK(!reads(rawZip({rawStored(longName, bytesOf("x"))})));
  // (dots that are part of a name are fine)
  CHECK(reads(rawZip({rawStored("saves/smas..srm", bytesOf("x")), rawStored(".hidden", bytesOf("x"))})));
}

TEST(zip_limits) {
  std::vector<RawEntry> many;
  for (int i = 0; i < 65; i++) many.push_back(rawStored("f" + std::to_string(i), bytesOf("x")));
  std::string err;
  CHECK(!reads(rawZip(many), &err));
  CHECK(err.find("too many") != std::string::npos);
  many.pop_back();
  CHECK(reads(rawZip(many)));

  ZipLimits lim;
  lim.maxEntryBytes = 1000;
  lim.maxTotalBytes = 2500;
  std::vector<ZipEntry> out;
  CHECK(!zipRead(rawZip({rawStored("big", fx::pattern(1001, 1))}), &out, &err, lim));
  CHECK(err.find("too large") != std::string::npos);
  CHECK(zipRead(rawZip({rawStored("a", fx::pattern(1000, 1)), rawStored("b", fx::pattern(1000, 2))}), &out, &err, lim));
  CHECK(!zipRead(rawZip({rawStored("a", fx::pattern(1000, 1)), rawStored("b", fx::pattern(1000, 2)),
                         rawStored("c", fx::pattern(1000, 3))}),
                 &out, &err, lim));

  // A few bytes that claim to decompress to gigabytes are refused before any memory is taken for them.
  RawEntry bomb;
  bomb.name = "bomb";
  bomb.method = 8;
  bomb.stored = {0x01, 0x00, 0x00, 0xff, 0xff};
  bomb.usize = 0xfffffff0u;
  CHECK(!reads(rawZip({bomb}), &err));
  CHECK(err.find("too large") != std::string::npos);
}

TEST(zip_every_truncation_and_every_damaged_byte_is_handled) {
  std::vector<ZipEntry> in(3);
  in[0].name = "smas-mobile-backup.txt";
  in[0].data = bytesOf("SMAS Mobile backup\nformat: 1\n");
  in[1].name = "saves/smas.srm";
  in[1].data = fx::pattern(600, 1);
  in[2].name = "settings/smas_port.ini";
  in[2].data = bytesOf("[video]\nfilter=1\n");
  const std::vector<uint8_t> zip = zipWrite(in);
  // Cut short anywhere: refused (never read past the end, never accepted as whole).
  for (size_t n = 0; n < zip.size(); n++) {
    std::vector<uint8_t> cut(zip.begin(), zip.begin() + n);
    std::vector<ZipEntry> out;
    std::string err;
    CHECK_MSG(!zipRead(cut, &out, &err), "accepted when cut to " + std::to_string(n));
  }
  // Any one byte changed: either refused, or what is read is still exactly what was written
  // (bytes that carry no meaning: version fields, timestamps).
  int refused = 0, harmless = 0, wrong = 0;
  for (size_t i = 0; i < zip.size(); i++) {
    for (uint8_t mask : {0x01, 0x80, 0xff}) {
      std::vector<uint8_t> bad = zip;
      bad[i] ^= mask;
      std::vector<ZipEntry> out;
      std::string err;
      if (!zipRead(bad, &out, &err)) {
        refused++;
        continue;
      }
      bool same = out.size() == in.size();
      for (size_t k = 0; same && k < in.size(); k++) same = out[k].data == in[k].data;
      // (a changed name is a different entry, not different contents: the backup reader decides about names)
      (same ? harmless : wrong)++;
    }
  }
  CHECK_EQ(wrong, 0);
  CHECK(refused > harmless);
}

// ---- backups ------------------------------------------------------------------

namespace {
const char kIni[] = "[video]\nfilter=2\nwidescreen=1\n[touch]\nscale=120\n";
const char kAch[] = "[achievements]\nsmb1_first_level=1794500000\n";

// A device's app folder with saves of every kind in it, made from `seed`.
struct Device {
  BackupPaths paths;
  BatteryStore battery;
  explicit Device(const std::wstring& root) {
    paths.dataDir = root;
    paths.saveDir = pathJoin(root, L"saves");
    makeDir(paths.saveDir);
    battery.setDir(paths.saveDir, 10000);
    std::vector<uint8_t> mem(BatteryStore::kSize);
    battery.load(mem.data(), 0);
  }
  void fill(uint32_t seed) {
    REQUIRE(battery.save(fx::sram(seed).data(), 0x925637c7));
    REQUIRE(states::writeSlot(paths.saveDir, 1, fx::stateShaped(30000, seed + 1)));
    REQUIRE(states::writeSlot(paths.saveDir, 7, fx::stateShaped(31000, seed + 7)));
    seal::Info info;
    info.aux = 2;
    REQUIRE(states::writeSealed(pathJoin(paths.saveDir, L"resume.sst"), seal::kResume, info, fx::stateShaped(32000, seed + 20)));
    fx::spit(pathJoin(paths.dataDir, L"smas_port.ini"), bytesOf(std::string(kIni) + "seed=" + std::to_string(seed) + "\n"));
    fx::spit(pathJoin(paths.dataDir, L"achievements.ini"), bytesOf(std::string(kAch) + "seed=" + std::to_string(seed) + "\n"));
  }
  // Whether the device holds exactly what fill(seed) made.
  void expect(uint32_t seed) {
    BatteryStore b;
    b.setDir(paths.saveDir, 10000);
    std::vector<uint8_t> mem(BatteryStore::kSize, 0);
    b.load(mem.data(), 0);
    CHECK_MSG(mem == fx::sram(seed), "game progress of seed " + std::to_string(seed));
    std::vector<uint8_t> st;
    CHECK(states::readSlot(paths.saveDir, 1, &st) && st == fx::stateShaped(30000, seed + 1));
    CHECK(states::readSlot(paths.saveDir, 7, &st) && st == fx::stateShaped(31000, seed + 7));
    seal::Info info;
    CHECK(states::readSealed(pathJoin(paths.saveDir, L"resume.sst"), seal::kResume, &info, &st) &&
          st == fx::stateShaped(32000, seed + 20));
    CHECK_EQ(info.aux, 2u);
    CHECK(fx::slurp(pathJoin(paths.dataDir, L"smas_port.ini")) == bytesOf(std::string(kIni) + "seed=" + std::to_string(seed) + "\n"));
    CHECK(fx::slurp(pathJoin(paths.dataDir, L"achievements.ini")) ==
          bytesOf(std::string(kAch) + "seed=" + std::to_string(seed) + "\n"));
  }
  // Everything in the app's folders, for "nothing changed".
  std::vector<std::pair<std::wstring, std::vector<uint8_t>>> everything() const {
    auto all = fx::snapshot(paths.dataDir);
    for (const wchar_t* sub : {L"saves", L"saves/history", L"saves/safety", L"roms"})
      for (auto& f : fx::snapshot(pathJoin(paths.dataDir, sub))) all.push_back({std::wstring(sub) + L"/" + f.first, f.second});
    return all;
  }
  std::vector<uint8_t> backup(BackupSummary* s = nullptr) {
    std::vector<uint8_t> zip;
    std::string err;
    CHECK_MSG(buildBackup(paths, battery, "1.0.0", 0x925637c7, &zip, s, &err), err);
    return zip;
  }
  bool restore(const std::vector<uint8_t>& zip, std::string* err = nullptr, BackupSummary* done = nullptr,
               std::wstring* safety = nullptr) {
    std::string why;
    const bool ok = restoreBackup(zip, paths, battery, "1.0.0", 0x925637c7, done, safety, &why);
    if (err) *err = why;
    return ok;
  }
};

std::string manifestText(int format = kBackupFormat) {
  return "SMAS Mobile backup\nformat: " + std::to_string(format) + "\napp_version: 1.0.0\ncreated_unix: 1794500000\n";
}
ZipEntry entry(const std::string& name, const std::vector<uint8_t>& data) {
  ZipEntry e;
  e.name = name;
  e.data = data;
  return e;
}
}  // namespace

TEST(backup_nothing_to_back_up) {
  Device d(check::tempDir());
  std::vector<uint8_t> zip;
  std::string err;
  CHECK(!buildBackup(d.paths, d.battery, "1.0.0", 0, &zip, nullptr, &err));
  CHECK(err.find("nothing to back up") != std::string::npos);
}

TEST(backup_holds_everything_and_says_so) {
  Device d(check::tempDir());
  d.fill(1);
  BackupSummary s;
  const std::vector<uint8_t> zip = d.backup(&s);
  CHECK(s.progress);
  CHECK_EQ(s.states, 2);
  CHECK(s.stateSlots == std::vector<int>({1, 7}));
  CHECK(s.resume);
  CHECK(s.settings);
  CHECK(s.achievements);
  CHECK_EQ(s.bytes, zip.size());
  CHECK_EQ(s.appVersion, std::string("1.0.0"));
  CHECK_EQ(s.describe(), std::string("game progress, 2 save states, the point you last stopped at, settings and controls and achievements"));
  // Reading it back says the same.
  BackupSummary t;
  std::string err;
  REQUIRE(inspectBackup(zip, &t, &err));
  CHECK(t.progress && t.states == 2 && t.resume && t.settings && t.achievements);
  CHECK_EQ(t.romCrc, 0x925637c7u);
  CHECK(t.created > 1700000000LL);
  CHECK(t.notes.empty());
}

TEST(backup_is_an_ordinary_zip_with_a_readable_description) {
  Device d(check::tempDir());
  d.fill(1);
  const std::vector<uint8_t> zip = d.backup();
  std::vector<ZipEntry> entries;
  std::string err;
  REQUIRE(zipRead(zip, &entries, &err));
  REQUIRE(!entries.empty());
  CHECK_EQ(entries[0].name, std::string(kBackupManifestName));  // (first: what the archive is)
  const std::string m(entries[0].data.begin(), entries[0].data.end());
  CHECK(m.find("SMAS Mobile backup") == 0);
  CHECK(m.find("format: 1") != std::string::npos);
  CHECK(m.find("app_version: 1.0.0") != std::string::npos);
  CHECK(m.find("game_file_crc32: 925637C7") != std::string::npos);
  CHECK(m.find("contains no game") != std::string::npos);
  CHECK(m.find("Restore Backup") != std::string::npos);
  // The games' progress is a standard battery file, 8 KB, as other emulators read it.
  bool srm = false;
  for (const ZipEntry& e : entries)
    if (e.name == "saves/smas.srm") srm = e.data == fx::sram(1);
  CHECK(srm);
}

TEST(backup_never_contains_the_game_file) {
  Device d(check::tempDir());
  d.fill(1);
  makeDir(pathJoin(d.paths.dataDir, L"roms"));
  fx::spit(pathJoin(pathJoin(d.paths.dataDir, L"roms"), L"game.sfc"), fx::pattern(0x8000, 1));
  fx::spit(pathJoin(d.paths.saveDir, L"stray.bin"), fx::pattern(100, 1));
  std::vector<ZipEntry> entries;
  std::string err;
  REQUIRE(zipRead(d.backup(), &entries, &err));
  size_t total = 0;
  for (const ZipEntry& e : entries) {
    total += e.data.size();
    const bool known = e.name == kBackupManifestName || e.name == "saves/smas.srm" || e.name == "saves/state1.sst" ||
                       e.name == "saves/state7.sst" || e.name == "saves/resume.sst" ||
                       e.name == "settings/smas_port.ini" || e.name == "settings/achievements.ini";
    CHECK_MSG(known, e.name);
  }
  CHECK_EQ(entries.size(), (size_t)7);
}

TEST(backup_restores_onto_a_new_device_exactly) {
  Device a(check::tempDir());
  a.fill(1);
  const std::vector<uint8_t> zip = a.backup();
  Device b(check::tempDir());
  std::string err;
  BackupSummary done;
  std::wstring safety = L"x";
  CHECK_MSG(b.restore(zip, &err, &done, &safety), err);
  b.expect(1);
  CHECK(done.progress && done.states == 2 && done.resume && done.settings && done.achievements);
  CHECK(safety.empty());  // (nothing was there to keep)
  CHECK(fx::names(pathJoin(b.paths.saveDir, L"safety")).empty());
}

TEST(backup_restore_keeps_what_it_replaces) {
  Device d(check::tempDir());
  d.fill(1);
  const std::vector<uint8_t> old = d.backup();
  d.fill(2);
  std::string err;
  std::wstring safety;
  CHECK_MSG(d.restore(old, &err, nullptr, &safety), err);
  d.expect(1);
  // What was there (seed 2) is a backup of its own, in the app's folder...
  REQUIRE(!safety.empty());
  CHECK(pathName(safety).find(L"before-restore-") == 0);
  const std::vector<uint8_t> kept = fx::slurp(safety);
  BackupSummary s;
  REQUIRE(inspectBackup(kept, &s, &err));
  CHECK(s.progress && s.states == 2 && s.resume && s.settings && s.achievements);
  // ... the games' progress is a restore point as well ...
  bool point = false;
  std::vector<uint8_t> mem(BatteryStore::kSize);
  for (const auto& p : d.battery.restorePoints())
    if (d.battery.readRestorePoint(p, mem.data()) && mem == fx::sram(2)) point = true;
  CHECK(point);
  // ... the state slots' previous contents are beside them ...
  CHECK(fx::slurp(states::slotPath(d.paths.saveDir, 1) + L".prev") == fx::stateShaped(30000, 3));
  // ... and restoring the safety copy undoes the restore.
  CHECK_MSG(d.restore(kept, &err), err);
  d.expect(2);
}

TEST(backup_restoring_the_same_backup_twice_is_harmless) {
  Device d(check::tempDir());
  d.fill(1);
  const std::vector<uint8_t> zip = d.backup();
  std::string err;
  CHECK_MSG(d.restore(zip, &err), err);
  CHECK_MSG(d.restore(zip, &err), err);
  d.expect(1);
}

TEST(backup_with_only_some_parts_leaves_the_rest_alone) {
  Device d(check::tempDir());
  d.fill(5);
  const std::vector<uint8_t> zip = zipWrite({entry(kBackupManifestName, bytesOf(manifestText())),
                                            entry("settings/smas_port.ini", bytesOf("[video]\nfilter=0\n"))});
  std::string err;
  BackupSummary done;
  CHECK_MSG(d.restore(zip, &err, &done), err);
  CHECK(done.settings && !done.progress && done.states == 0 && !done.resume && !done.achievements);
  CHECK(fx::slurp(pathJoin(d.paths.dataDir, L"smas_port.ini")) == bytesOf("[video]\nfilter=0\n"));
  // (the saves are untouched)
  BatteryStore b;
  b.setDir(d.paths.saveDir, 10000);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  b.load(mem.data(), 0);
  CHECK(mem == fx::sram(5));
  std::vector<uint8_t> st;
  CHECK(states::readSlot(d.paths.saveDir, 1, &st) && st == fx::stateShaped(30000, 6));
}

TEST(backup_zipped_again_inside_a_folder_still_restores) {
  Device a(check::tempDir());
  a.fill(1);
  std::vector<ZipEntry> entries;
  std::string err;
  REQUIRE(zipRead(a.backup(), &entries, &err));
  for (ZipEntry& e : entries) e.name = "My backup (1)/" + e.name;
  entries.push_back(entry("__MACOSX/._junk", bytesOf("junk")));
  Device b(check::tempDir());
  BackupSummary done;
  CHECK_MSG(b.restore(zipWrite(entries), &err, &done), err);
  b.expect(1);
  REQUIRE(done.notes.size() == 1);
  CHECK(done.notes[0].find("__MACOSX") != std::string::npos);
}

TEST(backup_unsound_archives_change_nothing) {
  const std::vector<uint8_t> goodState = fx::stateShaped(30000, 1);
  std::vector<uint8_t> badState = goodState;
  badState[5000] ^= 1;
  seal::Info info;
  const std::vector<uint8_t> resume = seal::make([&] { info.kind = seal::kResume; return info; }(), goodState.data(), goodState.size());
  std::vector<uint8_t> badResume = resume;
  badResume[100] ^= 1;
  const std::vector<uint8_t> man = bytesOf(manifestText());
  Device src(check::tempDir());
  src.fill(1);
  std::vector<uint8_t> flipped = src.backup();
  flipped[flipped.size() / 2] ^= 0x04;
  std::vector<uint8_t> cut = src.backup();
  cut.resize(cut.size() - 30);

  struct Case {
    const char* what;
    std::vector<uint8_t> zip;
    const char* says;
  } cases[] = {
      {"not an archive at all", fx::pattern(5000, 1), "not a zip"},
      {"empty", {}, "too short"},
      {"an archive of something else", zipWrite({entry("holiday.jpg", fx::pattern(500, 1))}), "not a SMAS Mobile backup"},
      {"a description that is not ours", zipWrite({entry(kBackupManifestName, bytesOf("something else\nformat: 1\n"))}), "not a SMAS Mobile backup"},
      {"no format", zipWrite({entry(kBackupManifestName, bytesOf("SMAS Mobile backup\n")), entry("saves/smas.srm", fx::sram(1))}), "format"},
      {"a newer format", zipWrite({entry(kBackupManifestName, bytesOf(manifestText(kBackupFormat + 1))), entry("saves/smas.srm", fx::sram(1))}), "newer version"},
      {"nothing in it", zipWrite({entry(kBackupManifestName, man)}), "no saves or settings"},
      {"progress of the wrong size", zipWrite({entry(kBackupManifestName, man), entry("saves/smas.srm", fx::pattern(8191, 1))}), "8192"},
      {"a damaged save state", zipWrite({entry(kBackupManifestName, man), entry("saves/smas.srm", fx::sram(9)), entry("saves/state3.sst", badState)}), "save state 3"},
      {"a save state that is not one", zipWrite({entry(kBackupManifestName, man), entry("saves/state3.sst", fx::pattern(30000, 2))}), "save state 3"},
      {"a damaged resume state", zipWrite({entry(kBackupManifestName, man), entry("saves/resume.sst", badResume)}), "resume"},
      {"a bare state as the resume state", zipWrite({entry(kBackupManifestName, man), entry("saves/resume.sst", goodState)}), "resume"},
      {"settings that are not text", zipWrite({entry(kBackupManifestName, man), entry("settings/smas_port.ini", fx::pattern(300, 1))}), "settings"},
      {"achievements that are not text", zipWrite({entry(kBackupManifestName, man), entry("settings/achievements.ini", std::vector<uint8_t>(40, 0))}), "achievements"},
      {"the same entry twice", zipWrite({entry(kBackupManifestName, man), entry("saves/smas.srm", fx::sram(1)), entry("saves/smas.srm", fx::sram(2))}), "two entries"},
      {"two descriptions", zipWrite({entry(kBackupManifestName, man), entry("x/" + std::string(kBackupManifestName), man), entry("saves/smas.srm", fx::sram(1))}), "more than one"},
      {"an entry that climbs out of the archive", zipWrite({entry(kBackupManifestName, man), entry("saves/smas.srm", fx::sram(1)), entry("../../evil.sh", bytesOf("x"))}), "unsafe name"},
      {"one bit changed in transit", flipped, ""},
      {"cut short in transit", cut, ""},
  };
  for (const Case& c : cases) {
    Device d(check::tempDir());
    d.fill(4);
    const auto before = d.everything();
    std::string err;
    std::wstring safety;
    CHECK_MSG(!d.restore(c.zip, &err, nullptr, &safety), c.what);
    CHECK_MSG(!err.empty(), c.what);
    CHECK_MSG(err.find(c.says) != std::string::npos, std::string(c.what) + ": " + err);
    CHECK_MSG(d.everything() == before, c.what);  // (not one file added, removed or altered)
    BackupSummary s;
    CHECK_MSG(!inspectBackup(c.zip, &s, &err), c.what);
  }
}

TEST(backup_entries_it_does_not_know_are_never_written) {
  Device d(check::tempDir());
  const std::vector<uint8_t> zip = zipWrite({
      entry(kBackupManifestName, bytesOf(manifestText())),
      entry("saves/smas.srm", fx::sram(3)),
      entry("saves/state0.sst", fx::stateShaped(100, 1)),    // (no such slot)
      entry("saves/state10.sst", fx::stateShaped(100, 1)),
      entry("saves/smas.sav", fx::pattern(100, 1)),          // (the app's own file names are not accepted from outside)
      entry("saves/history/smas-20260101-000000.sav", fx::pattern(100, 1)),
      entry("roms/game.sfc", fx::pattern(0x8000, 1)),
      entry("settings/other.ini", bytesOf("x=1\n")),
      entry("lib/arm64-v8a/libmain.so", fx::pattern(100, 1)),
  });
  std::string err;
  BackupSummary done;
  CHECK_MSG(d.restore(zip, &err, &done), err);
  CHECK_EQ(done.notes.size(), (size_t)7);
  // Only the one known entry was written, to its own place.
  for (const auto& f : d.everything()) {
    const bool expected = f.first == L"saves/smas.sav";
    CHECK_MSG(expected, check::show(f.first));
  }
  CHECK(!fileExists(pathJoin(d.paths.dataDir, L"roms")));
  CHECK(!fileExists(pathJoin(d.paths.dataDir, L"lib")));
}

TEST(backup_restore_stops_if_the_safety_copy_cannot_be_written) {
  Device src(check::tempDir());
  src.fill(1);
  const std::vector<uint8_t> zip = src.backup();
  Device d(check::tempDir());
  d.fill(2);
  const auto before = d.everything();
  for (int kind = 0; kind < 3; kind++) {
    Faults faults;
    if (kind == 0) faults.f.failWriteAfter = 1000;
    if (kind == 1) faults.f.failFsync = true;
    if (kind == 2) faults.f.failRename = true;
    std::string err;
    CHECK(!d.restore(zip, &err));
    CHECK_MSG(err.find("nothing was changed") != std::string::npos, err);
  }
  CHECK(d.everything() == before);
  d.expect(2);
}

TEST(backup_restore_that_runs_out_of_storage_part_way_loses_nothing) {
  Device src(check::tempDir());
  src.fill(1);
  const std::vector<uint8_t> zip = src.backup();
  Device d(check::tempDir());
  d.fill(2);
  // Room for the safety copy and a little more: the restore itself then fails somewhere.
  const size_t safetySize = d.backup().size();
  Faults faults;
  faults.f.failWriteAfter = (long long)safetySize + 20000;
  std::string err;
  std::wstring safety;
  BackupSummary done;
  CHECK(!d.restore(zip, &err, &done, &safety));
  CHECK_MSG(err.find("safety copy") != std::string::npos, err);
  REQUIRE(!safety.empty());
  faults.f.failWriteAfter = -1;
  // Whatever was and wasn't written, every file on the device is whole (old or new, never half)...
  std::vector<uint8_t> mem(BatteryStore::kSize), st;
  BatteryStore b;
  b.setDir(d.paths.saveDir, 10000);
  const auto r = b.load(mem.data(), 0);
  CHECK(r.origin == BatteryStore::Origin::Current);
  CHECK(mem == fx::sram(1) || mem == fx::sram(2));
  CHECK(states::readSlot(d.paths.saveDir, 1, &st));
  CHECK(st == fx::stateShaped(30000, 2) || st == fx::stateShaped(30000, 3));
  // ... and the safety copy brings back all of what was there.
  CHECK_MSG(d.restore(fx::slurp(safety), &err), err);
  d.expect(2);
}

TEST(backup_safety_copies_are_capped) {
  Device src(check::tempDir());
  src.fill(1);
  const std::vector<uint8_t> zip = src.backup();
  Device d(check::tempDir());
  d.fill(2);
  std::wstring last;
  for (int i = 0; i < 9; i++) {
    std::string err;
    REQUIRE(d.restore(zip, &err, nullptr, &last));
  }
  const auto copies = fx::names(pathJoin(d.paths.saveDir, L"safety"));
  CHECK_EQ(copies.size(), (size_t)5);
  CHECK(fileExists(last));  // (the newest is among those kept)
}

TEST(backup_leaves_out_a_damaged_state_and_says_so) {
  Device d(check::tempDir());
  d.fill(1);
  fx::flipBit(states::slotPath(d.paths.saveDir, 7), 9000);
  fx::flipBit(pathJoin(d.paths.saveDir, L"resume.sst"), 9000);
  BackupSummary s;
  const std::vector<uint8_t> zip = d.backup(&s);
  CHECK_EQ(s.states, 1);
  CHECK(!s.resume);
  REQUIRE(s.notes.size() == 2);
  CHECK(s.notes[0].find("save state 7 was left out") != std::string::npos);
  CHECK(s.notes[1].find("resume state was left out") != std::string::npos);
  BackupSummary t;
  std::string err;
  CHECK(inspectBackup(zip, &t, &err));  // (what is in it is sound)
}

TEST(backup_file_name_and_descriptions) {
  const std::string n = backupFileName();
  CHECK_EQ(n.size(), std::string("SMAS-Mobile-backup-20261112-1830.zip").size());
  CHECK(n.find("SMAS-Mobile-backup-20") == 0);
  CHECK(n.rfind(".zip") == n.size() - 4);
  for (char c : n) CHECK(isalnum((unsigned char)c) || c == '-' || c == '.');  // (a name every file system takes)
  BackupSummary s;
  CHECK_EQ(s.describe(), std::string("nothing"));
  CHECK(s.empty());
  s.states = 1;
  CHECK_EQ(s.describe(), std::string("1 save state"));
  s.progress = true;
  CHECK_EQ(s.describe(), std::string("game progress and 1 save state"));
  s.states = 3;
  s.settings = true;
  CHECK_EQ(s.describe(), std::string("game progress, 3 save states and settings and controls"));
}
