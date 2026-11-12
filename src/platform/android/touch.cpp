// On-screen controls: see touch.h.
#include "platform/android/touch.h"
#include <algorithm>
#include <cmath>
#include "core/snes.h"

// Layout, in units of u = 1/8 of the screen height: the D-pad bottom left, A B
// X Y as the SNES pad's diamond bottom right, L and R above them at the sides,
// Select and Start bottom centre (SMB1's and the Lost Levels' scores are along
// the top), Menu top left, rewind and fast forward top right.
void TouchControls::layout(float W, float H) {
  if (W == W_ && H == H_) return;
  W_ = W;
  H_ = H;
  u_ = H / 8.0f;
  const float u = u_;
  padR_ = u * 1.45f;
  padX_ = u * 0.55f + padR_;
  padY_ = H - u * 0.45f - padR_;
  const float br = u * 0.56f, cx = W - u * 2.35f, cy = H - u * 2.05f, d = u * 1.08f;
  btn_[BtnA] = {cx + d, cy, br, 0};
  btn_[BtnB] = {cx, cy + d, br, 0};
  btn_[BtnX] = {cx, cy - d, br, 0};
  btn_[BtnY] = {cx - d, cy, br, 0};
  btn_[BtnL] = {u * 1.35f, u * 2.15f, u * 0.4f, u * 1.6f};
  btn_[BtnR] = {W - u * 1.35f, u * 2.15f, u * 0.4f, u * 1.6f};
  btn_[BtnSelect] = {W * 0.5f - u * 0.95f, H - u * 0.5f, u * 0.32f, u * 1.3f};
  btn_[BtnStart] = {W * 0.5f + u * 0.95f, H - u * 0.5f, u * 0.32f, u * 1.3f};
  btn_[BtnMenu] = {u * 0.75f, u * 0.62f, u * 0.42f, 0};
  btn_[BtnFF] = {W - u * 0.75f, u * 0.62f, u * 0.42f, 0};
  btn_[BtnRewind] = {W - u * 1.85f, u * 0.62f, u * 0.42f, 0};
}

TouchControls::Finger* TouchControls::find(int64_t id) {
  for (auto& f : fingers_)
    if (f.id == id) return &f;
  return nullptr;
}

bool TouchControls::shown(int b) const {
  return !(b == BtnFF && !showFF) && !(b == BtnRewind && !showRewind);
}

int TouchControls::controlAt(float x, float y) const {
  if (!inGame) return -1;
  for (int b = 0; b < BtnCount; b++) {
    if (!shown(b)) continue;
    const Shape& s = btn_[b];
    float reach = s.r * (b <= BtnY ? 1.3f : 1.45f);  // a little forgiving around each
    float dx = std::max(0.0f, std::fabs(x - s.x) - s.w * 0.5f + (s.w > 0 ? s.r : 0)), dy = y - s.y;
    if (s.w > 0) {
      if (std::fabs(x - s.x) <= s.w * 0.5f + s.r * 0.45f && std::fabs(dy) <= reach) return b;
    } else if (std::hypot(dx, dy) <= reach) {
      return b;
    }
  }
  if (std::hypot(x - padX_, y - padY_) <= padR_ * 1.5f) return -2;
  return -1;
}

void TouchControls::onDown(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) f = find(-1);
  if (!f) return;
  f->id = id;
  f->x0 = f->x = x;
  f->y0 = f->y = f->lastY = y;
  f->moved = false;
  f->ctl = controlAt(x, y);
  if (f->ctl >= 0) {
    pressed_[f->ctl] = true;
    latch_[f->ctl] = 3;
  } else if (f->ctl == -2 && dpadBits(x, y)) {
    dpadLatch_ = dpadBits(x, y);
    dpadLatchN_ = 3;
  }
}

void TouchControls::onMove(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) return;
  // a face-button finger that slides onto another face button presses that one instead (rolling thumb)
  if (f->ctl >= BtnA && f->ctl <= BtnY) {
    for (int b = BtnA; b <= BtnY; b++)
      if (b != f->ctl && std::hypot(x - btn_[b].x, y - btn_[b].y) <= btn_[b].r * 1.05f) {
        bool was = buttonHeld(b);
        f->ctl = b;
        if (!was) pressed_[b] = true;
        break;
      }
  }
  if (f->ctl == -1) {
    drag_ += y - f->lastY;
    f->lastY = y;
  }
  f->x = x;
  f->y = y;
  if (std::hypot(x - f->x0, y - f->y0) > u_ * 0.25f) f->moved = true;
}

void TouchControls::onUp(int64_t id, float x, float y) {
  Finger* f = find(id);
  if (!f) return;
  if (f->ctl == -1 && !f->moved) {
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
  tap_ = false;
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
  if (!inGame) return 0;
  static const uint16_t kBits[] = {kPadA, kPadB, kPadX, kPadY, kPadL, kPadR, kPadSelect, kPadStart};
  uint16_t s = 0;
  for (const auto& f : fingers_) {
    if (f.id == -1) continue;
    if (f.ctl == -2) s |= dpadBits(f.x, f.y);
    else if (f.ctl >= 0 && f.ctl <= BtnStart) s |= kBits[f.ctl];
  }
  return s;
}

uint16_t TouchControls::takeSnesButtons() {
  uint16_t s = snesButtons();
  if (!inGame) return s;
  static const uint16_t kBits[] = {kPadA, kPadB, kPadX, kPadY, kPadL, kPadR, kPadSelect, kPadStart};
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

bool TouchControls::held(Button b) const { return inGame && buttonHeld(b); }

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

void TouchControls::draw(Canvas* cv, float op) const {
  if (!inGame || !visible || !cv || op <= 0.01f) return;
  const float u = u_;
  auto light = [&](float k) { return colorF(1, 1, 1, std::min(1.0f, k * op)); };
  auto dark = [&](float k) { return colorF(0.043f, 0.055f, 0.086f, std::min(1.0f, k * op)); };
  const float line = std::max(2.0f, u * 0.03f);

  // D-pad: a disc with a cross, the held directions lit
  uint16_t dirs = 0;
  for (const auto& f : fingers_)
    if (f.id != -1 && f.ctl == -2) dirs |= dpadBits(f.x, f.y);
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
}
