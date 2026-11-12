// SMAS Mobile: the application (hub, game, menus, saves, achievements).
//
// Everything here is platform-independent C++ and is built and tested on a
// desktop host as well (tests/). The few things only Android can do (the
// window and main loop, the system file pickers, vibration, the display's
// safe area) are behind app/platform.h, implemented in
// platform/android/app_android.cpp.
//
// The source is split by concern:
//   app.cpp        the frame loop, input, layout, the in-game and settings menus
//   app_hub.cpp    the hub, the game file (import and validation), Help, About
//   app_saves.cpp  saved progress: battery saves, save states, the automatic
//                  resume point, backups, Save Management
#pragma once
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/achstore.h"
#include "app/backup.h"
#include "app/canvas.h"
#include "app/config.h"
#include "app/hub.h"
#include "app/menu.h"
#include "app/music.h"
#include "app/romcheck.h"
#include "app/savestore.h"
#include "core/rewind.h"
#include "core/snes.h"
#include "game/smas.h"
#include "platform/android/audio_sdl.h"
#include "platform/android/input_sdl.h"
#include "platform/android/touch.h"
#include "platform/android/video_gles.h"

extern const wchar_t* kAppTitle;      // L"SMAS Mobile"
extern const wchar_t kLogFileName[];  // L"smas-mobile.log", in the app's folder on shared storage
extern const char* kAppName;          // "SMAS Mobile"
extern const char* kAppVersion;       // "1.0.0" (version.properties, through the build)
extern const char* kPublisher;        // "LIN4CRE"
extern const char* kUpstreamVersion;  // the open-source port this app is built on: "1.1.1"
extern const char* kSourceUrl;
extern const char* kReleasesUrl;
extern const char* kUpstreamUrl;
// "1.2.3" as 10203 (recorded in save files).
uint32_t appVersionNumber();

struct App {
  bool running = true;
  bool focused = true;
  Config cfg;
  // The app's private folder and what is kept in it.
  std::wstring exeDir, configPath, saveDir, shotDir, romDir, tmpDir, romPath, logPath;
  std::wstring msuDir;     // MSU-1 music packs (see app/msupack.h)
  std::wstring msuLoaded;  // the pack the console runs with (empty: none)
  std::string msuStatus;   // what became of the chosen pack, for the Audio page
  std::wstring msuBase;    // its tracks' path without "-N.pcm" (empty: none in use)
  std::unique_ptr<Snes> snes;
  std::unique_ptr<SmasGame> game;
  Video video;
  Audio audio;
  Input input;
  Menu menu;
  MusicPlayer music;

  // ---- the hub (app_hub.cpp) -------------------------------------------
  Hub hub;
  bool inHub = false;             // the hub shows; the game's console isn't run meanwhile
  Hub::Action hubTap = Hub::kNone;  // (a tile tapped: see frameHub)
  GameId playing = GameId::SMB1;  // the game last started
  bool menuHintShown = false;
  void frameHub(double dt);
  void refreshHub();              // the hub's lines, from the app's state
  void hubAction(Hub::Action a);
  void launchGame(GameId g);
  void returnToHub();
  void requestQuit();             // everything saved, then the app closes
  static GameId gameOfIndex(int i);  // 0-3 the four games, 4 the cartridge's own title screen
  static int indexOfGame(GameId g);
  static ColorF gameAccent(GameId g);

  // ---- the game file (app_hub.cpp) -------------------------------------
  RomCheck romInfo;               // what the game file in use is (see app/romcheck.h)
  bool loadRom();                 // the stored game file into a new console
  bool findRom(std::wstring* out);
  void romReady();
  void applyMsu(std::vector<uint8_t>& rom);
  void reloadForMsu();
  void removeGameFile();
  // A file the player chose, already copied to tmpDir: checked, and only if it
  // is the game moved into place. Says what happened either way.
  void importGameFile(const std::wstring& candidate, const std::string& shownName);

  // ---- system pickers (through app/platform.h) -------------------------
  enum class Picking { None, GameFile, Backup, Export, LogExport } picking = Picking::None;
  void pickGameFile();
  void pickBackup();
  void exportBackup();
  void exportLog();   // a copy of the log file, saved where the player chooses
  void pollPick();
  BackupSummary exportSummary;

  // ---- saved progress (app_saves.cpp) ----------------------------------
  BatteryStore battery;           // the games' own save files
  AsyncWriter writer;             // (the resume point, written off the main thread)
  bool restorePointKept = false;  // this session's restore point was made
  bool sramFailed = false;        // the last save of game progress failed (reported once)
  double sramTimer = 0;
  int stateSlot = 1;
  int pendingSave = -1;           // (a save asked for in a co-op turn's middle: see saveState)
  std::vector<uint8_t> undoState; // the moment before the last Load State (Undo Load State)
  int undoSlot = 0;
  // The automatic resume point ("Continue Playing").
  bool haveResume = false;
  seal::Info resumeInfo;
  double resumeTimer = 0;
  uint32_t resumeGeneration = 0;
  void loadSram();
  void saveSram();
  void saveState(int slot);
  void loadState(int slot);
  void undoLoadState();
  // Loads a state into the console, keeping the games' own save files as they
  // are now (a state never takes back progress the game has saved).
  bool applyState(const std::vector<uint8_t>& st);
  void saveResume(bool now);      // now: on this thread, before returning (the app is going away)
  void checkResume();             // is there a point to continue from?
  void continueGame();
  void dropResume();
  void restoreBackupFile(const std::wstring& file, const std::string& shownName);
  std::wstring statePath(int slot) const;
  std::wstring resumePath() const;
  std::string whenText(long long t) const;  // "today, 18:30" / "12 Nov, 18:30"

  // ---- lifecycle (called by the platform's main loop) ------------------
  void onBackground();   // the app is leaving the screen: everything is saved, the game pauses
  void onForeground();
  void onLowMemory();
  void onTerminating();
  void onDeviceReset();  // the graphics context was lost and made again
  void onResized(int w, int h);

  // ---- on-screen controls ----------------------------------------------
  TouchControls touch;
  bool touchActive = true;   // touch is the input in use (else a controller or keys)
  float insets[4] = {0, 0, 0, 0};  // the display's safe area: left, top, right, bottom, in pixels
  bool editingTouch = false; // the layout editor is open
  bool editorFromGame = false;
  unsigned exclusionKey = 0; // (the control rectangles last given to the system)
  void touchFrame();
  void openTouchEditor();
  void closeTouchEditor();
  void drawTouchEditor(Canvas* cv);
  bool editorTap(float x, float y);
  // The display's safe area and density (asked of the system at start, when the
  // screen changes, and every two seconds: a phone turned the other way round
  // moves its cutout without the screen's size changing).
  void refreshInsets();
  double screenTimer = 0;
  bool touchShown() const;   // the on-screen controls are in use (and so the 4:3 picture, if chosen)

  struct SDL_Window* window = nullptr;
  int lastWantExt = 0;       // widescreen margin chosen by the last usable layout
  float menuAnim = 0;
  // input state
  uint16_t padState[2] = {0, 0};
  uint16_t padSwallow[2] = {0, 0};  // buttons held when a menu closed: not the game's until let go
  bool prevAction[2][kActCount] = {};
  bool prevSelect[2] = {false, false};
  bool prevMenuBtn[2] = {false, false};
  float selectHeld[2] = {0, 0};
  bool selectConsumed[2] = {false, false};
  float selectPulse[2] = {0, 0};  // seconds of game Select still to send
  bool menuWasOpen = false;
  bool prevMenuNav[8] = {};
  // timing
  struct Pacing {
    int mode = -1;        // kPace* in effect
    double hz = 60;       // display refresh
    int perFrame = 0;     // refreshes per game frame when locked (0 = not a whole number)
    int vblanks = 0;      // refreshes since the last game frame (locked mode)
    double accum = 0;     // game time owed (unlocked vsync)
  } pacing;
  double fpsTimer = 0;
  int fpsFrames = 0;
  double fps = 0;
  // (for the log: how the last stretch of play ran)
  struct Perf {
    double window = 0;
    int passes = 0, frames = 0, slow = 0;
    double worstMs = 0, emuMs = 0;
  } perf;
  std::vector<int16_t> audioBuf;
  bool fastForward = false;
  // Rewind: a snapshot every other frame (see stepRewind).
  Rewind rewind;
  bool rewindHeld = false;  // the Rewind button is held (and rewinding is on)
  bool rewinding = false;   // showing the past: the machine is at a snapshot (plus a frame to draw it)
  unsigned rewindCount = 0;
  bool stepRewind(int steps);
  void endRewind();
  void restoreSnapshot(const std::vector<uint8_t>& st);

  // ---------------------------------------------------------------------
  bool init();      // (platform)
  void shutdown();  // (platform)
  void run();       // (platform)
  // After the platform has made the window, video, audio and input: folders,
  // settings, the game file, saves, the hub. Shared with the host tests.
  void start(const std::wstring& dataDir, const std::wstring& sharedDir);
  void stop();      // everything saved (before the platform tears down)
  void frame(double dt, int gameFrames);
  void render();
  void applyConfig();
  void computeLayout(IRect* dest, int* ext, int* srcW);
  void updateInput(double dt);
  void readPlayer(int p, uint16_t* snesPad, double dt);
  MenuInput menuInput();
  void openMenu();
  void pushPage(std::shared_ptr<MenuPage> p);
  void screenshot();
  // menu pages
  std::shared_ptr<MenuPage> pageMain();
  std::shared_ptr<MenuPage> pageDisplay();
  std::shared_ptr<MenuPage> pageGraphics();
  std::shared_ptr<MenuPage> pageColor();
  std::shared_ptr<MenuPage> pageAudio();
  std::shared_ptr<MenuPage> pageControls();
  std::shared_ptr<MenuPage> pageTouch();
  std::shared_ptr<MenuPage> pageBindings(int player, bool pad);
  std::shared_ptr<MenuPage> pageCheats();
  std::shared_ptr<MenuPage> pageLevelSelect();
  std::shared_ptr<MenuPage> pageSoundSelect();
  std::shared_ptr<MenuPage> pageMusicPlayer();
  std::shared_ptr<MenuPage> pageOptions();
  std::shared_ptr<MenuPage> pageAchievements();
  // (app_hub.cpp)
  std::shared_ptr<MenuPage> pageGameFile();
  std::shared_ptr<MenuPage> pageImportResult(const RomCheck& rc, const std::string& shownName, bool imported);
  std::shared_ptr<MenuPage> pageHelp();
  std::shared_ptr<MenuPage> pageHelpTopic(int topic);
  std::shared_ptr<MenuPage> pageAbout();
  std::shared_ptr<MenuPage> pageLicences();
  std::shared_ptr<MenuPage> pageUpdates();
  std::shared_ptr<MenuPage> pageText(const std::string& title, const std::string& subtitle, const std::string& text);
  // (app_saves.cpp)
  std::shared_ptr<MenuPage> pageSaves();
  std::shared_ptr<MenuPage> pageRestorePoints();
  std::shared_ptr<MenuPage> pageStateSlots();
  std::shared_ptr<MenuPage> pageStateSlot(int slot);
  std::string slotText(int slot) const;  // "12 Nov, 18:30" or "Empty"
  std::string bindingsText(int action) const;  // "F5 / LB": what does it (player 1)
  // achievements (see game/achievements.h)
  AchievementStore achStore;
  void achNewRun(bool counts);       // power-on / console reset (counts if on) or level select (doesn't)
  void achFrame();                   // after emulation: earned ones pop up and are stored
  void achAward(const std::string& id);
  void achCheckCollection();
  void setAchievements(bool on);
  MenuItem achievementsSwitch();     // Options, and the top of the Achievements page
  void achWelcome();                 // once, on the first start
  bool achLocked() const { return cfg.achievements; }  // cheats, level select, fast forward, rewind off
  // rebinding
  int bindPlayer = 0, bindAction = -1;
  bool bindPad = false;
  double bindTimer = 0;
  void startBind(int player, bool pad, int action);
  void updateBind(double dt);
};

extern App* g_app;
extern FILE* g_log;
void logf(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
std::string narrow(const std::wstring& w);
bool readAll(const std::wstring& path, std::vector<uint8_t>& out);
bool writeAll(const std::wstring& path, const void* d, size_t n);
// The pacing modes (see App::run).
enum { kPaceVsync = 0, kPaceTimer = 1 };

// Buttons that were held when a menu closed stay out of the game until they
// are let go: the press that chose "Save State" or "Resume" is not also a
// jump (upstream issue #30). `swallow` is the set still being held back.
inline uint16_t swallowHeld(uint16_t pad, uint16_t* swallow) {
  *swallow &= pad;
  return (uint16_t)(pad & ~*swallow);
}
