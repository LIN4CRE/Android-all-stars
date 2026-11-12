// On-screen controls: see touch.h.
#include "platform/android/touch.h"
#include <algorithm>
#include <cmath>
#include "core/snes.h"

namespace {
const uint16_t kBits[] = {kPadA, kPadB, kPadX, kPadY, kPadL, kPadR, kPadSelect, kPadStart};
}

void TouchControls::setInsets(float left, float top, float right, float bottom) {
  const float in[4] = {std::max(0.0f, left), std::max(0.0f, top), std::max(0.0f, right), std::max(0.0f, bottom)};
  for (int i = 0; i < 4; i++)
    if (in[i] != inset_[i]) inset_[i] = in[i], dirty_ = true;
}

void TouchControls::setAdjust(int scalePercent, const int offsets[kTouchGroupCount][2]) {
  scalePercent = std::clamp(scalePercent, kTouchScaleMin, kTouchScaleMax);
  if (scalePercent != scale_) scale_ = scalePercent, dirty_ = true;
  for (int g = 0; g < kTouchGroupCount; g++)
    for (int k = 0; k < 2; k++) {
      const float v = std::clamp(offsets[g][k], -kTouchOffsetMax, kTouchOffsetMax) / 1000.0f;
      if (v != offset_[g][k]) offset_[g][k] = v, dirty_ = true;
    }
}

void TouchControls::getOffsets(int offsets[kTouchGroupCount][2]) const {
  for (int g = 0; g < kTouchGroupCount; g++)
    for (int k = 0; k < 2; k++) offsets[g][k] = (int)std::lround(offset_[g][k] * 1000.0f);
}

void TouchControls::layout(float W, float H) {
  if (W == W_ && H == H_ && !dirty_) return;
  W_ = W;
  H_ = H;
  place();
}

int TouchControls::groupOf(int b) {
  switch (b) {
    case BtnA:
    case BtnB:
    case BtnX:
    case BtnY: return kTouchFace;
    case BtnL: return kTouchL;
    case BtnR: return kTouchR;
    case BtnSelect: return kTouchSelect;
    case BtnStart: return kTouchStart;
    case BtnMenu: return kTouchMenu;
    case BtnFF: return kTouchFF;
    case BtnRewind: return kTouchRewind;
    default: return kTouchDpad;
  }
}

// The standard layout. The unit u is an eighth of the safe area's height times
// the player's size setting; everything is measured from the safe area's
// edges (clear of a cutout and of rounded corners).
//
//   left side, top to bottom:   MENU, L, SELECT, the D-pad
//   right side, top to bottom:  rewind and fast forward, R, START, A B X Y
//
// Everything is at the two sides and nothing in the middle: with the 4:3
// picture (the default while these controls are in use) a phone's wide screen
// has a margin each side of the picture, and the controls are sized to fit in
// it (3.6 u at the standard size on a 20:9 screen), so the game is not
// covered. L, R, Select and Start share the room between the top row and the
// D-pad / A B X Y evenly, so nothing overlaps at any size from 70 to 130 %.
// Then each group's own move (the layout editor), kept on the screen.
void TouchControls::place() {
  dirty_ = false;
  if (W_ <= 0 || H_ <= 0) return;
  float L = inset_[0], T = inset_[1], R = W_ - inset_[2], B = H_ - inset_[3];
  // (insets that leave less than half the screen make no sense: the whole screen then)
  if (R - L < W_ * 0.5f) L = 0, R = W_;
  if (B - T < H_ * 0.5f) T = 0, B = H_;
  u_ = (B - T) / 8.0f * (float)scale_ / 100.0f;
  const float u = u_;
  // the top row
  btn_[BtnMenu] = {L + u * 0.75f, T + u * 0.62f, u * 0.42f, 0};
  btn_[BtnFF] = {R - u * 0.75f, T + u * 0.62f, u * 0.42f, 0};
  btn_[BtnRewind] = {R - u * 1.85f, T + u * 0.62f, u * 0.42f, 0};
  // the bottom corners, under the thumbs
  padR_ = u * 1.45f;
  padX_ = L + u * 0.55f + padR_;
  padY_ = B - u * 0.45f - padR_;
  const float br = u * 0.54f, d = u * 1.0f, cx = R - u * 2.05f, cy = B - u * 2.0f;
  btn_[BtnA] = {cx + d, cy, br, 0};
  btn_[BtnB] = {cx, cy + d, br, 0};
  btn_[BtnX] = {cx, cy - d, br, 0};
  btn_[BtnY] = {cx - d, cy, br, 0};
  // between them at each side: L and R, then Select and Start below, sharing
  // the room there is (never more than 0.7 u apart, so that small controls
  // stay near the thumbs)
  const float top = T + u * 1.04f;                              // the top row's lower edge
  const float bottom = std::min(padY_ - padR_, cy - d - br);    // the upper edge of the D-pad and of A B X Y
  const float lh = u * 0.8f, sh = u * 0.64f;
  const float gap = std::clamp((bottom - top - lh - sh) / 3.0f, 0.0f, u * 0.7f);
  const float sy = bottom - gap - sh * 0.5f, ly = sy - sh * 0.5f - gap - lh * 0.5f;
  btn_[BtnL] = {L + u * 1.35f, ly, lh * 0.5f, u * 1.6f};
  btn_[BtnR] = {R - u * 1.35f, ly, lh * 0.5f, u * 1.6f};
  btn_[BtnSelect] = {L + u * 1.35f, sy, sh * 0.5f, u * 1.3f};
  btn_[BtnStart] = {R - u * 1.35f, sy, sh * 0.5f, u * 1.3f};

  for (int g = 0; g < kTouchGroupCount; g++) {
    float dx = offset_[g][0] * H_, dy = offset_[g][1] * H_;
    // (kept wholly on the screen, however it was moved or the screen changed)
    const RectF b = groupBounds(g);
    const float margin = 2.0f;
    dx = std::clamp(dx, std::min(0.0f, margin - b.left), std::max(0.0f, W_ - margin - b.right));
    dy = std::clamp(dy, std::min(0.0f, margin - b.top), std::max(0.0f, H_ - margin - b.bottom));
    offset_[g][0] = dx / H_;
    offset_[g][1] = dy / H_;
    if (g == kTouchDpad) {
      padX_ += dx;
      padY_ += dy;
    } else {
      for (int k = 0; k < BtnCount; k++)
        if (groupOf(k) == g) btn_[k].x += dx, btn_[k].y += dy;
    }
  }
}

RectF TouchControls::buttonBounds(int b) const {
  if (b < 0 || b >= BtnCount) return rectF(0, 0, 0, 0);
  const Shape& s = btn_[b];
  const float hw = s.w > 0 ? s.w * 0.5f : s.r;
  return rectF(s.x - hw, s.y - s.r, s.x + hw, s.y + s.r);
}

RectF TouchControls::groupBounds(int g) const {
  if (g == kTouchDpad) return rectF(padX_ - padR_, padY_ - padR_, padX_ + padR_, padY_ + padR_);
  RectF r = rectF(1e9f, 1e9f, -1e9f, -1e9f);
  for (int k = 0; k < BtnCount; k++) {
    if (groupOf(k) != g) continue;
    const RectF b = buttonBounds(k);
    r.left = std::min(r.left, b.left);
    r.top = std::min(r.top, b.top);
    r.right = std::max(r.right, b.right);
    r.bottom = std::max(r.bottom, b.bottom);
  }
  return r;
}

// Whether two controls' shapes overlap: buttons by number, the D-pad as -1.
// (Shapes are circles and pills; a pill is tested as its straight part
// thickened by its radius.)
bool TouchControls::overlap(int a, int b) const {
  auto shape = [&](int k) { return k < 0 ? Shape{padX_, padY_, padR_, 0} : btn_[k]; };
  const Shape p = shape(a), q = shape(b);
  const float hp = p.w > 0 ? std::max(0.0f, p.w * 0.5f - p.r) : 0.0f, hq = q.w > 0 ? std::max(0.0f, q.w * 0.5f - q.r) : 0.0f;
  // (two horizontal segments: the gap between them along x, if any)
  const float dx = std::max(0.0f, std::fabs(p.x - q.x) - hp - hq), dy = p.y - q.y;
  return std::hypot(dx, dy) < p.r + q.r;
}

int TouchControls::controlRects(IRect* out, int max) const {
  int n = 0;
  for (int g = 0; g < kTouchGroupCount && n < max; g++) {
    if ((g == kTouchFF && !showFF) || (g == kTouchRewind && !showRewind)) continue;
    const RectF b = groupBounds(g);
    out[n++] = IRect{(int)std::floor(b.left), (int)std::floor(b.top), (int)std::ceil(b.right), (int)std::ceil(b.bottom)};
  }
  return n;
}

TouchControls::Finger* TouchControls::find(int64_t id) {
  for (auto& f : fingers_)
    if (f.id == id) return &f;
  return nullptr;
}

bool TouchControls::shown(int b) const {
  return !(b == BtnFF && !showFF) && !(b == BtnRewind && !showRewind);
}

// How far a point is outside a button's shape (a circle, or a pill): 0 or less on it.
float TouchControls::outside(const Shape& s, float x, float y) {
  const float half = s.w > 0 ? std::max(0.0f, s.w * 0.5f - s.r) : 0.0f;  // (a pill's straight part, each way)
  const float dx = std::max(0.0f, std::fabs(x - s.x) - half), dy = y - s.y;
  return std::hypot(dx, dy) - s.r;
}

// The control under a finger: the button it is on; else the D-pad if it is on
// that; else the nearest control that is within reach (a thumb a little off
// its button still presses it, but never one that belongs to a neighbour).
int TouchControls::controlAt(float x, float y) const {
  if (!inGame || !visible) return kNone;
  int on = kNone, near = kNone;
  float onD = 0, nearD = 1e9f;
  for (int b = 0; b < BtnCount; b++) {
    if (!shown(b)) continue;
    const float dist = outside(btn_[b], x, y);
    if (dist <= 0) {
      if (on == kNone || dist < onD) on = b, onD = dist;
    } else if (dist <= btn_[b].r * (b <= BtnY ? 0.3f : 0.45f) && dist < nearD) {
      near = b, nearD = dist;
    }
  }
  if (on != kNone) return on;
  const float pad = std::hypot(x - padX_, y - padY_) - padR_;
  if (pad <= 0) return kDpad;
  if (pad <= padR_ * 0.5f && pad < nearD) return kDpad;
  return near;
}

// (the layout editor: the group under a finger, a little forgiving)
int TouchControls::groupAt(float x, float y) const {
  const float slack = u_ * 0.25f;
  int best = -1;
  float bestArea = 1e18f;
  for (int g = 0; g < kTouchGroupCount; g++) {
    if ((g == kTouchFF && !showFF) || (g == kTouchRewind && !showRewind)) continue;
    const RectF b = groupBounds(g);
    if (x < b.left - slack || x > b.right + slack || y < b.top - slack || y > b.bottom + slack) continue;
    // (where two overlap, the smaller: it would be unreachable under the larger)
    const float area = (b.right - b.left) * (b.bottom - b.top);
    if (area < bestArea) bestArea = area, best = g;
  }
  return best;
}

void TouchControls::onDown(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) f = find(-1);
  if (!f) return;
  f->id = id;
  f->x0 = f->x = f->lastX = x;
  f->y0 = f->y = f->lastY = y;
  f->moved = false;
  if (editing) {
    const int g = groupAt(x, y);
    f->ctl = g >= 0 ? kEdit + g : kNone;
    if (g >= 0) editGroup_ = g, haptic_ = true;
    return;
  }
  f->ctl = controlAt(x, y);
  if (f->ctl >= 0) {
    pressed_[f->ctl] = true;
    latch_[f->ctl] = 3;
    haptic_ = true;
  } else if (f->ctl == kDpad) {
    const uint16_t bits = dpadBits(x, y);
    if (bits) {
      dpadLatch_ = bits;
      dpadLatchN_ = 3;
      haptic_ = true;
    }
    dpadLast_ = bits;
  }
}

void TouchControls::onMove(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) return;
  if (f->ctl >= kEdit) {
    // (the layout editor: the group follows the finger)
    const int g = f->ctl - kEdit;
    if (H_ > 0 && (x != f->lastX || y != f->lastY)) {
      offset_[g][0] += (x - f->lastX) / H_;
      offset_[g][1] += (y - f->lastY) / H_;
      place();
      moved_ = true;
    }
    f->lastX = f->x = x;
    f->lastY = f->y = y;
    f->moved = true;
    return;
  }
  // a face-button finger that slides onto another face button presses that one instead (rolling thumb)
  if (f->ctl >= BtnA && f->ctl <= BtnY) {
    for (int b = BtnA; b <= BtnY; b++)
      if (b != f->ctl && std::hypot(x - btn_[b].x, y - btn_[b].y) <= btn_[b].r * 1.05f) {
        bool was = buttonHeld(b);
        f->ctl = b;
        if (!was) {
          pressed_[b] = true;
          haptic_ = true;
        }
        break;
      }
  }
  if (f->ctl == kDpad) {
    const uint16_t bits = dpadBits(x, y);
    if (bits != dpadLast_ && bits) haptic_ = true;
    dpadLast_ = bits;
  }
  if (f->ctl == kNone) {
    drag_ += y - f->lastY;
    f->lastY = y;
  }
  f->x = x;
  f->y = y;
  if (std::hypot(x - f->x0, y - f->y0) > std::max(H_ / 32.0f, 1.0f)) f->moved = true;
}

void TouchControls::onUp(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) return;
  if (f->ctl == kNone && !f->moved) {
    tap_ = true;
    tapX_ = x;
    tapY_ = y;
  }
  *f = Finger();
}

void TouchControls::releaseAll() {
  for (auto& f : fingers_) f = Finger();
  for (bool& p : pressed_) p = false;
  for (int& l : latch_) l = 0;
  dpadLatchN_ = 0;
  dpadLast_ = 0;
  tap_ = false;
  haptic_ = false;
  drag_ = 0;
}

bool TouchControls::buttonHeld(int b) const {
  for (const auto& f : fingers_)
    if (f.id != -1 && f.ctl == b) return true;
  return false;
}

uint16_t TouchControls::dpadBits(float x, float y) const {
  float dx = x - padX_, dy = y - padY_;
  if (std::hypot(dx, dy) < padR_ * 0.22f) return 0;
  // 8 directions, 45-degree sectors: diagonals press both
  float a = std::atan2(dy, dx) * 57.29578f;  // -180..180, 0 = right, 90 = down
  uint16_t b = 0;
  if (a > -67.5f && a < 67.5f) b |= kPadRight;
  if (a > 112.5f || a < -112.5f) b |= kPadLeft;
  if (a > 22.5f && a < 157.5f) b |= kPadDown;
  if (a < -22.5f && a > -157.5f) b |= kPadUp;
  return b;
}

uint16_t TouchControls::snesButtons() const {
  if (!inGame || editing) return 0;
  uint16_t s = 0;
  for (const auto& f : fingers_) {
    if (f.id == -1) continue;
    if (f.ctl == kDpad) s |= dpadBits(f.x, f.y);
    else if (f.ctl >= 0 && f.ctl <= BtnStart) s |= kBits[f.ctl];
  }
  return s;
}

uint16_t TouchControls::takeSnesButtons() {
  uint16_t s = snesButtons();
  if (!inGame || editing) {
    // (nothing held over from before the menu or the editor)
    for (int& l : latch_) l = 0;
    dpadLatchN_ = 0;
    return s;
  }
  for (int b = 0; b <= BtnStart; b++)
    if (latch_[b] > 0) {
      s |= kBits[b];
      latch_[b]--;
    }
  if (dpadLatchN_ > 0) {
    s |= dpadLatch_;
    dpadLatchN_--;
  }
  return s;
}

bool TouchControls::held(Button b) const { return inGame && !editing && buttonHeld(b); }

bool TouchControls::takePressed(Button b) {
  bool p = pressed_[b];
  pressed_[b] = false;
  return p;
}

bool TouchControls::takeTap(float* x, float* y) {
  if (!tap_) return false;
  tap_ = false;
  *x = tapX_;
  *y = tapY_;
  return true;
}

float TouchControls::takeDrag() {
  float d = drag_;
  drag_ = 0;
  return d;
}

bool TouchControls::takeHaptic() {
  bool h = haptic_;
  haptic_ = false;
  return h;
}

bool TouchControls::takeMoved() {
  bool m = moved_;
  moved_ = false;
  return m;
}

void TouchControls::draw(Canvas* cv, float op) const {
  if (!cv) return;
  if (editing) op = 1.0f;
  else if (!inGame || !visible || op <= 0.01f) return;
  const float u = u_;
  auto light = [&](float k) { return colorF(1, 1, 1, std::min(1.0f, k * op)); };
  auto dark = [&](float k) { return colorF(0.043f, 0.055f, 0.086f, std::min(1.0f, k * op)); };
  const float line = std::max(2.0f, u * 0.03f);

  // D-pad: a disc with a cross, the held directions lit
  uint16_t dirs = 0;
  for (const auto& f : fingers_)
    if (f.id != -1 && f.ctl == kDpad) dirs |= dpadBits(f.x, f.y);
  RectF disc = rectF(padX_ - padR_, padY_ - padR_, padX_ + padR_, padY_ + padR_);
  cv->fillRound(disc, padR_, dark(0.35f));
  cv->strokeRound(disc, padR_, line, light(0.35f));
  const float arm = padR_ * 0.36f, len = padR_ * 0.86f;
  cv->fillRound(rectF(padX_ - arm, padY_ - len, padX_ + arm, padY_ + len), arm * 0.35f, light(0.22f));
  cv->fillRound(rectF(padX_ - len, padY_ - arm, padX_ + len, padY_ + arm), arm * 0.35f, light(0.22f));
  struct {
    uint16_t bit;
    float dx, dy;
  } kDirs[] = {{kPadUp, 0, -1}, {kPadDown, 0, 1}, {kPadLeft, -1, 0}, {kPadRight, 1, 0}};
  for (auto& d : kDirs) {
    if (!(dirs & d.bit)) continue;
    float x = padX_ + d.dx * len * 0.55f, y = padY_ + d.dy * len * 0.55f;
    cv->fillRound(rectF(x - arm, y - arm, x + arm, y + arm), arm * 0.35f, light(0.45f));
  }

  // buttons
  auto button = [&](int b, const wchar_t* label, float textScale, ColorF tint) {
    if (!shown(b)) return;
    const Shape& s = btn_[b];
    bool on = buttonHeld(b);
    float hw = s.w > 0 ? s.w * 0.5f : s.r;
    RectF r = rectF(s.x - hw, s.y - s.r, s.x + hw, s.y + s.r);
    ColorF fill = on ? light(0.45f) : colorF(tint.r, tint.g, tint.b, std::min(1.0f, 0.42f * op));
    cv->fillRound(r, s.r, fill);
    cv->strokeRound(r, s.r, line, light(on ? 0.8f : 0.45f));
    float size = s.r * textScale;
    float tw = cv->measure(label, size, kWeightBold);
    if (tw > hw * 1.7f) size *= hw * 1.7f / tw;
    cv->text(label, r, size, kWeightBold, light(0.9f), kAlignCenter, kParaCenter);
  };
  const ColorF kDark = dark(1), kRed = colorF(0.72f, 0.16f, 0.16f), kYellow = colorF(0.78f, 0.62f, 0.10f),
               kBlue = colorF(0.16f, 0.30f, 0.72f), kGreen = colorF(0.14f, 0.55f, 0.28f);
  button(BtnA, L"A", 1.05f, kRed);
  button(BtnB, L"B", 1.05f, kYellow);
  button(BtnX, L"X", 1.05f, kBlue);
  button(BtnY, L"Y", 1.05f, kGreen);
  button(BtnL, L"L", 1.0f, kDark);
  button(BtnR, L"R", 1.0f, kDark);
  button(BtnSelect, L"SELECT", 0.62f, kDark);
  button(BtnStart, L"START", 0.62f, kDark);
  button(BtnMenu, L"MENU", 0.5f, kDark);
  button(BtnFF, L"\x25B6\x25B6", 0.6f, kDark);
  button(BtnRewind, L"\x25C0\x25C0", 0.6f, kDark);

  // The layout editor: each group's frame, the one in hand brighter.
  if (editing) {
    for (int g = 0; g < kTouchGroupCount; g++) {
      if ((g == kTouchFF && !showFF) || (g == kTouchRewind && !showRewind)) continue;
      RectF b = groupBounds(g);
      const float pad = u * 0.12f;
      b = rectF(b.left - pad, b.top - pad, b.right + pad, b.bottom + pad);
      const bool hot = g == editGroup_;
      cv->strokeRound(b, u * 0.18f, hot ? line * 1.6f : line,
                      hot ? colorF(1.0f, 0.82f, 0.25f, 0.95f) : colorF(1, 1, 1, 0.35f));
    }
  }
}
