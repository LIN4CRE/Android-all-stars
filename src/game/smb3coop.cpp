// Super Mario Bros. 3: two players at once (an experimental test). Luigi is
// a second player run through the game's own player update: after Mario's
// (JSL $23:BFA7 at $20:8F2F), the variables that update keeps for the player
// are swapped for Luigi's, his controller (2) put in, and the same call made
// again; then Mario's come back. Luigi is drawn into free sprite entries as
// the frame goes to the screen, with his own pose's tiles and his colours.
// What he does to the level (blocks he bumps) stays; the camera follows Mario.
// Not yet: enemies and items see only Mario; the map, pipes and the goal are
// Mario's (Luigi is brought along).
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "game/smb3.h"
#include "game/stateext.h"

namespace {
// What the player's update keeps for the player (found by logging its writes
// while walking, running, jumping, ducking and flying in each form): position,
// speeds, form, timers, his pose's tiles ($0220-$022B, bank $0238) and his
// palette ($14E0-$14FF, its copy at $7F:C500); from a hit: the shrink, flashing
// and star timers ($0551-$0553); a slope's slide ($0545, $0546, $058A); a
// throw's pose ($0513); the statue ($0554, $057A); holding ($06A4, set by
// what he holds as it runs, and its copy for the next frame $06A5: see coopHoldCopy). Low ones are WRAM $0000-$1FFF;
// $7Fxxxx are the second bank.
const uint32_t kVars[] = {
    0x0012, 0x0014, 0x002E, 0x002F, 0x0043, 0x0052, 0x0053, 0x0055, 0x005E, 0x0070, 0x0079, 0x0082, 0x008B, 0x009C,
    0x009D, 0x00A6, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BF, 0x00D8, 0x00D9,
    0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x0220, 0x0221, 0x0222, 0x0223, 0x0224, 0x0225, 0x0226, 0x0227, 0x0228, 0x0229,
    0x022A, 0x022B, 0x0238, 0x0413, 0x0510, 0x0515, 0x0513, 0x0542, 0x0543, 0x0544, 0x0545, 0x0546, 0x0551, 0x0552, 0x0553, 0x0554, 0x055C, 0x0560, 0x0565, 0x0569, 0x056B,
    0x056C, 0x056D, 0x056E, 0x056F, 0x0570, 0x0573, 0x0574, 0x0578, 0x057A, 0x057B, 0x057F, 0x0583, 0x0585, 0x0587, 0x0589, 0x058A,
    0x058C, 0x05F4, 0x05FE, 0x0602, 0x0603, 0x0604, 0x0605, 0x0606, 0x0608, 0x0609, 0x060A, 0x060B, 0x06A4, 0x06A5, 0x0713, 0x074E,
    0x0760, 0x0781, 0x14E0, 0x14E1, 0x14E2, 0x14E3, 0x14E4, 0x14E5, 0x14E6, 0x14E7, 0x14E8, 0x14E9, 0x14EA, 0x14EB,
    0x14EC, 0x14ED, 0x14EE, 0x14EF, 0x14F0, 0x14F1, 0x14F2, 0x14F3, 0x14F4, 0x14F5, 0x14F6, 0x14F7, 0x14F8, 0x14F9,
    0x14FA, 0x14FB, 0x14FC, 0x14FD, 0x14FE, 0x14FF, 0x1500, 0x1CEF, 0x1CF2, 0x7FC500, 0x7FC501, 0x7FC502, 0x7FC503,
    0x7FC504, 0x7FC505, 0x7FC506, 0x7FC507, 0x7FC508, 0x7FC509, 0x7FC50A, 0x7FC50B, 0x7FC50C, 0x7FC50D, 0x7FC50E,
    0x7FC50F, 0x7FC510, 0x7FC511, 0x7FC512, 0x7FC513, 0x7FC514, 0x7FC515, 0x7FC516, 0x7FC517, 0x7FC518, 0x7FC519,
    0x7FC51A, 0x7FC51B, 0x7FC51C, 0x7FC51D, 0x7FC51E, 0x7FC51F, 0x7FC586,
    // (in the water ($0575) and its change's step ($0576): one brother in it,
    // the other out, the one out took the other's for his and went on leaving
    // the water every frame, standing still. Last, so that states saved
    // without them still load: see coopLoad.)
    0x0575, 0x0576,
    // (... and, each found the same way (one brother doing it, the other not,
    // against the game alone): the tail's wag, its count ($00BE) and timer
    // ($0511), floating the other down in its pose; the tail's spin ($0517,
    // the other spinning, and at half length); the star's somersault ($054F);
    // the Kuribo's Shoe ($0577: both in one); a Sledge Bro's landing's stun
    // ($1CF4: the other stunned mid-jump); an object's grace for a jump from
    // it ($101F), its push ($1020: Mario's turn took it, the one carried
    // never had it) and its launch ($0579, the high jump kept while B is
    // held: the other's turn cleared it).)
    0x00BE, 0x0511, 0x0517, 0x054F, 0x0577, 0x1CF4, 0x101F, 0x1020, 0x0579,
    // (... and the Kuribo's Shoe's hop ($0550: both in shoes, the one
    // standing cleared it, the other's high jump from a hop failed))
    0x0550,
};
constexpr int kVarCount = sizeof(kVars) / sizeof(kVars[0]);
// (states saved before the water's two, and before the last nine: those 0)
constexpr int kVarCountV1 = kVarCount - 12, kVarCountV2 = kVarCount - 10, kVarCountV3 = kVarCount - 1;
// Timers the player's update counts down that aren't a player's (the P
// switch $0567, the freeze for a power-up or a hit $0556, the screen's shake
// $1CF3, the star's music's $0428, and the rest of the $0510 loop's): counted
// down in his turn too, they ran at twice the speed. His turn's count is
// undone (one less than before it, it goes back: anything he sets stands).
const uint32_t kTickOnce[] = {0x0428, 0x0512, 0x0514, 0x0516, 0x0556, 0x0567, 0x1CF3};
constexpr int kTickOnceCount = sizeof(kTickOnce) / sizeof(kTickOnce[0]);
// What the pipe move's end ($20:EA44-EA6C) writes: the move, the exit, its
// stage, the transition's counter (see coopExec).
const uint32_t kPipeShared[5] = {0x0571, 0x0414, 0x0377, 0x0713, 0x0014};
// A continue's lives (LDA #4; STA $0736, and the two-player game's STA $0736,X): just past.
constexpr uint32_t kContinueLives = 0x20b13b, kContinueLives2 = 0x20949b;
// Kept as they are around his turn: the controller ($F2-$F9: held / newly
// pressed, each byte of the pad; $17/$18 copies), the camera ($0210-$0213,
// $25), [Player_Current] $0726, and the player's sprite entries ($0850-$087F,
// their high bytes $0A34-$0A3F).
const uint32_t kKeep[] = {0x0017, 0x0018, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9,
                          0x0210, 0x0211, 0x0212, 0x0213, 0x0025, 0x0726};
constexpr int kKeepCount = sizeof(kKeep) / sizeof(kKeep[0]);
constexpr uint32_t kPlayerCall = 0x8f2f;  // (in bank $20: see smb3.cpp)
constexpr int kOamFirst = 20, kOamCount = 12;  // the player's sprite entries
}  // namespace

uint8_t& Smb3Engine::wvar(uint32_t a) { return a >= 0x7f0000 ? snes_.wram[0x10000 + (a & 0xffff)] : snes_.wram[a & 0x1ffff]; }

void Smb3Engine::setCoop(bool on) {
  if (on == coop_) return;
  coop_ = on;
  p2Valid_ = false;
  p2Drawn_ = false;
  coopTurn_ = 0;
  objSwapped_ = -1;
  p2Dead_ = -1;
  p2Wait_ = false;
  musicBack_ = 0;
  roles_ = false;
}

namespace {
int vix(uint32_t a) { return (int)(std::find(kVars, kVars + kVarCount, a) - kVars); }
// Of his variables, the camera's that are the game's whoever is in: its high
// byte's copy $12 (his, never scrolled, left behind: an object meeting him
// tested its delete range on it, and enemies in sight in the right margin
// vanished as 1-1's camera passed 256), the frame's scroll $0781 and the
// vertical scroll $0542/$0543.
bool shared(uint32_t a) { return a == 0x0012 || a == 0x0542 || a == 0x0543 || a == 0x0781; }
}  // namespace

void Smb3Engine::coopPoke(uint32_t adr, uint8_t v) {
  const uint32_t* k = std::find(kVars, kVars + kVarCount, adr);
  if (k != kVars + kVarCount && p2_.size() == (size_t)kVarCount) p2_[k - kVars] = v;
}

// Luigi beside Mario: Mario's variables, 20 px behind; in his own form (a
// new area: the one he had; else the one the map keeps for him, $0748).
void Smb3Engine::coopSync() {
  const uint8_t suit = p2Valid_ && p2_.size() == (size_t)kVarCount ? p2_[vix(0x00BB)] : ram(0x0748) <= 6 ? ram(0x0748) : 0;
  p2_.resize(kVarCount);
  for (int i = 0; i < kVarCount; i++) p2_[i] = wvar(kVars[i]);
  p2_[vix(0x00BB)] = suit;
  // (behind him, unless that's out of the view: auto-scrolling, its left edge would crush him)
  const int cam = ram(0x0210) | ram(0x0211) << 8;
  int x = (ram(0x005e) | ram(0x0043) << 8) - 20;
  if (x < 8 || x - cam < 16) x += 40;
  if (ram(0x0571)) x = ram(0x005e) | ram(0x0043) << 8;  // (coming out of a pipe: with him, in it)
  p2_[std::find(kVars, kVars + kVarCount, 0x005Eu) - kVars] = (uint8_t)x;
  p2_[std::find(kVars, kVars + kVarCount, 0x0043u) - kVars] = (uint8_t)(x >> 8);
  p2_[std::find(kVars, kVars + kVarCount, 0x008Bu) - kVars] = 0;
  // (His form as the game sets one, [Player_QueueSuit] $0578 = suit + 1: the
  // rest of Mario's form, copied with him, made his drawing Mario's (a small
  // Luigi drawn as a big raccoon, hidden behind him).)
  p2_[vix(0x0578)] = (uint8_t)(suit + 1);
  p2Valid_ = true;
}

// The game's player died while the other plays on: the other takes the
// game's slot (their variables exchanged), and the one who died goes on in
// the second, dying there as Luigi does (his tune, his jump, back beside
// the other for a life). The game's death is undone: its flag ($02D9).
void Smb3Engine::coopSwapRoles() {
  for (int i = 0; i < kVarCount; i++) {
    if (shared(kVars[i])) continue;
    const uint8_t t = wvar(kVars[i]);
    wvar(kVars[i]) = p2_[i];
    p2_[i] = t;
  }
  for (int k = 0; k < 2; k++) std::swap(ground_[0][k], ground_[1][k]);
  std::swap(p1Idle_, p2Idle_);
  roles_ = !roles_;
  wvar(0x02D9) = 0;
  // (The rest of this frame (what he holds, a shell) reads the game's player's
  // controller as the game took it, $17/$18: his own now, not the other's.)
  const int ctl = roles_ ? 1 : 0;
  const uint8_t hi = (uint8_t)(snes_.input[ctl] >> 8);
  wvar(0x0017) = hi;
  wvar(0x0018) = (uint8_t)(hi & ~padHi_[ctl]);
  p2Dead_ = 0;
  musicTrack_ = lastTrack_;
  musicBack_ = 190;
  apuSnap_ = lastApu_;
}

// The game's player's turn starts ($20:8F2F): with Luigi in its slot, his
// controller where the game reads the player's (the two pads' bytes, $F2-$F9,
// exchanged; see coopExec for the second slot's).
void Smb3Engine::coopPadSwap() {
  if (!coop_ || !roles_ || !gameplay_) return;
  for (uint32_t a : {0x00F2u, 0x00F4u, 0x00F6u, 0x00F8u}) std::swap(snes_.wram[a], snes_.wram[a + 1]);
  padSwapped_ = true;
}

// A brother's colours for a form, as the game picks them ($23:C467, from the
// table at $23:C451, bank $3C): Mario's small/big/raccoon 0, fire 2; Luigi's
// 1 and 8; the statue 7; the other suits the same on both.
void Smb3Engine::playerPalette(int who, int suit, bool statue, uint16_t* out) const {
  const int idx = suit == 2 ? (who ? 8 : 2) : statue ? 7 : (suit == 3 || suit < 2) ? who : suit;
  const uint32_t pal = 0x3c0000 | snes_.peek(0x23c451 + 2 * idx) | snes_.peek(0x23c452 + 2 * idx) << 8;
  for (int i = 0; i < 16; i++) out[i] = (uint16_t)(snes_.peek(pal + 2 * i) | snes_.peek(pal + 2 * i + 1) << 8);
}

// Back after his death, small and blinking, where Mario stands.
void Smb3Engine::coopReturn() {
  p2Valid_ = false;  // (small: see coopSync)
  coopSync();
  p2_[vix(0x005E)] = ram(0x005e);
  p2_[vix(0x0043)] = ram(0x0043);
  p2_[vix(0x00BB)] = 0;
  p2_[vix(0x0578)] = 1;  // (small, as the game makes him: see coopSync)
  p2_[vix(0x00BF)] = 0;
  p2_[vix(0x0551)] = 0;
  p2_[vix(0x0552)] = 0x70;  // (blinking, as after a hit)
  p2_[vix(0x0553)] = 0;
  p2Dead_ = -1;
  p2Wait_ = false;
}

// Around each of his turns (his update, an object meeting him): a death
// started in it is his. Its tune plays (the area's music back after it, see
// coopTick); the game's death flag ($02D9) is Mario's.
void Smb3Engine::coopDeathBefore() {
  deathPSwitch_ = ram(0x0567);
  deathBf_ = p2_[vix(0x00BF)];
  deathFlag_ = ram(0x02d9);
  deathTrack_ = ram(0x1206);
}

void Smb3Engine::coopDeathAfter() {
  if (deathBf_ != 0 || p2_[vix(0x00BF)] == 0) return;
  wvar(0x02D9) = deathFlag_;
  wvar(0x0567) = deathPSwitch_;  // (the game's death ($27:A560) ends the P switch: the game's player's goes on)
  p2Dead_ = 0;
  if (deathTrack_ != 0x09) musicTrack_ = deathTrack_;
  musicBack_ = 190;  // (the tune's length)
  apuSnap_ = lastApu_;
}

// Once a frame in a level, before his turn.
void Smb3Engine::coopTick() {
  // (His tune over, nothing else since (the music the game last sent, $1206,
  // still the tune): the sound chip back as it was before it, the area's
  // music going on where it stopped, not from its start. The game only ever
  // writes its ports and checks their echo, never waiting on the chip: a
  // stale echo is cleared by it in a few frames. Without one (a state loaded
  // since), the area's music is queued again.)
  if (musicBack_ > 0 && --musicBack_ == 0 && ram(0x00BF) == 0 && ram(0x1206) == 0x09 && musicTrack_) {
    // (its clock kept: going on from now, not catching up 3 s at once; its
    // ports as it had seen them, or the one still holding the tune plays it
    // again)
    const uint64_t now = snes_.apu.cycles();
    if (!apuSnap_.empty() && snes_.apu.loadState(apuSnap_.data(), apuSnap_.size())) {
      snes_.apu.setCycles(now);
      wvar(0x1206) = musicTrack_;
    } else {
      wvar(0x1202) = musicTrack_;
    }
  }
  if (!musicBack_) apuSnap_.clear();
}

// Luigi in the game: valid, alive, and played (his controller touched in the
// last 3 s; else the camera is Mario's alone, Luigi carried along by its edge).
bool Smb3Engine::p2Plays() const {
  return coop_ && gameplay_ && p2Valid_ && p2Drawn_ && !p2Wait_ && p2Dead_ < 0 && p2Area_ == areaStamp_ &&
         p2_.size() == (size_t)kVarCount && p2Idle_ < 180;
}

// The camera, in the player's update (horizontal levels, not auto-scrolling:
// $23:D00A-$D093): the game's own, on the point midway between the two
// (Mario's X put in for it; on Luigi alone while Mario's controller rests);
// Luigi's update leaves it be. After it, the
// screen's edges hold each player in the view ($23:D0C0, in place of the
// game's 4:3 one: the wide view's, as far as the level goes).
bool Smb3Engine::coopCamera(uint32_t pc) {
  if (!coop_ || !gameplay_ || !inPlayer_) return false;
  CpuRegs& r = snes_.cpu.r;
  if (pc == 0x23d00a) {
    if (coopTurn_ == 1) {
      wvar(0x0781) = 0;  // (Luigi's: no scrolling, none to pass on at $D093)
      r.pc = 0xd093;
      return true;
    }
    const int mx = ram(0x005e) | ram(0x0043) << 8;
    int want = mx;
    if (p2Plays()) {
      const int lx = p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8;
      want = p1Idle_ >= 180 ? lx : (mx + lx) >> 1;
    }
    // (eased: no jump where what it follows changes, as a player joins or rests)
    if (camAim_ < 0 || camArea_ != areaStamp_ || !p2Valid_) camAim_ = want;
    camArea_ = areaStamp_;
    camAim_ += std::clamp(want - camAim_, -6, 6);
    if (camAim_ == mx) return false;
    camX_[0] = ram(0x005e);
    camX_[1] = ram(0x0043);
    const int mid = camAim_;
    wvar(0x005E) = (uint8_t)mid;
    wvar(0x0043) = (uint8_t)(mid >> 8);
    camMid_ = true;
    return false;
  }
  if (pc == 0x23d093) {
    if (camMid_) {
      wvar(0x005E) = camX_[0];
      wvar(0x0043) = camX_[1];
      camMid_ = false;
    }
    return false;
  }
  // The vertical scroll ($0216: up to 3 px a frame while the player is in
  // the screen's top $30, down while he's below $79; fixed at the bottom,
  // $EF, unless he flies): Mario's turn, on both; Luigi's takes it as it is.
  if (pc == 0x23d177) {
    if (coopTurn_ == 0 && p2Plays() && ram(0x0216) == 0xef && (p2_[vix(0x056E)] | p2_[vix(0x056B)])) {
      r.pc = 0xd18a;  // (Luigi flies: off the bottom)
      return true;
    }
    return false;
  }
  if (pc == 0x23d18a) {
    if (coopTurn_ == 1) {
      r.pc = 0xd1ef;
      return true;
    }
    if (!p2Plays()) return false;
    // (Up for the higher, unless the lower would leave the screen's bottom;
    // down for the lower, as far as he goes: off the top is only out of
    // sight, off the bottom is the game's fall.)
    const int cam = ram(0x0543) | ram(0x0542) << 8;
    const int ly = (int16_t)(p2_[vix(0x0070)] | p2_[vix(0x0055)] << 8);
    const int my = coopRests(0) ? ly : (int16_t)(ram(0x0070) | ram(0x0055) << 8);  // (resting: Luigi's alone)
    const int a = std::min(my, ly), b = std::max(my, ly);
    int y;
    // (The lower is kept above $A0, the game's fall being a screen Y of $C0:
    // the screen goes down just so far, whatever the higher. Else up, at
    // up to 3 px a frame, for the higher in the top band, while the lower
    // stays above $A0; else down for the lower below $79, as far as leaves
    // the higher out of that band (no undoing each other).)
    if (b - cam > 0xa0) y = b - 0x27;
    else if (a - cam < 0x30) y = b - cam + 3 <= 0xa0 ? a : cam + 0x50;
    else if (b - cam > 0x79) y = std::min(b, a + 0x49);
    else y = cam + 0x50;
    camY_[0] = ram(0x0070);
    camY_[1] = ram(0x0055);
    wvar(0x0070) = (uint8_t)y;
    wvar(0x0055) = (uint8_t)(y >> 8);
    camMidY_ = true;
    return false;
  }
  if (pc == 0x23d1ef) {
    if (camMidY_) {
      wvar(0x0070) = camY_[0];
      wvar(0x0055) = camY_[1];
      camMidY_ = false;
    }
    return false;
  }
  // (the edges: only with Luigi there to keep in view)
  if (!p2Valid_ || p2Wait_ || !r.mf) return false;
  const int cam = ram(0x0210) | ram(0x0211) << 8, levelEnd = (ram(0x0022) + 1) * 256;
  const int lo = std::max(cam + 16 - extL_, 16), hi = std::min(cam + 232 + extR_, levelEnd - 24);
  const int x = ram(0x005e) | ram(0x0043) << 8;
  const int8_t vx = (int8_t)ram(0x008b);
  const int to = std::clamp(x, lo, hi);
  // (Resting, caught by the edge as the camera follows the other: brought to
  // where he last stood, not pushed along into walls and holes.)
  if (to != x && coopRests(coopTurn_)) {
    coopBring(coopTurn_);
  } else if (to != x && std::abs(to - x) <= 64) {  // (far out: not the edge's to fix)
    wvar(0x005E) = (uint8_t)to;
    wvar(0x0043) = (uint8_t)(to >> 8);
    wvar(0x0079) = (uint8_t)(to - cam);
    if ((to < x) == (vx > 0)) wvar(0x008B) = 0;  // (into the edge: stopped)
  }
  r.pc = 0xd0fa;
  return true;
}

// The one resting (his variables in: his turn) to where the other last stood
// on the ground (else where he is).
void Smb3Engine::coopBring(int who) {
  const int* g = ground_[1 - who];
  const std::vector<uint8_t>& o = who == 0 ? p2_ : p1_;
  const int gx = g[0] >= 0 ? g[0] : o[vix(0x005E)] | o[vix(0x0043)] << 8;
  const int gy = g[0] >= 0 ? g[1] : o[vix(0x0070)] | o[vix(0x0055)] << 8;
  const int cam = ram(0x0210) | ram(0x0211) << 8;
  wvar(0x005E) = (uint8_t)gx;
  wvar(0x0043) = (uint8_t)(gx >> 8);
  wvar(0x0070) = (uint8_t)gy;
  wvar(0x0055) = (uint8_t)(gy >> 8);
  wvar(0x008B) = 0;
  wvar(0x009D) = 0;
  wvar(0x0079) = (uint8_t)(gx - cam);
}

// Bringing the one resting along (see kEdgeDeath): the other in play (not
// dying, not out), the place he'd be brought to in view.
bool Smb3Engine::coopCanBring(int who) const {
  const std::vector<uint8_t>& o = who == 0 ? p2_ : p1_;
  if (o.size() != (size_t)kVarCount || o[vix(0x00BF)] != 0) return false;
  if (who == 0 && (p2Dead_ >= 0 || p2Wait_ || !p2Valid_)) return false;
  const int* g = ground_[1 - who];
  const int gx = g[0] >= 0 ? g[0] : o[vix(0x005E)] | o[vix(0x0043)] << 8;
  const int cam = ram(0x0210) | ram(0x0211) << 8;
  return gx - cam >= 8 - extL_ && gx - cam <= 248 + extR_;
}

// Mario (0) or Luigi (1) resting while the other plays: his controller
// untouched for 3 s. The camera is the other's; he is brought along.
bool Smb3Engine::coopRests(int who) const {
  // (Mario rests while Luigi plays on, dying or waiting to come back too)
  const bool luigi = coop_ && gameplay_ && p2Valid_ && p2_.size() == (size_t)kVarCount && p2Area_ == areaStamp_ &&
                     (p2Idle_ < 180 || p2Dead_ >= 0 || p2Wait_);
  return who == 0 ? p1Idle_ >= 180 && luigi : p2Idle_ >= 180 && coop_ && p2Valid_;
}

// Which brother is left standing (his controller untouched for 3 s) while the
// other plays on: 0 the game's player, 1 Luigi, -1 neither. The objects meet
// the one playing then, not the nearer: the one left idle can't be hurt, and a
// Koopaling stood by him didn't feel Mario landing on its head.
int Smb3Engine::coopIdleOne() const {
  if (p1Idle_ >= 180 && p2Idle_ < 180 && p2Dead_ < 0) return 0;
  if (p2Idle_ >= 180 && p1Idle_ < 180) return 1;
  return -1;
}

// A brother left standing (his controller untouched for 3 s) while the other
// plays on can't be hurt (see kHurtGate) till he moves: he couldn't dodge.
// Whoever's in the game's slot now: the second's in his turn or an object's
// with him swapped in. (Pits, lava and the time still count.)
bool Smb3Engine::coopIdleSafe() const {
  if (!coop_ || !gameplay_ || !p2Valid_ || p2_.size() != (size_t)kVarCount || p2Area_ != areaStamp_ ||
      ram(0x072b) >= 2 || ram(0x0726) != 0)
    return false;
  const bool second = coopTurn_ == 1 || objSwapped_ >= 0;
  const int mine = second ? p2Idle_ : p1Idle_, other = second ? p1Idle_ : p2Idle_;
  if (mine < 180 || other >= 180) return false;
  return second || (p2Dead_ < 0 && !p2Wait_ && p2Drawn_);
}

// At the player update's return ($20:8F33).
bool Smb3Engine::coopExec() {
  CpuRegs& r = snes_.cpu.r;
  objSwapped_ = -1;  // (an object's turn cut short: a reset, a state loaded)
  if (coopTurn_ == 0 && padSwapped_) {
    // (as they were: the game's pressed-this-frame comes from them next frame)
    for (uint32_t a : {0x00F2u, 0x00F4u, 0x00F6u, 0x00F8u}) std::swap(snes_.wram[a], snes_.wram[a + 1]);
    padSwapped_ = false;
  }
  if (coopTurn_ == 0) {
    // Mario's update done: Luigi's turn (in a level, playing).
    if (!coop_ || !gameplay_) return false;
    // (Not in the game's own two-player game ([the number of players] $072B,
    // [Player_Current] $0726: Luigi's turn is the game's, on controller 2.)
    if (ram(0x072b) >= 2 || ram(0x0726) != 0) {
      p2Valid_ = p2Drawn_ = false;
      return false;
    }
    coopTick();
    // (The game's player killed, the other playing on: he takes over. Not a
    // time-up, $BF 3: that's both.)
    const uint8_t bf = ram(0x00BF);
    if ((bf == 1 || bf == 2) && p2Valid_ && p2Drawn_ && !p2Wait_ && p2Dead_ < 0 && p2Area_ == areaStamp_ &&
        p2_.size() == (size_t)kVarCount && p2_[vix(0x00BF)] == 0)
      coopSwapRoles();
    if (ram(0x00BF) != 0) return false;
    if (ram(0x1206) != 0x09) lastTrack_ = ram(0x1206);
    if (ram(0x1206) != 0x09 && ram(0x1202) == 0 && musicBack_ == 0) lastApu_ = snes_.apu.saveState();
    if (p2Valid_ && p2Area_ != areaStamp_ && p2Dead_ >= 0) {  // (dying as Mario went on: out)
      p2Dead_ = -1;
      p2Wait_ = true;
    }
    if (p2Wait_) {
      // (Out: back with a life (theirs are shared), Mario on his feet.)
      p2Drawn_ = false;
      p2Area_ = areaStamp_;
      // (Not while the game's player is below the level: the airship's fall
      // to the King's room after the wand, with no ground under him, brought
      // Luigi back each frame there, out again at once, a life each time.)
      const int gy = ram(0x0070) | ram(0x0055) << 8;
      if (ram(0x0736) == 0 || ram(0x00A6) != 0 || (gy > 0x1a0 && gy < 0x8000)) return false;
      // (... nor off the view: an auto-scroll's edge would crush him at once)
      const int gx = ram(0x005e) | ram(0x0043) << 8, gcam = ram(0x0210) | ram(0x0211) << 8;
      if (gx - gcam < 8 - extL_ || gx - gcam > 248 + extR_) return false;
      wvar(0x0736)--;
      coopReturn();
    }
    // (A new tileset is a new area too, though the game's sweep doesn't run
    // for it: the airship's fall into the King's room. The other was left
    // where he was, below the airship, missing from the room.)
    if (ram(0x070a) != coopTs_) {
      if (coopTs_ != 0xff) p2Area_ = ~0u;
      coopTs_ = ram(0x070a);
    }
    if (!p2Valid_ || p2Area_ != areaStamp_) {
      coopSync();
      p2Area_ = areaStamp_;
      ground_[0][0] = ground_[1][0] = -1;
    }
    // A pipe being taken ([the pipe move] $0571, the game's one for both):
    // Luigi goes down (or along, or up) it with Mario, from his place.
    if (ram(0x0571) && p2Dead_ < 0) {
      for (uint32_t a : {0x005Eu, 0x0043u, 0x0070u, 0x0055u}) p2_[vix(a)] = ram(a);
      p2_[vix(0x008B)] = p2_[vix(0x009D)] = 0;
    }
    pipeBefore_ = ram(0x0571);
    // (The pipe move's timer is each player's ($0510, the walk-in, the sink,
    // the rise out); the move ($0571) the game's: his turn, his timer out,
    // ended the move each frame (the rise out ended at once, Mario left in
    // the pipe's body). During it his is the game's player's, and what his
    // turn does to the move and the exit it starts is undone after it.)
    if (pipeBefore_) p2_[vix(0x0510)] = ram(0x0510);
    // The airship's departure (Worlds 1-7: the castle's yard, the ship
    // taking off, its anchor to jump onto; background 7 in tileset $0A, see
    // kAirshipDeparture): the game walks its player there and lifts him once
    // one reaches the anchor. The other, ahead, set it off: both lifted short
    // of it. He goes along: the game's player's every move, a little behind,
    // his own form and colours, his controller not read.
    departure_ = ram(0x070a) == 0x0a && ram(0x0350) == 7 && ram(0x0727) < 7;
    if (departure_ && p2Valid_ && !p2Wait_ && p2Dead_ < 0) {
      for (int i = 0; i < kVarCount; i++) {
        const uint32_t a = kVars[i];
        if (a == 0x00BB || a == 0x0578 || (a >= 0x14E0 && a <= 0x1500) || a >= 0x7F0000) continue;
        p2_[i] = wvar(a);
      }
      int x = (ram(0x005e) | ram(0x0043) << 8) - 10;
      if (x < 8) x += 20;
      p2_[vix(0x005E)] = (uint8_t)x;
      p2_[vix(0x0043)] = (uint8_t)(x >> 8);
      p2_[vix(0x0079)] = (uint8_t)(ram(0x0079) + (x - (ram(0x005e) | ram(0x0043) << 8)));
    }
    for (int i = 0; i < 5; i++) pipeHold_[i] = ram(kPipeShared[i]);
    coopDeathBefore();
    // (Resting out of sight, left behind or brought along: blinking, as
    // after a hit, so nothing he can't see hurts him. In view he's in the
    // game as ever: one standing still waiting for the other still gets hit.)
    auto unseen = [&](int x, int y) {
      const int cam = ram(0x0210) | ram(0x0211) << 8, camY = ram(0x0543) | ram(0x0542) << 8;
      return x - cam < -16 - extL_ || x - cam > 256 + extR_ || y - camY < -32 || y - camY > 0xc0;
    };
    if (coopRests(0) && unseen(ram(0x005e) | ram(0x0043) << 8, (int16_t)(ram(0x0070) | ram(0x0055) << 8)) &&
        ram(0x0552) < 8)
      wvar(0x0552) = 8;
    if (coopRests(1) && p2_.size() == (size_t)kVarCount &&
        unseen(p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8, (int16_t)(p2_[vix(0x0070)] | p2_[vix(0x0055)] << 8)) &&
        p2_[vix(0x0552)] < 8)
      p2_[vix(0x0552)] = 8;
    if (ram(0x00A6) == 0 && !coopRests(0)) {  // (on the ground, playing)
      ground_[0][0] = ram(0x005e) | ram(0x0043) << 8;
      ground_[0][1] = ram(0x0070) | ram(0x0055) << 8;
    }
    p1_.resize(kVarCount);
    for (int i = 0; i < kVarCount; i++) p1_[i] = wvar(kVars[i]);
    for (int i = 0; i < kKeepCount; i++) keep_[i] = wvar(kKeep[i]);
    std::memcpy(p1Oam_, &snes_.wram[0x0800 + 4 * kOamFirst], sizeof(p1Oam_));
    std::memcpy(p1OamHi_, &snes_.wram[0x0a20 + kOamFirst], sizeof(p1OamHi_));
    // (The player's entries hidden for him: what his drawing leaves, blinking
    // after a hit or coming back, was Mario's, taken for his.)
    for (int k = 0; k < kOamCount; k++) snes_.wram[0x0801 + 4 * (kOamFirst + k)] = 0xf8;
    for (int i = 0; i < kVarCount; i++) wvar(kVars[i]) = p2_[i];
    wvar(0x0012) = p1_[vix(0x0012)];  // (the camera's: see shared)
    // His controller, where a two-player game keeps the second's ($20:FB70,
    // one byte up from the first's): the pad's high byte (B Y Select Start
    // Up Down Left Right) and low (A X L R), held and newly pressed; the
    // update reads the current player's ($23:DFDE: $F2 + [Player_Current]).
    // (The second slot's brother: Luigi, or Mario after the roles changed;
    // his controller in his place, [Player_Current] his.)
    const int who = roles_ ? 0 : 1;
    const uint16_t pad = departure_ ? 0 : snes_.input[who];
    p2Idle_ = pad || departure_ ? 0 : std::min(p2Idle_ + 1, 1 << 20);
    p1Idle_ = snes_.input[1 - who] || departure_ ? 0 : std::min(p1Idle_ + 1, 1 << 20);
    const uint8_t hi = (uint8_t)(pad >> 8), lo = (uint8_t)(pad & 0xf0);
    wvar(0x00F2 + who) = hi;
    wvar(0x00F4 + who) = lo;
    wvar(0x00F6 + who) = (uint8_t)(hi & ~padHi_[who]);
    wvar(0x00F8 + who) = (uint8_t)(lo & ~padLo_[who]);
    p2Press_ = (uint8_t)(hi & ~padHi_[who]);
    p2PadHi_ = hi;
    p2PadLo_ = lo;
    padHi_[who] = hi;
    padLo_[who] = lo;
    padHi_[1 - who] = (uint8_t)(snes_.input[1 - who] >> 8);
    padLo_[1 - who] = (uint8_t)(snes_.input[1 - who] & 0xf0);
    wvar(0x0726) = (uint8_t)who;
    for (int i = 0; i < kTickOnceCount; i++) tickHold_[i] = wvar(kTickOnce[i]);
    coinHeld_ = ram(0x0564) != 0;
    launchHold_ = ram(0x0572);
    stageHold_ = ram(0x055A);
    coopTurn_ = 1;
    r.pc = kPlayerCall;
    return true;
  }
  // Luigi's update done: his variables kept, his sprites and pose; Mario's back.
  coopTurn_ = 0;
  for (int i = 0; i < kTickOnceCount; i++)
    if (tickHold_[i] && wvar(kTickOnce[i]) == (uint8_t)(tickHold_[i] - 1)) wvar(kTickOnce[i]) = tickHold_[i];
  // (The departure's script ($20:E990-E9E7: the jump to the anchor, the
  // lift), its stage $055A stepped on in each player's update: his too, the
  // jump's stage ended at once, the game's player lifted from the ground
  // short of the anchor. His step undone.)
  if (departure_) wvar(0x055A) = stageHold_;
  // (A white note block's launch, its speed $0572 from $80 counted up each
  // update ($20:E880/E88A): his too, twice as fast, and it never reached the
  // top, the bonus area. His count undone; his reaching the top ($D0) stands.)
  if (launchHold_ & 0x80) {
    const uint8_t d = (uint8_t)(wvar(0x0572) - launchHold_);
    if (d == 1 || d == 2) wvar(0x0572) = launchHold_;
  }
  // (The star's somersault ends for the game's player in the objects' code
  // ($27:9091: in water, sliding or on the ground), his only: for him here.)
  if (wvar(0x054F) && (wvar(0x0575) || wvar(0x058A) || !wvar(0x00A6))) wvar(0x054F) = 0;
  for (int i = 0; i < kVarCount; i++) p2_[i] = wvar(kVars[i]);
  coopDeathAfter();
  if (p2_[vix(0x00A6)] == 0 && p2_[vix(0x00BF)] == 0 && !coopRests(1)) {
    ground_[1][0] = p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8;
    ground_[1][1] = p2_[vix(0x0070)] | p2_[vix(0x0055)] << 8;
  }
  std::memcpy(p2Oam_, &snes_.wram[0x0800 + 4 * kOamFirst], sizeof(p2Oam_));
  std::memcpy(p2OamHi_, &snes_.wram[0x0a20 + kOamFirst], sizeof(p2OamHi_));
  // (His update scrolled the camera his way, and drew him for it; the
  // camera is Mario's: each sprite at its place on him, from his position
  // on Mario's screen. The game's 9th X bit is for its own screen: not used.)
  {
    const int camHis = wvar(0x0210) | wvar(0x0211) << 8, camMario = keep_[10] | keep_[11] << 8;
    const int px = wvar(0x005E) | wvar(0x0043) << 8;
    // (and his own vertical camera, $0542/$0543: kept with his variables)
    const int dy = (int16_t)((wvar(0x0543) | wvar(0x0542) << 8) - (p1_[vix(0x0543)] | p1_[vix(0x0542)] << 8));
    for (int k = 0; k < kOamCount; k++) {
      const int off = (int8_t)(uint8_t)(p2Oam_[4 * k] - (px - camHis));
      p2X_[k] = (int16_t)(px - camMario + off);
      p2Oam_[4 * k] = (uint8_t)p2X_[k];
      p2OamHi_[k] = (uint8_t)((p2OamHi_[k] & ~1) | (p2X_[k] >> 8 & 1));
      if (dy && p2Oam_[4 * k + 1] < 0xf0) {
        const int y = p2Oam_[4 * k + 1] + dy;
        p2Oam_[4 * k + 1] = (uint8_t)(y >= -16 && y < 0xe0 ? y : 0xf0);
      }
    }
  }
  if (wvar(0x0238)) {
    // (his pose's tiles to stream: six 64-byte blocks from bank $0238 to VRAM $6000, see $20:F650)
    const uint32_t bank = wvar(0x0238);
    for (int k = 0; k < 6; k++) {
      uint32_t src = bank << 16 | wvar(0x0220 + 2 * k) | wvar(0x0221 + 2 * k) << 8;
      for (int i = 0; i < 32; i++) p2Tiles_[32 * k + i] = (uint16_t)(snes_.peek(src + 2 * i) | snes_.peek(src + 2 * i + 1) << 8);
    }
  }
  // His colours, as the game picks the player's palette ($23:C467, only when
  // it changes): Luigi's own small, big or raccoon (index 1) and fire (8);
  // the other suits look the same on both; from the table at $23:C451 (bank $3C).
  playerPalette(roles_ ? 0 : 1, p2_[vix(0x00BB)], p2_[vix(0x057A)] != 0, p2Pal_);
  p2Drawn_ = true;
  for (int i = 0; i < kVarCount; i++) wvar(kVars[i]) = p1_[i];
  if (pipeBefore_) {
    for (int i = 0; i < 5; i++) wvar(kPipeShared[i]) = pipeHold_[i];
  } else if (ram(0x0571)) {
    wvar(0x0510) = p2_[vix(0x0510)];  // (he took a pipe: its timer the game's player's too, see above)
  }
  for (int i = 0; i < kKeepCount; i++) wvar(kKeep[i]) = keep_[i];
  std::memcpy(&snes_.wram[0x0800 + 4 * kOamFirst], p1Oam_, sizeof(p1Oam_));
  std::memcpy(&snes_.wram[0x0a20 + kOamFirst], p1OamHi_, sizeof(p1OamHi_));
  // (Auto-scrolling: the game holds the player at its view's edges to the
  // scroll's speed ($27:86BC, once a frame, Mario's); Luigi the same, as the
  // wide view's edges do for Mario: see autoscrollBounds.)
  if (autoscroll() && !vertical_ && p2Dead_ < 0) {
    const int cam = keep_[10] | keep_[11] << 8, levelEnd = (ram(0x0022) + 1) * 256;
    const int edgeL = std::max(cam - extL_, 0), edgeR = std::min(cam + 256 + extR_, levelEnd);
    int x = p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8;
    const bool left = x - edgeL <= 0x0f, right = x - edgeR >= -0x20;
    // (Resting at its left edge: brought to where the other last stood, as
    // at the camera's edge (see coopBring); pushed on, he went off his
    // platform into the gap below, a life.)
    auto bring = [&](std::vector<uint8_t>* v, int who) {
      const int* g = ground_[1 - who];
      if (g[0] < 0 || g[0] - cam < 8 - extL_ || g[0] - cam > 248 + extR_) return false;
      auto put = [&](uint32_t a, uint8_t b) { (v ? (*v)[vix(a)] : wvar(a)) = b; };
      put(0x005E, (uint8_t)g[0]);
      put(0x0043, (uint8_t)(g[0] >> 8));
      put(0x0070, (uint8_t)g[1]);
      put(0x0055, (uint8_t)(g[1] >> 8));
      put(0x008B, 0);
      put(0x009D, 0);
      put(0x0079, (uint8_t)(g[0] - cam));
      return true;
    };
    if (left && coopRests(1) && bring(&p2_, 1)) {
      x = p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8;
    } else if (const int gx = ram(0x005e) | ram(0x0043) << 8; gx - edgeL <= 0x0f && coopRests(0) && ram(0x00BF) == 0) {
      bring(nullptr, 0);
    }
    const bool left2 = x - edgeL <= 0x0f;
    if (left2 || right) {
      const bool pinned = right ? cam + 256 + extR_ >= levelEnd : cam - extL_ <= 0;
      const int v = pinned ? 0 : (int8_t)ram(0x1a0e), m = (int8_t)p2_[vix(0x008B)];
      if (left2 ? m < v : m > v) p2_[vix(0x008B)] = (uint8_t)v;
      if (left2 && x < edgeL) x = edgeL;
      if (right && x > edgeR - 0x20) x = edgeR - 0x20;
      p2_[vix(0x005E)] = (uint8_t)x;
      p2_[vix(0x0043)] = (uint8_t)(x >> 8);
    }
  }
  // (He took a pipe: Mario goes down it with him.)
  if (!pipeBefore_ && ram(0x0571) && p2Dead_ < 0) {
    for (uint32_t a : {0x005Eu, 0x0043u, 0x0070u, 0x0055u}) wvar(a) = p2_[vix(a)];
    wvar(0x008B) = wvar(0x009D) = 0;
  }
  // His dying: his update's (the jump, the fall); then out (see above).
  // (Below the level with no death of his own, as his update's view
  // followed him: out too.)
  const int p2y = p2_[vix(0x0070)] | p2_[vix(0x0055)] << 8;
  if (p2Dead_ >= 0 && ++p2Dead_ >= 160) p2Wait_ = true;
  // (Not with the game's player down there too: the airship's fall to the
  // King's room after the wand; he comes along, beside him in the next area.)
  const int gy = ram(0x0070) | ram(0x0055) << 8;
  if (p2Dead_ < 0 && !vertical_ && p2y > 0x1c0 && p2y < 0x8000 && !(gy > 0x1a0 && gy < 0x8000)) p2Wait_ = true;
  if (p2Wait_) {
    p2Dead_ = -1;
    p2Drawn_ = false;
  }
  return false;
}

void Smb3Engine::coopCheats(uint32_t f) {
  if (!coop_ || !p2Valid_ || p2_.size() != (size_t)kVarCount || coopMidTurn()) return;
  if ((f & kCheatStar) && p2_[vix(0x0553)] < 0x40) p2_[vix(0x0553)] = 0x40;
  if ((f & kCheatFlight) && (p2_[vix(0x00BB)] == 3 || p2_[vix(0x00BB)] == 5)) p2_[vix(0x056E)] = 0xff;
  if (f & kCheatPMeter) p2_[vix(0x0413)] = 0x7f;
}

// In the middle of a turn with Luigi's variables in (Mario's kept aside): a
// frame the game overran ends there. Not a moment for a save state or
// rewind's snapshot (see the app).
bool Smb3Engine::coopMidTurn() const {
  return coopTurn_ != 0 || objSwapped_ >= 0 || fireRet_ || platPhase_ || tilePhase_ || carryPhase_;
}

// A platform carries one player: its update meets the nearer only, and the
// other (on it too, or alone on it while the other stood nearer) fell
// through. With the other near it, its update runs again from the world as
// it was before, his variables in (Mario's if Luigi had it); if that run
// stood him on it ($28:B8C5, the game's for every platform: the player's Y
// on its top, on the ground, carried by its move), he keeps what it did to
// him. The world stays as the first run left it, or, the other alone on it,
// as his run did (a lift that drops under its rider drops).
void Smb3Engine::coopPlatBefore() {
  platPhase_ = 0;
  standHit_ = false;
  const int slot = snes_.cpu.r.x & 0xff;
  const uint8_t* w = snes_.wram;
  if (slot > 7 || !coop_ || !p2Valid_ || !p2Drawn_ || p2Wait_ || p2Dead_ >= 0 || !gameplay_ || p2Area_ != areaStamp_ ||
      p2_.size() != (size_t)kVarCount || ram(0x00BF) != 0 || p2_[vix(0x00BF)] != 0)
    return;
  const uint8_t st = w[0x0661 + slot];
  if (st == 0 || st == 4) return;  // (free; held)
  // (the other: not the one in for it)
  const bool farMario = objSwapped_ >= 0;
  if (farMario && objSave_.size() != (size_t)kVarCount) return;
  auto fv = [&](uint32_t a) { return farMario ? objSave_[vix(a)] : p2_[vix(a)]; };
  const int fx = fv(0x005E) | fv(0x0043) << 8, fy = (int16_t)(fv(0x0070) | fv(0x0055) << 8);
  const int ox = w[0x005f + slot] | w[0x0044 + slot] << 8, oy = (int16_t)(w[0x0071 + slot] | w[0x0056 + slot] << 8);
  if (std::abs(fx - ox) >= 64 || fy > oy + 16 || fy < oy - 96) return;
  platBefore_.assign(w, w + 0x2000);
  platSlot_ = slot;
  platPhase_ = 1;
}

namespace {
void restoreLow(uint8_t* w, const std::vector<uint8_t>& from) {  // (but the stack's page)
  std::copy(from.begin(), from.begin() + 0x100, w);
  std::copy(from.begin() + 0x200, from.end(), w + 0x200);
}
}  // namespace

bool Smb3Engine::coopPlatAfter() {
  CpuRegs& r = snes_.cpu.r;
  uint8_t* w = snes_.wram;
  if (platPhase_ == 1) {
    platHit1_ = standHit_;
    standHit_ = false;
    platPhase_ = 0;
    if (w[0x0661 + platSlot_] == 0) return false;  // (gone)
    platAfter_.assign(w, w + 0x2000);
    platNear_.resize(kVarCount);
    for (int i = 0; i < kVarCount; i++) platNear_[i] = wvar(kVars[i]);
    restoreLow(w, platBefore_);
    platFarMario_ = objSwapped_ >= 0;
    if (platFarMario_) {
      for (int i = 0; i < kVarCount; i++)
        if (!shared(kVars[i])) wvar(kVars[i]) = objSave_[i];
      wvar(0x0017) = objPad_[0];
      wvar(0x0018) = objPad_[1];
    } else {
      const int camX = (ram(0x005e) - ram(0x0079)) & 0xff, camY = (ram(0x0070) - ram(0x0082)) & 0xff;
      for (int i = 0; i < kVarCount; i++)
        if (!shared(kVars[i])) wvar(kVars[i]) = p2_[i];
      wvar(0x0079) = (uint8_t)(wvar(0x005E) - camX);
      wvar(0x0082) = (uint8_t)(wvar(0x0070) - camY);
      wvar(0x0017) = p2PadHi_;
      wvar(0x0018) = p2Press_;
    }
    // JSR (the object's update) again, returning to kObjUpdated
    auto push = [&](uint8_t v) {
      w[r.sp & 0x1fff] = v;
      r.sp = r.e ? (uint16_t)(0x100 | ((r.sp - 1) & 0xff)) : (uint16_t)(r.sp - 1);
    };
    const uint16_t ret = (uint16_t)(platRet_ - 1);
    push(ret >> 8);
    push(ret & 0xff);
    r.x = (uint16_t)platSlot_;
    r.pc = platCall_;
    platPhase_ = 2;
    return true;
  }
  if (platPhase_ != 2) return false;
  platPhase_ = 0;
  const bool hit2 = standHit_;
  standHit_ = false;
  if (!hit2) {
    restoreLow(w, platAfter_);
    for (int i = 0; i < kVarCount; i++) wvar(kVars[i]) = platNear_[i];
    return false;
  }
  std::vector<uint8_t> farV(kVarCount);
  for (int i = 0; i < kVarCount; i++) farV[i] = wvar(kVars[i]);
  if (platHit1_) restoreLow(w, platAfter_);
  for (int i = 0; i < kVarCount; i++) wvar(kVars[i]) = platNear_[i];
  wvar(0x0017) = platAfter_[0x0017];
  wvar(0x0018) = platAfter_[0x0018];
  for (int i = 0; i < kVarCount; i++) {
    if (shared(kVars[i])) continue;
    if (platFarMario_) objSave_[i] = farV[i];
    else p2_[i] = farV[i];
  }
  return false;
}

// An auto-scroll's hold on the player ($27:8697, from $27:8572 once a frame:
// within the view, carried along by the scroll when he moves its way, or
// riding it): the game's player's only, Luigi walked half a pixel a frame
// slower than Mario. Again with his variables in; what it sets that's the
// level's goes back.
bool Smb3Engine::coopAutoCarry() {
  CpuRegs& r = snes_.cpu.r;
  static const uint32_t kKeepIt[5] = {0x05FC, 0x0427, 0x1A14, 0x1A07, 0x074E};
  if (carryPhase_) {
    carryPhase_ = false;
    for (int i = 0; i < 5; i++) wvar(kKeepIt[i]) = carryHold_[i];
    coopObjAfter();
    return false;
  }
  if (!coop_ || !p2Valid_ || !p2Drawn_ || p2Wait_ || p2Dead_ >= 0 || !gameplay_ || p2Area_ != areaStamp_ ||
      objSwapped_ >= 0 || p2_.size() != (size_t)kVarCount || ram(0x00BF) != 0 || p2_[vix(0x00BF)] != 0 || !r.mf || !r.xf)
    return false;
  for (int i = 0; i < 5; i++) carryHold_[i] = ram(kKeepIt[i]);
  coopSwapIn(201);
  auto push = [&](uint8_t v) {
    snes_.wram[r.sp & 0x1fff] = v;
    r.sp = r.e ? (uint16_t)(0x100 | ((r.sp - 1) & 0xff)) : (uint16_t)(r.sp - 1);
  };
  push(0x85);
  push(0x74);
  r.pc = 0x8697;
  carryPhase_ = true;
  return true;
}

// The tiles under the player's feet ($27:B000, from $27:B299 once a frame:
// lava, a donut lift's block, ...): the game's player's only. Luigi didn't
// set a donut lift off, and sank in lava. Again with his variables in (a JSR
// to it, back here at $27:B29F).
bool Smb3Engine::coopTiles() {
  CpuRegs& r = snes_.cpu.r;
  if (tilePhase_) {
    tilePhase_ = false;
    coopObjAfter();
    return false;
  }
  if (!coop_ || !p2Valid_ || !p2Drawn_ || p2Wait_ || p2Dead_ >= 0 || !gameplay_ || p2Area_ != areaStamp_ ||
      objSwapped_ >= 0 || p2_.size() != (size_t)kVarCount || ram(0x00BF) != 0 || p2_[vix(0x00BF)] != 0 || !r.mf || !r.xf)
    return false;
  coopSwapIn(200);
  auto push = [&](uint8_t v) {
    snes_.wram[r.sp & 0x1fff] = v;
    r.sp = r.e ? (uint16_t)(0x100 | ((r.sp - 1) & 0xff)) : (uint16_t)(r.sp - 1);
  };
  push(0xb2);
  push(0x9e);
  r.pc = 0xb000;
  tilePhase_ = true;
  return true;
}

// The objects' run starts ($27:8FF4): what was held last frame for the
// player's update to see ($06A5 = $06A4, cleared for the objects to set
// again); the game's for Mario, this for Luigi's.
void Smb3Engine::coopHoldCopy() {
  if (!coop_ || p2_.size() != (size_t)kVarCount) return;
  p2_[vix(0x06A5)] = p2_[vix(0x06A4)];
  p2_[vix(0x06A4)] = 0;
}

// Each object's update (and tail check) meets one player: the nearer one.
// For Luigi, his variables are in for it, his screen position ($79/$82) put
// on Mario's camera, so what it does to the player (a stomp's bounce, a hit,
// a power-up, a shell picked up) is done to him. One held stays with whoever
// holds it.
void Smb3Engine::coopObjBefore() {
  const int slot = snes_.cpu.r.x & 0xff;
  if (slot > 7) return;
  const uint8_t* w = snes_.wram;
  bool luigi = false;
  if (coop_ && p2Valid_ && p2Drawn_ && !p2Wait_ && gameplay_ && p2Area_ == areaStamp_ && objSwapped_ < 0 && w[0x0661 + slot] &&
      ram(0x00BF) == 0) {
    auto v = [&](uint32_t a) { return (int)p2_[std::find(kVars, kVars + kVarCount, a) - kVars]; };
    if (v(0x00BF) == 0) {
      if (w[0x0661 + slot] == 4) {
        luigi = objOwner_[slot];  // (held)
      } else {
        const int ox = w[0x005f + slot] | w[0x0044 + slot] << 8, oy = w[0x0071 + slot] | w[0x0056 + slot] << 8;
        const int mx = ram(0x005e) | ram(0x0043) << 8, my = ram(0x0070) | ram(0x0055) << 8;
        const int lx = v(0x005E) | v(0x0043) << 8, ly = v(0x0070) | v(0x0055) << 8;
        // (straight-line distance: by the larger of the two alone, a far height
        // made them equal, Mario's then; a wand above both was never Luigi's)
        auto d2 = [](int dx, int dy) { return (int64_t)dx * dx + (int64_t)dy * dy; };
        const int rest = coopIdleOne();
        luigi = rest >= 0 ? rest == 0 : d2(ox - lx, (int16_t)(oy - ly)) < d2(ox - mx, (int16_t)(oy - my));
      }
    }
  }
  objOwner_[slot] = luigi;
  if (luigi) coopSwapIn(slot);
}

// The special objects (enemies' fireballs and hammers, boomerangs, a
// Koopaling's wand...: $1FC8,X, ten of them, their own test against the
// player at $27:D302, on 8-bit positions $05C9,X / $05BF,X): the same.
void Smb3Engine::coopSpecBefore() {
  const int slot = snes_.cpu.r.x & 0xff;
  if (slot > 9 || !coop_ || !p2Valid_ || !p2Drawn_ || p2Wait_ || p2Dead_ >= 0 || !gameplay_ || p2Area_ != areaStamp_ ||
      objSwapped_ >= 0 || !ram(0x1fc8 + slot) || ram(0x00BF) != 0 || p2_.size() != (size_t)kVarCount)
    return;
  // (X whole, its high byte $E2,X: by the low bytes, a brother 256 away was
  // as near as one beside it, and the game tests only those (see kSpecHitX))
  const int ox = ram(0x00e2 + slot) << 8 | ram(0x05c9 + slot), oy = ram(0x05bf + slot);
  auto d2 = [](int dx, int dy) { return (int64_t)dx * dx + (int64_t)dy * dy; };
  const int rest = coopIdleOne();
  if (rest >= 0 ? rest == 0
                : d2(ox - (p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8), (int8_t)(oy - p2_[vix(0x0070)])) <
                      d2(ox - (ram(0x005e) | ram(0x0043) << 8), (int8_t)(oy - ram(0x0070)))) {
    specKind_ = ram(0x1fc8 + slot);
    coopSwapIn(100 + slot);
  }
}

// Luigi's variables in for an object's turn (his screen position on Mario's
// camera, his controller), Mario's kept (see coopObjAfter).
void Smb3Engine::coopSwapIn(int id) {
  objSave_.resize(kVarCount);
  for (int i = 0; i < kVarCount; i++) objSave_[i] = wvar(kVars[i]);
  // (Mario's screen position gives the camera now; Luigi's on it.)
  const int camX = (ram(0x005e) - ram(0x0079)) & 0xff, camY = (ram(0x0070) - ram(0x0082)) & 0xff;
  for (int i = 0; i < kVarCount; i++)
    if (!shared(kVars[i])) wvar(kVars[i]) = p2_[i];
  wvar(0x0079) = (uint8_t)(wvar(0x005E) - camX);
  wvar(0x0082) = (uint8_t)(wvar(0x0070) - camY);
  // (his controller too: a shell held while Y is)
  objPad_[0] = ram(0x0017);
  objPad_[1] = ram(0x0018);
  wvar(0x0017) = p2PadHi_;
  wvar(0x0018) = p2Press_;
  objSwapped_ = id;
  coopDeathBefore();
}

void Smb3Engine::coopObjAfter() {
  if (objSwapped_ < 0) return;
  // (The level's end an object starts in its run with Luigi in ([the exit]
  // $0014, $0713: Boom Boom's orb's count over, $28:D6E5) is the game's:
  // kept in his variables, the level never ended.)
  const uint8_t ex[2] = {wvar(0x0014), wvar(0x0713)}, was[2] = {p2_[vix(0x0014)], p2_[vix(0x0713)]};
  for (int i = 0; i < kVarCount; i++) {
    if (!shared(kVars[i])) p2_[i] = wvar(kVars[i]);
    wvar(kVars[i]) = objSave_[i];
  }
  // (The Koopaling's wand ($1FC8 kind $10) caught by Luigi: the scene after
  // it (held up, the fall to the King's room, the King) is the game's
  // player's: Luigi's now, Mario the other. It showed Mario with the wand.)
  const int slot = objSwapped_ >= 100 ? objSwapped_ - 100 : -1;
  const bool wand = slot >= 0 && slot < 10 && specKind_ == 0x10 && ram(0x1fc8 + slot) != 0x10 && p2_[vix(0x00BF)] == 0;
  specKind_ = 0;
  if (p2_[vix(0x00BF)] == 0) {
    const uint32_t at[2] = {0x0014, 0x0713};
    for (int k = 0; k < 2; k++)
      if (ex[k] != was[k]) {
        wvar(at[k]) = ex[k];
        p2_[vix(at[k])] = was[k];
      }
  }
  wvar(0x0017) = objPad_[0];
  wvar(0x0018) = objPad_[1];
  objSwapped_ = -1;
  coopDeathAfter();
  if (wand) coopTakeOver();
}

// The other player becomes the game's (their variables exchanged), as for
// a death (see coopSwapRoles) but nobody died: a scene that's his.
void Smb3Engine::coopTakeOver() {
  for (int i = 0; i < kVarCount; i++) {
    if (shared(kVars[i])) continue;
    const uint8_t t = wvar(kVars[i]);
    wvar(kVars[i]) = p2_[i];
    p2_[i] = t;
  }
  for (int k = 0; k < 2; k++) std::swap(ground_[0][k], ground_[1][k]);
  std::swap(p1Idle_, p2Idle_);
  roles_ = !roles_;
  const int ctl = roles_ ? 1 : 0;
  const uint8_t hi = (uint8_t)(snes_.input[ctl] >> 8);
  wvar(0x0017) = hi;
  wvar(0x0018) = (uint8_t)(hi & ~padHi_[ctl]);
  // (Their drawings too: the game holds still after the wand (no update,
  // nobody drawn again), and its player's sprites stood where Mario was in
  // Luigi's pose. The game's slot gets his last, the other's this frame's:
  // each on the camera from his own place, his own pose's tiles, colours.)
  uint8_t oam[sizeof(p2Oam_)], oamHi[sizeof(p2OamHi_)];
  std::memcpy(oam, p2Oam_, sizeof(oam));
  std::memcpy(oamHi, p2OamHi_, sizeof(oamHi));
  std::memcpy(p2Oam_, &snes_.wram[0x0800 + 4 * kOamFirst], sizeof(p2Oam_));
  std::memcpy(p2OamHi_, &snes_.wram[0x0a20 + kOamFirst], sizeof(p2OamHi_));
  std::memcpy(&snes_.wram[0x0800 + 4 * kOamFirst], oam, sizeof(oam));
  std::memcpy(&snes_.wram[0x0a20 + kOamFirst], oamHi, sizeof(oamHi));
  const int cam = ram(0x0210) | ram(0x0211) << 8, px = p2_[vix(0x005E)] | p2_[vix(0x0043)] << 8;
  for (int k = 0; k < kOamCount; k++) {
    const int off = (int8_t)(uint8_t)(p2Oam_[4 * k] - (px - cam));
    p2X_[k] = (int16_t)(px - cam + off);
    p2OamHi_[k] = (uint8_t)((p2OamHi_[k] & ~1) | (p2X_[k] >> 8 & 1));
  }
  if (const uint32_t bank = p2_[vix(0x0238)])
    for (int k = 0; k < 6; k++) {
      const uint32_t src = bank << 16 | p2_[vix(0x0220 + 2 * k)] | p2_[vix(0x0221 + 2 * k)] << 8;
      for (int i = 0; i < 32; i++) p2Tiles_[32 * k + i] = (uint16_t)(snes_.peek(src + 2 * i) | snes_.peek(src + 2 * i + 1) << 8);
    }
  playerPalette(roles_ ? 0 : 1, p2_[vix(0x00BB)], p2_[vix(0x057A)] != 0, p2Pal_);
  p2Drawn_ = true;
}

// The throw (fire, hammer), past Mario's test ($27:B2DC): Luigi's, with his
// variables and controller in, as a JSR to the game's ($B3A3), back here.
bool Smb3Engine::coopFire() {
  CpuRegs& r = snes_.cpu.r;
  if (fireRet_) {
    fireRet_ = false;
    for (int k = 0; k < 2; k++)
      if (!projBefore_[k] && ram(0x1ce1 + k)) projOwner_[k] = 1;  // (his throw)
    for (int i = 0; i < kVarCount; i++) {
      if (!shared(kVars[i])) p2_[i] = wvar(kVars[i]);
      wvar(kVars[i]) = fireSave_[i];
    }
    wvar(0x0017) = firePad_[0];
    wvar(0x0018) = firePad_[1];
    return false;
  }
  if (!coop_ || !gameplay_ || !p2Valid_ || p2Wait_ || p2Dead_ >= 0 || p2Area_ != areaStamp_ || objSwapped_ >= 0 ||
      p2_.size() != (size_t)kVarCount || !r.mf || !r.xf)
    return false;
  const int suit = p2_[vix(0x00BB)];
  if ((suit != 2 && suit != 6) || !(p2Press_ & 0x40)) return false;
  if (ram(0x0556) | p2_[vix(0x056F)] | ram(0x0557) | p2_[vix(0x058C)] | p2_[vix(0x009C)]) return false;
  fireSave_.resize(kVarCount);
  for (int i = 0; i < kVarCount; i++) fireSave_[i] = wvar(kVars[i]);
  const int camX = (ram(0x005e) - ram(0x0079)) & 0xff, camY = (ram(0x0070) - ram(0x0082)) & 0xff;
  for (int i = 0; i < kVarCount; i++)
    if (!shared(kVars[i])) wvar(kVars[i]) = p2_[i];
  wvar(0x0079) = (uint8_t)(wvar(0x005E) - camX);
  wvar(0x0082) = (uint8_t)(wvar(0x0070) - camY);
  firePad_[0] = ram(0x0017);
  firePad_[1] = ram(0x0018);
  wvar(0x0017) = p2PadHi_;
  wvar(0x0018) = p2Press_;
  for (int k = 0; k < 2; k++) projBefore_[k] = ram(0x1ce1 + k);
  // (JSR $B3A3 from $B2D9: its return address, $B2DB, on the stack)
  auto push = [&](uint8_t v) {
    snes_.wram[r.sp & 0x1fff] = v;
    r.sp = r.e ? (uint16_t)(0x100 | ((r.sp - 1) & 0xff)) : (uint16_t)(r.sp - 1);
  };
  push(0xb2);
  push(0xdb);
  r.pc = 0xb3a3;
  fireRet_ = true;
  return true;
}

// A projectile of the player's ($27:B436, X its slot, 0-1): a hammer is drawn
// and flies as one by its thrower's form ($27:B5AB: the player's suit), so it
// runs with his variables in; a free slot is the game's player's next.
void Smb3Engine::coopProjBefore() {
  const int slot = snes_.cpu.r.x & 0xff;
  if (slot > 1) return;
  if (!ram(0x1ce1 + slot)) projOwner_[slot] = 0;
  if (!projOwner_[slot] || !coop_ || !gameplay_ || !p2Valid_ || objSwapped_ >= 0 || p2_.size() != (size_t)kVarCount)
    return;
  coopSwapIn(200 + slot);
}

// Out of the level (the map, a game over): a death or a wait of the second
// slot's ends with it. The game's own death took its life; he starts the next
// level small beside the game's player, with nothing more to pay. (A fall not
// yet counted, off the level's end in the goal walk: just over, no life, his
// suit kept; else the next level's first frame killed him.)
void Smb3Engine::coopLevelOver() {
  if (gameplay_ || p2_.size() != (size_t)kVarCount) return;
  // (On the map ([tileset] 0): the next level's player update runs once
  // before its area is built (areaStamp_ unchanged): he's placed beside the
  // game's player for it, not left where the last level had him.)
  if (ram(0x070a) == 0) p2Area_ = ~0u;
  if (p2Dead_ < 0 && !p2Wait_) {
    p2_[vix(0x00BF)] = 0;
    return;
  }
  p2Dead_ = -1;
  p2Wait_ = false;
  p2_[vix(0x00BF)] = 0;
  p2_[vix(0x00BB)] = 0;
  p2_[vix(0x0578)] = 1;
  p2_[vix(0x0552)] = 0;
  musicBack_ = 0;
}

// As the frame goes to the screen: Luigi's sprites into free entries.
void Smb3Engine::coopOam(Ppu& ppu) {
  if (coop_) coopLevelOver();
  // (Out of the level: his last pose is forgotten. The next level's iris
  // opens before its area is built (the same as the last's, re-entered after
  // both died): he'd stand there as he last was, mid-death.)
  if (!gameplay_) p2Drawn_ = false;
  coopMask_[0] = coopMask_[1] = 0;  // (applied on each line: see beginLine)
  coopMask2_[0] = coopMask2_[1] = 0;
  ppu.objAltTileWords = 0;
  // (Luigi in the game's slot: its sprites in his colours, whatever the game
  // loaded for its player, which it picks as Mario's)
  if (coop_ && roles_ && gameplay_) {
    playerPalette(1, ram(0x00BB), ram(0x057A) != 0, slotPal_);
    std::copy(slotPal_, slotPal_ + 16, ppu.objAlt2Colors);
    for (int k = 0; k < kOamCount; k++) coopMask2_[(kOamFirst + k) >> 6] |= 1ull << ((kOamFirst + k) & 63);
  }
  // (The game leaves entries it doesn't use as they were: last frame's copy
  // of him, still there, is hidden; else it stood where he was, taking the
  // entries.)
  for (int i = 0; i < usedCount_; i++) {
    uint8_t* e = &snes_.wram[0x0800 + 4 * used_[i]];
    if (std::memcmp(e, &usedBytes_[4 * i], 4) == 0) e[1] = 0xf8;
  }
  usedCount_ = 0;
  // (The map's WORLD box (see boxFrame): Mario's sprite in it ($84,$60, 16x16)
  // moved left, a copy of it beside him in Luigi's colours.)
  if (coop_ && boxShown_ && !gameplay_) {
    uint8_t* w = snes_.wram;
    for (int s = 0; s < 128; s++) {
      uint8_t* e = &w[0x0800 + 4 * s];
      if (e[1] != 0x60 || (e[0] != 0x84 && e[0] != 0x58) || !(w[0x0a20 + s] & 2)) continue;
      e[0] = 0x58;
      ppu.objTrueXSet[s] = false;
      for (int t = 127; t >= 0; t--) {
        if (t == s || w[0x0801 + 4 * t] < 0xf0) continue;
        std::memcpy(&w[0x0800 + 4 * t], e, 4);
        w[0x0800 + 4 * t] = 0x70;
        w[0x0a20 + t] = w[0x0a20 + s];
        ppu.objTrueXSet[t] = false;
        used_[0] = (uint8_t)t;
        std::memcpy(&usedBytes_[0], &w[0x0800 + 4 * t], 4);
        usedCount_ = 1;
        coopMask_[t >> 6] |= 1ull << (t & 63);
        playerPalette(1, 0, false, ppu.objAltColors);
        break;
      }
      break;
    }
  }
  // (The King's room ($29:C280's scene: no player update, the game's player
  // drawn by it): the other beside him, a copy of his sprites in the other's
  // colours. He was left behind on the airship, missing from the room.)
  const bool king = kingFrame_;
  kingFrame_ = false;
  if (coop_ && king && p2Valid_ && !p2Wait_ && p2_.size() == (size_t)kVarCount) {
    uint8_t* w = snes_.wram;
    int t = 127;
    for (int k = 0; k < kOamCount; k++) {
      const int s = kOamFirst + k;
      const uint8_t* e = &w[0x0800 + 4 * s];
      if (e[1] >= 0xf0) continue;
      while (t >= 0 && ((t >= kOamFirst && t < kOamFirst + kOamCount) || w[0x0801 + 4 * t] < 0xf0)) t--;
      if (t < 0) break;
      const int x9 = e[0] | (w[0x0a20 + s] & 1) << 8, x = (x9 >= 256 + 64 ? x9 - 512 : x9);
      const int nx = x - 20 >= -8 ? x - 20 : x + 20;
      std::memcpy(&w[0x0800 + 4 * t], e, 4);
      w[0x0800 + 4 * t] = (uint8_t)nx;
      w[0x0a20 + t] = (uint8_t)((w[0x0a20 + s] & ~1) | (nx >> 8 & 1));
      ppu.objTrueXSet[t] = false;
      used_[usedCount_] = (uint8_t)t;
      std::memcpy(&usedBytes_[4 * usedCount_++], &w[0x0800 + 4 * t], 4);
      coopMask_[t >> 6] |= 1ull << (t & 63);
      t--;
    }
    playerPalette(roles_ ? 0 : 1, p2_[vix(0x00BB)], false, ppu.objAltColors);
    return;
  }
  // (His last pose stays up on frames his update doesn't run: Mario hurt, the game frozen.)
  if (!coop_ || !p2Drawn_ || !gameplay_ || p2Area_ != areaStamp_) return;
  int put = 0;
  oamNeed_ = oamPut_ = 0;
  for (int k = 0; k < kOamCount; k++) oamNeed_ += p2Oam_[4 * k + 1] < 0xf0;
  for (int s = 127; s >= 0 && put < kOamCount; s--) {
    if (s >= kOamFirst && s < kOamFirst + kOamCount) continue;
    if (ram(0x0801 + 4 * s) < 0xf0) continue;  // (in use)
    // the next of his that's shown
    while (put < kOamCount && p2Oam_[4 * put + 1] >= 0xf0) put++;
    if (put >= kOamCount) break;
    std::memcpy(&snes_.wram[0x0800 + 4 * s], &p2Oam_[4 * put], 4);
    used_[usedCount_] = (uint8_t)s;
    std::memcpy(&usedBytes_[4 * usedCount_++], &p2Oam_[4 * put], 4);
    snes_.wram[0x0a20 + s] = p2OamHi_[put];
    ppu.objTrueX[s] = p2X_[put];
    ppu.objTrueXSet[s] = true;
    coopMask_[s >> 6] |= 1ull << (s & 63);
    put++;
    oamPut_++;
  }
  std::copy(p2Pal_, p2Pal_ + 16, ppu.objAltColors);
  std::copy(p2Tiles_, p2Tiles_ + 192, ppu.objAltTiles);
  ppu.objAltTileBase = 0x6000;
  ppu.objAltTileWords = 192;
}

// The title's fourth choice, CO-OP GAME, under 1 PLAYER GAME / 2 PLAYER GAME
// / BATTLE GAME: a one player game ([the menu's choice] $072B 0) with Luigi
// on controller 2. Its input ($20:B7EC): Select steps through the four, Up
// and Down move; the game's cursor sprite ($09F0) on the fourth row ($B7,
// after its 3 rows' $8F/$9F/$AF). The row itself is written to BG1's tile
// map while the menu is up (row 23, just under BATTLE GAME: the floor starts
// under it; from column 10, as the others). The
// title's font has only the letters it uses: C, O and - are drawn into three
// of BG1's free tiles, in its style (fill 1, outline 2). The game's start
// ($20:B8E1, making the choice the number of players) takes the pick.
namespace {
constexpr uint16_t kTitleRow1 = 0x024a;  // "1 PLAYER GAME": BG1's tile map, row 18 from column 10
constexpr uint16_t kTitleRow4 = 0x02ea;  // ... the fourth row (23)
constexpr uint16_t kTitleChr = 0x2000;   // BG1's tiles (4 bits a pixel)
// (8x8, ':' outline (colour 2), '.' fill (colour 1))
const char* const kTitleGlyphs[3][8] = {
    {"  ::::  ", " :....: ", ":..::..:", ":..:::: ", ":..:::: ", ":..::..:", " :....: ", "  ::::  "},  // C
    {"  ::::  ", " :....: ", ":..::..:", ":..::..:", ":..::..:", ":..::..:", " :....: ", "  ::::  "},  // O
    {"        ", "        ", "        ", ":::::: ", ":....: ", ":::::: ", "        ", "        "},     // -
};
}  // namespace

bool Smb3Engine::titleExec(uint32_t pc) {
  CpuRegs& r = snes_.cpu.r;
  if (pc == 0x2081be || pc == kContinueLives || pc == kContinueLives2) {
    // (the lives from the save: a co-op game's for two, at least 10; and a
    // continue's after a game over (LDA #4; STA $0736), the same)
    if ((coop_ || titlePick_ == 1) && ram(0x072b) < 2 && ram(0x0736) < 9) wvar(0x0736) = 9;
    return false;
  }
  if (pc == 0x20b8e1) {  // (the game starts from the menu)
    if (titleShown_) titlePick_ = titleCursor_ == 3;
    return false;
  }
  if (pc == 0x20b7ec) {
    const uint8_t pressed = ram(0x00F6) | ram(0x00F7);
    if (titleCursor_ != 3) titleCursor_ = std::min<int>(ram(0x072b), 2);
    const int was = titleCursor_;
    if (pressed & 0x20) titleCursor_ = (titleCursor_ + 1) % 4;
    else if (pressed & 0x08) titleCursor_ = std::max(titleCursor_ - 1, 0);
    else if (pressed & 0x04) titleCursor_ = std::min(titleCursor_ + 1, 3);
    if ((pressed & 0x0c) && titleCursor_ == was) wvar(0x1203) = 0;  // (at an end: no sound, as the game's)
    wvar(0x072B) = (uint8_t)(titleCursor_ < 3 ? titleCursor_ : 0);
    r.pc = 0xb81e;  // (the game's cursor drawn)
    return true;
  }
  if (titleCursor_ == 3) wvar(0x09F1) = 0xb7;  // (its Y: the fourth row)
  return false;
}

void Smb3Engine::titleFrame() {
  uint16_t* vram = snes_.ppu.vram;
  // (the menu up: its three rows' 1, 2 and B)
  const bool was = titleShown_;
  titleShown_ = atTitle() && vram[kTitleRow1] == 0x1c0e && vram[kTitleRow1 + 64] == 0x1c1e &&
                vram[kTitleRow1 + 128] == 0x1c08;
  if (!titleShown_) return;
  if (!was) {
    // (on 1 PLAYER GAME, as the game's; on CO-OP GAME with co-op on: turned on in the
    // options, 1 PLAYER GAME under Start turned it off again)
    titleCursor_ = coop_ ? 3 : 0;
    // Three tiles no tile map shows (BG1's and BG2's, sharing these tiles),
    // the empty ones first.
    bool used[0x300] = {};
    for (int a = 0; a < 0x2000; a++) used[vram[a] & 0x3ff & 0x2ff] = true;
    int n = 0;
    for (int pass = 0; pass < 2 && n < 3; pass++)
      for (int t = 0x2ff; t > 0 && n < 3; t--) {
        if (used[t]) continue;
        bool empty = true;
        for (int i = 0; i < 16 && empty; i++) empty = vram[kTitleChr + 16 * t + i] == 0;
        if (pass == 0 && !empty) continue;
        if (pass == 1 && empty) continue;
        titleGlyph_[n++] = (uint16_t)t;
        used[t] = true;
      }
  }
  for (int g = 0; g < 3; g++) {
    uint16_t* d = &vram[kTitleChr + 16 * titleGlyph_[g]];
    for (int y = 0; y < 8; y++) {
      uint8_t p0 = 0, p1 = 0;
      for (int x = 0; x < 8; x++) {
        const char c = kTitleGlyphs[g][y][x];
        if (c == '.') p0 |= 0x80 >> x;
        if (c == ':') p1 |= 0x80 >> x;
      }
      d[y] = (uint16_t)(p0 | p1 << 8);
      d[8 + y] = 0;
    }
  }
  // (row 23: "CO-OP GAME" from column 10)
  const uint16_t C = titleGlyph_[0], O = titleGlyph_[1], D = titleGlyph_[2];
  const uint16_t row[10] = {C, O, D, O, 0x0a, 0x5c, 0x1c, 0x0c, 0x1d, 0x1b};
  for (int i = 0; i < 10; i++) vram[kTitleRow4 + i] = (uint16_t)(row[i] == 0x5c ? 0x005c : 0x1c00 | row[i]);
}

// The status bar in co-op (BG1's tile map, its last screen, row 26): one
// badge for the pair, Mario's M and Luigi's L (made from the game's own, for
// its two-player game: tiles $288-$28B), x and the shared lives after it (two
// digits), and the score one
// column right of where the game draws it (moved as the game draws it: its
// first digit back in its column means drawn again) for the room. Off co-op,
// the game's own: M, x, lives, the score in its place. Done as each frame
// starts to be drawn (see beginLine): the game writes its lives, score and
// clock every other vblank, just before.
void Smb3Engine::hudFrame() {
  uint16_t* vram = snes_.ppu.vram;
  uint16_t* row = &vram[0x0C00 + 26 * 32];
  // (the bar there: its left border and Mario's badge, or the pair's)
  constexpr uint16_t kPair = 0x2fd;  // (three of the bar's spare tiles: the pair's badge)
  // (tile numbers only: drawn again after the map's item menu, the bar had
  // other attributes, $0283, and was the game's own from then on)
  auto tile = [](uint16_t v) { return v & 0x3ff; };
  const bool pair = tile(row[2]) == kPair && tile(row[3]) == kPair + 1;
  if (tile(row[1]) != 0x283 || !((tile(row[2]) == 0x288 && tile(row[3]) == 0x289) || pair)) {
    hudDrawn_ = false;
    return;
  }
  auto digit = [](uint16_t v) { return (v & 0x3ff) >= 0x290 && (v & 0x3ff) <= 0x299; };
  const uint16_t blank = 0x22ad;
  // (the lives after a game over: $FF, none)
  const int lives = ram(0x0736) == 0xff ? 0 : std::min(ram(0x0736) + 1, 99);
  if (coop_ && ram(0x072b) < 2) {  // (in levels and on the map; not the game's own 2-player games)
    if (digit(row[8])) {  // (the score, drawn in its column again: one right)
      for (int c = 15; c >= 9; c--) row[c] = row[c - 1];
      row[8] = blank;
    }
    // The pair's badge, three tiles from the game's four (M $288-$289, L
    // $28A-$28B): each letter with its outline, one column of its colour
    // either side, Mario's red half then Luigi's green, the ends as they
    // were. Then x and the lives, as the game's.
    auto px = [&](int t, int x, int y) {  // (a pixel of the 16-wide pair of tiles from t)
      const uint16_t* d = &vram[0x2000 + 16 * (t + (x >> 3))];
      const int b = 7 - (x & 7);
      return (d[y] >> b & 1) | (d[y] >> (b + 8) & 1) << 1 | (d[8 + y] >> b & 1) << 2 | (d[8 + y] >> (b + 8) & 1) << 3;
    };
    uint16_t out[3][16] = {};
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 24; x++) {
        const int c = x < 3    ? px(0x288, x, y)                    // (Mario's end, his red)
                      : x < 11 ? px(0x288, x + 1, y)                // (his M, outlined)
                      : x < 12 ? px(0x288, 12, y)                   // (red)
                      : x < 13 ? px(0x28a, 2, y)                    // (green)
                      : x < 21 ? px(0x28a, x - 9, y)                // (Luigi's L, outlined)
                      : x < 22 ? px(0x28a, 12, y)                   // (green)
                               : px(0x28a, x - 8, y);               // (his end)
        const int t = x >> 3, b = 7 - (x & 7);
        out[t][y] |= (uint16_t)((c & 1) << b | (c >> 1 & 1) << (b + 8));
        out[t][8 + y] |= (uint16_t)((c >> 2 & 1) << b | (c >> 3 & 1) << (b + 8));
      }
    for (int t = 0; t < 3; t++) std::copy(out[t], out[t] + 16, &vram[0x2000 + 16 * (kPair + t)]);
    for (int t = 0; t < 3; t++) row[2 + t] = (uint16_t)(0x2000 | (kPair + t));
    row[5] = 0x22a4;  // (x)
    row[6] = lives >= 10 ? (uint16_t)(0x2290 + lives / 10) : blank;
    row[7] = (uint16_t)(0x2290 + lives % 10);
    hudDrawn_ = true;
    return;
  }
  if (!hudDrawn_) return;
  // (the game's own again)
  if (!digit(row[8]) && digit(row[9])) {
    for (int c = 8; c <= 14; c++) row[c] = row[c + 1];
    row[15] = blank;
  }
  row[2] = 0x2288;
  row[3] = 0x2289;
  row[4] = 0x22a4;  // (x)
  row[5] = lives >= 10 ? (uint16_t)(0x2290 + lives / 10) : blank;
  row[6] = (uint16_t)(0x2290 + lives % 10);
  row[7] = blank;
  hudDrawn_ = false;
}

// The black "MARIO START!" card before a level ($27:E182: OAM entries 0-5,
// the name in three 16x16 sprites, then START! in three; their places and
// tiles from its table at $27:E1DF / $27:E1E8, three per name): in co-op
// LUIGI START! under it, as the two-player game shows his, the two lines
// either side of the card's one. The game draws the card once and stops
// sending its sprites to the PPU, so each is put in both (at the picture's
// top, every frame), the PPU's 16x16 bit too. The card's own are put back as
// the game wrote them: the box's sprite copy (see coopOam) once left a stray
// letter over the name.
void Smb3Engine::cardFrame() {
  uint8_t* w = snes_.wram;
  Ppu& ppu = snes_.ppu;
  auto ent = [&](int s) { return &w[0x0800 + 4 * s]; };
  auto put = [&](int s, uint8_t x, uint8_t y, uint8_t tile) {
    uint8_t* e = ent(s);
    e[0] = x;
    e[1] = y;
    e[2] = tile;
    e[3] = 0x22;
    w[0x0a20 + s] = 2;
    std::memcpy(&ppu.oam[4 * s], e, 4);
    uint8_t& hi = ppu.oam[512 + (s >> 2)];
    hi = (uint8_t)((hi & ~(3 << ((s & 3) * 2))) | 2 << ((s & 3) * 2));
    ppu.objTrueXSet[s] = false;
  };
  // (last frame's LUIGI START!, still as written: gone, put again below)
  for (int i = 0; i < cardCount_; i++) {
    const int s = cardUsed_[i];
    if (std::memcmp(ent(s), &cardBytes_[4 * i], 4) == 0) ent(s)[1] = ppu.oam[4 * s + 1] = 0xf8;
  }
  cardCount_ = 0;
  if (!coop_ || gameplay_ || ram(0x072b) >= 2 || (ppu.mainScreen() & 0x0f) != 0) return;
  const bool card = ent(0)[2] == 0xa0 && ent(0)[3] == 0x22 && ent(3)[0] == 0x80 && ent(3)[2] == 0xae &&
                    ent(3)[3] == 0x22 && (ent(3)[1] == 0x60 || ent(3)[1] == 0x56);
  if (!card) return;
  static const uint8_t kX[6] = {0x58, 0x68, 0x68, 0x80, 0x90, 0x98};
  static const uint8_t kMario[6] = {0xa0, 0xa2, 0xa2, 0xae, 0x8c, 0x8d}, kLuigi[6] = {0xa4, 0xa6, 0xa6, 0xae, 0x8c, 0x8d};
  for (int s = 0; s < 6; s++) put(s, kX[s], 0x56, kMario[s]);
  for (int s = 6; s < 128; s++)  // (a stray letter of the card's, see above)
    if (ent(s)[1] == 0x60 && ent(s)[3] == 0x22 && ent(s)[2] >= 0xa0 && ent(s)[2] <= 0xae)
      ent(s)[1] = ppu.oam[4 * s + 1] = 0xf8;
  for (int t = 127; t >= 6 && cardCount_ < 6; t--) {
    if (ent(t)[1] < 0xf0) continue;
    put(t, kX[cardCount_], 0x6a, kLuigi[cardCount_]);
    cardUsed_[cardCount_] = (uint8_t)t;
    std::memcpy(&cardBytes_[4 * cardCount_++], ent(t), 4);
  }
}

// The map's box as a world starts (BG3's tile map $5000, rows 8-15, columns
// 8-23: WORLD n, then MARIO, his sprite and his lives): in co-op, no name,
// both brothers side by side (the sprites: see coopOam) and the lives a column
// left, all centred. (Drawn once by the game: done as MARIO appears there.)
void Smb3Engine::boxFrame() {
  uint16_t* v = &snes_.ppu.vram[0x5000];
  auto at = [&](int r, int c) -> uint16_t& { return v[r * 32 + c]; };
  static const uint16_t kMario[5] = {0x0150, 0x0151, 0x0157, 0x015d, 0x016b};
  const bool box = at(8, 8) == 0x0192 && at(8, 23) == 0x4192 && at(10, 12) == 0x0152;
  bool name = box;
  for (int i = 0; i < 5 && name; i++) name = at(13, 10 + i) == kMario[i];
  if (box && name && coop_ && ram(0x072b) < 2) {
    for (int c = 10; c <= 14; c++) at(13, c) = 0x01fe;
    for (int c = 18; c <= 20; c++) at(13, c) = at(13, c + 1);
    at(13, 21) = 0x01fe;
  }
  // (Shown: the box with its name gone (ours) on a screen showing a layer.
  // Not on the black "MARIO START!" card (sprites only on screen): the box's
  // tiles still in the tile map, a letter of it at the box sprite's place was
  // moved and copied over the name.)
  bool blanked = box;
  for (int c = 10; c <= 14 && blanked; c++) blanked = at(13, c) == 0x01fe;
  boxShown_ = blanked && coop_ && ram(0x072b) < 2 && (snes_.ppu.mainScreen() & 0x0f) != 0;
}

// Save states (and rewind): Luigi as he is, at the frame's end. (A state
// without him: he joins Mario afresh.)
void Smb3Engine::coopSave(std::vector<uint8_t>& out) const {
  if (p2_.size() != (size_t)kVarCount) return;
  ExtWriter w{out};
  w.v((uint8_t)'C');
  w.v((uint8_t)1);  // (version)
  w.v((uint16_t)kVarCount);
  w.v((uint8_t)((int)p2Valid_ | p2Drawn_ << 1 | p2Wait_ << 2 | roles_ << 3));
  out.insert(out.end(), p2_.begin(), p2_.end());
  w.v(p2Oam_);
  w.v(p2OamHi_);
  w.v(p2X_);
  w.v(p2Tiles_);
  w.v(p2Pal_);
  w.v(p2PadHi_);
  w.v(p2PadLo_);
  w.v(p2Dead_);
  w.v(musicBack_);
  w.v(musicTrack_);
  w.v(p1Idle_);
  w.v(p2Idle_);
  w.v(camAim_);
  w.v(objOwner_);
  w.v(padHi_);
  w.v(padLo_);
  // (the music before a death's tune, and the sound chip as it was before it, while it plays)
  w.v(lastTrack_);
  const uint32_t snap = musicBack_ > 0 ? (uint32_t)apuSnap_.size() : 0;
  w.v(snap);
  out.insert(out.end(), apuSnap_.begin(), apuSnap_.begin() + snap);
}

void Smb3Engine::coopLoad(const uint8_t* p, size_t n) {
  coopTs_ = 0xff;  // (the tileset as loaded: not a new area)
  p2Valid_ = p2Drawn_ = p2Wait_ = roles_ = false;
  p2Dead_ = -1;
  musicBack_ = 0;
  camAim_ = -1;
  coopTurn_ = 0;
  objSwapped_ = -1;
  usedCount_ = 0;
  ExtReader r{p, p + n};
  uint8_t tag = 0, ver = 0, flags = 0;
  uint16_t count = 0;
  if (!p) return;
  r.v(tag);
  r.v(ver);
  r.v(count);
  r.v(flags);
  if (!r.ok || tag != 'C' || ver != 1 || (count != kVarCount && count != kVarCountV1 && count != kVarCountV2 && count != kVarCountV3) ||
      r.p + count > r.end)
    return;
  std::vector<uint8_t> vars(r.p, r.p + count);
  r.p += count;
  vars.resize(kVarCount, 0);  // (an older state's: those it hasn't, 0)
  uint8_t oam[48], oamHi[12], track;
  int16_t x[12];
  uint16_t tiles[192], pal[16];
  uint8_t padHi, padLo, owner[8];
  int dead, back, idle1, idle2, aim;
  r.v(oam);
  r.v(oamHi);
  r.v(x);
  r.v(tiles);
  r.v(pal);
  r.v(padHi);
  r.v(padLo);
  r.v(dead);
  r.v(back);
  r.v(track);
  r.v(idle1);
  r.v(idle2);
  r.v(aim);
  r.v(owner);
  if (!r.ok) return;
  // (the controllers as last seen; older states: as held now, nothing new)
  ExtReader rp = r;
  uint8_t pads[4];
  rp.v(pads);
  for (int i = 0; i < 2; i++) {
    padHi_[i] = rp.ok ? pads[i] : (uint8_t)(snes_.input[i] >> 8);
    padLo_[i] = rp.ok ? pads[2 + i] : (uint8_t)(snes_.input[i] & 0xf0);
  }
  uint8_t lastTrack = 0;
  uint32_t snap = 0;
  rp.v(lastTrack);
  rp.v(snap);
  apuSnap_.clear();
  if (rp.ok) {
    lastTrack_ = lastTrack;
    if (rp.p + snap <= rp.end) apuSnap_.assign(rp.p, rp.p + snap);
  }
  p2_ = vars;
  p2Valid_ = flags & 1;
  p2Drawn_ = flags & 2;
  p2Wait_ = flags & 4;
  roles_ = flags & 8;
  std::memcpy(p2Oam_, oam, sizeof(oam));
  std::memcpy(p2OamHi_, oamHi, sizeof(oamHi));
  std::memcpy(p2X_, x, sizeof(x));
  std::memcpy(p2Tiles_, tiles, sizeof(tiles));
  std::memcpy(p2Pal_, pal, sizeof(pal));
  std::memcpy(objOwner_, owner, sizeof(owner));
  p2PadHi_ = padHi;
  p2PadLo_ = padLo;
  p2Dead_ = std::clamp(dead, -1, 1000);
  musicBack_ = std::clamp(back, 0, 1000);
  musicTrack_ = track;
  p1Idle_ = std::max(idle1, 0);
  p2Idle_ = std::max(idle2, 0);
  camAim_ = aim;
  p2Area_ = camArea_ = areaStamp_;  // (the area it was saved in)
}

std::string Smb3Engine::coopDebug() const {
  if (p2_.size() != (size_t)kVarCount) return "p2 none";
  auto v = [&](uint32_t a) { return (int)p2_[std::find(kVars, kVars + kVarCount, a) - kVars]; };
  char buf[320];
  int cam = ram(0x0210) | ram(0x0211) << 8;
  snprintf(buf, sizeof buf, "p2 x=%d y=%d rel=%d suit=%d dying=%d dead=%d wait=%d lives=%d music=%d | p1 x=%d | oam %d,%d hi %02X | aim %d idle %d/%d"
           " | bl=%d skip=%d/%d snd=%d/%d | area %u/%u drawn %d gp %d | oam %d/%d",
           v(0x005E) | v(0x0043) << 8, v(0x0070) | v(0x0055) << 8, (v(0x005E) | v(0x0043) << 8) - cam, v(0x00BB),
           v(0x00BF), p2Dead_, (int)p2Wait_, ram(0x0736), musicBack_, ram(0x005e) | ram(0x0043) << 8, p2Oam_[0],
           p2Oam_[1], p2OamHi_[0], camAim_, p1Idle_, p2Idle_, v(0x0552), v(0x0583), v(0x0587), (int)lastApu_.size(), (int)apuSnap_.size(), p2Area_, areaStamp_, (int)p2Drawn_, (int)gameplay_, oamPut_, oamNeed_);
  return buf;
}
