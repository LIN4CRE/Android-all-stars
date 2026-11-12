// On-screen controls for touch screens (Android), as the Mickey & Donald
// Collection's: an SNES pad over the game (D-pad, A B X Y, L R, Select, Start)
// with Menu, rewind and fast forward; and taps and drags for the launcher and
// the menus. Positions in screen pixels.
#pragma once
#include <cstdint>
#include "app/canvas.h"

class TouchControls {
public:
  enum Button { BtnA, BtnB, BtnX, BtnY, BtnL, BtnR, BtnSelect, BtnStart, BtnMenu, BtnFF, BtnRewind, BtnCount };

  void onDown(int64_t finger, float x, float y);
  void onMove(int64_t finger, float x, float y);
  void onUp(int64_t finger, float x, float y);
  void releaseAll();

  void layout(float W, float H);
  uint16_t snesButtons() const;  // the SNES pad bits held on the on-screen pad
  // ... once per refresh: and a quick tap's (down and up between two reads) for a few refreshes
  uint16_t takeSnesButtons();
  bool held(Button b) const;
  bool takePressed(Button b);  // went down since the last call
  // A short tap anywhere off the pad (the launcher, the menus), and vertical
  // dragging since the last call.
  bool takeTap(float* x, float* y);
  float takeDrag();

  void draw(Canvas* cv, float opacity) const;

  bool visible = true;   // shown while touch is the input in use
  bool inGame = false;   // the pad only exists during play
  bool showFF = false, showRewind = false;
  float opacity = 0.7f;

private:
  struct Finger {
    int64_t id = -1;
    int ctl = -1;
    float x0 = 0, y0 = 0, x = 0, y = 0, lastY = 0;
    bool moved = false;
  };
  static const int kFingers = 10;
  Finger fingers_[kFingers];
  float W_ = 0, H_ = 0, u_ = 0;
  float padX_ = 0, padY_ = 0, padR_ = 0;
  struct Shape {
    float x, y, r, w;  // centre, radius (w > 0: a pill this wide)
  } btn_[BtnCount] = {};
  bool pressed_[BtnCount] = {};
  int latch_[BtnCount] = {};
  uint16_t dpadLatch_ = 0;
  int dpadLatchN_ = 0;
  bool tap_ = false;
  float tapX_ = 0, tapY_ = 0, drag_ = 0;

  Finger* find(int64_t id);
  int controlAt(float x, float y) const;  // -2 the D-pad, 0.. a button, -1 nothing
  bool shown(int b) const;
  bool buttonHeld(int b) const;
  uint16_t dpadBits(float x, float y) const;
};
