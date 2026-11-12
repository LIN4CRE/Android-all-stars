// Keyboard keys as Windows virtual-key codes, on every platform: the settings
// file stores key bindings as these numbers (Android maps its key codes to
// them, see platform/android/input_sdl.cpp).
#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
enum : int {
  VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12,
  VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24,
  VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E,
  VK_NUMPAD0 = 0x60, VK_NUMPAD9 = 0x69, VK_F1 = 0x70, VK_F4 = 0x73, VK_F5 = 0x74, VK_F8 = 0x77, VK_F11 = 0x7A,
  VK_F12 = 0x7B, VK_F24 = 0x87, VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3,
  VK_LMENU = 0xA4, VK_RMENU = 0xA5, VK_OEM_1 = 0xBA, VK_OEM_PLUS = 0xBB, VK_OEM_COMMA = 0xBC, VK_OEM_MINUS = 0xBD,
  VK_OEM_PERIOD = 0xBE, VK_OEM_2 = 0xBF, VK_OEM_3 = 0xC0, VK_OEM_4 = 0xDB, VK_OEM_5 = 0xDC, VK_OEM_6 = 0xDD,
  VK_OEM_7 = 0xDE,
};
#endif
