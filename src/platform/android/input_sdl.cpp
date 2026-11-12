// Controllers and keyboards on Android: see input_sdl.h.
#include "platform/android/input_sdl.h"
#include <SDL.h>
#include <cstring>
#include "app/canvas.h"
#include "app/vkeys.h"

bool Input::init() {
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");  // buttons by position (Nintendo pads too)
  if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  rescan_ = true;
  return true;
}

void Input::shutdown() {
  for (auto*& c : ctl_)
    if (c) SDL_GameControllerClose((SDL_GameController*)c);
  ctl_.clear();
  pads_.clear();
  clearKeys();
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

// Pads keep the slot they first got; an unplugged one leaves it disconnected.
void Input::scan() {
  rescan_ = false;
  for (size_t i = 0; i < ctl_.size(); i++)
    if (ctl_[i] && !SDL_GameControllerGetAttached((SDL_GameController*)ctl_[i])) {
      SDL_GameControllerClose((SDL_GameController*)ctl_[i]);
      ctl_[i] = nullptr;
      pads_[i].connected = false;
    }
  for (int j = 0; j < SDL_NumJoysticks(); j++) {
    if (!SDL_IsGameController(j)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
    bool open = false;
    for (auto* c : ctl_)
      if (c && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick((SDL_GameController*)c)) == id) open = true;
    if (open) continue;
    SDL_GameController* c = SDL_GameControllerOpen(j);
    if (!c) continue;
    size_t slot = ctl_.size();
    for (size_t i = 0; i < ctl_.size(); i++)
      if (!ctl_[i]) {
        slot = i;
        break;
      }
    if (slot == ctl_.size()) {
      if ((int)slot >= kMaxPads) {
        SDL_GameControllerClose(c);
        continue;
      }
      ctl_.push_back(nullptr);
      pads_.push_back(PadState());
    }
    ctl_[slot] = (_SDL_GameController*)c;
    PadState& p = pads_[slot];
    p = PadState();
    p.connected = true;
    const char* n = SDL_GameControllerName(c);
    p.name = widenUtf8(n ? n : "Controller");
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
  for (size_t i = 0; i < ctl_.size(); i++) {
    SDL_GameController* c = (SDL_GameController*)ctl_[i];
    PadState& p = pads_[i];
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
