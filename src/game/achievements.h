// Achievements: what each game's are, and the per-frame checks that earn them
// (from the game's RAM after every emulated frame). The app keeps what has
// been earned (achievements.ini), shows the pop-ups and decides whether a run
// counts: one started with achievements on (power-on or console reset), with
// no cheats, level select, fast forward or rewind since.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

enum class GameId;  // (game/smas.h)

struct AchievementDef {
  const char* id;     // stable id stored in achievements.ini ("smb1_warp")
  GameId game;        // GameId::Boot: across the collection
  const char* name;
  const char* desc;
  bool secret = false;  // marked "Secret:" in the list
  int target = 0;       // shown as progress (count, or how many of a set) when > 0
  bool set = false;     // progress is a bit mask of things done (see AchievementEvent::Mark)
};

// Every achievement, game by game (SMB1, Lost Levels, SMB2, SMB3), then the collection's.
const std::vector<AchievementDef>& achievementDefs();
const AchievementDef* findAchievement(const std::string& id);
// The "finished the game" achievement of each game (the collection's All-Star needs all four).
extern const char* const kFinishedGame[4];

struct AchievementEvent {
  enum Kind { Unlock, Progress, Mark, Add } kind;
  std::string id;
  int value = 0;  // Progress: the count reached now (the best is kept); Mark: the bit done;
                  // Add: how many more (a total kept across play: enemies defeated)
};

class AchievementTracker {
public:
  bool counts = false;  // checks run (see the header comment)
  // A new run: power-on / console reset (counts if achievements are on) or the
  // level select (doesn't).
  void newRun(bool counts);
  // After every emulated frame: `wram` is the console's 128 KB of work RAM.
  void frame(const uint8_t* wram, GameId game);
  // SMB1 / The Lost Levels co-op, before each frame (see Smb1Engine::coopLuigiLive).
  void setCoop(bool luigiLive, uint32_t luigiPoles, uint32_t p2Hurts) {
    coopLuigi_ = luigiLive;
    luigiPoles_ = luigiPoles;
    p2Hurts_ = p2Hurts;
  }
  std::vector<AchievementEvent> take();

  // Save states carry whether the run counts and its facts: a state saved
  // while it didn't count doesn't count when loaded, until the next reset.
  std::vector<uint8_t> save() const;
  void load(const uint8_t* p, size_t n);  // n == 0: a state from before achievements

private:
  std::vector<AchievementEvent> events_;
  uint32_t run_[4] = {};  // per game (SMB1, LL, SMB2, SMB3): facts about the playthrough
  int32_t s_[64] = {};    // frame-to-frame memory of the running game's checks
  uint8_t prev_[0x2000] = {};  // last frame's $0000-$1FFF
  bool havePrev_ = false;
  bool coopLuigi_ = false;
  uint32_t luigiPoles_ = 0, p2Hurts_ = 0, lastLuigiPoles_ = 0, lastP2Hurts_ = 0;
  GameId last_ = static_cast<GameId>(0);  // (GameId::Boot)
  std::set<std::string> sent_;  // unlocks already reported this run (once is enough)
  void unlock(const char* id) {
    if (sent_.insert(id).second) events_.push_back({AchievementEvent::Unlock, id, 0});
  }
  void progress(const char* id, int v) { events_.push_back({AchievementEvent::Progress, id, v}); }
  void mark(const char* id, int bit) { events_.push_back({AchievementEvent::Mark, id, bit}); }
  void add(const char* id, int n) {
    if (n > 0) events_.push_back({AchievementEvent::Add, id, n});
  }
  void smb1(const uint8_t* w, bool ll);
  void smb2(const uint8_t* w);
  void smb3(const uint8_t* w);
};
