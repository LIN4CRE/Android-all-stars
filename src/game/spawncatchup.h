// Keeps enemy spawning complete when a widened spawn range grows (SMB2 and
// SMB3). Those games check a single spawn column, at the edge of the range
// the player moves towards, each frame; columns that jump into range at once
// (the width or enemy setting changes, a state saved at a narrower width is
// loaded) would never be checked, and their enemies would never appear. They
// are queued here and checked one per frame through the game's own spawn
// routine instead of its usual column.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

class SpawnCatchUp {
public:
  // Margins patched in last frame; -1: the game's own range; kAssumeCurrent:
  // take the next frame's range as already covered (after a level-load sweep,
  // or a state without this data).
  static constexpr int kAssumeCurrent = -2;
  int prevL = kAssumeCurrent, prevR = kAssumeCurrent;

  void reset() {
    r_ = l_ = Range();
    prevL = prevR = kAssumeCurrent;
    haveCols_ = haveBase_ = false;
  }
  void inactive() {  // the game's own range is patched back in
    r_.on = l_.on = false;
    prevL = prevR = -1;
    haveCols_ = haveBase_ = false;  // (on return the margins are caught up wherever the camera is)
  }
  void assumeCovered() {
    prevL = prevR = kAssumeCurrent;
    haveCols_ = haveBase_ = false;
  }
  // This frame's check was used for another column: check `col` later.
  void displaced(bool right, int col) { (right ? r_ : l_).add(col, col); }

  // Called when this frame's margins are patched in. rightCol(m)/leftCol(m)
  // give the spawn column (level x) for margin m.
  // Compared in level columns with last frame's: the camera's own motion moves
  // the spawn column by at most one a frame, which the game checks itself (a
  // held view widening as the camera moves the other way doesn't move it).
  // A camera that jumps (a door or pipe to another place or area, without a
  // dark frame between: SMB3's World 1 fortress door) has not scrolled past
  // anything: what lies between is not queued, and what was queued belongs to
  // the place left; only the margins at the new place are, as when the game's
  // own range was in use. Queueing the span spawned that fortress's hidden
  // chest, Warp Whistle and all, as the player walked in. Returns true on a jump.
  template <class RightCol, class LeftCol>
  bool track(int extL, int extR, RightCol rightCol, LeftCol leftCol) {
    int r1 = rightCol(extR), l1 = leftCol(extL), base = rightCol(0);
    bool jumped = haveBase_ && std::abs(base - prevBase_) > kMaxScroll;
    prevBase_ = base;
    haveBase_ = true;
    if (jumped) {
      r_.on = l_.on = false;
      if (prevR != kAssumeCurrent) prevL = prevR = -1;
      haveCols_ = false;
    }
    if (prevR != kAssumeCurrent) {
      int r0 = haveCols_ ? prevRC_ : rightCol(std::max(prevR, 0));
      int l0 = haveCols_ ? prevLC_ : leftCol(std::max(prevL, 0));
      if (r1 > r0 + 16) r_.add(r0 + 16, r1);
      if (l1 < l0 - 16) l_.add(l1, l0 - 16);
    }
    prevL = extL;
    prevR = extR;
    prevRC_ = r1;
    prevLC_ = l1;
    haveCols_ = true;
    return jumped;
  }

  bool pending() const { return r_.on || l_.on; }

  // At the spawn routine, which is about to check column `natural` on the
  // right (or left) side: the column to check instead. The displaced column
  // is queued in turn, so nothing is skipped. Only that side's columns: some
  // objects set up from the side the game spawned them on (SMB3 4-1's chasing
  // fish, caught up through the left check at a level start, came in from
  // the left at x -127 rather than from the right).
  bool next(bool right, int natural, int* col) {
    Range& mine = right ? r_ : l_;
    if (!mine.on) return false;
    *col = mine.lo;
    mine.lo += 16;
    if (mine.lo > mine.hi) mine.on = false;
    if (*col != natural) mine.add(natural, natural);
    return true;
  }

  // Save-state data.
  void save(std::vector<uint8_t>& out) const {
    int32_t v[8] = {prevL, prevR, r_.on, r_.lo, r_.hi, l_.on, l_.lo, l_.hi};
    const uint8_t* p = (const uint8_t*)v;
    out.insert(out.end(), p, p + sizeof(v));
  }
  static constexpr size_t kSaveSize = 8 * sizeof(int32_t);
  void load(const uint8_t* p) {
    int32_t v[8];
    std::memcpy(v, p, sizeof(v));
    auto margin = [](int32_t m) { return std::clamp<int32_t>(m, kAssumeCurrent, 256); };
    prevL = margin(v[0]);
    prevR = margin(v[1]);
    r_ = Range{v[2] != 0, v[3], v[4]};
    l_ = Range{v[5] != 0, v[6], v[7]};
    for (Range* g : {&r_, &l_})  // (bounded: a damaged state can't queue forever)
      if (g->on && (g->hi < g->lo || g->hi - g->lo > 4096)) g->on = false;
    haveCols_ = haveBase_ = false;  // (unless loadCols follows)
  }
  // Last frame's spawn columns (a later addition to the save data).
  void saveCols(std::vector<uint8_t>& out) const {
    int32_t v[3] = {prevRC_, prevLC_, haveCols_};
    const uint8_t* p = (const uint8_t*)v;
    out.insert(out.end(), p, p + sizeof(v));
  }
  static constexpr size_t kColsSize = 3 * sizeof(int32_t);
  void loadCols(const uint8_t* p) {
    int32_t v[3];
    std::memcpy(v, p, sizeof(v));
    prevRC_ = v[0];
    prevLC_ = v[1];
    haveCols_ = v[2] != 0 && std::abs(v[0]) < 0x10000 && std::abs(v[1]) < 0x10000;
  }

private:
  struct Range {
    bool on = false;
    int lo = 0, hi = 0;
    void add(int a, int b) {
      if (!on) {
        on = true;
        lo = a;
        hi = b;
      } else {
        lo = std::min(lo, a);
        hi = std::max(hi, b);
      }
    }
  };
  Range r_, l_;
  int prevRC_ = 0, prevLC_ = 0;  // last frame's spawn columns (valid if haveCols_)
  bool haveCols_ = false;
  // Last frame's 4:3 spawn column (the camera's part of it), to tell a jump.
  static constexpr int kMaxScroll = 48;  // (more than any scroll moves in a frame)
  int prevBase_ = 0;
  bool haveBase_ = false;
};
