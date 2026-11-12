// Controllers and keyboards on Android: see input_sdl.h.
#include "platform/android/input_sdl.h"
#include <SDL.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include "app/canvas.h"
#include "app/vkeys.h"

namespace {
// "Basic mode" state, written by Android's UI thread, read by poll().
std::atomic<uint32_t> g_basicButtons{0};
std::atomic<int> g_basicX{0}, g_basicY{0};  // thousandths
std::atomic<bool> g_basicSeen{false};
std::mutex g_basicNameLock;
std::string g_basicName;
}  // namespace

void inputBasicPad(uint32_t buttons, float x, float y) {
  g_basicButtons.store(buttons, std::memory_order_relaxed);
  g_basicX.store((int)(x * 1000.0f), std::memory_order_relaxed);
  g_basicY.store((int)(y * 1000.0f), std::memory_order_relaxed);
  g_basicSeen.store(true, std::memory_order_relaxed);
}

void inputBasicPadName(const char* name) {
  std::lock_guard<std::mutex> lock(g_basicNameLock);
  g_basicName = name ? name : "";
}

bool Input::init() {
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");  // buttons by position (Nintendo pads too)
  if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  rescan_ = true;
  return true;
}

void Input::shutdown() {
  for (auto*& c : ctl_)
    if (c) SDL_GameControllerClose((SDL_GameController*)c);
  for (auto*& j : joy_)
    if (j) SDL_JoystickClose((SDL_Joystick*)j);
  ctl_.clear();
  joy_.clear();
  pads_.clear();
  basicSlot_ = -1;
  clearKeys();
}

// A free slot (one whose pad was unplugged, or a new one); -1 when all are taken.
int Input::newSlot() {
  for (size_t i = 0; i < pads_.size(); i++)
    if (!ctl_[i] && !joy_[i] && (int)i != basicSlot_) return (int)i;
  if ((int)pads_.size() >= kMaxPads) return -1;
  ctl_.push_back(nullptr);
  joy_.push_back(nullptr);
  pads_.push_back(PadState());
  return (int)pads_.size() - 1;
}

void Input::onKey(int vk, bool down) {
  if (vk <= 0 || vk >= 256) return;
  keys_[vk] = down;
  if (down) sticky_[vk] = 2;
}

void Input::clearKeys() {
  std::memset(keys_, 0, sizeof(keys_));
  std::memset(sticky_, 0, sizeof(sticky_));
}

bool Input::anyPad() const {
  for (const PadState& p : pads_)
    if (p.connected) return true;
  return false;
}

bool Input::anyPadActivity() const {
  for (const PadState& p : pads_) {
    if (!p.connected) continue;
    for (bool b : p.buttons)
      if (b) return true;
    for (int a = 0; a < 4; a++)
      if (p.axes[a] > 0.6f || p.axes[a] < -0.6f) return true;
  }
  return false;
}

// Pads keep the slot they first got; an unplugged one leaves it disconnected.
void Input::scan() {
  rescan_ = false;
  for (size_t i = 0; i < ctl_.size(); i++) {
    if (ctl_[i] && !SDL_GameControllerGetAttached((SDL_GameController*)ctl_[i])) {
      SDL_GameControllerClose((SDL_GameController*)ctl_[i]);
      ctl_[i] = nullptr;
      pads_[i].connected = false;
    }
    if (joy_[i] && !SDL_JoystickGetAttached((SDL_Joystick*)joy_[i])) {
      SDL_JoystickClose((SDL_Joystick*)joy_[i]);
      joy_[i] = nullptr;
      pads_[i].connected = false;
    }
  }
  for (int j = 0; j < SDL_NumJoysticks(); j++) {
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
    bool open = false;
    for (auto* c : ctl_)
      if (c && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick((SDL_GameController*)c)) == id) open = true;
    for (auto* k : joy_)
      if (k && SDL_JoystickInstanceID((SDL_Joystick*)k) == id) open = true;
    if (open) continue;
    if (SDL_IsGameController(j)) {
      SDL_GameController* c = SDL_GameControllerOpen(j);
      if (!c) continue;
      const int slot = newSlot();
      if (slot < 0) {
        SDL_GameControllerClose(c);
        continue;
      }
      ctl_[slot] = (_SDL_GameController*)c;
      PadState& p = pads_[slot];
      p = PadState();
      p.connected = true;
      const char* n = SDL_GameControllerName(c);
      p.name = widenUtf8(n ? n : "Controller");
      continue;
    }
    // A pad SDL has no layout for: its buttons, hat and axes as they come.
    // (Not sensors and the like: something with buttons or a hat.)
    SDL_Joystick* js = SDL_JoystickOpen(j);
    if (!js) continue;
    if (SDL_JoystickNumButtons(js) < 2 && SDL_JoystickNumHats(js) < 1) {
      SDL_JoystickClose(js);
      continue;
    }
    const int slot = newSlot();
    if (slot < 0) {
      SDL_JoystickClose(js);
      continue;
    }
    joy_[slot] = (_SDL_Joystick*)js;
    PadState& p = pads_[slot];
    p = PadState();
    p.connected = true;
    p.kind = PadState::Generic;
    const char* n = SDL_JoystickName(js);
    p.name = widenUtf8(std::string(n ? n : "Controller") + " (generic)");
  }
}

void Input::poll() {
  for (uint8_t& s : sticky_)
    if (s) s--;
  if (rescan_) scan();
  static const SDL_GameControllerButton kMap[14] = {
      SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_B,          SDL_CONTROLLER_BUTTON_X,
      SDL_CONTROLLER_BUTTON_Y,         SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
      SDL_CONTROLLER_BUTTON_BACK,      SDL_CONTROLLER_BUTTON_START,      SDL_CONTROLLER_BUTTON_LEFTSTICK,
      SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_DPAD_UP,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,
      SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
  };
  // "Basic mode": a pad that only reaches the app as key presses (see input_sdl.h).
  if (basicSlot_ < 0 && g_basicSeen.load(std::memory_order_relaxed)) {
    basicSlot_ = newSlot();
    if (basicSlot_ >= 0) {
      PadState& p = pads_[basicSlot_];
      p = PadState();
      p.connected = true;
      p.kind = PadState::Basic;
      std::lock_guard<std::mutex> lock(g_basicNameLock);
      p.name = widenUtf8((g_basicName.empty() ? std::string("Controller") : g_basicName) + " (basic mode)");
    }
  }
  for (size_t i = 0; i < pads_.size(); i++) {
    SDL_GameController* c = (SDL_GameController*)ctl_[i];
    SDL_Joystick* js = (SDL_Joystick*)joy_[i];
    PadState& p = pads_[i];
    if ((int)i == basicSlot_) {
      const uint32_t bits = g_basicButtons.load(std::memory_order_relaxed);
      for (int b = 0; b < 32 && b < kPadButtons; b++) p.buttons[b] = (bits >> b) & 1;
      p.axes[0] = g_basicX.load(std::memory_order_relaxed) / 1000.0f;
      p.axes[1] = g_basicY.load(std::memory_order_relaxed) / 1000.0f;
      continue;
    }
    if (js) {
      // Buttons 0-9 as they come; the first hat as the D-pad (10-13); any more
      // buttons after the standard sixteen. Axes 0-5 as they come.
      std::memset(p.buttons, 0, sizeof(p.buttons));
      const int nb = SDL_JoystickNumButtons(js);
      for (int b = 0; b < nb; b++) {
        const int to = b < 10 ? b : b + 6;
        if (to < kPadButtons) p.buttons[to] = SDL_JoystickGetButton(js, b) != 0;
      }
      if (SDL_JoystickNumHats(js) > 0) {
        const Uint8 h = SDL_JoystickGetHat(js, 0);
        p.buttons[10] = (h & SDL_HAT_UP) != 0;
        p.buttons[11] = (h & SDL_HAT_DOWN) != 0;
        p.buttons[12] = (h & SDL_HAT_LEFT) != 0;
        p.buttons[13] = (h & SDL_HAT_RIGHT) != 0;
      }
      const int na = SDL_JoystickNumAxes(js);
      for (int a = 0; a < 6; a++) p.axes[a] = a < na ? SDL_JoystickGetAxis(js, a) / 32767.0f : 0.0f;
      continue;
    }
    if (!c) {
      std::memset(p.buttons, 0, sizeof(p.buttons));
      std::memset(p.axes, 0, sizeof(p.axes));
      continue;
    }
    for (int b = 0; b < 14; b++) p.buttons[b] = SDL_GameControllerGetButton(c, kMap[b]) != 0;
    p.buttons[14] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
    p.buttons[15] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
    p.buttons[16] = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_GUIDE) != 0;
    p.axes[0] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    p.axes[1] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    p.axes[2] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    p.axes[3] = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
  }
}

bool Input::active(const Binding& b, int padIdx, float dz) const {
  switch (b.type) {
    case Binding::Key: return keyDown(b.code);
    case Binding::PadButton: {
      const PadState* p = pad(padIdx);
      return p && b.code < kPadButtons && p->buttons[b.code];
    }
    case Binding::PadAxisPos: {
      const PadState* p = pad(padIdx);
      return p && b.code < 6 && p->axes[b.code] > dz;
    }
    case Binding::PadAxisNeg: {
      const PadState* p = pad(padIdx);
      return p && b.code < 6 && p->axes[b.code] < -dz;
    }
    case Binding::PadHat: {
      const PadState* p = pad(padIdx);
      return p && p->buttons[10 + (b.code & 3)];
    }
    default: return false;
  }
}

void Input::resetCapture() {
  std::memcpy(prevKeys_, keys_, sizeof(keys_));
  prevPads_ = pads_;
}

bool Input::capture(Binding* out, bool wantPads, int padIdx, float) {
  bool got = false;
  if (!wantPads) {
    for (int k = 1; k < 256 && !got; k++)
      if (keyDown(k) && !prevKeys_[k]) {
        out->type = Binding::Key;
        out->code = (uint16_t)k;
        got = true;
      }
  } else if (const PadState* p = pad(padIdx)) {
    const PadState* q = padIdx < (int)prevPads_.size() ? &prevPads_[padIdx] : nullptr;
    for (int i = 0; i < kPadButtons && !got; i++)
      if (p->buttons[i] && !(q && q->buttons[i])) {
        out->type = Binding::PadButton;
        out->code = (uint16_t)i;
        got = true;
      }
    for (int a = 0; a < 6 && !got; a++) {
      float prev = q ? q->axes[a] : 0;
      if (p->axes[a] > 0.6f && prev <= 0.6f) {
        out->type = Binding::PadAxisPos;
        out->code = (uint16_t)a;
        got = true;
      } else if (p->axes[a] < -0.6f && prev >= -0.6f) {
        out->type = Binding::PadAxisNeg;
        out->code = (uint16_t)a;
        got = true;
      }
    }
  }
  resetCapture();
  return got;
}

int vkFromScancode(int sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
  if (sc == SDL_SCANCODE_0) return '0';
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return VK_NUMPAD0 + 1 + (sc - SDL_SCANCODE_KP_1);
  switch (sc) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_AC_BACK: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    default: return 0;
  }
}
