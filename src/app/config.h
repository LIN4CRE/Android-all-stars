// User settings, kept as a small text (INI) file in the app's private folder.
// It is written whole or not at all (core/fileio.h), and every value read
// from it is range-checked: a damaged or hand-edited file can at worst lose a
// setting, which then takes its default.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum AspectMode {
  kAspect4x3 = 0,
  kAspect16x10,
  kAspect16x9,
  kAspect21x9,
  kAspectAuto,  // match the display
  kAspectCount
};

enum FilterMode {
  kFilterNearest = 0,
  kFilterSharp,
  kFilterBilinear,
  kFilterCrtScanlines,
  kFilterCrtAperture,
  kFilterLcd,
  kFilterCount
};

enum ColorProfile {
  kColorOriginal = 0,
  kColorVivid,
  kColorCrt,
  kColorMuted,
  kColorGrayscale,
  kColorSepia,
  kColorPocket,     // 4-shade green handheld look
  kColorVirtual,    // red monochrome
  kColorNight,      // warm, reduced blue light
  kColorConsole,    // SNES video DAC response (darker mid-tones)
  kColorCount
};

// Bindable actions. The first 12 map to SNES buttons.
enum Action {
  kActUp = 0, kActDown, kActLeft, kActRight,
  kActA, kActB, kActX, kActY, kActL, kActR, kActStart, kActSelect,
  kActMenu, kActSaveState, kActLoadState, kActFastForward, kActScreenshot,
  kActRewind,
  kActCount
};

// A binding: keyboard VK code or joypad control. (Codes read from the
// settings file are range-checked: see decodeBinding in config.cpp.)
constexpr int kBindKeyCodes = 256, kBindPadButtons = 48, kBindPadAxes = 6, kBindHatDirs = 4;
struct Binding {
  enum Type : uint8_t { None = 0, Key, PadButton, PadAxisPos, PadAxisNeg, PadHat };
  Type type = None;
  uint16_t code = 0;  // VK / button index / axis index / hat direction
  bool operator==(const Binding& o) const { return type == o.type && code == o.code; }
};

// What can be moved in the on-screen controls' layout editor: the D-pad, the
// four face buttons (together), and each other button.
enum TouchGroup {
  kTouchDpad = 0, kTouchFace, kTouchL, kTouchR, kTouchSelect, kTouchStart, kTouchMenu, kTouchFF, kTouchRewind,
  kTouchGroupCount
};
constexpr int kTouchScaleMin = 70, kTouchScaleMax = 130;  // percent (no two controls overlap in this range)
constexpr int kTouchOffsetMax = 2500;                     // thousandths of the screen's height
const char* touchGroupKey(int g);   // its name in the settings file ("dpad")
const char* touchGroupName(int g);  // ... and on screen ("D-pad")

struct Config {
  // Display
  int aspect = kAspectAuto;
  bool touchPicture4x3 = true;  // while the on-screen controls show: a 4:3 picture, the controls beside it
  bool pixelAspectCrt = true;  // 8:7 pixels (as seen on a TV) vs square pixels
  int scaling = 0;             // 0 fit, 1 integer, 2 stretch
  bool showFps = false;
  bool screen60 = true;        // ask the system for the display's 60 Hz mode (see platform::preferSixtyHz)
  int lastGame = 0;            // the game chosen last: 0-3 the four games, 4 the cartridge's own title screen
  bool hudAnchor = true;
  bool dimWrap = true;         // SMB2: dim the margins of areas that wrap round
  int pillarbox = 1;           // 0 black, 1 blurred game, 2 dark frame
  int widescreenEnemies = 1;   // 1 = enemies active in full view
  bool coop = false;           // SMB1, LL, SMB3: two players at once, Luigi on controller 2
  bool nesBrickBounce = false; // SMB1, LL: a brick broken from below sends Mario back down, as on the NES
  // Graphics
  int filter = kFilterSharp;
  int scanlines = 45;          // 0-100
  int crtCurve = 0;            // 0-100
  bool noSpriteLimit = false;
  // Color
  int colorProfile = kColorOriginal;
  int brightness = 100, contrast = 100, saturation = 100, gamma = 100;
  int colorblind = 0;          // 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia
  int colorTemp = 0;           // -100 cool .. +100 warm
  int playerPalette = 0;       // character palette variant: Mario's
  int luigiPalette = 0;        // ... Luigi's
  // Audio
  int volume = 80;
  int audioLatency = 64;       // ms
  int musicVolume = 100;       // the games' music, percent (0: off, the sound effects kept)
  int sfxVolume = 100;         // ... and their sound effects
  std::wstring msuPack;        // an MSU-1 music pack: its folder in the msu folder, or empty (see app/msupack.h)
  int msuVolume = 100;         // its music's volume, percent
  int interpolation = 0;       // 0 = hardware gaussian
  // Input
  int selectMode = 0;          // 0 = Select opens the menu, 1 = tap: game Select / hold: menu
  int menuStyle = 0;           // the menus' look: 0 Storybook, 1 Retro, 2 Themed Dark, 3 Classic (Menu::Style)
  bool confirmRight = false;   // menus: the right face button confirms, the bottom one backs out (Nintendo's way)
  int deadzone = 35;           // percent
  int touchMode = 0;           // on-screen controls (touch screens): 0 while touch is used, 1 always, 2 never
  int touchOpacity = 70;       // percent
  int touchScale = 100;        // their size, percent of the standard one
  bool touchHaptics = true;    // a short vibration as a control is pressed
  // Each group's move from its standard place, in thousandths of the screen's height (x, y).
  int touchOffset[kTouchGroupCount][2] = {};
  bool pauseOnReturn = true;   // back from another app: the game waits, paused, until Resume
  bool autoResume = true;      // the point you stopped at is kept, for Continue Playing
  long long lastBackup = 0;    // when a backup was last exported (seconds since 1970; 0: never)
  bool rewind = true;          // hold Rewind to step back through the last minute
  bool fastForward = true;     // hold Fast Forward to run at 4x
  // Achievements count (cheats, level select, fast forward and rewind are off meanwhile)
  bool achievements = true;
  int padIndex[2] = {0, 1};    // which physical pad drives player 1/2
  Binding keys[2][kActCount];
  Binding pad[2][kActCount];
  // Cheats (bit flags per cheat id)
  uint64_t cheats = 0;

  void setDefaults();
  bool load(const std::wstring& path);
  bool save(const std::wstring& path) const;
};

const char* actionName(int a);
std::string bindingName(const Binding& b);
