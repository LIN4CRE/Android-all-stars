// A stand-in for Android, for the host tests: the video, audio and input
// layers do nothing visible (the canvas records what is drawn), and the
// system's pickers, vibration and safe area answer whatever the test says.
// With it the whole application (app/*.cpp) runs headless on a desktop.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "app/app.h"
#include "app/canvas.h"

namespace fake {

struct Drawn {
  enum Kind { Fill, Stroke, Text, Image } kind = Fill;
  RectF r{};
  std::wstring text;
  float size = 0;
  float width = 0;  // (text: its measured width)
};

struct Platform {
  int width = 2400, height = 1080;
  float insets[4] = {0, 0, 0, 0};
  bool insetsKnown = true;
  float density = 2.625f;  // pixels per dp (a 1080-line phone's; 0: the system does not say)
  float fontScale = 1.0f;  // the system's font-size setting

  // The player's answer to the next system picker.
  enum Answer { Choose, Cancel, Fail, TooLarge } answer = Choose;
  std::vector<uint8_t> document;  // what they choose (Choose)
  std::string documentName = "file.bin";
  bool pickerOpens = true;

  // What the app did.
  int pickersOpened = 0, saversOpened = 0;
  std::vector<uint8_t> saved;  // the last document saved with "save as"
  std::string savedName, savedMime;
  std::vector<std::string> urls;
  int haptics = 0;
  int sixtyHz = -1;  // what the app last asked of the display (-1: nothing yet)
  std::vector<IRect> exclusion;
  long long audioFrames = 0;

  // A controller: connected or not, and the buttons held (standard layout bits).
  bool pad = false;
  uint32_t padButtons = 0;

  std::vector<Drawn> drawn;  // the last frame's drawing

  // Everything drawn is drawn on this canvas as well, and text is measured
  // with it (the preview tool's software canvas: see soft_canvas.h).
  Canvas* mirror = nullptr;
};

Platform& sys();
void reset();

// One run of the app, from its start to the end of its process.
struct Session {
  std::unique_ptr<App> app;
  std::wstring dataDir, sharedDir;
  Session(const std::wstring& data, const std::wstring& shared);
  ~Session();
  void frames(int n = 1, int gameFrames = 1);
  // A quick tap at a point.
  void tap(float x, float y);
  // The menu's top page: the row with this label is used (true if it was there and usable).
  bool choose(const std::string& label);
  bool hasRow(const std::string& label);
  std::string pageTitle();
  std::string pageText();  // every row's label and value, a line each
  // Whether the last frame drew this text anywhere.
  bool drew(const std::wstring& text);
  // The app ends as when Android kills it in the background: nothing more is saved.
  void kill();
  // ... or as when the player exits.
  void exit();
};

// A buffer that the game-file check accepts as "Super Mario All-Stars,
// another version": 2 MiB with only the cartridge header's title and a
// four-instruction program that idles. It contains no game data whatsoever
// and exists only in memory and in the tests' temporary folders.
std::vector<uint8_t> syntheticGameFile(uint8_t fill = 0xff);

}  // namespace fake
