// Controllers and keyboards on Android: keys by virtual-key code (the settings
// file's), controllers in the standard layout (buttons 0-15 = A B X Y LB RB
// Back Start LS RS Up Down Left Right LT RT, axes 0-3 = LX LY RX RY).
//
// A controller reaches the app one of three ways, tried in this order:
//   1. SDL's GameController API: a pad SDL knows the layout of;
//   2. SDL's Joystick API: a pad SDL sees but has no layout for (its buttons
//      as they come; they can be assigned in Controls > Joypad);
//   3. "basic mode": a pad Android reports only as key presses, which SDL
//      does not treat as a controller at all (see SmasActivity.dispatchKeyEvent).
// Without 2 and 3 such pads showed as "not connected" and only two of their
// buttons did anything (upstream issue #7).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "app/config.h"

constexpr int kPadButtons = 48;

struct PadState {
  bool connected = false;
  bool xinput = false;
  enum Kind : uint8_t { Standard, Generic, Basic } kind = Standard;  // (which of the three ways above)
  std::wstring name;
  bool buttons[kPadButtons] = {};
  float axes[6] = {};
};

struct _SDL_GameController;
struct _SDL_Joystick;

// "Basic mode" (thread-safe: called from Android's UI thread): the buttons
// held, as bits in the standard layout, and the stick or hat, -1..1 each.
void inputBasicPad(uint32_t buttons, float x, float y);
void inputBasicPadName(const char* name);

class Input {
public:
  bool init();
  void shutdown();
  void poll();
  void onKey(int vk, bool down);
  void clearKeys();
  void rescanDevices() { rescan_ = true; }

  // (a key down and up again before the next poll, as Back sends it, still reads as down once)
  bool keyDown(int vk) const { return vk >= 0 && vk < 256 && (keys_[vk] || sticky_[vk] > 0); }
  static constexpr int kMaxPads = 8;
  int padCount() const { return (int)pads_.size(); }
  const PadState* pad(int i) const { return (i >= 0 && i < (int)pads_.size()) ? &pads_[i] : nullptr; }
  bool anyPad() const;  // a controller is connected
  bool anyPadActivity() const;  // ... and a button is down or a stick is pushed

  bool active(const Binding& b, int padIdx, float deadzone) const;
  bool capture(Binding* out, bool pads, int padIdx, float deadzone);
  void resetCapture();

private:
  bool keys_[256] = {};
  bool prevKeys_[256] = {};
  uint8_t sticky_[256] = {};
  std::vector<PadState> pads_;
  std::vector<PadState> prevPads_;
  std::vector<_SDL_GameController*> ctl_;
  std::vector<_SDL_Joystick*> joy_;  // (a slot has one or the other, or neither)
  int basicSlot_ = -1;
  bool rescan_ = true;
  int newSlot();
  void scan();
};

// SDL scancode -> Windows virtual-key code (0 if unmapped).
int vkFromScancode(int scancode);
