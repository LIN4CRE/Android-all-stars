// Game file validation: see romcheck.h.
#include "app/romcheck.h"
#include <cstdio>
#include <cstring>
#include "app/sys.h"
#include "core/crc32.h"

namespace {
const char kTitle[] = "SUPER MARIO ALL_STARS";  // the cartridge header's 21 characters

std::string sizeText(size_t n) {
  char buf[48];
  if (n >= 1048576) snprintf(buf, sizeof buf, "%.2f MB", n / 1048576.0);
  else if (n >= 1024) snprintf(buf, sizeof buf, "%.1f KB", n / 1024.0);
  else snprintf(buf, sizeof buf, "%zu bytes", n);
  return buf;
}

bool startsWith(const std::vector<uint8_t>& d, const char* magic, size_t n, size_t at = 0) {
  return d.size() >= at + n && std::memcmp(d.data() + at, magic, n) == 0;
}

// The title in a Super Nintendo cartridge header at `at`, if one is there:
// 21 printable characters, and a checksum beside its own complement.
bool snesHeaderAt(const std::vector<uint8_t>& d, size_t at, std::string* title) {
  if (d.size() < at + 0x20) return false;
  const uint8_t* h = d.data() + at;
  int letters = 0;
  for (int i = 0; i < 21; i++) {
    if (h[i] < 0x20 || h[i] > 0x7e) return false;
    if (h[i] != ' ') letters++;
  }
  if (letters < 2) return false;
  const unsigned sum = h[0x1e] | h[0x1f] << 8, inv = h[0x1c] | h[0x1d] << 8;
  if ((sum ^ inv) != 0xffff) return false;
  std::string t((const char*)h, 21);
  while (!t.empty() && t.back() == ' ') t.pop_back();
  *title = t;
  return true;
}

constexpr size_t kComboCartSize = 0x280000;

void comboCart(RomCheck* r) {
  r->verdict = RomCheck::ComboCart;
  r->headline = "That is Super Mario All-Stars + Super Mario World";
  r->explain = "It is a different cartridge (the one with Super Mario World added) and is not supported. SMAS "
               "Mobile runs Super Mario All-Stars (USA), the four-game cartridge, 2 MB.";
}

const char kNeeded[] = "SMAS Mobile needs your own copy of Super Mario All-Stars (USA) for the Super Nintendo: a file "
                       "of 2 MB, usually named \"Super Mario All-Stars (USA).sfc\".";
}  // namespace

RomCheck checkGameFile(const std::vector<uint8_t>& d) {
  RomCheck r;
  r.size = d.size();
  if (d.empty()) {
    r.verdict = RomCheck::Empty;
    r.headline = "That file is empty";
    r.explain = "It has no data in it (0 bytes): the download or copy may not have finished. Get the file again, "
                "then choose it once more.";
    return r;
  }
  // The game itself, wherever its first bytes might otherwise point: the
  // cartridge header's title, where Super Mario All-Stars keeps it.
  {
    const size_t off = (d.size() & 0x3ff) == 0x200 ? 0x200 : 0;
    if (d.size() <= kMaxGameFileBytes && d.size() >= off + 0x8000 &&
        std::memcmp(d.data() + off + 0x7fc0, kTitle, sizeof(kTitle) - 1) == 0) {
      r.copierHeader = off != 0;
      r.size = d.size() - off;
      r.crc = crc32(d.data() + off, d.size() - off);
      r.title = kTitle;
      char crc[16];
      snprintf(crc, sizeof crc, "%08X", r.crc);
      if (r.size == kComboCartSize) {
        // (the cartridge with Super Mario World added is 2.5 MB whatever its header says)
        comboCart(&r);
      } else if (r.size == kSmasUsaSize && r.crc == kSmasUsaCrc) {
        r.verdict = RomCheck::Verified;
        r.headline = "Super Mario All-Stars (USA) - verified";
        r.explain = "This is exactly the version SMAS Mobile is made for.";
      } else {
        r.verdict = RomCheck::Unverified;
        r.headline = "Super Mario All-Stars - a different version";
        r.explain = std::string("It is Super Mario All-Stars, but not the exact USA version SMAS Mobile is made for (") +
                    sizeText(r.size) + ", checksum " + crc + "; the USA version is 2.00 MB, checksum 925637C7). It "
                    "will be used, but widescreen, cheats and the level select are tuned for the USA version and may "
                    "misbehave.";
      }
      return r;
    }
  }
  // Compressed archives: the game file is inside.
  const char* archive = nullptr;
  if (startsWith(d, "PK\x03\x04", 4) || startsWith(d, "PK\x05\x06", 4)) archive = "ZIP";
  else if (startsWith(d, "7z\xbc\xaf\x27\x1c", 6)) archive = "7-Zip";
  else if (startsWith(d, "Rar!\x1a\x07", 6)) archive = "RAR";
  else if (startsWith(d, "\x1f\x8b", 2)) archive = "GZip";
  if (archive) {
    // (one of this app's own backups says so in its first entry's name)
    const char kBackupName[] = "smas-mobile-backup.txt";
    bool backup = false;
    for (size_t i = 0; i + sizeof(kBackupName) - 1 <= d.size() && i < 256; i++)
      backup = backup || std::memcmp(d.data() + i, kBackupName, sizeof(kBackupName) - 1) == 0;
    if (backup) {
      r.verdict = RomCheck::SaveFile;
      r.headline = "That is a SMAS Mobile backup, not a game file";
      r.explain = "Backups hold saved progress. To use it, go to Save Management and choose Restore Backup.";
      return r;
    }
    r.verdict = RomCheck::Archive;
    r.headline = std::string("That is a compressed ") + archive + " archive";
    r.explain = std::string("The game file is inside it. Open the archive in your phone's Files app, extract it, then "
                            "choose the .sfc file that comes out. ") + kNeeded;
    return r;
  }
  if (startsWith(d, "SMASMOB", 7) || startsWith(d, "SMSS", 4)) {
    r.verdict = RomCheck::SaveFile;
    r.headline = "That is a save file, not a game file";
    r.explain = "It holds saved progress from this app. Saves are restored from a backup in Save Management; here, "
                "choose the game itself.";
    return r;
  }
  const char* other = nullptr;
  if (startsWith(d, "NES\x1a", 4)) other = "a Nintendo Entertainment System (NES) game";
  else if (startsWith(d, "FDS\x1a", 4)) other = "a Famicom Disk System game";
  else if (startsWith(d, "\xce\xed\x66\x66\xcc\x0d\x00\x0b", 8, 0x104)) other = "a Game Boy game";
  else if (startsWith(d, "\x80\x37\x12\x40", 4) || startsWith(d, "\x37\x80\x40\x12", 4)) other = "a Nintendo 64 game";
  else if (startsWith(d, "\x89PNG", 4) || startsWith(d, "\xff\xd8\xff", 3)) other = "a picture";
  else if (startsWith(d, "%PDF", 4)) other = "a PDF document";
  else if (startsWith(d, "PATCH", 5) || startsWith(d, "BPS1", 4)) other = "a patch, to be applied to a game file";
  if (other) {
    r.verdict = RomCheck::OtherSystem;
    r.headline = std::string("That is ") + other;
    r.explain = kNeeded;
    return r;
  }
  if (d.size() > kMaxGameFileBytes) {
    r.verdict = RomCheck::TooLarge;
    r.headline = "That file is too large to be the game (" + sizeText(d.size()) + ")";
    r.explain = kNeeded;
    return r;
  }

  // A copier header (512 bytes some old dumps begin with) is skipped.
  const size_t off = (d.size() & 0x3ff) == 0x200 ? 0x200 : 0;
  r.copierHeader = off != 0;
  r.size = d.size() - off;
  if (d.size() < off + 0x8000) {
    r.verdict = d.size() == 0x2000 || d.size() == 0x800 ? RomCheck::SaveFile : RomCheck::TooSmall;
    if (r.verdict == RomCheck::SaveFile) {
      r.headline = "That looks like a save file (" + sizeText(d.size()) + "), not a game file";
      r.explain = std::string("A battery save holds progress, not the game. ") + kNeeded;
    } else {
      r.headline = "That file is too small to be the game (" + sizeText(d.size()) + ")";
      r.explain = std::string("It may be incomplete. ") + kNeeded;
    }
    return r;
  }
  r.crc = crc32(d.data() + off, d.size() - off);

  // Where other games keep their cartridge header (to say which game it is instead).
  std::string title;
  bool found = false;
  for (size_t at : {(size_t)0x7fc0, (size_t)0xffc0, (size_t)0x40ffc0})
    if (!found && snesHeaderAt(d, off + at, &title)) found = true;
  if (found) {
    r.title = title;
    if (title.find("ALL_STARS + WORLD") != std::string::npos || title.find("ALL_STARS+WORLD") != std::string::npos) {
      comboCart(&r);
    } else {
      r.verdict = RomCheck::OtherGame;
      r.headline = "That is a different Super Nintendo game: \"" + title + "\"";
      r.explain = kNeeded;
    }
    return r;
  }
  r.verdict = RomCheck::NotAGame;
  r.headline = "That file is not a game SMAS Mobile can run";
  r.explain = std::string("Nothing in it looks like a Super Nintendo game (") + sizeText(d.size()) + "). " + kNeeded;
  return r;
}

RomCheck checkGameFile(const std::wstring& path) {
  const long long n = fileSize(path);
  RomCheck r;
  if (n < 0) {
    r.verdict = RomCheck::NotAGame;
    r.headline = "That file could not be opened";
    r.explain = "It may have been moved or deleted, or the app was not allowed to read it. Choose it again.";
    return r;
  }
  if ((unsigned long long)n > kMaxGameFileBytes) {
    r.size = (size_t)n;
    r.verdict = RomCheck::TooLarge;
    r.headline = "That file is too large to be the game (" + sizeText((size_t)n) + ")";
    r.explain = kNeeded;
    return r;
  }
  std::vector<uint8_t> d;
  if (!readFile(path, d, kMaxGameFileBytes)) {
    r.verdict = RomCheck::NotAGame;
    r.headline = "That file could not be read";
    r.explain = "The storage reported an error while reading it. Choose it again; if it keeps failing, copy the "
                "file to the phone's own storage first.";
    return r;
  }
  return checkGameFile(d);
}
