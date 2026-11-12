// The hub: SMAS Mobile's home screen, shown at start and whenever no game is
// running. Everything the app can do starts here:
//
//   Continue Playing        where you stopped, exactly (the automatic resume state)
//   Import Game File        shown first while no game file has been imported
//   the four games, and the cartridge's own title screen
//   Game File               import, replace or check your own game file
//   Save Management         back up and restore saves, restore points, save states
//   Controls                on-screen controls (size, layout, haptics), controllers
//   Display, Audio          picture and sound settings
//   Achievements, Music Player
//   Help, About             troubleshooting; licences, credits, what this app is and is not
//   Updates                 opens the releases page (the app itself never goes online)
//   Exit
//
// It is drawn with the app's own Canvas (no Android views), works with touch,
// a controller or a keyboard, and keeps clear of the display's cutout. All
// artwork is drawn here from plain shapes: nothing from the games is shown.
#pragma once
#include <string>
#include <vector>
#include "app/canvas.h"
#include "app/menu.h"

class Hub {
public:
  enum Action {
    kNone = 0,
    kContinue,
    kImport,
    kPlaySMB1,
    kPlaySMBLL,
    kPlaySMB2,
    kPlaySMB3,
    kPlayAllStars,
    kGameFile,
    kSaves,
    kControls,
    kDisplay,
    kAudio,
    kAchievements,
    kMusic,
    kHelp,
    kAbout,
    kUpdates,
    kExit,
    kActionCount
  };
  // What the hub shows of the app's state (set whenever it changes).
  struct State {
    bool haveGame = false;       // a game file has been imported
    bool gameVerified = false;   // ... and is exactly the supported version
    bool haveResume = false;     // there is a point to continue from
    std::string resumeLine;      // "Super Mario Bros. 3  -  today, 18:30"
    std::string gameLine;        // "Verified" / "Not imported yet"
    std::string savesLine;       // "Saved today, 18:30"
    std::string achievementsLine;  // "12 of 60"
    std::string version;         // "1.0.0"
    bool confirmRight = false;   // (the pad's right face button confirms)
    bool touch = true;           // touch is the input in use: no focus ring, touch hints
  };
  void setState(const State& s);
  const State& state() const { return state_; }

  // Keys and controller: moves the focus; the action chosen with Confirm (kExit with Back).
  Action update(const MenuInput& in, float dt);
  // A tap: the tile's action (a tile that can't be used yet returns what is needed first).
  Action tap(float x, float y);
  void focus(Action a);
  Action focused() const;
  ColorF accent() const;

  // `insets`: left, top, right, bottom, in pixels.
  void draw(Menu& menu, int W, int H, const float insets[4]);

  // The tiles as laid out for a screen (also used by the tests).
  struct Tile {
    Action action = kNone;
    RectF r{};
    int kind = 0;  // 0 a tile, 1 the wide bar (Continue / Import), 2 a game card
    bool enabled = true;
  };
  std::vector<Tile> layout(int W, int H, const float insets[4]) const;
  static const char* title(Action a);
  static ColorF gameColor(Action a);

private:
  State state_;
  Action focus_ = kNone;
  bool ring_ = false;      // the focus ring shows (keys or a controller were used)
  float repeat_ = 0;
  int repeatDir_ = 0;
  float time_ = 0;
  std::vector<Tile> tiles_;  // (as last drawn: taps and focus moves use these)
  int lastW_ = 0, lastH_ = 0;
  float lastInsets_[4] = {0, 0, 0, 0};
  void ensureLayout();
  void move(int dx, int dy);
  const Tile* find(Action a) const;
  Action useTile(const Tile& t) const;
  std::string subtitle(Action a) const;
};
