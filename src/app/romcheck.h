// Is this the game file SMAS Mobile runs? The one place that decides, used by
// the importer (before a picked file is kept), at start (before a stored one
// is run) and by the Game File page; with an explanation a player can act on
// when the answer is no.
//
// No game data is bundled with the app: the player brings their own copy of
// Super Mario All-Stars (USA), and it never leaves the device.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RomCheck {
  enum Verdict {
    Verified,      // Super Mario All-Stars (USA): the dump the app is tuned for
    Unverified,    // Super Mario All-Stars, another version or dump: runs, less tested
    Empty,
    Archive,       // a .zip / .7z / .rar / .gz: the game file is inside it
    TooSmall,
    TooLarge,
    SaveFile,      // a save, a save state or a backup, not a game
    OtherSystem,   // a game for another console
    OtherGame,     // a Super Nintendo game, but not this one
    ComboCart,     // Super Mario All-Stars + Super Mario World
    NotAGame,      // nothing recognisable
  };
  Verdict verdict = NotAGame;
  bool accepted() const { return verdict == Verified || verdict == Unverified; }
  std::string title;        // the cartridge's own title, when it has one
  uint32_t crc = 0;         // CRC-32 of the game data (without a copier header)
  size_t size = 0;          // bytes of game data
  bool copierHeader = false;
  std::string headline;     // one line: what it is
  std::string explain;      // what to do about it (a sentence or two)
};

constexpr uint32_t kSmasUsaCrc = 0x925637c7;
constexpr size_t kSmasUsaSize = 0x200000;
constexpr size_t kMaxGameFileBytes = 16u << 20;  // nothing bigger is even read

RomCheck checkGameFile(const std::vector<uint8_t>& data);
// ... of a file on disk (too large or unreadable: said so).
RomCheck checkGameFile(const std::wstring& path);
