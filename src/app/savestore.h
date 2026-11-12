// The player's saved progress on disk, kept so that it can be trusted:
//
//  * every file is written whole or not at all (core/fileio.h);
//  * every file carries a checksum, and is checked before it is used;
//  * a new save never replaces the last good one until it has been written
//    out and read back; the two saves before it are kept beside it;
//  * a file that turns out to be damaged is set aside under another name
//    (never deleted, never overwritten) and the newest good copy is used;
//  * the save as it was at the start of each session is kept as a restore
//    point.
//
// Three kinds of file, all in the app's private "saves" folder:
//
//   smas.sav (+ .1 .2, history/)  the games' own save files (the cartridge's
//                                 battery memory, 8 KB), sealed
//   state1.sst .. state9.sst      save states (the core's format, which has
//                                 its own checksum) and state<N>.sst.prev,
//                                 what the slot held before
//   resume.sst                    the automatic "continue where I left off"
//                                 state; sealed
//
// (The moment before the last Load State, for "Undo Load State", is kept in
// memory only: it is worth nothing once the app has been closed.)
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// A sealed file: a 48-byte header (what it is, how long, checksums, when)
// and the payload. Nothing is used from a file whose seal doesn't check out.
namespace seal {
constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return (uint32_t)(uint8_t)a | (uint32_t)(uint8_t)b << 8 | (uint32_t)(uint8_t)c << 16 | (uint32_t)(uint8_t)d << 24;
}
constexpr uint32_t kBattery = fourcc('B', 'A', 'T', 'T');  // the games' save files
constexpr uint32_t kResume = fourcc('R', 'S', 'U', 'M');   // the automatic resume state
constexpr size_t kHeaderSize = 48;

struct Info {
  uint32_t kind = 0;
  uint32_t romCrc = 0;      // the game file it was made with (0: not recorded)
  uint32_t aux = 0;         // (resume: which game, 0-3)
  uint32_t generation = 0;  // counts up with each save: the highest is the newest
  long long time = 0;       // when it was written, seconds since 1970
  uint32_t appVersion = 0;
};

enum class Error {
  None,
  Missing,
  Unreadable,  // the storage would not give it up (an I/O error): it may be fine, so it is left where it is
  TooShort,
  BadMagic,
  BadVersion,
  BadHeader,
  BadLength,
  BadPayload,
  WrongKind,
};
const char* errorText(Error e);

std::vector<uint8_t> make(const Info& info, const void* payload, size_t n);
// The payload of a sealed file of the kind asked for, checked. `payload` may be null (the header only).
Error open(const std::vector<uint8_t>& file, uint32_t wantKind, Info* info, std::vector<uint8_t>* payload);
// ... read from disk.
Error read(const std::wstring& path, uint32_t wantKind, Info* info, std::vector<uint8_t>* payload,
           size_t maxBytes = 8u << 20);
}  // namespace seal

// ---------------------------------------------------------------------------
// The games' own save files (the cartridge's battery memory).
class BatteryStore {
public:
  static constexpr size_t kSize = 0x2000;
  static constexpr int kMaxRestorePoints = 20;

  enum class Origin {
    Fresh,      // no save yet: a new one is started
    Current,    // the last save, intact
    Recovered,  // the last save was damaged or missing: an earlier copy is used
    Imported,   // a plain .srm file (from another emulator or an older version) is used
    Damaged,    // saves exist but none can be read: all set aside, a new one is started
  };
  struct LoadResult {
    Origin origin = Origin::Fresh;
    long long time = 0;         // when the copy in use was written (0: unknown)
    int setAside = 0;           // damaged files renamed out of the way
    int unreadable = 0;         // files the storage would not read (left in place)
    std::string source;         // the file used ("smas.sav.1"), for the log
    std::string problem;        // what was wrong with the newest one, for the log
  };
  struct RestorePoint {
    std::wstring path;
    std::string name;
    long long time = 0;       // when the save it holds was written
    uint32_t generation = 0;  // ... and its place in the order of saves
    uint32_t crc = 0;
  };

  void setDir(const std::wstring& saveDir, uint32_t appVersion);
  // The newest good save into `sram` (untouched unless one is found).
  LoadResult load(uint8_t* sram, uint32_t romCrc);
  // Writes `sram` as the newest save. Nothing is written when it is what the
  // newest save already holds. False (and why) on failure: the saves on disk
  // are then exactly as they were.
  bool save(const uint8_t* sram, uint32_t romCrc, std::string* err = nullptr);
  // The save as it is on disk kept as a restore point (once per session; the
  // oldest beyond kMaxRestorePoints make way). False if there is nothing to keep.
  bool keepRestorePoint();
  std::vector<RestorePoint> restorePoints() const;  // newest first
  // A restore point's memory into `sram` (checked first; false if it is damaged).
  bool readRestorePoint(const RestorePoint& p, uint8_t* sram) const;
  // The plain 8 KB of the newest save, for a backup (false: none).
  bool exportPlain(std::vector<uint8_t>* out) const;
  bool exists() const;
  long long lastSavedTime() const { return lastTime_; }
  std::wstring dir() const { return dir_; }

private:
  std::wstring dir_;
  uint32_t appVersion_ = 0;
  uint32_t generation_ = 0;
  uint32_t lastCrc_ = 0;
  bool haveLast_ = false;
  long long lastTime_ = 0;
  std::wstring path(const wchar_t* suffix) const;
  struct Candidate {
    std::wstring path;
    seal::Info info;
    std::vector<uint8_t> payload;
    seal::Error err = seal::Error::Missing;
    bool present = false;
  };
  Candidate probe(const std::wstring& p) const;
  int setAside(const std::wstring& p) const;
};

// ---------------------------------------------------------------------------
// Save states.
namespace states {
constexpr int kSlots = 9;
// Whether these bytes are a whole save state the core will accept (its magic
// number, version and checksum): checked before a slot is replaced, restored
// from a backup, or loaded.
bool valid(const std::vector<uint8_t>& bytes, std::string* why = nullptr);
std::wstring slotPath(const std::wstring& saveDir, int slot);
// Writes a slot. What it held before is kept beside it (".prev"). The bytes
// are checked before and read back after; on any failure the slot is as it was.
bool writeSlot(const std::wstring& saveDir, int slot, const std::vector<uint8_t>& bytes, std::string* err = nullptr);
bool readSlot(const std::wstring& saveDir, int slot, std::vector<uint8_t>* bytes, std::string* err = nullptr);
bool slotUsed(const std::wstring& saveDir, int slot);
// A sealed state (the resume point): written whole, read back, checked.
bool writeSealed(const std::wstring& path, uint32_t kind, const seal::Info& info, const std::vector<uint8_t>& state,
                 std::string* err = nullptr);
bool readSealed(const std::wstring& path, uint32_t kind, seal::Info* info, std::vector<uint8_t>* state,
                std::string* err = nullptr);
}  // namespace states

// Enough room to write `bytes` (twice over, for the temporary copy, and a
// margin)? `err` says how much is needed when not. True when it can't be told.
bool roomToWrite(const std::wstring& dir, size_t bytes, std::string* err = nullptr);

// ---------------------------------------------------------------------------
// Writes a file on another thread, so that a save made during play (the
// automatic resume state, half a megabyte once a minute) never holds up a
// frame. One file at a time; a newer one waiting replaces an older one.
class AsyncWriter {
public:
  ~AsyncWriter();
  void submit(const std::wstring& path, std::vector<uint8_t> bytes);
  // Waits until everything submitted is on disk (before a write of the same
  // file from the main thread, and before the app goes to the background).
  void flush();
  int written() const;
  int failed() const;

private:
  mutable std::mutex m_;
  std::condition_variable cv_, idle_;
  std::thread worker_;
  bool started_ = false, quit_ = false, busy_ = false, pending_ = false;
  std::wstring path_;
  std::vector<uint8_t> bytes_;
  int written_ = 0, failed_ = 0;
  void run();
};
