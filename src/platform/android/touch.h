// On-screen controls for touch screens: a Super Nintendo pad at the sides of
// the game's picture (D-pad, A B X Y, L R, Select, Start) with Menu, rewind
// and fast forward; and taps and drags for the hub and the menus. Positions
// in screen pixels.
//
// * Every finger is followed on its own (up to ten at once): holding B to run
//   while pressing A to jump and steering with the D-pad are three fingers,
//   each owning the control it went down on until it lifts.
// * A press shorter than a frame still reaches the game (it is held for three
//   frames), so a quick tap is never lost.
// * The layout keeps clear of the display's cutout and rounded corners (the
//   safe-area insets), can be made larger or smaller, and each control can be
//   dragged to where the player's thumbs are (the layout editor).
// * Nothing here talks to Android: this file is the same in the host tests.
#pragma once
#include <cstdint>
#include "app/canvas.h"
#include "app/config.h"

class TouchControls {
public:
  enum Button { BtnA, BtnB, BtnX, BtnY, BtnL, BtnR, BtnSelect, BtnStart, BtnMenu, BtnFF, BtnRewind, BtnCount };

  void onDown(int64_t finger, float x, float y);
  void onMove(int64_t finger, float x, float y);
  void onUp(int64_t finger, float x, float y);
  void releaseAll();

  // The screen's size, and the part of it the system keeps at each edge (a
  // cutout, rounded corners), in pixels.
  void layout(float W, float H);
  void setInsets(float left, float top, float right, float bottom);
  // The player's adjustments: the size (percent) and where each group was
  // moved to (thousandths of the screen's height from its standard place).
  void setAdjust(int scalePercent, const int offsets[kTouchGroupCount][2]);
  void getOffsets(int offsets[kTouchGroupCount][2]) const;

  uint16_t snesButtons() const;  // the SNES pad bits held on the on-screen pad
  // ... once per refresh: and a quick tap's (down and up between two reads) for a few refreshes
  uint16_t takeSnesButtons();
  bool held(Button b) const;
  bool takePressed(Button b);  // went down since the last call
  // A short tap anywhere off the pad (the hub, the menus), and vertical
  // dragging since the last call.
  bool takeTap(float* x, float* y);
  float takeDrag();
  // A control was pressed (or the D-pad's direction changed) since the last
  // call: the moment for a touch of vibration.
  bool takeHaptic();

  // The layout editor: fingers drag the controls instead of pressing them.
  bool editing = false;
  bool takeMoved();  // a control was dragged since the last call (to store the layout)
  int editGroup() const { return editGroup_; }  // the group being dragged, or the last one (-1: none yet)

  void draw(Canvas* cv, float opacity) const;
  // Where the controls are (for the system's edge gestures to keep off them): up to `max` rectangles.
  int controlRects(IRect* out, int max) const;
  // A group's bounding box and a button's (tests, the editor's frames), and
  // whether two controls' shapes overlap (the D-pad is -1).
  RectF groupBounds(int group) const;
  RectF buttonBounds(int b) const;
  bool overlap(int a, int b) const;
  float unit() const { return u_; }

  bool visible = true;   // shown while touch is the input in use
  bool inGame = false;   // the pad only exists during play
  bool showFF = false, showRewind = false;

private:
  struct Finger {
    int64_t id = -1;
    int ctl = -1;  // a button, kDpad, kNone, or kEdit + group
    float x0 = 0, y0 = 0, x = 0, y = 0, lastX = 0, lastY = 0;
    bool moved = false;
  };
  static constexpr int kFingers = 10;
  static constexpr int kNone = -1, kDpad = -2, kEdit = 100;
  Finger fingers_[kFingers];
  float W_ = 0, H_ = 0, u_ = 0;
  float inset_[4] = {0, 0, 0, 0};  // left, top, right, bottom
  int scale_ = 100;
  float offset_[kTouchGroupCount][2] = {};  // pixels' worth, as fractions of H
  bool dirty_ = true;
  float padX_ = 0, padY_ = 0, padR_ = 0;
  struct Shape {
    float x, y, r, w;  // centre, radius (w > 0: a pill this wide)
  } btn_[BtnCount] = {};
  bool pressed_[BtnCount] = {};
  int latch_[BtnCount] = {};
  uint16_t dpadLatch_ = 0;
  int dpadLatchN_ = 0;
  uint16_t dpadLast_ = 0;
  bool tap_ = false, haptic_ = false, moved_ = false;
  float tapX_ = 0, tapY_ = 0, drag_ = 0;
  int editGroup_ = -1;

  void place();
  Finger* find(int64_t id);
  int controlAt(float x, float y) const;  // kDpad, 0.. a button, kNone
  static float outside(const Shape& s, float x, float y);
  int groupAt(float x, float y) const;
  static int groupOf(int button);
  bool shown(int b) const;
  bool buttonHeld(int b) const;
  uint16_t dpadBits(float x, float y) const;
};
