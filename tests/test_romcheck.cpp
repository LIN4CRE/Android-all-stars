// The game-file check (app/romcheck.h): what is accepted, what is refused, and
// that every refusal says something the player can act on.
//
// No game data is used. "A game file" here is a buffer of filler bytes with
// the cartridge header's 21-character title at the place the real one has it.
#include <cstring>
#include "app/backup.h"
#include "app/romcheck.h"
#include "app/savestore.h"
#include "check.h"
#include "core/zipfile.h"
#include "fixtures.h"

namespace {
const char kTitle[] = "SUPER MARIO ALL_STARS";

std::vector<uint8_t> titled(size_t size, const char* title = kTitle, size_t headerAt = 0x7fc0) {
  std::vector<uint8_t> d = fx::pattern(size, 31);
  // (filler that could never look like a header or an archive by accident)
  for (uint8_t& b : d) b |= 0x80;
  std::memset(&d[headerAt], ' ', 21);
  std::memcpy(&d[headerAt], title, std::min<size_t>(strlen(title), 21));
  // a cartridge header's checksum and its complement
  d[headerAt + 0x1c] = 0x34;
  d[headerAt + 0x1d] = 0x12;
  d[headerAt + 0x1e] = (uint8_t)~0x34;
  d[headerAt + 0x1f] = (uint8_t)~0x12;
  return d;
}

std::vector<uint8_t> magic(const char* m, size_t n, size_t total = 70000) {
  std::vector<uint8_t> d(total, 0x80);
  std::memcpy(d.data(), m, n);
  return d;
}
}  // namespace

TEST(romcheck_empty_file) {
  const RomCheck r = checkGameFile(std::vector<uint8_t>());
  CHECK(r.verdict == RomCheck::Empty);
  CHECK(!r.accepted());
  CHECK(r.headline.find("empty") != std::string::npos);
}

TEST(romcheck_recognises_the_game_by_its_title) {
  const RomCheck r = checkGameFile(titled(kSmasUsaSize));
  CHECK(r.verdict == RomCheck::Unverified);  // (the title is right, the contents are not the USA version's)
  CHECK(r.accepted());
  CHECK_EQ(r.title, std::string(kTitle));
  CHECK_EQ(r.size, kSmasUsaSize);
  CHECK(!r.copierHeader);
  CHECK(r.explain.find("925637C7") != std::string::npos);  // (says what the supported version's checksum is)
  char mine[16];
  snprintf(mine, sizeof mine, "%08X", r.crc);
  CHECK(r.explain.find(mine) != std::string::npos);  // (and what this file's is)
}

TEST(romcheck_verified_needs_the_size_and_the_checksum) {
  std::vector<uint8_t> d = titled(kSmasUsaSize);
  fx::forceCrc(d, 0x1000, kSmasUsaCrc);
  const RomCheck r = checkGameFile(d);
  CHECK(r.verdict == RomCheck::Verified);
  CHECK(r.accepted());
  CHECK_EQ(r.crc, kSmasUsaCrc);
  CHECK(r.headline.find("verified") != std::string::npos);
  // The same checksum on another size is not it.
  std::vector<uint8_t> half = titled(kSmasUsaSize / 2);
  fx::forceCrc(half, 0x1000, kSmasUsaCrc);
  CHECK(checkGameFile(half).verdict == RomCheck::Unverified);
  // One changed byte and it is no longer the verified version.
  d[0x12345] ^= 1;
  CHECK(checkGameFile(d).verdict == RomCheck::Unverified);
}

TEST(romcheck_copier_header_is_skipped) {
  std::vector<uint8_t> plain = titled(kSmasUsaSize);
  fx::forceCrc(plain, 0x1000, kSmasUsaCrc);
  std::vector<uint8_t> headered(0x200, 0);
  headered.insert(headered.end(), plain.begin(), plain.end());
  const RomCheck r = checkGameFile(headered);
  CHECK(r.verdict == RomCheck::Verified);
  CHECK(r.copierHeader);
  CHECK_EQ(r.size, kSmasUsaSize);  // (the game data, without the 512 bytes)
  CHECK_EQ(r.crc, kSmasUsaCrc);
}

TEST(romcheck_title_wins_over_first_bytes) {
  // A game file whose first bytes happen to be an archive's or a picture's
  // magic number is still the game: what decides is the cartridge header.
  for (const char* m : {"PK\x03\x04", "\x1f\x8b\x08\x00", "\x89PNG", "NES\x1a", "SMSS"}) {
    std::vector<uint8_t> d = titled(kSmasUsaSize);
    std::memcpy(d.data(), m, 4);
    CHECK_MSG(checkGameFile(d).accepted(), m);
  }
}

TEST(romcheck_archives_say_to_extract) {
  struct {
    const char* m;
    size_t n;
    const char* name;
  } kinds[] = {{"PK\x03\x04", 4, "ZIP"}, {"PK\x05\x06", 4, "ZIP"}, {"7z\xbc\xaf\x27\x1c", 6, "7-Zip"},
               {"Rar!\x1a\x07", 6, "RAR"}, {"\x1f\x8b", 2, "GZip"}};
  for (const auto& k : kinds) {
    const RomCheck r = checkGameFile(magic(k.m, k.n));
    CHECK_MSG(r.verdict == RomCheck::Archive, k.name);
    CHECK_MSG(r.headline.find(k.name) != std::string::npos, r.headline);
    CHECK(r.explain.find("extract") != std::string::npos);
    CHECK(!r.accepted());
  }
  // (even a tiny one, and one as large as a game)
  CHECK(checkGameFile(magic("PK\x03\x04", 4, 30)).verdict == RomCheck::Archive);
  CHECK(checkGameFile(magic("PK\x03\x04", 4, kSmasUsaSize)).verdict == RomCheck::Archive);
}

TEST(romcheck_own_backup_points_to_restore) {
  std::vector<ZipEntry> entries(2);
  entries[0].name = kBackupManifestName;
  entries[0].data = {'x'};
  entries[1].name = "saves/smas.srm";
  entries[1].data = fx::sram(1);
  const RomCheck r = checkGameFile(zipWrite(entries));
  CHECK(r.verdict == RomCheck::SaveFile);
  CHECK(r.headline.find("backup") != std::string::npos);
  CHECK(r.explain.find("Restore Backup") != std::string::npos);
}

TEST(romcheck_save_files_are_not_games) {
  // A sealed save of this app.
  seal::Info info;
  info.kind = seal::kBattery;
  const auto mem = fx::sram(1);
  RomCheck r = checkGameFile(seal::make(info, mem.data(), mem.size()));
  CHECK(r.verdict == RomCheck::SaveFile);
  // A save state (even one as large as a small game).
  r = checkGameFile(fx::stateShaped(300000, 1));
  CHECK(r.verdict == RomCheck::SaveFile);
  // A bare battery file from any emulator: 8 KB (or 2 KB).
  r = checkGameFile(fx::sram(2));
  CHECK(r.verdict == RomCheck::SaveFile);
  CHECK(r.headline.find("save file") != std::string::npos);
  r = checkGameFile(std::vector<uint8_t>(0x800, 0x80));
  CHECK(r.verdict == RomCheck::SaveFile);
}

TEST(romcheck_other_systems_are_named) {
  struct {
    std::vector<uint8_t> d;
    const char* word;
  } kinds[] = {
      {magic("NES\x1a", 4), "NES"},
      {magic("FDS\x1a", 4), "Famicom Disk"},
      {magic("\x80\x37\x12\x40", 4), "Nintendo 64"},
      {magic("\x37\x80\x40\x12", 4), "Nintendo 64"},
      {magic("\x89PNG", 4), "picture"},
      {magic("\xff\xd8\xff", 3), "picture"},
      {magic("%PDF", 4), "PDF"},
      {magic("PATCH", 5), "patch"},
      {magic("BPS1", 4), "patch"},
  };
  for (const auto& k : kinds) {
    const RomCheck r = checkGameFile(k.d);
    CHECK_MSG(r.verdict == RomCheck::OtherSystem, k.word);
    CHECK_MSG(r.headline.find(k.word) != std::string::npos, r.headline);
  }
  std::vector<uint8_t> gb(70000, 0x80);
  std::memcpy(&gb[0x104], "\xce\xed\x66\x66\xcc\x0d\x00\x0b", 8);
  CHECK(checkGameFile(gb).headline.find("Game Boy") != std::string::npos);
}

TEST(romcheck_other_snes_games_are_named) {
  RomCheck r = checkGameFile(titled(0x100000, "TEST CARTRIDGE"));
  CHECK(r.verdict == RomCheck::OtherGame);
  CHECK_EQ(r.title, std::string("TEST CARTRIDGE"));
  CHECK(r.headline.find("TEST CARTRIDGE") != std::string::npos);
  CHECK(!r.accepted());
  // (a header where the other cartridge layouts keep it)
  r = checkGameFile(titled(0x100000, "HIGH MAPPED GAME", 0xffc0));
  CHECK(r.verdict == RomCheck::OtherGame);
  CHECK_EQ(r.title, std::string("HIGH MAPPED GAME"));
  r = checkGameFile(titled(0x600000, "EXTENDED GAME", 0x40ffc0));
  CHECK(r.verdict == RomCheck::OtherGame);
  // With a copier header in front.
  std::vector<uint8_t> headered(0x200, 0);
  const auto g = titled(0x100000, "TEST CARTRIDGE");
  headered.insert(headered.end(), g.begin(), g.end());
  r = checkGameFile(headered);
  CHECK(r.verdict == RomCheck::OtherGame);
  CHECK(r.copierHeader);
}

TEST(romcheck_the_cartridge_with_super_mario_world_is_refused) {
  RomCheck r = checkGameFile(titled(0x280000, "ALL_STARS + WORLD"));
  CHECK(r.verdict == RomCheck::ComboCart);
  CHECK(!r.accepted());
  CHECK(r.headline.find("Super Mario World") != std::string::npos);
  // (by its size too, whatever title its header carries)
  r = checkGameFile(titled(0x280000));
  CHECK(r.verdict == RomCheck::ComboCart);
}

TEST(romcheck_too_small_too_large_unrecognisable) {
  RomCheck r = checkGameFile(std::vector<uint8_t>(1000, 0x80));
  CHECK(r.verdict == RomCheck::TooSmall);
  CHECK(r.headline.find("too small") != std::string::npos);
  r = checkGameFile(std::vector<uint8_t>(kMaxGameFileBytes + 1, 0x80));
  CHECK(r.verdict == RomCheck::TooLarge);
  CHECK(r.headline.find("too large") != std::string::npos);
  r = checkGameFile(std::vector<uint8_t>(kSmasUsaSize, 0x80));
  CHECK(r.verdict == RomCheck::NotAGame);
  // (the right title in a file too large to be it is not accepted either)
  r = checkGameFile(titled(kMaxGameFileBytes + 0x8000));
  CHECK(!r.accepted());
}

TEST(romcheck_every_refusal_explains_itself) {
  const std::vector<std::vector<uint8_t>> files = {
      {},
      magic("PK\x03\x04", 4),
      std::vector<uint8_t>(1000, 0x80),
      std::vector<uint8_t>(kMaxGameFileBytes + 1, 0x80),
      fx::sram(1),
      magic("NES\x1a", 4),
      titled(0x100000, "TEST CARTRIDGE"),
      titled(0x280000, "ALL_STARS + WORLD"),
      std::vector<uint8_t>(kSmasUsaSize, 0x80),
  };
  int seen[RomCheck::NotAGame + 1] = {};
  for (const auto& f : files) {
    const RomCheck r = checkGameFile(f);
    seen[r.verdict]++;
    CHECK(!r.accepted());
    CHECK_MSG(r.headline.size() > 10, r.headline);
    CHECK_MSG(r.explain.size() > 40, r.explain);
    CHECK(r.headline.back() != '.');  // (a heading, not a sentence)
  }
  // (every reason for refusing was produced once)
  for (int v = RomCheck::Empty; v <= RomCheck::NotAGame; v++) CHECK_MSG(seen[v] == 1, std::to_string(v));
}

TEST(romcheck_file_on_disk) {
  const std::wstring dir = check::tempDir();
  RomCheck r = checkGameFile(pathJoin(dir, L"missing.sfc"));
  CHECK(!r.accepted());
  CHECK(r.headline.find("could not be opened") != std::string::npos);
  const std::wstring p = pathJoin(dir, L"game.sfc");
  fx::spit(p, titled(kSmasUsaSize));
  r = checkGameFile(p);
  CHECK(r.verdict == RomCheck::Unverified);
  // A file longer than any game is refused by its size alone, without being read into memory.
  const std::wstring big = pathJoin(dir, L"big.bin");
  fx::spit(big, std::vector<uint8_t>(kMaxGameFileBytes + 1, 0x80));
  r = checkGameFile(big);
  CHECK(r.verdict == RomCheck::TooLarge);
  CHECK_EQ(r.size, kMaxGameFileBytes + 1);
}

TEST(romcheck_any_bytes_at_any_length_are_handled) {
  // Lengths around every offset the check looks at, with three kinds of filler.
  const size_t edges[] = {0x200, 0x400, 0x800, 0x2000, 0x7fc0, 0x7fe0, 0x8000, 0x8200, 0xffc0, 0xffe0, 0x10000, 0x10200};
  int accepted = 0;
  for (size_t e : edges) {
    for (int delta = -33; delta <= 33; delta++) {
      const size_t n = e + delta;
      for (int kind = 0; kind < 3; kind++) {
        std::vector<uint8_t> d = kind == 0 ? std::vector<uint8_t>(n, 0x41) : kind == 1 ? fx::pattern(n, (uint32_t)n) : std::vector<uint8_t>(n, 0);
        const RomCheck r = checkGameFile(d);
        accepted += r.accepted();
        CHECK(!r.headline.empty());
      }
    }
  }
  CHECK_EQ(accepted, 0);
  // The title cut off by the end of the file, at every length.
  const std::vector<uint8_t> whole = titled(0x8000);
  for (size_t n = 0x7fb0; n < 0x8000; n++) {
    std::vector<uint8_t> cut(whole.begin(), whole.begin() + n);
    CHECK(!checkGameFile(cut).accepted());
  }
  CHECK(checkGameFile(whole).accepted());
}
