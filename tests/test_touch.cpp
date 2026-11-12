// The on-screen controls (platform/android/touch.cpp): where they are on
// screens of every shape, and what fingers do to them.
#include <cmath>
#include <string>
#include "check.h"
#include "core/snes.h"
#include "platform/android/touch.h"

namespace {

struct Screen {
  float w, h;
  float in[4];  // left, top, right, bottom
  const char* name;
};
const Screen kScreens[] = {
    {2400, 1080, {0, 0, 0, 0}, "20:9 phone"},
    {2340, 1080, {0, 0, 0, 0}, "19.5:9 phone"},
    {1920, 1080, {0, 0, 0, 0}, "16:9 phone"},
    {3200, 1440, {0, 0, 0, 0}, "20:9 phone, 1440p"},
    {1600, 720, {0, 0, 0, 0}, "20:9 phone, 720p"},
    {2520, 1080, {0, 0, 0, 0}, "21:9 phone"},
    {2560, 1600, {0, 0, 0, 0}, "16:10 tablet"},
    {2048, 1536, {0, 0, 0, 0}, "4:3 tablet"},
    {2400, 1080, {120, 0, 0, 0}, "cutout on the left"},
    {2400, 1080, {0, 0, 96, 0}, "cutout on the right"},
    {2400, 1080, {90, 40, 90, 30}, "insets all round"},
    {1280, 720, {0, 0, 0, 48}, "navigation bar below"},
};
const int kScales[] = {70, 75, 80, 85, 90, 95, 100, 105, 110, 115, 120, 125, 130};
const Screen kPhone = kScreens[0];

TouchControls make(const Screen& s, int scale = 100, bool extras = true) {
  TouchControls t;
  t.inGame = true;
  t.visible = true;
  t.showFF = t.showRewind = extras;
  const int none[kTouchGroupCount][2] = {};
  t.setInsets(s.in[0], s.in[1], s.in[2], s.in[3]);
  t.setAdjust(scale, none);
  t.layout(s.w, s.h);
  return t;
}

struct Pt {
  float x, y;
};
Pt mid(const RectF& r) { return {(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}; }
Pt at(const TouchControls& t, TouchControls::Button b) { return mid(t.buttonBounds(b)); }
Pt dpad(const TouchControls& t, float dx, float dy) {
  // a point on the D-pad: (dx, dy) of the way from its middle to its edge
  const RectF r = t.groupBounds(kTouchDpad);
  const Pt c = mid(r);
  const float rad = (r.right - r.left) * 0.5f;
  return {c.x + dx * rad, c.y + dy * rad};
}
std::string where(const Screen& s, int scale) { return std::string(s.name) + " at " + std::to_string(scale) + "%"; }

// Whether a point is on a button as it is drawn (a circle, or a pill).
bool onButton(const TouchControls& t, int b, float x, float y) {
  const RectF r = t.buttonBounds(b);
  const float rad = (r.bottom - r.top) * 0.5f, cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
  const float half = std::max(0.0f, (r.right - r.left) * 0.5f - rad);
  const float dx = std::max(0.0f, std::fabs(x - cx) - half);
  return std::hypot(dx, y - cy) <= rad * 0.98f;
}

const uint16_t kBit[] = {kPadA, kPadB, kPadX, kPadY, kPadL, kPadR, kPadSelect, kPadStart};

}  // namespace

TEST(touch_standard_layout_is_inside_the_safe_area) {
  for (const Screen& s : kScreens)
    for (int scale : kScales) {
      const TouchControls t = make(s, scale);
      for (int g = 0; g < kTouchGroupCount; g++) {
        const RectF b = t.groupBounds(g);
        const std::string w = where(s, scale) + ", " + touchGroupName(g);
        CHECK_MSG(b.left >= s.in[0] - 0.01f, w);
        CHECK_MSG(b.top >= s.in[1] - 0.01f, w);
        CHECK_MSG(b.right <= s.w - s.in[2] + 0.01f, w);
        CHECK_MSG(b.bottom <= s.h - s.in[3] + 0.01f, w);
        CHECK_MSG(b.right > b.left && b.bottom > b.top, w);
      }
    }
}

TEST(touch_no_two_controls_overlap_at_any_size) {
  for (const Screen& s : kScreens)
    for (int scale : kScales) {
      const TouchControls t = make(s, scale);
      for (int a = -1; a < TouchControls::BtnCount; a++)
        for (int b = a + 1; b < TouchControls::BtnCount; b++)
          CHECK_MSG(!t.overlap(a, b), where(s, scale) + ": controls " + std::to_string(a) + " and " + std::to_string(b));
    }
}

TEST(touch_controls_are_a_usable_size) {
  // (a fingertip is about 9 mm: on a phone 65 mm tall, an eighth of the height)
  for (const Screen& s : kScreens) {
    const TouchControls t = make(s, 100);
    const float safeH = s.h - s.in[1] - s.in[3];
    for (int b = TouchControls::BtnA; b <= TouchControls::BtnY; b++) {
      const RectF r = t.buttonBounds(b);
      CHECK_MSG(r.right - r.left >= safeH * 0.12f, s.name);
    }
    const RectF d = t.groupBounds(kTouchDpad);
    CHECK_MSG(d.right - d.left >= safeH * 0.33f, s.name);
    CHECK_MSG(t.unit() > 0, s.name);
  }
}

// With the 4:3 picture (the default with touch controls) a wide phone has
// room at each side for all of them: nothing is over the game.
TEST(touch_controls_fit_beside_the_picture_on_a_wide_phone) {
  for (const Screen& s : {kScreens[0], kScreens[3], kScreens[4], kScreens[5]}) {
    const TouchControls t = make(s, 100);
    const float pw = s.h * (256.0f * 8.0f / 7.0f) / 224.0f, left = (s.w - pw) * 0.5f, right = left + pw;
    for (int g = 0; g < kTouchGroupCount; g++) {
      const RectF b = t.groupBounds(g);
      CHECK_MSG(b.right <= left + 0.5f || b.left >= right - 0.5f, std::string(s.name) + ", " + touchGroupName(g));
    }
  }
}

TEST(touch_every_finger_is_followed_on_its_own) {
  TouchControls t = make(kPhone);
  const Pt b = at(t, TouchControls::BtnB), a = at(t, TouchControls::BtnA), right = dpad(t, 0.6f, 0);
  t.onDown(1, b.x, b.y);  // hold B to run
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadB);
  t.onDown(2, right.x, right.y);  // steer
  CHECK_EQ(t.snesButtons(), (uint16_t)(kPadB | kPadRight));
  t.onDown(3, a.x, a.y);  // jump
  CHECK_EQ(t.snesButtons(), (uint16_t)(kPadB | kPadRight | kPadA));
  t.onUp(3, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)(kPadB | kPadRight));
  t.onUp(1, b.x, b.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadRight);
  t.onUp(2, right.x, right.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);

  // every button at once (eight fingers), then two more than can be followed
  for (int k = 0; k <= TouchControls::BtnStart; k++) {
    const Pt p = at(t, (TouchControls::Button)k);
    t.onDown(10 + k, p.x, p.y);
  }
  uint16_t all = 0;
  for (uint16_t bit : kBit) all |= bit;
  CHECK_EQ(t.snesButtons(), all);
  for (int k = 0; k < 6; k++) t.onDown(100 + k, right.x, right.y);  // (ten are followed: the rest are ignored)
  CHECK_EQ(t.snesButtons(), (uint16_t)(all | kPadRight));
  for (int k = 0; k < 6; k++) t.onUp(100 + k, right.x, right.y);
  for (int k = 0; k <= TouchControls::BtnStart; k++) t.onUp(10 + k, 0, 0);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
  // a finger that was never seen going down, lifting: nothing happens
  t.onUp(999, 5, 5);
  t.onMove(999, 6, 6);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
}

TEST(touch_a_quick_tap_still_reaches_the_game) {
  TouchControls t = make(kPhone);
  const Pt a = at(t, TouchControls::BtnA);
  // down and up between two frames
  t.onDown(1, a.x, a.y);
  t.onUp(1, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);  // (no finger is on it now)
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)kPadA);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)kPadA);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)kPadA);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  // the D-pad too
  const Pt up = dpad(t, 0, -0.6f);
  t.onDown(2, up.x, up.y);
  t.onUp(2, up.x, up.y);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)kPadUp);
  t.takeSnesButtons();
  t.takeSnesButtons();
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  // a tap made while the menu is open is not carried into the game
  t.onDown(3, a.x, a.y);
  t.onUp(3, a.x, a.y);
  t.inGame = false;
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  t.inGame = true;
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
}

TEST(touch_dpad_gives_eight_directions) {
  TouchControls t = make(kPhone);
  struct {
    float dx, dy;
    uint16_t want;
  } const kDirs[] = {
      {0.6f, 0, kPadRight},      {-0.6f, 0, kPadLeft},       {0, -0.6f, kPadUp},
      {0, 0.6f, kPadDown},       {0.5f, -0.5f, kPadRight | kPadUp}, {-0.5f, -0.5f, kPadLeft | kPadUp},
      {0.5f, 0.5f, kPadRight | kPadDown}, {-0.5f, 0.5f, kPadLeft | kPadDown}, {0.05f, 0.05f, 0},
  };
  for (const auto& d : kDirs) {
    const Pt p = dpad(t, d.dx, d.dy);
    t.onDown(1, p.x, p.y);
    CHECK_EQ(t.snesButtons(), d.want);
    t.onUp(1, p.x, p.y);
    t.releaseAll();
  }
  // all the way round: never left with right, never up with down, never nothing
  const Pt c = dpad(t, 0, 0), e = dpad(t, 0.7f, 0);
  const float rad = e.x - c.x;
  t.onDown(1, e.x, e.y);
  for (int deg = 0; deg < 360; deg += 3) {
    const float a = (float)deg * 3.14159265f / 180.0f;
    t.onMove(1, c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
    const uint16_t b = t.snesButtons();
    CHECK(b != 0);
    CHECK(!((b & kPadLeft) && (b & kPadRight)));
    CHECK(!((b & kPadUp) && (b & kPadDown)));
  }
  // a thumb that slides off the pad still steers (it went down on the pad)
  t.onMove(1, c.x + rad * 4, c.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadRight);
  t.onMove(1, c.x, c.y - rad * 4);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadUp);
  t.onUp(1, 0, 0);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
}

TEST(touch_a_thumb_rolls_from_one_button_to_the_next) {
  TouchControls t = make(kPhone);
  const Pt b = at(t, TouchControls::BtnB), a = at(t, TouchControls::BtnA);
  t.onDown(1, b.x, b.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadB);
  // half way: still B
  t.onMove(1, (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadB);
  t.onMove(1, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadA);
  t.onMove(1, b.x, b.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadB);
  t.onUp(1, b.x, b.y);
  // ... but a finger on L does not become A by passing over it
  const Pt l = at(t, TouchControls::BtnL);
  t.onDown(2, l.x, l.y);
  t.onMove(2, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadL);
  t.onUp(2, a.x, a.y);
}

// A finger on a control as it is drawn always presses that control, on every
// screen and at every size; and just off its edge still does.
TEST(touch_a_finger_on_a_control_presses_that_control) {
  for (const Screen& s : kScreens)
    for (int scale : {70, 100, 130}) {
      TouchControls t = make(s, scale);
      for (int b = 0; b < TouchControls::BtnCount; b++) {
        const RectF r = t.buttonBounds(b);
        int tried = 0;
        for (int iy = 0; iy <= 8; iy++)
          for (int ix = 0; ix <= 8; ix++) {
            const float x = r.left + (r.right - r.left) * (float)ix / 8.0f, y = r.top + (r.bottom - r.top) * (float)iy / 8.0f;
            if (!onButton(t, b, x, y)) continue;
            tried++;
            t.onDown(1, x, y);
            bool ok;
            if (b <= TouchControls::BtnStart) ok = t.snesButtons() == kBit[b];
            else ok = t.held((TouchControls::Button)b) && t.snesButtons() == 0;
            CHECK_MSG(ok, where(s, scale) + ", button " + std::to_string(b));
            t.onUp(1, x, y);
            t.releaseAll();
          }
        CHECK(tried >= 20);
      }
      // just outside A's edge (a tenth of its radius): still A
      const RectF ra = t.buttonBounds(TouchControls::BtnA);
      const float rad = (ra.right - ra.left) * 0.5f;
      const Pt a = mid(ra);
      t.onDown(1, a.x + rad * 1.1f, a.y);
      CHECK_MSG(t.snesButtons() == kPadA, where(s, scale));
      t.releaseAll();
      // well away from everything (the middle of the screen): nothing
      t.onDown(1, s.w * 0.5f, s.h * 0.5f);
      CHECK_MSG(t.snesButtons() == 0, where(s, scale));
      t.releaseAll();
    }
}

// In the room between two controls, the nearer one is pressed: a thumb a
// little off its button never presses the neighbour instead.
TEST(touch_between_two_controls_the_nearer_one_is_pressed) {
  for (int scale : {70, 100, 130}) {
    TouchControls t = make(kPhone, scale);
    const RectF l = t.buttonBounds(TouchControls::BtnL), sel = t.buttonBounds(TouchControls::BtnSelect);
    const float x = (l.left + l.right) * 0.5f, gap = sel.top - l.bottom;
    REQUIRE(gap > 0);
    const float reach = std::min(gap * 0.25f, t.unit() * 0.1f);  // (a little off the edge)
    t.onDown(1, x, l.bottom + reach);
    CHECK_EQ(t.snesButtons(), (uint16_t)kPadL);
    t.releaseAll();
    t.onDown(1, x, sel.top - reach);
    CHECK_EQ(t.snesButtons(), (uint16_t)kPadSelect);
    t.releaseAll();
    // Select and the D-pad under it
    const RectF d = t.groupBounds(kTouchDpad);
    const float rad = (d.right - d.left) * 0.5f, cx = (d.left + d.right) * 0.5f, cy = (d.top + d.bottom) * 0.5f;
    const float sx = (sel.left + sel.right) * 0.5f;
    const float padTop = cy - std::sqrt(std::max(0.0f, rad * rad - (sx - cx) * (sx - cx)));  // the disc's edge above sx
    REQUIRE(padTop > sel.bottom);
    const float room = padTop - sel.bottom;
    t.onDown(1, sx, sel.bottom + std::min(room * 0.25f, t.unit() * 0.1f));
    CHECK_EQ(t.snesButtons(), (uint16_t)kPadSelect);
    t.releaseAll();
    t.onDown(1, sx, padTop - room * 0.25f);
    CHECK((t.snesButtons() & kPadSelect) == 0);
    CHECK((t.snesButtons() & (kPadUp | kPadDown | kPadLeft | kPadRight)) != 0);
    t.releaseAll();
  }
}

TEST(touch_menu_button_is_reported_once) {
  TouchControls t = make(kPhone);
  const Pt m = at(t, TouchControls::BtnMenu);
  CHECK(!t.takePressed(TouchControls::BtnMenu));
  t.onDown(1, m.x, m.y);
  CHECK(t.takePressed(TouchControls::BtnMenu));
  CHECK(!t.takePressed(TouchControls::BtnMenu));  // (held: not again)
  t.onUp(1, m.x, m.y);
  CHECK(!t.takePressed(TouchControls::BtnMenu));
  CHECK_EQ(t.snesButtons(), (uint16_t)0);  // (not one of the game's buttons)
}

TEST(touch_rewind_and_fast_forward_only_when_shown) {
  TouchControls t = make(kPhone, 100, false);
  const Pt ff = at(t, TouchControls::BtnFF), rw = at(t, TouchControls::BtnRewind);
  t.onDown(1, ff.x, ff.y);
  t.onDown(2, rw.x, rw.y);
  CHECK(!t.held(TouchControls::BtnFF));
  CHECK(!t.held(TouchControls::BtnRewind));
  t.releaseAll();
  t.showFF = t.showRewind = true;
  t.onDown(1, ff.x, ff.y);
  t.onDown(2, rw.x, rw.y);
  CHECK(t.held(TouchControls::BtnFF));
  CHECK(t.held(TouchControls::BtnRewind));
  t.releaseAll();
  CHECK(!t.held(TouchControls::BtnFF));
}

TEST(touch_outside_a_game_fingers_tap_and_drag) {
  TouchControls t = make(kPhone);
  t.inGame = false;  // (the hub, a menu)
  const Pt d = dpad(t, 0.5f, 0);
  float x = 0, y = 0;
  CHECK(!t.takeTap(&x, &y));
  t.onDown(1, d.x, d.y);  // (where the D-pad would be: there is none now)
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  t.onUp(1, d.x, d.y);
  REQUIRE(t.takeTap(&x, &y));
  CHECK_EQ(x, d.x);
  CHECK_EQ(y, d.y);
  CHECK(!t.takeTap(&x, &y));  // (once)
  CHECK(!t.takeHaptic());     // (taps on the hub and menus do not vibrate)

  // a drag scrolls and is not a tap
  t.onDown(2, 800, 600);
  t.onMove(2, 800, 560);
  t.onMove(2, 802, 450);
  CHECK_EQ(t.takeDrag(), -150.0f);
  CHECK_EQ(t.takeDrag(), 0.0f);
  t.onMove(2, 802, 500);
  CHECK_EQ(t.takeDrag(), 50.0f);
  t.onUp(2, 802, 500);
  CHECK(!t.takeTap(&x, &y));

  // a finger that trembles a little while tapping is still a tap
  t.onDown(3, 400, 300);
  t.onMove(3, 404, 303);
  t.onUp(3, 404, 303);
  CHECK(t.takeTap(&x, &y));
}

TEST(touch_hidden_controls_press_nothing) {
  TouchControls t = make(kPhone);
  t.visible = false;  // (a controller is in use)
  const Pt a = at(t, TouchControls::BtnA);
  t.onDown(1, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  CHECK(!t.takeHaptic());
  t.onUp(1, a.x, a.y);
}

TEST(touch_vibration_marks_presses_and_direction_changes) {
  TouchControls t = make(kPhone);
  CHECK(!t.takeHaptic());
  const Pt a = at(t, TouchControls::BtnA);
  t.onDown(1, a.x, a.y);
  CHECK(t.takeHaptic());
  CHECK(!t.takeHaptic());
  t.onMove(1, a.x + 2, a.y + 1);  // (resting on the button)
  CHECK(!t.takeHaptic());
  t.onUp(1, a.x, a.y);
  CHECK(!t.takeHaptic());  // (letting go is not felt)

  const Pt right = dpad(t, 0.6f, 0), right2 = dpad(t, 0.7f, 0.05f), down = dpad(t, 0, 0.6f), centre = dpad(t, 0, 0);
  t.onDown(2, right.x, right.y);
  CHECK(t.takeHaptic());
  t.onMove(2, right2.x, right2.y);  // (the same direction)
  CHECK(!t.takeHaptic());
  t.onMove(2, down.x, down.y);  // (another)
  CHECK(t.takeHaptic());
  t.onMove(2, centre.x, centre.y);  // (to the middle: nothing pressed, nothing felt)
  CHECK(!t.takeHaptic());
  t.onUp(2, centre.x, centre.y);

  // rolling onto another button is felt
  const Pt b = at(t, TouchControls::BtnB);
  t.onDown(3, b.x, b.y);
  t.takeHaptic();
  t.onMove(3, a.x, a.y);
  CHECK(t.takeHaptic());
  t.releaseAll();
  CHECK(!t.takeHaptic());
  // the empty middle of the screen is not
  t.onDown(4, kPhone.w * 0.5f, kPhone.h * 0.5f);
  CHECK(!t.takeHaptic());
  t.releaseAll();
}

TEST(touch_release_all_lets_go_of_everything) {
  TouchControls t = make(kPhone);
  const Pt a = at(t, TouchControls::BtnA), up = dpad(t, 0, -0.6f);
  t.onDown(1, a.x, a.y);
  t.onDown(2, up.x, up.y);
  t.releaseAll();  // (the app went to the background)
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);  // (nor a tap held over)
  CHECK(!t.takeHaptic());
  // the fingers' later events are harmless
  t.onMove(1, a.x, a.y);
  t.onUp(1, a.x, a.y);
  t.onUp(2, up.x, up.y);
  float x, y;
  CHECK(!t.takeTap(&x, &y));
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
}

TEST(touch_size_is_kept_in_range_and_scales_everything) {
  const TouchControls small = make(kPhone, 70), normal = make(kPhone, 100), large = make(kPhone, 130);
  CHECK(std::fabs(small.unit() - normal.unit() * 0.7f) < 0.01f);
  CHECK(std::fabs(large.unit() - normal.unit() * 1.3f) < 0.01f);
  const RectF a = normal.buttonBounds(TouchControls::BtnA), b = large.buttonBounds(TouchControls::BtnA);
  CHECK(std::fabs((b.right - b.left) - (a.right - a.left) * 1.3f) < 0.01f);
  // out of range: the nearest allowed
  const TouchControls tiny = make(kPhone, 5), huge = make(kPhone, 900);
  CHECK_EQ(tiny.unit(), small.unit());
  CHECK_EQ(huge.unit(), large.unit());
  CHECK_EQ(kTouchScaleMin, 70);
  CHECK_EQ(kTouchScaleMax, 130);
}

TEST(touch_moved_groups_stay_on_the_screen) {
  for (const Screen& s : {kScreens[0], kScreens[2], kScreens[7], kScreens[10]})
    for (int dir = 0; dir < 4; dir++) {
      TouchControls t = make(s, 130);
      int off[kTouchGroupCount][2];
      for (auto& o : off) {
        o[0] = dir == 0 ? -kTouchOffsetMax : dir == 1 ? kTouchOffsetMax : 0;
        o[1] = dir == 2 ? -kTouchOffsetMax : dir == 3 ? kTouchOffsetMax : 0;
      }
      t.setAdjust(130, off);
      t.layout(s.w, s.h);
      for (int g = 0; g < kTouchGroupCount; g++) {
        const RectF b = t.groupBounds(g);
        const std::string w = std::string(s.name) + ", " + touchGroupName(g) + ", direction " + std::to_string(dir);
        CHECK_MSG(b.left >= 0 && b.top >= 0 && b.right <= s.w && b.bottom <= s.h, w);
      }
      // what it reports is where they ended up: the same layout again from that
      int got[kTouchGroupCount][2];
      t.getOffsets(got);
      TouchControls again = make(s, 130);
      again.setAdjust(130, got);
      again.layout(s.w, s.h);
      for (int g = 0; g < kTouchGroupCount; g++) {
        const RectF b1 = t.groupBounds(g), b2 = again.groupBounds(g);
        CHECK(std::fabs(b1.left - b2.left) < 1.5f && std::fabs(b1.top - b2.top) < 1.5f);
      }
    }
  // numbers beyond the allowed range count as the range's end (never garbage)
  TouchControls t = make(kPhone);
  int wild[kTouchGroupCount][2];
  for (auto& o : wild) o[0] = 2000000000, o[1] = -2000000000;
  t.setAdjust(100, wild);
  t.layout(kPhone.w, kPhone.h);
  for (int g = 0; g < kTouchGroupCount; g++) {
    const RectF b = t.groupBounds(g);
    CHECK(b.left >= 0 && b.top >= 0 && b.right <= kPhone.w && b.bottom <= kPhone.h);
  }
}

TEST(touch_layout_editor_drags_one_group) {
  TouchControls t = make(kPhone);
  t.editing = true;
  const RectF before = t.groupBounds(kTouchDpad), faceBefore = t.groupBounds(kTouchFace);
  const Pt c = mid(before);
  CHECK_EQ(t.editGroup(), -1);
  t.onDown(1, c.x, c.y);
  CHECK_EQ(t.editGroup(), (int)kTouchDpad);
  CHECK(t.takeHaptic());
  CHECK(!t.takeMoved());
  t.onMove(1, c.x + 100, c.y - 50);
  CHECK(t.takeMoved());
  CHECK(!t.takeMoved());
  const RectF after = t.groupBounds(kTouchDpad);
  CHECK(std::fabs(after.left - (before.left + 100)) < 0.01f);
  CHECK(std::fabs(after.top - (before.top - 50)) < 0.01f);
  // the others stay where they are, and nothing reaches the game meanwhile
  const RectF faceAfter = t.groupBounds(kTouchFace);
  CHECK_EQ(faceAfter.left, faceBefore.left);
  CHECK_EQ(faceAfter.top, faceBefore.top);
  CHECK_EQ(t.snesButtons(), (uint16_t)0);
  CHECK_EQ(t.takeSnesButtons(), (uint16_t)0);
  t.onUp(1, c.x + 100, c.y - 50);
  float x, y;
  CHECK(!t.takeTap(&x, &y));  // (a drag, not a tap)

  int off[kTouchGroupCount][2];
  t.getOffsets(off);
  CHECK_EQ(off[kTouchDpad][0], (int)std::lround(100.0f / kPhone.h * 1000.0f));
  CHECK_EQ(off[kTouchDpad][1], (int)std::lround(-50.0f / kPhone.h * 1000.0f));
  CHECK_EQ(off[kTouchFace][0], 0);
  CHECK_EQ(off[kTouchFace][1], 0);

  // dragged past the screen's edge it stops at the edge
  const Pt c2 = mid(t.groupBounds(kTouchDpad));
  t.onDown(2, c2.x, c2.y);
  t.onMove(2, c2.x - 5000, c2.y + 5000);
  t.onUp(2, c2.x - 5000, c2.y + 5000);
  const RectF edge = t.groupBounds(kTouchDpad);
  CHECK(edge.left >= 0 && edge.left <= 3);
  CHECK(edge.bottom <= kPhone.h && edge.bottom >= kPhone.h - 3);

  // two fingers move two groups at once
  const Pt f = mid(t.groupBounds(kTouchFace)), st = mid(t.groupBounds(kTouchStart));
  const RectF startBefore = t.groupBounds(kTouchStart);
  t.onDown(3, f.x, f.y);
  t.onDown(4, st.x, st.y);
  t.onMove(3, f.x - 30, f.y);
  t.onMove(4, st.x, st.y + 20);
  CHECK(std::fabs(t.groupBounds(kTouchFace).left - (faceBefore.left - 30)) < 0.01f);
  CHECK(std::fabs(t.groupBounds(kTouchStart).top - (startBefore.top + 20)) < 0.01f);
  t.onUp(3, 0, 0);
  t.onUp(4, 0, 0);

  // a tap on empty space is passed on (the editor's own buttons)
  t.onDown(5, kPhone.w * 0.5f, kPhone.h * 0.5f);
  t.onUp(5, kPhone.w * 0.5f, kPhone.h * 0.5f);
  REQUIRE(t.takeTap(&x, &y));
  CHECK_EQ(x, kPhone.w * 0.5f);

  // leaving the editor: the controls work where they now are
  t.editing = false;
  t.releaseAll();
  const Pt a = at(t, TouchControls::BtnA);
  t.onDown(6, a.x, a.y);
  CHECK_EQ(t.snesButtons(), (uint16_t)kPadA);
  t.releaseAll();
}

TEST(touch_tells_the_system_where_the_controls_are) {
  TouchControls t = make(kPhone, 100, true);
  IRect r[kTouchGroupCount];
  CHECK_EQ(t.controlRects(r, kTouchGroupCount), (int)kTouchGroupCount);
  int n = 0;
  for (int g = 0; g < kTouchGroupCount; g++) {
    const RectF b = t.groupBounds(g);
    CHECK((float)r[n].left <= b.left && (float)r[n].top <= b.top && (float)r[n].right >= b.right && (float)r[n].bottom >= b.bottom);
    CHECK(r[n].right - r[n].left < (int)kPhone.w / 2);
    n++;
  }
  // rewind and fast forward hidden: not among them
  t.showFF = t.showRewind = false;
  CHECK_EQ(t.controlRects(r, kTouchGroupCount), (int)kTouchGroupCount - 2);
  // never more than there is room for
  IRect few[3];
  CHECK_EQ(t.controlRects(few, 3), 3);
  CHECK_EQ(t.controlRects(few, 0), 0);
}

TEST(touch_follows_the_screen_and_the_safe_area) {
  TouchControls t = make(kPhone);
  const RectF d1 = t.groupBounds(kTouchDpad), f1 = t.groupBounds(kTouchFace);
  // a cutout appears on the left: that side moves in, the other stays
  t.setInsets(120, 0, 0, 0);
  t.layout(kPhone.w, kPhone.h);
  const RectF d2 = t.groupBounds(kTouchDpad), f2 = t.groupBounds(kTouchFace);
  CHECK(std::fabs(d2.left - (d1.left + 120)) < 0.01f);
  CHECK(std::fabs(f2.left - f1.left) < 0.01f);
  // the phone is turned the other way round: the cutout is on the right
  t.setInsets(0, 0, 120, 0);
  t.layout(kPhone.w, kPhone.h);
  CHECK(std::fabs(t.groupBounds(kTouchDpad).left - d1.left) < 0.01f);
  CHECK(std::fabs(t.groupBounds(kTouchFace).right - (f1.right - 120)) < 0.01f);
  // another screen size (a split window, a foldable unfolded)
  t.setInsets(0, 0, 0, 0);
  t.layout(1280, 720);
  const RectF d3 = t.groupBounds(kTouchDpad);
  CHECK(d3.bottom <= 720 && d3.right <= 1280);
  CHECK(std::fabs((d3.right - d3.left) - (d1.right - d1.left) * 720.0f / 1080.0f) < 0.01f);
  // insets that make no sense are not believed
  TouchControls odd = make(Screen{2400, 1080, {2000, 900, 2000, 900}, "nonsense"});
  for (int g = 0; g < kTouchGroupCount; g++) {
    const RectF b = odd.groupBounds(g), ref = make(kPhone).groupBounds(g);
    CHECK_EQ(b.left, ref.left);
    CHECK_EQ(b.top, ref.top);
  }
  // negative ones count as none
  TouchControls neg = make(Screen{2400, 1080, {-50, -50, -50, -50}, "negative"});
  CHECK_EQ(neg.groupBounds(kTouchDpad).left, d1.left);
}

TEST(touch_drawing_needs_a_canvas_and_a_game) {
  // (no canvas: nothing to do, and no crash)
  TouchControls t = make(kPhone);
  t.draw(nullptr, 1.0f);
  t.inGame = false;
  t.draw(nullptr, 1.0f);
}
