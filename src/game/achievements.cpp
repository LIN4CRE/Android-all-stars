#include "game/achievements.h"
#include "game/smas.h"
#include <cstring>

// ---------------------------------------------------------------------------
// The list

namespace {
const std::vector<AchievementDef> kDefs = {
    // Super Mario Bros.
    {"smb1_toad", GameId::SMB1, "Wrong Castle", "Defeat the false Bowser at the end of World 1."},
    {"smb1_castles", GameId::SMB1, "Castle Crasher", "Clear the castle of every world.", false, 8, true},
    {"smb1_princess", GameId::SMB1, "Thank You Mario!", "Rescue the Princess in World 8-4."},
    {"smb1_nowarp", GameId::SMB1, "The Long Way Round", "Finish the game from World 1-1 without using a warp zone."},
    {"smb1_warp", GameId::SMB1, "Shortcut", "Travel through a warp zone."},
    {"smb1_top", GameId::SMB1, "Top of the Pole", "Grab a flagpole at the very top for 5000 points."},
    {"smb1_fireworks", GameId::SMB1, "Fireworks Show", "Finish a level to a fireworks display."},
    {"smb1_heaven", GameId::SMB1, "Coin Heaven", "Climb a vine up to a hidden area."},
    {"smb1_coins", GameId::SMB1, "Pocket Change", "Collect 100 coins for an extra life."},
    {"smb1_flawless", GameId::SMB1, "Not a Scratch", "Clear a castle without getting hurt."},
    {"smb1_star", GameId::SMB1, "Star Power", "Defeat 5 enemies with one Starman."},
    {"smb1_lives", GameId::SMB1, "Lives to Spare", "Have 20 lives at once."},
    {"smb1_luigi", GameId::SMB1, "Player Two", "Finish a level as Luigi."},
    {"smb1_speed", GameId::SMB1, "Speedy Plumber", "Finish World 1-1 with 300 or more on the clock.", true},
    {"smb1_score", GameId::SMB1, "Six Figures", "Score 100,000 points."},
    {"smb1_fire", GameId::SMB1, "Fired Up", "Grab a Fire Flower."},
    {"smb1_world8", GameId::SMB1, "Express to World 8", "Warp straight to World 8."},
    {"smb1_hammerbro", GameId::SMB1, "Bro Down", "Defeat a Hammer Bro."},
    {"smb1_bowserfire", GameId::SMB1, "Barbecue Bowser", "Defeat Bowser with fireballs instead of the axe."},
    {"smb1_enemies", GameId::SMB1, "Goomba Stomper", "Defeat 100 enemies.", false, 100},
    // The Lost Levels
    {"ll_toad", GameId::SMBLL, "Same Old Story", "Clear the castle of World 1."},
    {"ll_castles", GameId::SMBLL, "Lost No More", "Clear the castles of Worlds 1 to 8.", false, 8, true},
    {"ll_princess", GameId::SMBLL, "Peach Found", "Rescue the Princess in World 8-4."},
    {"ll_world9", GameId::SMBLL, "Secret World 9", "Reach World 9 by finishing the game without a warp zone."},
    {"ll_letters", GameId::SMBLL, "The Very End", "Clear the castle of World D."},
    {"ll_warp", GameId::SMBLL, "Warp Ahead", "Travel forward through a warp zone."},
    {"ll_backwarp", GameId::SMBLL, "Wrong Way!", "Get sent back by a warp zone.", true},
    {"ll_top", GameId::SMBLL, "Lofty Leap", "Grab a flagpole at the very top for 5000 points."},
    {"ll_fireworks", GameId::SMBLL, "Celebration", "Finish a level to a fireworks display."},
    {"ll_heaven", GameId::SMBLL, "Cloud Nine", "Climb a vine up to a hidden area."},
    {"ll_coins", GameId::SMBLL, "Coin Purse", "Collect 100 coins for an extra life."},
    {"ll_flawless", GameId::SMBLL, "Lost but Unharmed", "Clear a castle without getting hurt."},
    {"ll_star", GameId::SMBLL, "Shooting Star", "Defeat 5 enemies with one Starman."},
    {"ll_speed", GameId::SMBLL, "No Time to Lose", "Finish World 1-1 with 300 or more on the clock.", true},
    {"ll_fire", GameId::SMBLL, "Hot Stuff", "Grab a Fire Flower."},
    {"ll_hammerbro", GameId::SMBLL, "Brother Beater", "Defeat a Hammer Bro."},
    {"ll_luigi", GameId::SMBLL, "Green Machine", "Clear a castle in the LUIGI GAME."},
    {"ll_bowserfire", GameId::SMBLL, "Bowser Flambé", "Defeat Bowser with fireballs instead of the axe."},
    {"ll_enemies", GameId::SMBLL, "Lost Legion", "Defeat 100 enemies.", false, 100},
    // Super Mario Bros. 2
    {"smb2_mouser", GameId::SMB2, "Bomb Squad", "Defeat Mouser in World 1."},
    {"smb2_tryclyde", GameId::SMB2, "Snake Charmer", "Defeat Tryclyde in World 2."},
    {"smb2_mouser2", GameId::SMB2, "Cat and Mouser", "Defeat Mouser again in World 3."},
    {"smb2_fryguy", GameId::SMB2, "Cool Customer", "Defeat Fryguy in World 4."},
    {"smb2_clawgrip", GameId::SMB2, "Rock Lobster", "Defeat Clawgrip in World 5."},
    {"smb2_tryclyde2", GameId::SMB2, "Heads Will Roll", "Defeat Tryclyde again in World 6."},
    {"smb2_wart", GameId::SMB2, "Wake Up!", "Defeat Wart and free Subcon."},
    {"smb2_heroes", GameId::SMB2, "Dream Team", "Clear a level with each of the four heroes.", false, 4, true},
    {"smb2_unscathed", GameId::SMB2, "Untouchable", "Clear a level without getting hurt."},
    {"smb2_jackpot", GameId::SMB2, "Jackpot", "Win an extra life at the Bonus Chance."},
    {"smb2_hearts", GameId::SMB2, "Full of Heart", "Have four hearts of health."},
    {"smb2_mushroom", GameId::SMB2, "Mushroom Hunter", "Find a mushroom in Subspace."},
    {"smb2_star", GameId::SMB2, "Starman", "Collect five cherries and catch the Starman."},
    {"smb2_warp", GameId::SMB2, "Down the Jar", "Warp ahead through a jar."},
    {"smb2_pow", GameId::SMB2, "POW!", "Defeat three enemies at once."},
    {"smb2_lives", GameId::SMB2, "Extra Lives", "Have 10 lives at once."},
    {"smb2_subspace", GameId::SMB2, "Through the Door", "Enter Subspace."},
    {"smb2_phanto", GameId::SMB2, "Phanto Menace", "Pick up a key and wake a Phanto."},
    {"smb2_birdo", GameId::SMB2, "Egg Return", "Defeat Birdo."},
    {"smb2_enemies", GameId::SMB2, "Veggie Tales", "Defeat 100 enemies.", false, 100},
    // Super Mario Bros. 3
    {"smb3_wand", GameId::SMB3, "Royal Rescue", "Defeat a Koopaling and return a king's wand."},
    {"smb3_wands", GameId::SMB3, "All the King's Wands", "Return the wands of all seven kings.", false, 7, true},
    {"smb3_dark", GameId::SMB3, "Dark Land", "Reach World 8."},
    {"smb3_bowser", GameId::SMB3, "The End of Bowser", "Defeat Bowser and rescue the Princess."},
    {"smb3_whistle", GameId::SMB3, "Warp Whistle", "Blow a warp whistle to reach the Warp Zone."},
    {"smb3_frog", GameId::SMB3, "Frog Prince", "Wear the Frog Suit."},
    {"smb3_tanooki", GameId::SMB3, "Tanooki Time", "Wear the Tanooki Suit."},
    {"smb3_hammer", GameId::SMB3, "Hammer Time", "Wear the Hammer Suit."},
    {"smb3_pwing", GameId::SMB3, "Wings of Wonder", "Fly with a P-Wing."},
    {"smb3_cards", GameId::SMB3, "Full House", "Collect three matching cards at the goal."},
    {"smb3_stars", GameId::SMB3, "Five-Up", "Collect three Star cards at the goal.", true},
    {"smb3_coins", GameId::SMB3, "Coin Hoard", "Collect 100 coins for an extra life."},
    {"smb3_items", GameId::SMB3, "Packed Inventory", "Fill all 28 item slots."},
    {"smb3_toad", GameId::SMB3, "Toad's Treasure", "Open a chest in a Toad House."},
    {"smb3_score", GameId::SMB3, "Seven Figures", "Score 1,000,000 points in one game (a game over resets the score)."},
    {"smb3_lives", GameId::SMB3, "Lives Galore", "Have 99 lives."},
    {"smb3_statue", GameId::SMB3, "Stone Cold", "Turn to stone in the Tanooki Suit."},
    {"smb3_shoe", GameId::SMB3, "If the Shoe Fits", "Hop into Kuribo's Shoe."},
    {"smb3_music", GameId::SMB3, "Lullaby", "Use a Music Box on the map."},
    {"smb3_rock", GameId::SMB3, "Rock Breaker", "Smash a rock on the map with a Hammer."},
    {"smb3_enemies", GameId::SMB3, "Koopa Krusher", "Defeat 100 enemies.", false, 100},
    // Across the collection
    {"smas_allstar", GameId::Boot, "All-Star", "Finish all four games.", false, 4},
    {"smas_complete", GameId::Boot, "Completionist", "Unlock every other achievement."},
};
}  // namespace

const char* const kFinishedGame[4] = {"smb1_princess", "ll_princess", "smb2_wart", "smb3_bowser"};

const std::vector<AchievementDef>& achievementDefs() { return kDefs; }

const AchievementDef* findAchievement(const std::string& id) {
  for (const AchievementDef& d : kDefs)
    if (id == d.id) return &d;
  return nullptr;
}

// ---------------------------------------------------------------------------
// The tracker

void AchievementTracker::newRun(bool c) {
  counts = c;
  std::memset(run_, 0, sizeof run_);
  std::memset(s_, 0, sizeof s_);
  havePrev_ = false;
  last_ = GameId::Boot;
  events_.clear();
  sent_.clear();
}

std::vector<AchievementEvent> AchievementTracker::take() {
  std::vector<AchievementEvent> e;
  e.swap(events_);
  return e;
}

std::vector<uint8_t> AchievementTracker::save() const {
  std::vector<uint8_t> out(1 + sizeof run_);
  out[0] = counts ? 1 : 0;
  std::memcpy(out.data() + 1, run_, sizeof run_);
  return out;
}

void AchievementTracker::load(const uint8_t* p, size_t n) {
  counts = n >= 1 + sizeof run_ && p[0] == 1;
  if (n >= 1 + sizeof run_) std::memcpy(run_, p + 1, sizeof run_);
  else std::memset(run_, 0, sizeof run_);
  std::memset(s_, 0, sizeof s_);  // (the checks start afresh from the loaded point)
  havePrev_ = false;
  last_ = GameId::Boot;
  sent_.clear();
}

void AchievementTracker::frame(const uint8_t* wram, GameId game) {
  if (!counts) return;
  if (game != last_) {  // another title started: its checks start afresh
    std::memset(s_, 0, sizeof s_);
    havePrev_ = false;
    last_ = game;
  }
  if (!havePrev_) {  // (the co-op counts as they stand: only what happens from here)
    lastLuigiPoles_ = luigiPoles_;
    lastP2Hurts_ = p2Hurts_;
  }
  if (havePrev_) {
    switch (game) {
      case GameId::SMB1: smb1(wram, false); break;
      case GameId::SMBLL: smb1(wram, true); break;
      case GameId::SMB2: smb2(wram); break;
      case GameId::SMB3: smb3(wram); break;
      default: break;
    }
  }
  std::memcpy(prev_, wram, sizeof prev_);
  havePrev_ = true;
}

// ---------------------------------------------------------------------------
// Super Mario Bros. and The Lost Levels (one engine; the NES RAM layout for
// $0700-$07FF, see docs/re/smb1.md).
//   $0770 OperMode (1 play, 2 a castle cleared: the toad / princess scene)
//   $0772 its task (3 = play), $000F GameEngineSubroutine (1 vine climb into
//   a bonus area, 4 flagpole, $0B dying), $075F world, $075C level,
//   $010F the flagpole score (0 = 5000 ... 4 = 100), $075E coins, $075A lives
//   - 1, $07AF star timer, $07AE injury timer, $07E9-$07EB timer digits,
//   $0753 current player (1 = Luigi), $07CF/$07D5 the players' score digits,
//   enemy slots: flag $10+k, ID $1C+k, state $29+k ($20 = defeated),
//   score floating up from slot k: $0110+k (its timer $0138+k), $0283
//   Bowser's hit points, $0756 player status (2 fiery). (The Lost Levels'
//   LUIGI GAME: $0753 1 in play; $077A is only its menu's choice.)
void AchievementTracker::smb1(const uint8_t* w, bool ll) {
  const uint8_t* p = prev_;
  uint32_t& run = run_[ll ? 1 : 0];
  enum { kFromStart = 1, kWarped = 2, kPlayed = 4 };
  enum { sLastWorld, sLastLevel, sHurt, sFlag, sStarKills, sValid };
  auto id = [&](const char* smb1Id, const char* llId) { return ll ? llId : smb1Id; };
  const int world = w[0x075f], level = w[0x075c];
  const bool play = w[0x0770] == 1 && w[0x0772] == 3, wasPlay = p[0x0770] == 1 && p[0x0772] == 3;

  // A level starts (or restarts after a death): the first one of the run at
  // 1-1 is a game from the start. A castle's attempt starts unhurt.
  if (play && !wasPlay) {
    if (!(run & kPlayed) && world == 0 && level == 0) run |= kFromStart;
    run |= kPlayed;
    if (level == 3) s_[sHurt] = 0;
  }
  if (play) {
    // A new world reached other than from its castle: a warp zone.
    if (s_[sValid] && world != s_[sLastWorld]) {
      if (s_[sLastLevel] != 3) {
        run |= kWarped;
        if (!ll || world > s_[sLastWorld]) unlock(id("smb1_warp", "ll_warp"));
        else unlock("ll_backwarp");
        if (!ll && world == 7) unlock("smb1_world8");
      }
    }
    s_[sLastWorld] = world;
    s_[sLastLevel] = level;
    s_[sValid] = 1;
    if (ll && world == 8) unlock("ll_world9");
  }
  // Hurt: the injury timer starts, or a death. (Co-op: either player.)
  if ((w[0x07ae] && !p[0x07ae]) || (w[0x000f] == 0x0b && p[0x000f] != 0x0b)) s_[sHurt] = 1;
  if (p2Hurts_ > lastP2Hurts_) s_[sHurt] = 1;
  lastP2Hurts_ = p2Hurts_;
  // (Co-op: Luigi finishes as the game's player, the axe or the flag his;
  // or kept aside, riding the flagpole down.)
  const bool luigi = w[0x0753] == 1 || coopLuigi_;
  if (!ll && luigiPoles_ > lastLuigiPoles_) unlock("smb1_luigi");
  lastLuigiPoles_ = luigiPoles_;

  // A castle cleared
  if (w[0x0770] == 2 && p[0x0770] == 1) {
    if (world < 8) mark(id("smb1_castles", "ll_castles"), world);
    if (world == 0) unlock(id("smb1_toad", "ll_toad"));
    if (world == 7) {
      unlock(id("smb1_princess", "ll_princess"));
      if (!ll && (run & kFromStart) && !(run & kWarped)) unlock("smb1_nowarp");
    }
    if (ll && world == 12) unlock("ll_letters");
    if (!s_[sHurt]) unlock(id("smb1_flawless", "ll_flawless"));
    if (!ll && luigi) unlock("smb1_luigi");
    if (ll && (w[0x0753] == 1 || coopLuigi_)) unlock("ll_luigi");  // (its LUIGI GAME: [CurrentPlayer] 1 in play)
  }
  // The flagpole: the score is chosen as it is grabbed (look a frame later)
  if (w[0x000f] == 4 && p[0x000f] != 4) s_[sFlag] = 1;
  else if (s_[sFlag] && w[0x000f] == 4) {
    s_[sFlag] = 0;
    if (w[0x010f] == 0) unlock(id("smb1_top", "ll_top"));
    int t = w[0x07e9] * 100 + w[0x07ea] * 10 + w[0x07eb];
    if (world == 0 && level == 0 && t >= 300) unlock(id("smb1_speed", "ll_speed"));
    if (!ll && luigi) unlock("smb1_luigi");
  }
  // Bonus area by vine; fireworks (enemy $16)
  if (w[0x000f] == 1 && p[0x000f] != 1) unlock(id("smb1_heaven", "ll_heaven"));
  for (int k = 0; k < 6; k++)
    if (w[0x10 + k] && w[0x1c + k] == 0x16) unlock(id("smb1_fireworks", "ll_fireworks"));
  // The 100th coin (the tally rolls over for an extra life)
  if (play && wasPlay && p[0x075e] == 99 && w[0x075e] == 0 && w[0x075a] > p[0x075a]) unlock(id("smb1_coins", "ll_coins"));
  // Enemies defeated during one Starman
  if (w[0x07af]) {
    for (int k = 0; k < 6; k++)
      if (w[0x10 + k] && (w[0x29 + k] & 0x20) && !(p[0x29 + k] & 0x20)) s_[sStarKills]++;
    if (s_[sStarKills] >= 5) unlock(id("smb1_star", "ll_star"));
  } else {
    s_[sStarKills] = 0;
  }
  // A Fire Flower (either brother in co-op: the one kept aside powers up with
  // his variables in)
  if (play && w[0x0756] == 2 && p[0x0756] != 2) unlock(id("smb1_fire", "ll_fire"));
  // Enemies defeated: a score floats up from the enemy's slot ($0110+k
  // [FloateyNum_Control], its timer $0138+k set afresh, so two in a row in
  // one slot count) as it's knocked out (its state's $20: a fireball, a
  // star, a shell, a block bumped under it, a stomp that drops it) or a
  // Goomba ($06) squashed (state 4). Not a Koopa stomped into its shell, the
  // shell kicked, a Paratroopa's wings lost (scores too); not fireworks
  // ($16) or a power-up ($2E, slot 5's when one is out).
  if (play && wasPlay) {
    int n = 0;
    for (int k = 0; k < 6; k++) {
      const uint8_t e = w[0x1c + k], st = w[0x29 + k];
      const bool out = (st & 0x20) || (e == 0x06 && st == 0x04);
      if (w[0x0110 + k] && w[0x0138 + k] > p[0x0138 + k] && out && e != 0x16 && e != 0x2e) {
        n++;
        if (e == 0x05) unlock(id("smb1_hammerbro", "ll_hammerbro"));
      }
    }
    add(id("smb1_enemies", "ll_enemies"), n);
  }
  // Bowser brought down by fireballs: his hit points ([BowserHitPoints],
  // here $0283, 5 or more) run out in a castle, in play (the player alive:
  // a death's restart clears them too), he ($2D) there the frame before (as
  // they run out he may already be the false one he was: a Goomba in World
  // 1). The axe drops him without them.
  if (level == 3 && play && wasPlay && w[0x000f] == 8 && w[0x0283] == 0 && p[0x0283] != 0) {
    bool bowser = false;
    for (int k = 0; k < 6; k++) bowser = bowser || (p[0x10 + k] && p[0x1c + k] == 0x2d);
    if (bowser) unlock(id("smb1_bowserfire", "ll_bowserfire"));
  }
  if (!ll) {
    if (w[0x075a] >= 19 && w[0x075a] < 0x80) unlock("smb1_lives");  // (a game over leaves it $FF)
    if (w[0x07cf] >= 1 || w[0x07d5] >= 1) unlock("smb1_score");
  }
}

// ---------------------------------------------------------------------------
// Super Mario Bros. 2 (docs/re/smb2.md).
//   A level clear raises the clearing character's count ($062D + $008F) in
//   the frame it happens, $0533 still the level cleared (the request $04ED is
//   set and handled within a frame). Bosses close levels 1-3 (2), 2-3 (5),
//   3-3 (8), 4-3 (11), 5-3 (14), 6-3 (17); Wart 7-2 ($13).
//   $04C3 health ($10 per heart), $04C4 extra hearts (2 = four), $04E1 star
//   timer, $04EE lives, $0635 world, $0629 level within it, $0217 $80 in an
//   area, $0050 player state (7 dying), objects 0-4: state $51+X (1 alive, 2
//   dying), the Bonus Chance: $0100 $40, $0217 0, $0775 2, $0789 1.
void AchievementTracker::smb2(const uint8_t* w) {
  const uint8_t* p = prev_;
  enum { sHurt, sLevel, sWorld, sLevelInWorld, sValid, sFell, sFellFrames };
  const int ch = w[0x008f] & 3, level = w[0x0533];
  const bool inArea = w[0x0217] == 0x80;
  // A new level (not a restart after a death) starts unhurt.
  if (inArea && (!s_[sValid] || level != s_[sLevel])) {
    // A world skipped, or the next one reached other than from its last
    // level: a warp (the jars warp from 1-3, 3-1, 4-2 and 5-3, two or three
    // worlds on).
    const int dw = w[0x0635] - s_[sWorld];
    if (s_[sValid] && (dw >= 2 || (dw == 1 && s_[sLevelInWorld] != 2))) unlock("smb2_warp");
    s_[sHurt] = 0;
    s_[sLevel] = level;
    s_[sValid] = 1;
  }
  if (inArea) {
    s_[sWorld] = w[0x0635];
    s_[sLevelInWorld] = w[0x0629];
  }
  if ((inArea && w[0x04c3] < p[0x04c3]) || (w[0x0050] == 7 && p[0x0050] != 7)) s_[sHurt] = 1;
  // A level cleared
  if (w[0x062d + ch] == p[0x062d + ch] + 1 && w[0x008f] == p[0x008f]) {
    mark("smb2_heroes", ch);
    if (!s_[sHurt]) unlock("smb2_unscathed");
    switch (level) {
      case 2: unlock("smb2_mouser"); break;
      case 5: unlock("smb2_tryclyde"); break;
      case 8: unlock("smb2_mouser2"); break;
      case 11: unlock("smb2_fryguy"); break;
      case 14: unlock("smb2_clawgrip"); break;
      case 17: unlock("smb2_tryclyde2"); break;
      case 0x13: unlock("smb2_wart"); break;
      default: break;
    }
  }
  const bool bonus = w[0x0100] == 0x40 && w[0x0217] == 0 && w[0x0775] == 2 && w[0x0789] == 1;
  if (bonus && w[0x04ee] > p[0x04ee]) unlock("smb2_jackpot");
  if (w[0x04c4] >= 2) unlock("smb2_hearts");
  if (inArea && w[0x04c4] > p[0x04c4]) unlock("smb2_mushroom");
  if (inArea && w[0x04e1] && !p[0x04e1]) unlock("smb2_star");
  if (w[0x04ee] >= 10 && w[0x04ee] < 100) unlock("smb2_lives");
  if (inArea && w[0x0628] == 2) unlock("smb2_subspace");  // ([$0628] 2: in Subspace)
  // The objects ($90+X their type, the NES game's enemies: Phanto $17,
  // Birdo $1C (the items after them are 2 on: the key $3D); $51+X their
  // state, 1 alive, 2 knocked out; $29+X their X): a Phanto awake (it hangs
  // still until the key is picked up, then flies), Birdo knocked out, any
  // enemy knocked out (not hearts, eggs, bubbles, fireballs, rocks or jar
  // generators: types $01-$31, the sparks the last, but those).
  if (inArea) {
    int n = 0;
    for (int k = 0; k < 9; k++) {
      const uint8_t t = w[0x90 + k];
      if (t == 0x17 && p[0x90 + k] == 0x17 && w[0x51 + k] == 1 && w[0x29 + k] != p[0x29 + k]) unlock("smb2_phanto");
      if (w[0x51 + k] == 2 && p[0x51 + k] == 1 && t == p[0x90 + k]) {
        if (t == 0x1c) unlock("smb2_birdo");
        const bool foe = t >= 0x01 && t <= 0x31 && t != 0x11 && t != 0x15 && t != 0x16 && t != 0x1b && t != 0x1e &&
                         t != 0x20 && t != 0x22;
        n += foe;
      }
    }
    add("smb2_enemies", n);
  }
  // Three enemies (slots 0-4) knocked out within a few frames: a POW, a bomb
  // or one thrown enemy through a crowd.
  if (inArea) {
    int fell = 0;
    for (int k = 0; k < 5; k++) fell += w[0x51 + k] == 2 && p[0x51 + k] == 1;
    if (s_[sFellFrames] && ++s_[sFellFrames] > 8) s_[sFell] = s_[sFellFrames] = 0;
    if (fell) {
      if (!s_[sFellFrames]) s_[sFellFrames] = 1;
      s_[sFell] += fell;
      if (s_[sFell] >= 3) unlock("smb2_pow");
    }
  }
}

// ---------------------------------------------------------------------------
// Super Mario Bros. 3 (docs/re/smb3.md).
//   The map: $070A tileset 0 and $0100 $C0 ($070A is 0 on the title too, but
//   $0100 is $20 there). $0727 world (8 = the Warp Zone), $0726 player,
//   suit in a level $00BB (4 frog, 5 tanooki, 6 hammer), $056E flight ($FF =
//   P-Wing), cards $1D9C (Luigi $1DBF), items $1D80 (Luigi $1DA3, 28), coins
//   $1DA2 (Luigi $1DC5), lives $0736+player, score $0715-$0717 (/10, big
//   endian), level objects' IDs $0671+slot (Bowser $18).
void AchievementTracker::smb3(const uint8_t* w) {
  const uint8_t* p = prev_;
  enum { sMapWorld, sValid, sWarping, sBowser };
  const int pl = w[0x0726] & 1;
  const bool map = w[0x070a] == 0 && w[0x0100] == 0xc0;
  const bool level = w[0x070a] != 0 && (w[0x0100] == 0xc0 || w[0x0100] == 0x80 || w[0x0100] == 0x95);
  const int world = w[0x0727];
  // World progress on the map: one world on from the last = its airship
  // beaten (not the Warp Zone and where it leads, nor a saved game loaded
  // from the title).
  if (w[0x0100] == 0x20) s_[sValid] = 0;
  if (map) {
    if (world == 8) {
      unlock("smb3_whistle");
      s_[sWarping] = 1;
    } else {
      if (s_[sValid] && !s_[sWarping] && world == s_[sMapWorld] + 1 && s_[sMapWorld] <= 6) {
        unlock("smb3_wand");
        mark("smb3_wands", s_[sMapWorld]);
      }
      s_[sMapWorld] = world;
      s_[sValid] = 1;
      s_[sWarping] = 0;
    }
    if (world == 7) unlock("smb3_dark");
  }
  if (level) {
    const uint8_t suit = w[0x00bb];
    if (suit == 4) unlock("smb3_frog");
    if (suit == 5) unlock("smb3_tanooki");
    if (suit == 6) unlock("smb3_hammer");
    if (w[0x056e] == 0xff && (suit == 3 || suit == 5)) unlock("smb3_pwing");
    if (w[0x057a]) unlock("smb3_statue");  // ([Player_Statue] its timer)
    if (w[0x0577]) unlock("smb3_shoe");    // ([Player_Kuribo])
    // Enemies defeated: an object's state ($0661+X) to 6 (killed, falling:
    // a star, a shell, a fireball, a tail), 7 (squashed: a stomp) or 8 (gone
    // in a puff), from walking about (2) or a shell (3); once each (6 then 8
    // is one). Not from 5, thrown or kicked: a Buster Beetle's ice block
    // breaking on a wall went 5 to 6.
    int n = 0;
    for (int k = 0; k < 8; k++) {
      const uint8_t s = w[0x0661 + k], was = p[0x0661 + k];
      if ((s == 6 || s == 7 || s == 8) && (was == 2 || was == 3)) n++;
    }
    add("smb3_enemies", n);
    // Toad House: an item received
    if (w[0x070a] == 7) {
      int now = 0, was = 0;
      for (int i = 0; i < 28; i++) {
        now += w[0x1d80 + pl * 0x23 + i] != 0;
        was += p[0x1d80 + pl * 0x23 + i] != 0;
      }
      if (now > was) unlock("smb3_toad");
    }
  }
  // Cards: three alike the moment the third arrives
  const uint8_t* c = w + 0x1d9c + pl * 0x23;
  const uint8_t* pc = p + 0x1d9c + pl * 0x23;
  if (c[0] && c[0] == c[1] && c[1] == c[2] && !(pc[0] == c[0] && pc[1] == c[1] && pc[2] == c[2])) {
    unlock("smb3_cards");
    if (c[0] == 3) unlock("smb3_stars");
  }
  // Coins: the 100th rolls over for an extra life
  const uint16_t coins = 0x1da2 + pl * 0x23;
  if (p[coins] == 99 && w[coins] == 0 && w[0x0736 + pl] > p[0x0736 + pl] && w[0x0736 + pl] != 0xff)
    unlock("smb3_coins");
  // Items used on the map: one fewer of a kind (the bar closes up behind it)
  if (map) {
    auto count = [&](const uint8_t* m, uint8_t item) {
      int n = 0;
      for (int i = 0; i < 28; i++) n += m[0x1d80 + pl * 0x23 + i] == item;
      return n;
    };
    if (count(w, 0x0d) < count(p, 0x0d)) unlock("smb3_music");
    if (count(w, 0x0b) < count(p, 0x0b)) unlock("smb3_rock");
  }
  bool full = true;
  for (int i = 0; i < 28 && full; i++) full = w[0x1d80 + pl * 0x23 + i] != 0;
  if (full) unlock("smb3_items");
  if (((w[0x0715] << 16) | (w[0x0716] << 8) | w[0x0717]) >= 100000) unlock("smb3_score");
  if (w[0x0736 + pl] >= 98 && w[0x0736 + pl] != 0xff) unlock("smb3_lives");
  // Bowser (World 8): his ground pound breaks through the floor and he falls
  // below the level (object $18 stays in its slot, Y ~$401, while the time
  // is tallied and the princess's door opens). Through that door the ending
  // runs in NMI mode $20 like the title, but with the castle tileset still
  // set (the title has 0). A death after his fall returns to the map.
  if (level && world == 7)
    for (int k = 0; k < 8; k++)
      if (w[0x0671 + k] == 0x18 && ((w[0x56 + k] << 8) | w[0x71 + k]) >= 0x1c0) s_[sBowser] = 1;
  if (map) s_[sBowser] = 0;
  if (s_[sBowser] && w[0x0100] == 0x20 && w[0x070a] != 0) {
    unlock("smb3_bowser");
    s_[sBowser] = 0;
  }
}
