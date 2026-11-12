// SMAS Mobile: the frame loop, input, layout, and the in-game and settings
// menus (see app.h).
#include "app/app.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "app/achexport.h"
#include "app/msupack.h"
#include "app/platform.h"
#include "app/sys.h"
#include "app/vkeys.h"
#include "core/fileio.h"
#include "core/png.h"

#ifndef SMAS_VERSION_NAME
#define SMAS_VERSION_NAME "0.0.0-dev"
#endif

const wchar_t* kAppTitle = L"SMAS Mobile";
const wchar_t kLogFileName[] = L"smas-mobile.log";
const char* kAppName = "SMAS Mobile";
const char* kAppVersion = SMAS_VERSION_NAME;
const char* kPublisher = "LIN4CRE";
const char* kUpstreamVersion = "1.1.1";
const char* kSourceUrl = "https://github.com/LIN4CRE/Android-all-stars";
const char* kReleasesUrl = "https://github.com/LIN4CRE/Android-all-stars/releases";
const char* kUpstreamUrl = "https://github.com/Darhkfox/SuperMarioAllStars";

uint32_t appVersionNumber() {
  int a = 0, b = 0, c = 0;
  sscanf(kAppVersion, "%d.%d.%d", &a, &b, &c);
  return (uint32_t)(std::clamp(a, 0, 999) * 10000 + std::clamp(b, 0, 99) * 100 + std::clamp(c, 0, 99));
}

App* g_app = nullptr;
FILE* g_log = nullptr;  // (opened by the platform)

void logf(const char* fmt, ...) {
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  fputs(timeText("%H:%M:%S ").c_str(), g_log);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  fflush(g_log);
}

std::string narrow(const std::wstring& w) { return narrowUtf8(w); }

bool readAll(const std::wstring& path, std::vector<uint8_t>& out) { return readFile(path, out, 16u << 20); }

bool writeAll(const std::wstring& path, const void* d, size_t n) { return writeFileSafely(path, d, n); }

// A rewind snapshot back in the machine. Battery saves are kept as they are
// now: rewinding never takes back progress the game has saved.
void App::restoreSnapshot(const std::vector<uint8_t>& st) {
  pendingSave = -1;
  std::vector<uint8_t> keep(snes->sram, snes->sram + sizeof(snes->sram));
  bool dirty = snes->sramDirty;
  if (!snes->loadState(st)) return;
  std::memcpy(snes->sram, keep.data(), keep.size());
  snes->sramDirty = dirty;
  game->onStateLoaded();
}

// While Rewind is held: `steps` snapshots back (each two frames, so twice the
// game's speed), the oldest staying on screen. Each is drawn by running a
// frame from it, silently; letting go returns to the last one shown (see
// endRewind). False (play on) when there is nothing to go back to.
bool App::stepRewind(int steps) {
  if (!rewinding && rewind.snapshots() < 2) return false;
  if (steps <= 0) return true;
  std::vector<uint8_t> st;
  bool moved = false;
  for (int i = 0; i < steps && rewind.stepBack(&st); i++) moved = true;
  if (!moved) {
    if (rewinding) return true;  // (at the oldest: stay there)
    return false;
  }
  if (!rewinding) audio.flush();
  rewinding = true;
  restoreSnapshot(st);
  game->beforeFrame();
  snes->runFrame();
  game->afterFrame();
  audioBuf.clear();
  snes->takeAudio(audioBuf);  // (not played)
  fpsFrames++;
  return true;
}

void App::endRewind() {
  rewinding = false;
  if (!rewind.newest().empty()) restoreSnapshot(rewind.newest());
  audio.flush();
}

void App::screenshot() {
  makeDir(shotDir);
  const std::wstring name = widenUtf8(timeText("smas_%Y%m%d_%H%M%S.png"));
  int eL = snes->ppu.extL, eR = snes->ppu.extR;
  bool ok = writePng(narrow(pathJoin(shotDir, name)), snes->ppu.frame + (kMaxExt - eL), 256 + eL + eR,
                     snes->ppu.frameHeight(), kLineW);
  menu.notify(ok ? "Screenshot saved to the app's screenshots folder" : "Screenshot failed");
}

// ---------------------------------------------------------------------------
// Display


void App::applyConfig() {
  audio.setVolume(cfg.volume / 100.0f);
  audio.setLatency(cfg.audioLatency);
  menu.confirmRight = cfg.confirmRight;
  menu.style = cfg.menuStyle;
  if (snes) snes->ppu.noSpriteLimit = cfg.noSpriteLimit;
  if (snes) {
    snes->apu.dsp.musicGain = cfg.musicVolume * 256 / 100;
    snes->apu.dsp.sfxGain = cfg.sfxVolume * 256 / 100;
  }
  // (a music pack's songs are music too: its own volume, times the music's)
  if (snes && snes->msu) snes->msu->setGain(cfg.msuVolume / 100.0f * cfg.musicVolume / 100.0f);
  music.setMsuGain(cfg.msuVolume / 100.0f);
  if (game) {
    game->hudAnchor = cfg.hudAnchor;
    game->dimWrap = cfg.dimWrap;
    game->enemyRange = cfg.widescreenEnemies != 0;
    game->setNesBrickBounce(cfg.nesBrickBounce);
    game->coop = cfg.coop;
    game->setCheats(achLocked() ? 0 : cfg.cheats);  // (achievements on: no cheats)
    game->playerPalette = cfg.playerPalette;
    game->luigiPalette = cfg.luigiPalette;
  }
}

// Decide the widescreen margin and the destination rectangle.
void App::computeLayout(IRect* dest, int* extOut, int* srcW) {
  // The picture keeps to the display's safe area (clear of a cutout).
  const int insL = (int)insets[0], insT = (int)insets[1];
  int W = video.width() - insL - (int)insets[2], H = video.height() - insT - (int)insets[3];
  if (W < 16 || H < 16) W = video.width(), H = video.height();
  double par = cfg.pixelAspectCrt ? 8.0 / 7.0 : 1.0;
  double target;
  // With the on-screen controls in use, a 4:3 picture leaves the sides of a
  // phone's wide screen for them: thumbs never cover the game.
  const int aspect = touchShown() && cfg.touchPicture4x3 ? (int)kAspect4x3 : cfg.aspect;
  switch (aspect) {
    case kAspect4x3: target = 0; break;
    case kAspect16x10: target = 16.0 / 10.0; break;
    case kAspect16x9: target = 16.0 / 9.0; break;
    case kAspect21x9: target = 21.0 / 9.0; break;
    default: target = (double)W / std::max(H, 1); break;
  }
  int wantExt = 0;
  if (target > 0) {
    double needW = 224.0 * target / par;
    wantExt = (int)std::floor((needW - 256.0) / 2.0);
    wantExt = std::clamp(wantExt, 0, SmasGame::kMaxSupportedExt);
  }
  // A minimised window has a 1x1 client area: keep the last layout, or the
  // game would switch to 4:3 (and its widescreen patches off) meanwhile.
  if (W < 16 || H < 16) wantExt = lastWantExt;
  lastWantExt = wantExt;
  game->setWidescreen(wantExt);
  int extL = game->activeExtL(), extR = game->activeExtR();
  *extOut = extL;
  int sw = 256 + extL + extR;
  *srcW = sw;
  int sh = snes->ppu.frameHeight();
  double imgW = sw * par, imgH = sh;
  // (The scale is the full wide picture's whatever is shown: a screen the
  // game shows 4:3 in a widescreen session (SMB2's open vertical areas, the
  // menus) keeps the size of the rest, beside it the pillarbox; it was drawn
  // a fifth bigger on a 16:9 display, the HUD jumping, as the view switched.)
  int fullSw = std::max(sw, 256 + 2 * wantExt);
  double dw, dh;
  if (cfg.scaling == 2) {  // stretch: fill the target aspect area
    double fullW = (target > 0 ? 224.0 * target : 256.0 * par);
    double s = std::min(W / fullW, H / imgH);
    dh = imgH * s;
    dw = imgW * s;
    if (extL + extR >= 2 * wantExt || target <= 0) {
      dw = std::min((double)W, fullW * s);
    }
  } else if (cfg.scaling == 1) {  // integer: whole multiples on both axes
    // (CRT pixels: the nearest whole width to 8/7 of the height)
    int s = std::max(1, (int)std::floor(H / imgH)), hs;
    for (;; s--) {
      hs = std::max(1, (int)std::lround(s * par));
      if (fullSw * hs <= W || s == 1) break;
    }
    dw = (double)sw * hs;
    dh = imgH * s;
  } else {
    double s = std::min(W / (fullSw * par), H / imgH);
    dw = imgW * s;
    dh = imgH * s;
    // The margins are whole pixels: fill the last bit of a display the
    // chosen aspect matches (a stretch of under two pixels).
    if (target > 0 && extL + extR >= 2 * wantExt && dw < W && W - dw < 2 * par * s + 1) dw = W;
  }
  int x0 = insL + (int)std::lround((W - dw) / 2), y0 = insT + (int)std::lround((H - dh) / 2);
  *dest = IRect{x0, y0, x0 + (int)std::lround(dw), y0 + (int)std::lround(dh)};
}

// ---------------------------------------------------------------------------
// Input

void App::readPlayer(int p, uint16_t* out, double dt) {
  float dz = cfg.deadzone / 100.0f;
  int padIdx = cfg.padIndex[p];
  bool act[kActCount];
  for (int a = 0; a < kActCount; a++) {
    act[a] = input.active(cfg.keys[p][a], padIdx, dz) || input.active(cfg.pad[p][a], padIdx, dz);
  }
  // Left stick always drives the D-pad
  if (const PadState* ps = input.pad(padIdx)) {
    act[kActLeft] |= ps->axes[0] < -dz;
    act[kActRight] |= ps->axes[0] > dz;
    act[kActUp] |= ps->axes[1] < -dz;
    act[kActDown] |= ps->axes[1] > dz;
  }
  // Select and the menu.
  // * The Menu bindings (Esc; the pad's View/Select by default) open the menu.
  // * selectMode 0: Select opens the menu too. The game's own Select (used by
  //   All-Stars' pause -> save menu) is on the left stick button, or
  //   Menu -> "Press Select In Game".
  // * selectMode 1: tap Select = game Select, hold Select = menu.
  auto isSelectBinding = [&](const Binding& b) { return b == cfg.keys[p][kActSelect] || b == cfg.pad[p][kActSelect]; };
  bool select = input.active(cfg.keys[p][kActSelect], padIdx, dz) || input.active(cfg.pad[p][kActSelect], padIdx, dz);
  bool menuBtn = false;
  for (const Binding* b : {&cfg.keys[p][kActMenu], &cfg.pad[p][kActMenu]})
    if (b->type != Binding::None && !isSelectBinding(*b) && input.active(*b, padIdx, dz)) menuBtn = true;
  bool menuPressed = menuBtn && !prevMenuBtn[p];
  bool selectOut = false;
  if (p != 0) {
    selectOut = select;  // player 2 has no menu button
  } else if (cfg.selectMode == 0) {
    if (select && !prevSelect[p]) menuPressed = true;
    bool lsFree = true;
    for (int a = 0; a < kActCount; a++)
      if (cfg.pad[p][a].type == Binding::PadButton && cfg.pad[p][a].code == 8) lsFree = false;
    const PadState* ps = input.pad(padIdx);
    if (lsFree && ps && ps->buttons[8]) selectOut = true;  // left stick press
  } else {
    if (select) {
      if (menu.open()) selectConsumed[p] = true;  // a press in the menu (Back) isn't a game tap
      selectHeld[p] += (float)dt;
      if (selectHeld[p] >= 0.4f && !selectConsumed[p]) {
        selectConsumed[p] = true;
        menuPressed = true;
      }
    } else {
      if (selectHeld[p] > 0 && !selectConsumed[p]) selectPulse[p] = 0.1f;  // tap -> game Select
      selectHeld[p] = 0;
      selectConsumed[p] = false;
    }
  }
  prevSelect[p] = select;
  prevMenuBtn[p] = menuBtn;
  if (selectPulse[p] > 0) {
    selectOut = true;
    selectPulse[p] -= (float)dt;
  }

  uint16_t v = 0;
  if (act[kActUp]) v |= kPadUp;
  if (act[kActDown]) v |= kPadDown;
  if (act[kActLeft]) v |= kPadLeft;
  if (act[kActRight]) v |= kPadRight;
  if (act[kActA]) v |= kPadA;
  if (act[kActB]) v |= kPadB;
  if (act[kActX]) v |= kPadX;
  if (act[kActY]) v |= kPadY;
  if (act[kActL]) v |= kPadL;
  if (act[kActR]) v |= kPadR;
  if (act[kActStart]) v |= kPadStart;
  if (selectOut) v |= kPadSelect;
  // no opposing directions
  if ((v & kPadUp) && (v & kPadDown)) v &= ~(kPadUp | kPadDown);
  if ((v & kPadLeft) && (v & kPadRight)) v &= ~(kPadLeft | kPadRight);
  *out = v;

  if (p == 0) {
    if (menuPressed && !menu.open() && !inHub && !editingTouch) {
      logf("menu opened by %s (select key=%d pad=%d, menu button=%d, pad %d)",
           select ? "Select" : "Menu button", (int)input.active(cfg.keys[p][kActSelect], padIdx, dz),
           (int)input.active(cfg.pad[p][kActSelect], padIdx, dz), (int)menuBtn, padIdx);
      openMenu();
    }
    auto edge = [&](int a) { return act[a] && !prevAction[p][a]; };
    if (!menu.open() && !inHub && !editingTouch) {
      if (edge(kActSaveState)) saveState(stateSlot);
      if (edge(kActLoadState)) loadState(stateSlot);
      if (edge(kActScreenshot)) screenshot();
    }
    // (Not while the level select drives the game to the chosen level.)
    const bool inGame = !inHub && !editingTouch && game && !game->levelLoading();
    act[kActRewind] |= touch.held(TouchControls::BtnRewind);
    act[kActFastForward] |= touch.held(TouchControls::BtnFF);
    rewindHeld = cfg.rewind && !achLocked() && act[kActRewind] && !menu.open() && inGame;
    fastForward = cfg.fastForward && !achLocked() && act[kActFastForward] && !menu.open() && !rewindHeld && inGame;
    // (pressed while achievements keep them off: say so, or they just seemed broken)
    if (achLocked() && !menu.open() && inGame && (edge(kActRewind) || edge(kActFastForward)))
      menu.notify("Fast forward and rewind are off while achievements are on (Settings > Achievements)", 3.5f);
  }
  for (int a = 0; a < kActCount; a++) prevAction[p][a] = act[a];
}

MenuInput App::menuInput() {
  MenuInput m;
  float dz = 0.5f;
  bool nav[8] = {};
  nav[0] = input.keyDown(VK_UP);
  nav[1] = input.keyDown(VK_DOWN);
  nav[2] = input.keyDown(VK_LEFT);
  nav[3] = input.keyDown(VK_RIGHT);
  nav[4] = input.keyDown(VK_RETURN) || input.keyDown(VK_SPACE);
  nav[5] = input.keyDown(VK_ESCAPE) || input.keyDown(VK_BACK);
  // When Select opens the menu, Select also backs out of it.
  if (cfg.selectMode == 0)
    nav[5] |= input.active(cfg.keys[0][kActSelect], cfg.padIndex[0], 0.5f) ||
              input.active(cfg.pad[0][kActSelect], cfg.padIndex[0], 0.5f);
  nav[6] = input.keyDown(VK_PRIOR) || input.keyDown('Q');
  nav[7] = input.keyDown(VK_NEXT) || input.keyDown('E');
  for (int i = 0; i < input.padCount(); i++) {
    const PadState* p = input.pad(i);
    if (!p) continue;
    nav[0] |= p->buttons[10] || p->axes[1] < -dz;
    nav[1] |= p->buttons[11] || p->axes[1] > dz;
    nav[2] |= p->buttons[12] || p->axes[0] < -dz;
    nav[3] |= p->buttons[13] || p->axes[0] > dz;
    // (face buttons by position: 0 bottom, 1 right; Menu Buttons picks which confirms)
    const int ok = cfg.confirmRight ? 1 : 0;
    nav[4] |= p->buttons[ok] || p->buttons[7];
    nav[5] |= p->buttons[1 - ok] || p->buttons[6];
    nav[6] |= p->buttons[4];
    nav[7] |= p->buttons[5];
  }
  // directions are level-triggered (menu handles repeat); buttons are edges
  m.up = nav[0];
  m.down = nav[1];
  m.left = nav[2];
  m.right = nav[3];
  m.confirm = nav[4] && !prevMenuNav[4];
  m.back = nav[5] && !prevMenuNav[5];
  m.tabLeft = nav[6] && !prevMenuNav[6];
  m.tabRight = nav[7] && !prevMenuNav[7];
  for (int i = 0; i < 8; i++) prevMenuNav[i] = nav[i];
  return m;
}

void App::openMenu() {
  if (menu.open()) return;
  menu.push(pageMain());
  menuAnim = 0;
  // swallow the button that opened the menu
  menuInput();
  prevMenuNav[4] = prevMenuNav[5] = true;
}

// A page over the hub or another page.
void App::pushPage(std::shared_ptr<MenuPage> p) {
  if (!menu.open()) menuAnim = 0;
  menu.push(std::move(p));
}

void App::updateInput(double dt) {
  input.poll();
  if (input.anyPadActivity()) touchActive = false;  // (a controller in use: the on-screen controls step aside)
  for (int p = 0; p < 2; p++) readPlayer(p, &padState[p], dt);
  touchFrame();
  padState[0] |= touch.takeSnesButtons();
  // A menu just closed (or the game was just entered): what is held now chose
  // something in the menu, and is not the game's until it is let go.
  const bool away = menu.open() || inHub || editingTouch;
  if (menuWasOpen && !away)
    for (int p = 0; p < 2; p++) padSwallow[p] = padState[p];
  menuWasOpen = away;
  if (!snes) return;
  for (int p = 0; p < 2; p++) snes->input[p] = away ? 0 : swallowHeld(padState[p], &padSwallow[p]);
}

// ---------------------------------------------------------------------------
// Rebinding

void App::startBind(int player, bool pad, int action) {
  bindPlayer = player;
  bindPad = pad;
  bindAction = action;
  bindTimer = 6.0;
  menu.capturing = true;
  input.resetCapture();
}

void App::updateBind(double dt) {
  if (bindAction < 0) return;
  bindTimer -= dt;
  char buf[160];
  snprintf(buf, sizeof buf, "Press a %s for \"%s\"  (Back cancels)  %d",
           bindPad ? "controller button" : "key", actionName(bindAction), (int)std::ceil(bindTimer));
  menu.captureText = buf;
  bool cancel = bindTimer <= 0;
  Binding b;
  if (input.keyDown(VK_ESCAPE)) cancel = true;
  else if (input.keyDown(VK_DELETE)) {
    (bindPad ? cfg.pad : cfg.keys)[bindPlayer][bindAction] = Binding();
    cancel = true;
  } else if (input.capture(&b, bindPad, cfg.padIndex[bindPlayer], cfg.deadzone / 100.0f)) {
    (bindPad ? cfg.pad : cfg.keys)[bindPlayer][bindAction] = b;
    cancel = true;
  }
  if (cancel) {
    bindAction = -1;
    menu.capturing = false;
    prevMenuNav[4] = prevMenuNav[5] = true;  // don't let the key also act in the menu
    menu.refresh();
    cfg.save(configPath);
  }
}

// ---------------------------------------------------------------------------
// Menu pages

std::shared_ptr<MenuPage> App::pageMain() {
  auto p = std::make_shared<MenuPage>();
  p->rebuild = [this](MenuPage& pg) {
    GameId g = game->current();
    // (on the All-Stars file panel the hub went to: the game being started)
    const bool inTitle = g == GameId::SMB1 || g == GameId::SMBLL || g == GameId::SMB2 || g == GameId::SMB3;
    pg.title = inTitle ? SmasGame::gameName(g)
               : playing == GameId::Menu ? "Super Mario All-Stars"
                                         : SmasGame::gameName(playing);
    pg.subtitle = "Paused";
    pg.items.clear();
    pg.items.push_back(MenuItem::action("Resume", [this]() { menu.closeAll(); }, "Return to the game."));
    pg.items.push_back(MenuItem::action("Press Select In Game", [this]() {
      menu.closeAll();
      selectPulse[0] = 0.12f;
    }, "Send the controller's Select button to the game, e.g. while paused to open the All-Stars save menu."));

    pg.items.push_back(MenuItem::header("Save states"));
    MenuItem slot = MenuItem::choice(
        "Slot", {"1", "2", "3", "4", "5", "6", "7", "8", "9"}, [this]() { return stateSlot - 1; },
        [this](int v) { stateSlot = v + 1; },
        "Choose the save-state slot (" + bindingsText(kActSaveState) + " saves, " + bindingsText(kActLoadState) +
            " loads in game).");
    slot.valueText = [this]() {
      // the slot and when it was saved (or that it's empty)
      return std::to_string(stateSlot) + "  \xE2\x80\xA2  " + slotText(stateSlot);
    };
    pg.items.push_back(slot);
    pg.items.push_back(MenuItem::action("Save State", [this]() {
      saveState(stateSlot);
      menu.closeAll();
    }, "Save a snapshot of the game to the selected slot. What the slot held before is kept as its previous "
       "version (Save Management)."));
    pg.items.push_back(MenuItem::action("Load State", [this]() {
      if (!states::slotUsed(saveDir, stateSlot)) {
        menu.notify("Slot " + std::to_string(stateSlot) + " is empty");
        return;
      }
      loadState(stateSlot);
      menu.closeAll();
    }, "Go back to the snapshot in the selected slot. Progress the game itself has saved is never undone by "
       "this, and Undo Load State takes you back to where you were."));
    if (!undoState.empty()) {
      pg.items.push_back(MenuItem::action("Undo Load State", [this]() {
        undoLoadState();
        menu.closeAll();
      }, "Back to the moment before you loaded state " + std::to_string(undoSlot) + "."));
    }

    pg.items.push_back(MenuItem::header("Game"));
    // with achievements on, the level select and cheats are greyed out (Options turns achievements off)
    auto locked = [this]() { return std::string(achLocked() ? "Off (achievements on)" : ""); };
    MenuItem ls = MenuItem::submenu("Level Select", [this]() { menu.push(pageLevelSelect()); },
                                    "Jump straight to any world and level of the four games.");
    ls.enabled = [this]() { return !achLocked(); };
    ls.valueText = locked;
    pg.items.push_back(ls);
    MenuItem ch = MenuItem::submenu("Cheats", [this]() { menu.push(pageCheats()); },
                                    "Toggle cheats for each game. They apply immediately.");
    ch.enabled = [this]() { return !achLocked(); };
    ch.valueText = locked;
    pg.items.push_back(ch);
    MenuItem ac = MenuItem::submenu("Achievements", [this]() { menu.push(pageAchievements()); },
                                    "Every game's achievements, and what you have earned.");
    ac.valueText = [this]() {
      int got = 0;
      for (const AchievementDef& d : achievementDefs()) got += achStore.unlocked(d.id) ? 1 : 0;
      return std::to_string(got) + " / " + std::to_string((int)achievementDefs().size());
    };
    pg.items.push_back(ac);
    pg.items.push_back(MenuItem::submenu("Sound Select", [this]() { menu.push(pageSoundSelect()); },
                                         "Classic sound test: play any music number and sound effect."));
    pg.items.push_back(MenuItem::submenu("Music Player", [this]() { menu.push(pageMusicPlayer()); },
                                         "Browse and listen to the soundtrack of all four games."));
    pg.items.push_back(MenuItem::submenu("Settings", [this]() { menu.push(pageOptions()); },
                                         "Display, graphics, colour, audio, controls, speed and achievements."));
    pg.items.push_back(MenuItem::submenu("Help", [this]() { menu.push(pageHelp()); },
                                         "How saving works, the controls, and what to do when something is wrong."));

    pg.items.push_back(MenuItem::header("System"));
    pg.items.push_back(MenuItem::action("Take Screenshot", [this]() { screenshot(); },
                                        "Saves the game picture (without the menu) to the app's screenshots folder."));
    pg.items.push_back(MenuItem::action("Reset Game", [this]() {
      menu.confirm("Reset the game?", [this]() {
        menu.closeAll();
        launchGame(playing);
      }, "Back to its save files. Progress since the game last saved is lost (a save state keeps it).");
    }, "Start this game again from its save files, as at power-on."));
    pg.items.push_back(MenuItem::action("Return to Hub", [this]() { returnToHub(); },
                                        "Leave the game for the hub. This exact moment is kept: Continue Playing "
                                        "brings you back to it."));
    pg.items.push_back(MenuItem::action("Exit", [this]() { requestQuit(); },
                                        "Saves everything (this exact moment too, for Continue Playing) and closes "
                                        "the app."));
  };
  p->onLeave = [this]() { cfg.save(configPath); };
  return p;
}

std::string App::bindingsText(int a) const {
  std::string k = bindingName(cfg.keys[0][a]), j = bindingName(cfg.pad[0][a]);
  bool hk = !k.empty() && k != "-", hj = !j.empty() && j != "-";
  return hk && hj ? k + " / " + j : hk ? k : hj ? j : "unbound";
}

std::shared_ptr<MenuPage> App::pageOptions() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Settings";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::submenu("Display", [this]() { menu.push(pageDisplay()); },
                                         "Widescreen, the picture's shape and scaling."));
    pg.items.push_back(MenuItem::submenu("Graphics", [this]() { menu.push(pageGraphics()); },
                                         "Filters, CRT effects and sprite options."));
    pg.items.push_back(MenuItem::submenu("Colour", [this]() { menu.push(pageColor()); },
                                         "Colour profiles, picture adjustments and accessibility filters."));
    pg.items.push_back(MenuItem::submenu("Audio", [this]() { menu.push(pageAudio()); },
                                         "Volume, latency and custom soundtracks (MSU-1 music packs)."));
    pg.items.push_back(MenuItem::submenu("Controls", [this]() { menu.push(pageControls()); },
                                         "On-screen controls, controllers and keyboards for both players."));
    pg.items.push_back(MenuItem::choice(
        "Menu Style", {"Storybook", "Retro", "Themed Dark", "Classic"}, [this]() { return cfg.menuStyle; },
        [this](int v) {
          cfg.menuStyle = v;
          applyConfig();
        },
        "How these menus look: Storybook (bright and chunky), Retro (black, bricks and a pixel font), Themed Dark "
        "(dark, under the game's own heading) or Classic (the original dark panel)."));
    pg.items.push_back(MenuItem::header("Leaving and coming back"));
    pg.items.push_back(MenuItem::toggle(
        "Keep My Place", [this]() { return cfg.autoResume ? 1 : 0; },
        [this](int v) {
          cfg.autoResume = v != 0;
          if (!v) dropResume();
        },
        "The exact moment you stop at is kept (when you leave the app, return to the hub, and once a minute while "
        "you play), so Continue Playing takes you back to it, even if Android closed the app meanwhile."));
    pg.items.push_back(MenuItem::toggle(
        "Pause When I Come Back", [this]() { return cfg.pauseOnReturn ? 1 : 0; },
        [this](int v) { cfg.pauseOnReturn = v != 0; },
        "Back from another app or a locked screen, the game waits on this menu until you choose Resume."));
    pg.items.push_back(MenuItem::header("Gameplay"));
    pg.items.push_back(MenuItem::toggle(
        "NES Brick Bounce", [this]() { return cfg.nesBrickBounce ? 1 : 0; },
        [this](int v) {
          cfg.nesBrickBounce = v != 0;
          applyConfig();
        },
        "Super Mario Bros. and The Lost Levels: breaking a brick from below sends Mario back down, as on the NES. "
        "Off: he keeps rising, as in the original All-Stars. (BMF54123's brick fix.)"));
    pg.items.push_back(MenuItem::header("Multiplayer"));
    pg.items.push_back(MenuItem::toggle(
        "Two-Player Co-op", [this]() { return cfg.coop ? 1 : 0; },
        [this](int v) {
          cfg.coop = v != 0;
          applyConfig();
        },
        "Mario and Luigi play at once, Luigi on controller 2, in Super Mario Bros., The Lost Levels and Super "
        "Mario Bros. 3 (the screen follows both; a player left idle a few seconds is brought along). "
        "Each of those games' title screens also has CO-OP GAME, which sets this."));
    pg.items.push_back(MenuItem::header("Speed"));
    MenuItem ff = MenuItem::toggle("Fast Forward", [this]() { return cfg.fastForward && !achLocked() ? 1 : 0; },
                                   [this](int v) { cfg.fastForward = v != 0; },
                                   "Hold the on-screen fast forward button (or " + bindingsText(kActFastForward) +
                                       ") to run the game at 4x speed (sound off).");
    ff.enabled = [this]() { return !achLocked(); };
    ff.valueText = [this]() { return std::string(achLocked() ? "Off (achievements on)" : ""); };
    pg.items.push_back(ff);
    MenuItem rw = MenuItem::toggle("Rewind", [this]() { return cfg.rewind && !achLocked() ? 1 : 0; },
                                   [this](int v) {
                                     cfg.rewind = v != 0;
                                     if (!v) rewind.clear();
                                   },
                                   "Hold the on-screen rewind button (or " + bindingsText(kActRewind) +
                                       ") to step back through the last minute of play. Saved game progress is "
                                       "never undone.");
    rw.enabled = [this]() { return !achLocked(); };
    rw.valueText = [this]() { return std::string(achLocked() ? "Off (achievements on)" : ""); };
    pg.items.push_back(rw);
    // (right under what it switches off)
    pg.items.push_back(MenuItem::header("Achievements"));
    pg.items.push_back(achievementsSwitch());
    pg.items.push_back(MenuItem::header("Controller and keyboard shortcuts"));
    auto hot = [&](const char* what, int a) {
      MenuItem it = MenuItem::info(what);
      it.valueText = [this, a]() { return bindingsText(a); };
      pg.items.push_back(it);
    };
    hot("Menu", kActMenu);
    hot("Save state", kActSaveState);
    hot("Load state", kActLoadState);
    hot("Fast forward (hold)", kActFastForward);
    hot("Rewind (hold)", kActRewind);
    hot("Screenshot", kActScreenshot);
  };
  p->onLeave = [this]() { cfg.save(configPath); };
  return p;
}

// ---------------------------------------------------------------------------
// Achievements

void App::achNewRun(bool counts) {
  if (game) game->achievements().newRun(counts);
}

void App::setAchievements(bool on) {
  if (on == cfg.achievements) return;
  cfg.achievements = on;
  // They only count in a game started with them on (from the hub or
  // Reset Game): switched on mid-game, what cheats, the level select, fast
  // forward or rewind did in this game can't earn anything.
  if (game) game->achievements().counts = false;
  if (on) {
    rewind.clear();
    if (!inHub) menu.notify("Achievements count from your next Reset Game", 4.0f);
  }
  applyConfig();  // cheats leave (or come back into) the running game
}

void App::achFrame() {
  AchievementTracker& t = game->achievements();
  if (!t.counts) return;
  std::vector<AchievementEvent> ev = t.take();
  if (ev.empty()) return;
  bool dirty = false;
  for (const AchievementEvent& e : ev) {
    const AchievementDef* d = findAchievement(e.id);
    if (!d) continue;
    if (e.kind == AchievementEvent::Unlock) {
      achAward(e.id);
    } else if (e.kind == AchievementEvent::Progress) {
      dirty = achStore.setProgress(e.id, e.value) || dirty;
      if (d->target > 0 && e.value >= d->target) achAward(e.id);
    } else if (e.kind == AchievementEvent::Add) {
      const int total = achStore.progress(e.id) + e.value;
      dirty = achStore.setProgress(e.id, total) || dirty;
      if (d->target > 0 && total >= d->target) achAward(e.id);
    } else if (e.kind == AchievementEvent::Mark && e.value >= 0 && e.value < 31) {
      int mask = achStore.progress(e.id) | (1 << e.value);
      dirty = achStore.setProgress(e.id, mask) || dirty;
      int found = 0;
      for (int b = 0; b < 31; b++) found += (mask >> b) & 1;
      if (found >= d->target) achAward(e.id);
    }
  }
  if (dirty) achStore.save();
}

void App::achAward(const std::string& id) {
  if (!achStore.unlock(id)) return;
  if (const AchievementDef* d = findAchievement(id)) menu.achievement(d->name, d->desc);
  achCheckCollection();
  achStore.save();
}

// All-Star (finish all four games) and Completionist (everything else).
void App::achCheckCollection() {
  int finished = 0;
  for (const char* id : kFinishedGame) finished += achStore.unlocked(id) ? 1 : 0;
  achStore.setProgress("smas_allstar", finished);
  if (finished == 4 && achStore.unlock("smas_allstar")) {
    if (const AchievementDef* d = findAchievement("smas_allstar")) menu.achievement(d->name, d->desc);
  }
  bool all = true;
  for (const AchievementDef& d : achievementDefs())
    if (std::string(d.id) != "smas_complete") all = all && achStore.unlocked(d.id);
  if (all && achStore.unlock("smas_complete")) {
    if (const AchievementDef* d = findAchievement("smas_complete")) menu.achievement(d->name, d->desc);
  }
}

// The on / off switch. Switching off during a game that counts asks first:
// that game can't earn any more until Reset Game.
MenuItem App::achievementsSwitch() {
  MenuItem it = MenuItem::toggle(
      "Achievements", [this]() { return cfg.achievements ? 1 : 0; },
      [this](int v) {
        if (v || inHub || !game || !game->achievements().counts) {
          setAchievements(v != 0);
          cfg.save(configPath);
          menu.refresh();
          return;
        }
        menu.confirm("Turn achievements off?", [this]() {
          setAchievements(false);
          cfg.save(configPath);
        }, "Cheats, Level Select, Fast Forward and Rewind come back. Achievements count again after Reset "
           "Game.");
      },
      "On: earn achievements; cheats, level select, fast forward and rewind are off. Off: those come back.");
  it.valueText = [this]() {
    return std::string(cfg.achievements && !inHub && game && !game->achievements().counts ? "from your next Reset Game"
                                                                                                : "");
  };
  return it;
}

std::shared_ptr<MenuPage> App::pageAchievements() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Achievements";
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(achievementsSwitch());
    pg.items.push_back(MenuItem::action("Export to CSV", [this]() {
      // (where screenshots go: the app's own folder on shared storage)
      const std::wstring dir = pathJoin(pathDir(shotDir), L"achievements export");
      std::string err;
      if (!exportAchievements(dir, achStore, &err)) {
        menu.notify("Export failed: " + err, 5.0f);
        return;
      }
      logf("achievements exported to %s", narrow(dir).c_str());
      menu.notify("Exported to the app's \"achievements export\" folder (beside its screenshots folder)", 6.0f);
    }, "Writes achievements.csv (ID, title, description, points, trophy, hidden, progress, unlocked and when, "
       "icons) and an icons folder, earned and locked, for tools such as PlayniteAchievements' spreadsheet import."));
    // the running game's first
    GameId order[4] = {GameId::SMB1, GameId::SMBLL, GameId::SMB2, GameId::SMB3};
    GameId cur = inHub || !game ? gameOfIndex(cfg.lastGame) : game->current();
    for (int i = 1; i < 4; i++)
      if (order[i] == cur) std::swap(order[0], order[i]);
    int total = 0, got = 0;
    auto add = [&](const std::string& group, GameId g) {
      bool any = false;
      for (const AchievementDef& d : achievementDefs()) {
        if (d.game != g) continue;
        if (!any) pg.items.push_back(MenuItem::header(group));
        any = true;
        const bool have = achStore.unlocked(d.id);
        total++;
        got += have ? 1 : 0;
        // (a secret one shows what it is too, marked as a secret)
        MenuItem it = MenuItem::action(d.name, []() {}, d.secret ? std::string("Secret: ") + d.desc : std::string(d.desc));
        std::string id = d.id;
        int target = d.target;
        bool set = d.set;
        it.valueText = [this, id, target, set]() {
          std::string on = achStore.unlockedOn(id);
          if (!on.empty()) return std::string("\xE2\x9C\x93  ") + on;
          int have = achStore.progress(id);
          if (set) {  // a set of things: how many were done
            int n = 0;
            for (int b = 0; b < 31; b++) n += (have >> b) & 1;
            have = n;
          }
          if (target > 0) return std::to_string(std::min(have, target)) + " / " + std::to_string(target);
          return std::string();
        };
        pg.items.push_back(it);
      }
    };
    for (GameId g : order) add(SmasGame::gameName(g), g);
    add("Across the collection", GameId::Boot);
    pg.subtitle = std::to_string(got) + " of " + std::to_string(total) + " unlocked";
  };
  return p;
}

// Once, on the first start: what achievements are and what they switch off.
void App::achWelcome() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Achievements";
  p->subtitle = std::to_string((int)achievementDefs().size()) +
                " to earn across Super Mario Bros., The Lost Levels, Super Mario Bros. 2 and 3";
  p->wide = true;
  p->items.push_back(MenuItem::info("Rescue princesses, defeat the bosses, find secrets and pull off tricky moves."));
  p->items.push_back(MenuItem::header("To keep them fair, while achievements are on"));
  p->items.push_back(MenuItem::info("Cheats, Level Select, Fast Forward and Rewind are switched off."));
  p->items.push_back(MenuItem::info("Save states still work. Turn achievements off any time in Settings."));
  p->items.push_back(MenuItem::separator());
  p->items.push_back(MenuItem::action("Play with achievements", [this]() { menu.closeAll(); },
                                      "Each one pops up as you earn it; the menu's Achievements page lists them all."));
  p->items.push_back(MenuItem::action("Turn achievements off", [this]() {
    setAchievements(false);
    cfg.save(configPath);
    menu.closeAll();
  }, "Cheats, Level Select, Fast Forward and Rewind stay available. Turn achievements on again in Settings."));
  menuAnim = 0;
  menu.push(p);
}

std::shared_ptr<MenuPage> App::pageDisplay() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Display";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::choice(
        "Aspect Ratio", {"4:3 (Original)", "16:10 Widescreen", "16:9 Widescreen", "21:9 Ultrawide", "Auto (Fill Display)"},
        [this]() { return cfg.aspect; },
        [this](int v) { cfg.aspect = v; },
        "Widescreen shows more of each level to the left and right. Auto matches your display. (While the "
        "on-screen controls are in use, Picture With Touch Controls below decides.)"));
    pg.items.push_back(MenuItem::choice(
        "Picture With Touch Controls", {"4:3, Controls Beside It", "As Aspect Ratio"},
        [this]() { return cfg.touchPicture4x3 ? 0 : 1; }, [this](int v) { cfg.touchPicture4x3 = v == 0; },
        "4:3: while you play with the on-screen controls the picture is the original 4:3 in the middle of the "
        "screen, the controls at its sides. On a phone's wide screen they fit beside it, so your thumbs do not "
        "cover the game. As Aspect Ratio: the picture is as wide as chosen above, the controls over it."));
    pg.items.push_back(MenuItem::choice("Pixel Shape", {"CRT (8:7, as on a TV)", "Square (1:1)"},
                                        [this]() { return cfg.pixelAspectCrt ? 0 : 1; },
                                        [this](int v) { cfg.pixelAspectCrt = v == 0; },
                                        "CRT matches how the game looked on a television."));
    pg.items.push_back(MenuItem::choice("Scaling", {"Fit", "Integer (Pixel Perfect)", "Stretch"},
                                        [this]() { return cfg.scaling; }, [this](int v) { cfg.scaling = v; },
                                        "Integer scaling keeps every pixel the same size (with CRT pixels, as close to "
                                        "8:7 as whole screen pixels allow)."));
    pg.items.push_back(MenuItem::toggle("Re-anchor HUD", [this]() { return cfg.hudAnchor ? 1 : 0; },
                                        [this](int v) {
                                          cfg.hudAnchor = v;
                                          applyConfig();
                                        },
                                        "In widescreen, move score, lives and timers out to the screen edges (SMB3's "
                                        "status bar stays centred)."));
    pg.items.push_back(MenuItem::toggle("Dim Wrap-Around Sides", [this]() { return cfg.dimWrap ? 1 : 0; },
                                        [this](int v) {
                                          cfg.dimWrap = v;
                                          applyConfig();
                                        },
                                        "SMB2's climbing areas, SMB3's vertical areas and its battle game wrap round "
                                        "at the edges of the 4:3 picture: dim the widescreen sides there, so it's "
                                        "clear where the play space ends."));
    pg.items.push_back(MenuItem::choice("Widescreen Enemies", {"Original Timing", "Active In Full View"},
                                        [this]() { return cfg.widescreenEnemies; },
                                        [this](int v) {
                                          cfg.widescreenEnemies = v;
                                          applyConfig();
                                        },
                                        "Active: enemies spawn at the edge of the wide view and behave normally "
                                        "there. Original: identical to the 4:3 game."));
    pg.items.push_back(MenuItem::choice("Side Borders", {"Black", "Blurred Game", "Dark Frame"},
                                        [this]() { return cfg.pillarbox; }, [this](int v) { cfg.pillarbox = v; },
                                        "What fills the screen where the game has nothing to show (menus, 4:3)."));
    pg.items.push_back(MenuItem::choice(
        "Screen Refresh Rate", {"Device Default", "60 Hz (Matches the Games)"}, [this]() { return cfg.screen60 ? 1 : 0; },
        [this](int v) {
          cfg.screen60 = v != 0;
          platform::preferSixtyHz(cfg.screen60);
        },
        "The games run at 60 frames a second. On a 90, 120 or 144 Hz screen, 60 Hz asks Android to run the screen "
        "at 60 while SMAS Mobile is open, so every refresh shows one whole frame: steadier motion and less battery "
        "use. Choose Device Default if the picture flickers or the phone ignores the request."));
    pg.items.push_back(MenuItem::toggle("Show FPS", [this]() { return cfg.showFps ? 1 : 0; },
                                        [this](int v) { cfg.showFps = v; },
                                        "The game's frames per second, top left. It should read about 60."));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageGraphics() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Graphics";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::choice(
        "Filter", {"Nearest (Sharp Pixels)", "Sharp Bilinear", "Smooth (Bilinear)", "CRT Scanlines", "CRT Aperture Grille",
                   "LCD Grid"},
        [this]() { return cfg.filter; }, [this](int v) { cfg.filter = v; },
        "Sharp Bilinear keeps pixels crisp without uneven sizes."));
    pg.items.push_back(MenuItem::slider("Scanline / Effect Strength", 0, 100, 5, [this]() { return cfg.scanlines; },
                                        [this](int v) { cfg.scanlines = v; }, "%",
                                        "Strength of the CRT and LCD effects."));
    pg.items.push_back(MenuItem::slider("Screen Curvature", 0, 100, 5, [this]() { return cfg.crtCurve; },
                                        [this](int v) { cfg.crtCurve = v; }, "%", "Simulated curved CRT glass."));
    pg.items.push_back(MenuItem::toggle("Remove Sprite Limit", [this]() { return cfg.noSpriteLimit ? 1 : 0; },
                                        [this](int v) {
                                          cfg.noSpriteLimit = v;
                                          applyConfig();
                                        },
                                        "Reduce sprite flicker by lifting the 32 sprites per line hardware limit."));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageColor() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Colour";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::choice(
        "Colour Profile",
        {"Original", "Vivid", "CRT Television", "Muted", "Greyscale", "Sepia", "Pocket (Green)", "Virtual (Red)",
         "Night (Warm)", "Console Accurate"},
        [this]() { return cfg.colorProfile; }, [this](int v) { cfg.colorProfile = v; },
        "Overall colour style of the picture."));
    pg.items.push_back(MenuItem::choice(
        "Mario's Colours", {"Original", "Classic NES", "Fire", "Ice", "Shadow", "Gold", "Forest", "Ocean"},
        [this]() { return cfg.playerPalette; },
        [this](int v) {
          cfg.playerPalette = v;
          applyConfig();
        },
        "Alternate outfit colours for Mario in Super Mario Bros. and The Lost Levels."));
    pg.items.push_back(MenuItem::choice(
        "Luigi's Colours", {"Original", "Classic NES", "Fire", "Ice", "Shadow", "Gold", "Forest", "Ocean"},
        [this]() { return cfg.luigiPalette; },
        [this](int v) {
          cfg.luigiPalette = v;
          applyConfig();
        },
        "Alternate outfit colours for Luigi in Super Mario Bros. and The Lost Levels (co-op, or playing as Luigi)."));
    pg.items.push_back(MenuItem::slider("Brightness", 50, 150, 5, [this]() { return cfg.brightness; },
                                        [this](int v) { cfg.brightness = v; }, "%"));
    pg.items.push_back(MenuItem::slider("Contrast", 50, 150, 5, [this]() { return cfg.contrast; },
                                        [this](int v) { cfg.contrast = v; }, "%"));
    pg.items.push_back(MenuItem::slider("Saturation", 0, 200, 5, [this]() { return cfg.saturation; },
                                        [this](int v) { cfg.saturation = v; }, "%"));
    pg.items.push_back(MenuItem::slider("Gamma", 50, 150, 5, [this]() { return cfg.gamma; },
                                        [this](int v) { cfg.gamma = v; }, "%"));
    pg.items.push_back(MenuItem::slider("Colour Temperature", -100, 100, 10, [this]() { return cfg.colorTemp; },
                                        [this](int v) { cfg.colorTemp = v; }, "",
                                        "Negative values are cooler (bluer), positive values warmer."));
    pg.items.push_back(MenuItem::choice("Colour Vision Assist",
                                        {"Off", "Protanopia", "Deuteranopia", "Tritanopia"},
                                        [this]() { return cfg.colorblind; }, [this](int v) { cfg.colorblind = v; },
                                        "Shifts colours so they are easier to tell apart with colour blindness."));
    pg.items.push_back(MenuItem::action("Reset Picture Settings", [this]() {
      cfg.brightness = cfg.contrast = cfg.gamma = 100;
      cfg.saturation = 100;
      cfg.colorProfile = 0;
      cfg.colorblind = 0;
      cfg.colorTemp = 0;
      menu.refresh();
    }));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageAudio() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Audio";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::slider("Volume", 0, 100, 5, [this]() { return cfg.volume; },
                                        [this](int v) {
                                          cfg.volume = v;
                                          applyConfig();
                                        },
                                        "%"));
    pg.items.push_back(MenuItem::slider("Music Volume", 0, 100, 5, [this]() { return cfg.musicVolume; },
                                        [this](int v) {
                                          cfg.musicVolume = v;
                                          applyConfig();
                                        },
                                        "%", "The games' music (and a music pack's). 0% turns the music off and keeps "
                                             "the sound effects."));
    pg.items.push_back(MenuItem::slider("Sound Effects Volume", 0, 100, 5, [this]() { return cfg.sfxVolume; },
                                        [this](int v) {
                                          cfg.sfxVolume = v;
                                          applyConfig();
                                        },
                                        "%", "Jumps, coins, stomps and the rest. 0% turns them off and keeps the music."));
    pg.items.push_back(MenuItem::slider("Latency", 20, 160, 10, [this]() { return cfg.audioLatency; },
                                        [this](int v) {
                                          cfg.audioLatency = v;
                                          applyConfig();
                                          audio.flush();  // take effect now, not after a slow drift
                                        },
                                        " ms", "Lower values respond faster; raise it if you hear crackling."));
    // MSU-1 music packs (app/msupack.h): Off, then the msu folder's packs
    pg.items.push_back(MenuItem::header("Custom Soundtrack (MSU-1)"));
    if (msuDir.empty()) msuDir = pathJoin(exeDir, L"msu");
    auto packs = std::make_shared<std::vector<std::wstring>>(listMsuPacks(msuDir));
    if (!cfg.msuPack.empty() && std::find(packs->begin(), packs->end(), cfg.msuPack) == packs->end())
      packs->push_back(cfg.msuPack);  // (chosen, since gone: shown so it can be turned off)
    std::vector<std::string> names = {"Off"};
    for (const auto& n : *packs) names.push_back(narrow(n));
    std::string help;
    if (!cfg.msuPack.empty() && cfg.msuPack == msuLoaded && !msuStatus.empty()) help = msuStatus + " ";
    else if (cfg.msuPack != msuLoaded)
      help = inHub ? "Starts when you next start a game. "
                        : "Choose Restart With This Pack below to hear it now (or it starts with your next game). ";
    help += packs->empty() ? "Put a pack in its own folder in the app's msu folder (on shared storage, beside its "
                             "screenshots folder): its MSU-1 patch (.bps or .ips) and its tracks (.pcm), then come "
                             "back here."
                           : "A pack is a folder in the app's msu folder: its MSU-1 patch (.bps or .ips) and tracks "
                             "(.pcm).";
    pg.items.push_back(MenuItem::choice(
        "Music Pack", names,
        [this, packs]() {
          for (size_t i = 0; i < packs->size(); i++)
            if ((*packs)[i] == cfg.msuPack) return (int)i + 1;
          return 0;
        },
        [this, packs](int v) {
          cfg.msuPack = v <= 0 ? std::wstring() : (*packs)[(size_t)std::min<int>(v, (int)packs->size()) - 1];
          if (inHub) {
            reloadForMsu();
            if (!cfg.msuPack.empty() && !(snes && snes->msu)) menu.notify(msuStatus, 5.0f);
          }
          cfg.save(configPath);
          menu.refresh();  // (its help says what became of it)
        },
        help));
    if (!inHub && snes && cfg.msuPack != msuLoaded) {
      // (in a game: the console is rebuilt with the pack, so the game starts again)
      pg.items.push_back(MenuItem::action("Restart With This Pack", [this]() {
        menu.confirm("Restart the game with this music?", [this]() { launchGame(playing); },
                     "The game starts again from its title. Progress since your last save is lost.");
      }, "Starts the game again from its title with the chosen music pack (or none)."));
    }
    pg.items.push_back(MenuItem::slider("Music Pack Volume", 0, 100, 5, [this]() { return cfg.msuVolume; },
                                        [this](int v) {
                                          cfg.msuVolume = v;
                                          applyConfig();
                                        },
                                        "%"));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageControls() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Controls";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::header("Touch screen"));
    pg.items.push_back(MenuItem::submenu("On-Screen Controls", [this]() { menu.push(pageTouch()); },
                                         "Their size, where each one sits, how see-through they are, vibration."));
    pg.items.push_back(MenuItem::header("Controllers"));
    std::vector<std::string> pads;
    int connected = 0;
    for (int i = 0; i < 8; i++) {
      const PadState* ps = input.pad(i);
      std::string n = "Pad " + std::to_string(i + 1);
      if (!ps) n += " (not connected)";
      else n += ": " + narrow(ps->name) + (ps->connected ? "" : " (unplugged)");
      if (ps && ps->connected) connected++;
      pads.push_back(n);
    }
    MenuItem found = MenuItem::info("Connected");
    found.valueText = [connected]() {
      return connected == 0 ? std::string("None") : connected == 1 ? std::string("1 controller") : std::to_string(connected) + " controllers";
    };
    pg.items.push_back(found);
    pg.items.push_back(MenuItem::choice("Player 1 Uses", pads, [this]() { return cfg.padIndex[0]; },
                                        [this](int v) { cfg.padIndex[0] = v; },
                                        "Which connected controller plays as player 1. A controller listed as "
                                        "(generic) or (basic mode) may need its buttons assigned below."));
    pg.items.push_back(MenuItem::choice("Player 2 Uses", pads, [this]() { return cfg.padIndex[1]; },
                                        [this](int v) { cfg.padIndex[1] = v; }));
    pg.items.push_back(MenuItem::submenu("Player 1 Controller Buttons", [this]() { menu.push(pageBindings(0, true)); },
                                         "Which controller button does what. Choose a row, then press the button."));
    pg.items.push_back(MenuItem::submenu("Player 2 Controller Buttons", [this]() { menu.push(pageBindings(1, true)); }));
    pg.items.push_back(MenuItem::choice("Select Button", {"Opens Menu", "Tap: Game Select  -  Hold: Menu"},
                                        [this]() { return cfg.selectMode; },
                                        [this](int v) { cfg.selectMode = v; },
                                        "Opens Menu: Select brings up this menu; the game's own Select is on the "
                                        "left stick button or Menu > Press Select In Game."));
    pg.items.push_back(MenuItem::choice(
        "Menu Buttons", {"Bottom Confirms", "Right Confirms"}, [this]() { return cfg.confirmRight ? 1 : 0; },
        [this](int v) {
          cfg.confirmRight = v != 0;
          applyConfig();
        },
        "Which face button chooses in the menus and the hub: the bottom one (A on an Xbox pad), or the right one (A "
        "on a Nintendo pad); the other goes back. The games' own buttons are set by their TYPE A / TYPE B and "
        "the Controller Buttons pages."));
    pg.items.push_back(MenuItem::slider("Stick Deadzone", 10, 80, 5, [this]() { return cfg.deadzone; },
                                        [this](int v) { cfg.deadzone = v; }, "%"));
    pg.items.push_back(MenuItem::header("Keyboard"));
    pg.items.push_back(MenuItem::submenu("Player 1 Keys", [this]() { menu.push(pageBindings(0, false)); },
                                         "For a keyboard connected to the phone or tablet."));
    pg.items.push_back(MenuItem::submenu("Player 2 Keys", [this]() { menu.push(pageBindings(1, false)); }));
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Restore Default Buttons and Keys", [this]() {
      menu.confirm("Restore the default buttons and keys?", [this]() {
        Config d;
        d.setDefaults();
        memcpy(cfg.keys, d.keys, sizeof(cfg.keys));
        memcpy(cfg.pad, d.pad, sizeof(cfg.pad));
        cfg.save(configPath);
        menu.notify("Buttons and keys reset");
      }, "For both players. The on-screen controls are not changed.");
    }));
  };
  p->onLeave = [this]() { cfg.save(configPath); };
  return p;
}

// The on-screen controls: size, layout, opacity, vibration.
std::shared_ptr<MenuPage> App::pageTouch() {
  auto p = std::make_shared<MenuPage>();
  p->title = "On-Screen Controls";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::choice("Show", {"While Touching", "Always", "Never"},
                                        [this]() { return cfg.touchMode; }, [this](int v) { cfg.touchMode = v; },
                                        "While Touching: shown once you touch the screen, hidden when a controller "
                                        "or keyboard is used. Never: for playing with a controller only."));
    pg.items.push_back(MenuItem::action("Move the Controls", [this]() { openTouchEditor(); },
                                        "Shows the controls so you can drag each one to where your thumbs rest. "
                                        "Nothing can be moved off the screen."));
    pg.items.push_back(MenuItem::slider("Size", kTouchScaleMin, kTouchScaleMax, 5, [this]() { return cfg.touchScale; },
                                        [this](int v) { cfg.touchScale = v; }, "%",
                                        "How large the controls are. 100% suits most phones. Smaller suits a tablet; "
                                        "larger if the buttons are hard to hit."));
    pg.items.push_back(MenuItem::slider("Opacity", 10, 100, 10, [this]() { return cfg.touchOpacity; },
                                        [this](int v) { cfg.touchOpacity = v; }, "%",
                                        "How solid the controls look over the picture."));
    pg.items.push_back(MenuItem::toggle("Vibrate On Press", [this]() { return cfg.touchHaptics ? 1 : 0; },
                                        [this](int v) {
                                          cfg.touchHaptics = v != 0;
                                          if (v) platform::haptic();
                                        },
                                        "A short tick as a control is pressed, so you can feel it without looking. "
                                        "(Follows the phone's own touch-vibration setting.)"));
    pg.items.push_back(MenuItem::choice(
        "Picture With Touch Controls", {"4:3, Controls Beside It", "As Aspect Ratio"},
        [this]() { return cfg.touchPicture4x3 ? 0 : 1; }, [this](int v) { cfg.touchPicture4x3 = v == 0; },
        "4:3: the picture sits in the middle of the screen with the controls at its sides. On a phone's wide screen "
        "they fit beside it, so your thumbs do not cover the game. As Aspect Ratio: the picture as wide as Display > "
        "Aspect Ratio says, the controls over it."));
    pg.items.push_back(MenuItem::action("Restore the Standard Layout", [this]() {
      menu.confirm("Put every control back in its standard place?", [this]() {
        for (auto& o : cfg.touchOffset) o[0] = o[1] = 0;
        cfg.touchScale = 100;
        cfg.save(configPath);
        menu.notify("On-screen controls reset");
      }, "Size goes back to 100% too.");
    }));
  };
  p->onLeave = [this]() { cfg.save(configPath); };
  return p;
}

std::shared_ptr<MenuPage> App::pageBindings(int player, bool pad) {
  auto p = std::make_shared<MenuPage>();
  p->title = "Player " + std::to_string(player + 1) + (pad ? " Controller" : " Keys");
  p->subtitle = pad ? "Positional layout: the bottom face button is SNES B" : "";
  p->rebuild = [this, player, pad](MenuPage& pg) {
    pg.items.clear();
    int last = player == 0 ? kActCount : kActSelect + 1;
    for (int a = 0; a < last; a++) {
      MenuItem it;
      it.kind = MenuItem::Bind;
      it.label = actionName(a);
      it.valueText = [this, player, pad, a]() {
        return bindingName((pad ? cfg.pad : cfg.keys)[player][a]);
      };
      it.onActivate = [this, player, pad, a]() { startBind(player, pad, a); };
      it.help = pad ? "Choose it, then press the controller button (or move the stick) to use."
                    : "Choose it, then press the key to use.";
      pg.items.push_back(it);
    }
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageCheats() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Cheats";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    const auto& list = SmasGame::cheatList();
    if (list.empty()) {
      pg.items.push_back(MenuItem::info("No cheats available."));
      return;
    }
    GameId last = GameId::Boot;
    for (const CheatDef& c : list) {
      if (c.game != last) {
        pg.items.push_back(MenuItem::separator(SmasGame::gameName(c.game)));
        last = c.game;
      }
      int id = c.id;
      pg.items.push_back(MenuItem::toggle(c.name, [this, id]() { return (cfg.cheats >> id) & 1 ? 1 : 0; },
                                          [this, id](int v) {
                                            if (v) cfg.cheats |= 1ull << id;
                                            else cfg.cheats &= ~(1ull << id);
                                            applyConfig();
                                          },
                                          c.desc));
    }
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Disable All Cheats", [this]() {
      cfg.cheats = 0;
      applyConfig();
      menu.refresh();
    }));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageLevelSelect() {
  // Every level listed under its world (as the Mickey & Donald Collection's),
  // the game chosen at the top; each asks first.
  auto p = std::make_shared<MenuPage>();
  p->title = "Level Select";
  auto selGame = std::make_shared<int>(0);
  // SMB2's character, in the select screen's order (its $008F codes below);
  // the one being played, if SMB2 is.
  static const char* const kChars[] = {"Mario", "Luigi", "Toad", "Peach"};
  static const int kCharCode[] = {0, 3, 2, 1};
  auto selChar = std::make_shared<int>(0);
  auto selCoop = std::make_shared<int>(0);  // (1 PLAYER unless chosen: as each title screen starts)
  if (game->current() == GameId::SMB2)
    for (int i = 0; i < 4; i++)
      if (kCharCode[i] == (snes->wram[0x008f] & 3)) *selChar = i;
  {
    // Start on the game being played, if any.
    const auto& defs = SmasGame::levelSelectList();
    GameId cur = game->current();
    if (cur != GameId::SMB1 && cur != GameId::SMBLL && cur != GameId::SMB2 && cur != GameId::SMB3) cur = playing;
    for (size_t i = 0; i < defs.size(); i++)
      if (defs[i].game == cur) *selGame = (int)i;
  }
  p->rebuild = [this, selGame, selChar, selCoop](MenuPage& pg) {
    pg.items.clear();
    const auto& defs = SmasGame::levelSelectList();
    if (defs.empty()) {
      pg.items.push_back(MenuItem::info("Level select is not available."));
      return;
    }
    std::vector<std::string> games;
    for (auto& d : defs) games.push_back(SmasGame::gameName(d.game));
    *selGame = std::clamp(*selGame, 0, (int)defs.size() - 1);
    const LevelSelectDef& d = defs[*selGame];
    pg.subtitle = SmasGame::gameName(d.game);
    pg.items.push_back(MenuItem::choice("Game", games, [selGame]() { return *selGame; },
                                        [this, selGame](int v) {
                                          *selGame = v;
                                          menu.refresh();
                                        },
                                        "Which game's levels are listed."));
    if (d.game == GameId::SMB2)
      pg.items.push_back(MenuItem::choice("Character", {kChars[0], kChars[1], kChars[2], kChars[3]},
                                          [selChar]() { return *selChar; }, [selChar](int v) { *selChar = v; },
                                          "Who plays the level (chosen for you on the character select)."));
    const bool coopGame = d.game == GameId::SMB1 || d.game == GameId::SMBLL || d.game == GameId::SMB3;
    if (coopGame)
      pg.items.push_back(MenuItem::choice("Players", {"1 Player", "Co-op"}, [selCoop]() { return *selCoop; },
                                          [selCoop](int v) { *selCoop = v; },
                                          "One player, or Mario and Luigi at once (Luigi on controller 2), as the "
                                          "title screen's CO-OP GAME."));
    const int gi = *selGame;
    const bool smb2 = d.game == GameId::SMB2;
    for (int w = 0; w < (int)d.worlds.size(); w++) {
      pg.items.push_back(MenuItem::header(d.worlds[w]));
      for (int l = 0; l < (int)d.levels[w].size(); l++) {
        const std::string name = d.levels[w][l];
        const std::string where = std::string(SmasGame::gameName(d.game)) + ", " + d.worlds[w];
        pg.items.push_back(MenuItem::action(name, [this, gi, w, l, name, where, smb2, selChar, selCoop, coopGame]() {
          const int ch = smb2 ? kCharCode[*selChar] : -1;
          const int co = coopGame ? *selCoop : -1;
          menu.confirm("Start " + name + (smb2 ? std::string(" as ") + kChars[*selChar] : std::string()) +
                           (co == 1 ? std::string(" in co-op") : std::string()) + "?",
                       [this, gi, w, l, ch, co]() {
            const auto& defs2 = SmasGame::levelSelectList();
            std::string err;
            if (co >= 0 && cfg.coop != (co == 1)) {  // (the choice sets the setting, as a title screen's does)
              cfg.coop = co == 1;
              applyConfig();
              cfg.save(configPath);
            }
            if (game->requestLevel(defs2[gi].game, w, l, &err, ch)) {
              playing = defs2[gi].game;  // (Reset Game, Return to Hub: this one now)
              rewind.clear();
              achNewRun(false);  // (a level select run never counts)
              menu.closeAll();
              audio.flush();
            } else {
              menu.notify(err);
            }
          }, where + ". Progress in the current game is not saved.");
        }, where + "."));
      }
    }
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageSoundSelect() { return music.soundSelectPage(this->menu); }
std::shared_ptr<MenuPage> App::pageMusicPlayer() { return music.playerPage(this->menu); }

// One pass of the main loop: input and menus every display refresh, then
// `gameFrames` emulated frames (0 when this refresh repeats the last picture).
void App::frame(double dt, int gameFrames) {
  dt = std::min(dt, 0.1);
  menu.tickToasts((float)dt);
  screenTimer += dt;
  if (screenTimer >= 2.0) {
    screenTimer = 0;
    refreshInsets();
  }
  pollPick();
  updateInput(dt);
  menu.showKeys = !touchActive;  // (the footer's line of keys: for keys and controllers)
  if (bindAction >= 0) updateBind(dt);
  if (editingTouch) {
    // (the layout editor: the game waits; Back or Confirm closes it)
    MenuInput mi = menuInput();
    if (mi.back || mi.confirm) closeTouchEditor();
    return;
  }
  if (inHub || !snes) {
    frameHub(dt);
    return;
  }

  bool menuOpen = menu.open();
  if (menuOpen) {
    MenuInput mi = menuInput();
    // Menu button closes the menu from the top level
    if (menu.top() && !menu.capturing) menu.update(mi, (float)dt);
    menuAnim = std::min(1.0f, menuAnim + (float)dt * 8.0f);
  } else {
    menuInput();  // keep edge state fresh
  }
  if (menuOpen && !menu.open()) {
    cfg.save(configPath);
    audio.flush();
  }
  if (inHub || !snes) return;  // (the menu left the game: Return to Hub)

  music.update(dt, menu.open());
  bool emulate = !menu.open();
  if (rewinding && (!rewindHeld || !emulate)) endRewind();
  if ((!cfg.rewind || achLocked()) && rewind.snapshots()) rewind.clear();
  const auto emuStart = std::chrono::steady_clock::now();
  if (emulate && rewindHeld && stepRewind(gameFrames)) {
    // (showing the past)
  } else if (emulate) {
    // Level select: the game is run to the chosen level out of sight (black,
    // sound off), as fast as it goes for most of each refresh.
    bool loading = game->levelLoading();
    int runs = loading ? 1000 : gameFrames * (fastForward ? 4 : 1);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; i++) {
      if (loading && (!game->levelLoading() || std::chrono::steady_clock::now() - start > std::chrono::milliseconds(12)))
        break;
      // (Of the frames a pass runs, only the last is drawn: a device that
      // falls behind runs two a pass, and drawing is most of a frame's work.
      // The game keeps its speed and its sound; the picture changes less
      // often. Fast forward too.)
      snes->ppu.skipRender = !loading && i < runs - 1;
      game->beforeFrame();
      snes->runFrame();
      game->afterFrame();
      if (game->coopPick >= 0) {  // (a title screen's co-op choice: the setting follows)
        cfg.coop = game->coopPick != 0;
        game->coopPick = -1;
        cfg.save(configPath);
      }
      audioBuf.clear();
      snes->takeAudio(audioBuf);
      bool mute = fastForward || loading;
      if (!mute) audio.push(audioBuf.data(), audioBuf.size() / 2, Snes::kSampleRate);
      fpsFrames++;
      if (pendingSave >= 0 && !game->coopMidTurn()) saveState(pendingSave);
      if (cfg.rewind && !achLocked() && !loading && !game->coopMidTurn() && (++rewindCount & 1) == 0)
        rewind.push(snes->saveState());
    }
    snes->ppu.skipRender = false;
    achFrame();
  }
  // (for the log: how play is running, once a minute; nothing is logged per frame)
  if (emulate) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - emuStart).count();
    perf.window += dt;
    perf.passes++;
    perf.frames += gameFrames;
    perf.emuMs += ms;
    perf.worstMs = std::max(perf.worstMs, ms);
    if (ms > 1000.0 / Snes::kFrameRate) perf.slow++;
    if (perf.window >= 60.0) {
      int underruns = 0, minQueued = 0;
      audio.takeStats(&underruns, &minQueued);
      logf("play: %.1f fps over %.0f s at %.1f Hz; emulation %.2f ms a pass (worst %.1f), %d slow passes; audio "
           "underruns %d",
           perf.frames / perf.window, perf.window, pacing.hz, perf.emuMs / std::max(1, perf.passes), perf.worstMs,
           perf.slow, underruns);
      perf = Perf();
    }
  }
  // The games' own save files: within five seconds of the game writing them.
  sramTimer += dt;
  if (sramTimer > 5.0 && snes->sramDirty) {
    sramTimer = 0;
    saveSram();
  }
  // The resume point: once a minute of play, written on another thread.
  if (emulate && !rewinding) {
    resumeTimer += dt;
    if (resumeTimer >= 60.0) {
      resumeTimer = 0;
      saveResume(false);
    }
  }
  fpsTimer += dt;
  if (fpsTimer >= 0.5) {
    fps = fpsFrames / fpsTimer;
    fpsFrames = 0;
    fpsTimer = 0;
  }
}

void App::render() {
  Canvas* ui = video.canvas();
  const int W = video.width(), H = video.height();
  char fpsText[32] = "";
  if (inHub || !snes) {
    video.clear();
    video.beginUi();
    if (editingTouch) {
      drawTouchEditor(ui);
    } else {
      menu.setAccent(hub.accent());
      menu.setGameArt(cfg.lastGame >= 0 && cfg.lastGame <= 3 ? cfg.lastGame + 1 : 0);
      hub.draw(menu, W, H, insets);
      if (menu.open()) menu.draw(W, H, menuAnim);
    }
    menu.drawOsd(W, H, fpsText);
    video.endUi();
    return;
  }
  IRect dest;
  int ext, srcW;
  computeLayout(&dest, &ext, &srcW);
  const int from = kMaxExt - ext;
  video.upload(snes->ppu.frame + from, srcW, snes->ppu.frameHeight(), kLineW);
  VideoParams vp;
  vp.srcW = srcW;
  vp.srcH = snes->ppu.frameHeight();
  vp.dest = dest;
  vp.filter = cfg.filter;
  vp.scanlines = cfg.scanlines / 100.0f;
  vp.curve = cfg.crtCurve / 100.0f;
  vp.colorProfile = cfg.colorProfile;
  vp.brightness = cfg.brightness / 100.0f;
  vp.contrast = cfg.contrast / 100.0f;
  vp.saturation = cfg.saturation / 100.0f;
  vp.gamma = cfg.gamma / 100.0f;
  vp.colorblind = cfg.colorblind;
  vp.temperature = cfg.colorTemp / 100.0f;
  vp.pillarbox = cfg.pillarbox;
  vp.frame = snes->frameCount();
  bool loading = game->levelLoading();
  if (loading) vp.fade = 0;
  video.draw(vp);
  video.beginUi();
  // The menu's accent: the running game's colour (and its heading's art).
  switch (game->current()) {
    case GameId::SMB1: menu.setGameArt(1); break;
    case GameId::SMBLL: menu.setGameArt(2); break;
    case GameId::SMB2: menu.setGameArt(3); break;
    case GameId::SMB3: menu.setGameArt(4); break;
    default: menu.setGameArt(0); break;
  }
  switch (game->current()) {
    case GameId::SMB1:
    case GameId::SMBLL:
    case GameId::SMB2:
    case GameId::SMB3: menu.setAccent(gameAccent(game->current())); break;
    default: menu.setAccent(gameAccent(playing)); break;  // (its file panel)
  }
  if (cfg.pillarbox == 2) {
    // dark frame: subtle border around the picture
    RectF r = rectF((float)dest.left - 2, (float)dest.top - 2, (float)dest.right + 2, (float)dest.bottom + 2);
    if (ui) ui->strokeRect(r, 3.0f, colorF(0.25f, 0.25f, 0.3f, 1.0f));
  }
  if (editingTouch) {
    drawTouchEditor(ui);
  } else {
    touch.draw(ui, cfg.touchOpacity / 100.0f);
    if (menu.open()) menu.draw(W, H, menuAnim);
  }
  if (cfg.showFps) snprintf(fpsText, sizeof fpsText, "%.1f FPS", fps);
  menu.drawOsd(W, H, fpsText, rewinding ? "<< Rewind" : fastForward ? ">> Fast Forward" : loading ? "Loading..." : "");
  video.endUi();
}

// ---------------------------------------------------------------------------
// On-screen controls

bool App::touchShown() const {
  if (editingTouch) return true;
  return touchActive ? cfg.touchMode != 2 : cfg.touchMode == 1;
}

void App::refreshInsets() {
  float l = 0, t = 0, r = 0, b = 0;
  if (!platform::safeInsets(&l, &t, &r, &b)) l = t = r = b = 0;
  // (never more than a fifth of the screen at an edge, whatever the system says)
  const float W = (float)video.width(), H = (float)video.height();
  insets[0] = std::clamp(l, 0.0f, W * 0.2f);
  insets[1] = std::clamp(t, 0.0f, H * 0.2f);
  insets[2] = std::clamp(r, 0.0f, W * 0.2f);
  insets[3] = std::clamp(b, 0.0f, H * 0.2f);
  menu.setSafeArea(insets[0], insets[1], insets[2], insets[3]);
  // Text and rows of a size for the screen in hand. The menus measure
  // everything by the screen's height, which suits a monitor or a TV; a
  // phone's 1080 lines are 7 cm tall, so its text is scaled up to about 15 sp
  // and the rows that can be tapped are at least 40 dp tall.
  float dp = 0, sp = 0;
  if (!platform::displayDensity(&dp, &sp) || dp < 0.5f || sp < 0.5f) dp = sp = H / 411.0f;  // (not known: as a phone's)
  const float base = 26.0f * std::max(H / 1080.0f, 0.55f);  // (the menus' text at scale 1, in pixels)
  menu.textScale = std::clamp(15.0f * sp / base, 1.0f, 1.75f);
  menu.minRow = 40.0f * dp;
}

// Taps and drags: on the hub's tiles, the menus' rows, the layout editor; the
// on-screen pad's Menu button.
void App::touchFrame() {
  touch.setInsets(insets[0], insets[1], insets[2], insets[3]);
  touch.setAdjust(cfg.touchScale, cfg.touchOffset);
  touch.layout((float)video.width(), (float)video.height());
  touch.editing = editingTouch;
  touch.inGame = !inHub && snes && !menu.open() && !editingTouch && !(game && game->levelLoading());
  touch.visible = touchShown();
  touch.showFF = cfg.fastForward && !achLocked();
  touch.showRewind = cfg.rewind && !achLocked();
  if (touch.takeHaptic() && cfg.touchHaptics) platform::haptic();
  if (touch.takePressed(TouchControls::BtnMenu) && touch.inGame) openMenu();
  float dy = touch.takeDrag(), x = 0, y = 0;
  bool tapped = touch.takeTap(&x, &y);
  if (editingTouch) {
    if (touch.takeMoved()) touch.getOffsets(cfg.touchOffset);
    if (tapped) editorTap(x, y);
  } else if (menu.open()) {
    if (dy != 0) menu.drag(dy);
    if (tapped) menu.tap(x, y);
  } else if (inHub && tapped) {
    hubTap = hub.tap(x, y);
  }
  // The system's edge gestures keep off the controls while they are in use.
  IRect rects[kTouchGroupCount];
  const int n = touch.inGame && touch.visible ? touch.controlRects(rects, kTouchGroupCount) : 0;
  unsigned key = (unsigned)n * 2654435761u;
  for (int i = 0; i < n; i++)
    key = (key ^ (unsigned)(rects[i].left * 31 + rects[i].top * 131 + rects[i].right * 517 + rects[i].bottom * 1031)) *
          16777619u;
  if (key != exclusionKey) {
    exclusionKey = key;
    platform::setGestureExclusion(rects, n);
  }
}

namespace {
// The layout editor's buttons, top centre: Smaller, Larger, Reset, Done.
struct EditorUi {
  RectF btn[4];
  RectF hint;
};
EditorUi editorUi(float W, float H, const float in[4]) {
  EditorUi e;
  const float u = (H - in[1] - in[3]) / 8.0f, bw = u * 1.75f, bh = u * 0.72f, gap = u * 0.16f;
  const float total = bw * 4 + gap * 3, x0 = in[0] + (W - in[0] - in[2] - total) * 0.5f, y0 = in[1] + u * 0.22f;
  for (int i = 0; i < 4; i++) e.btn[i] = rectF(x0 + i * (bw + gap), y0, x0 + i * (bw + gap) + bw, y0 + bh);
  e.hint = rectF(in[0], y0 + bh + u * 0.1f, W - in[2], y0 + bh + u * 0.55f);
  return e;
}
}  // namespace

void App::openTouchEditor() {
  editorFromGame = !inHub && snes;
  menu.closeAll();
  touch.releaseAll();
  editingTouch = true;
  touch.editing = true;
  touchActive = true;
}

void App::closeTouchEditor() {
  if (!editingTouch) return;
  touch.getOffsets(cfg.touchOffset);
  editingTouch = false;
  touch.editing = false;
  touch.releaseAll();
  cfg.save(configPath);
  // (back to the page it was opened from)
  if (editorFromGame) openMenu();
  pushPage(pageTouch());
  prevMenuNav[4] = prevMenuNav[5] = true;
}

bool App::editorTap(float x, float y) {
  const EditorUi e = editorUi((float)video.width(), (float)video.height(), insets);
  auto in = [&](const RectF& r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; };
  if (in(e.btn[0])) cfg.touchScale = std::max(kTouchScaleMin, cfg.touchScale - 5);
  else if (in(e.btn[1])) cfg.touchScale = std::min(kTouchScaleMax, cfg.touchScale + 5);
  else if (in(e.btn[2])) {
    for (auto& o : cfg.touchOffset) o[0] = o[1] = 0;
    cfg.touchScale = 100;
  } else if (in(e.btn[3])) {
    closeTouchEditor();
  } else {
    return false;
  }
  if (cfg.touchHaptics) platform::haptic();
  return true;
}

void App::drawTouchEditor(Canvas* cv) {
  if (!cv) return;
  const float W = (float)video.width(), H = (float)video.height();
  const bool overGame = !inHub && snes;
  cv->fillRect(rectF(0, 0, W, H), colorF(0.03f, 0.04f, 0.08f, overGame ? 0.45f : 1.0f));
  if (!overGame) {
    // (no game on screen: where its picture will be)
    const float sh = H - insets[1] - insets[3], sw = W - insets[0] - insets[2];
    float pw = cfg.touchPicture4x3 ? std::min(sw, sh * (256.0f * 8.0f / 7.0f) / 224.0f) : sw;
    const RectF pic = rectF(insets[0] + (sw - pw) * 0.5f, insets[1], insets[0] + (sw + pw) * 0.5f, insets[1] + sh);
    cv->fillRect(pic, colorF(0.10f, 0.13f, 0.22f, 1.0f));
    cv->strokeRect(pic, std::max(2.0f, H / 400.0f), colorF(1, 1, 1, 0.25f));
    menu.text(L"The game's picture", pic, H / 28.0f, false, colorF(1, 1, 1, 0.35f), kAlignCenter);
  }
  touch.draw(cv, 1.0f);
  const EditorUi e = editorUi(W, H, insets);
  static const wchar_t* const kNames[4] = {L"Smaller", L"Larger", L"Reset", L"Done"};
  for (int i = 0; i < 4; i++) {
    const RectF& r = e.btn[i];
    const float rad = (r.bottom - r.top) * 0.3f;
    cv->fillRound(r, rad, i == 3 ? colorF(0.16f, 0.46f, 0.80f, 0.95f) : colorF(0.12f, 0.15f, 0.25f, 0.95f));
    cv->strokeRound(r, rad, std::max(2.0f, H / 500.0f), colorF(1, 1, 1, 0.5f));
    menu.text(kNames[i], r, (r.bottom - r.top) * 0.42f, true, colorF(1, 1, 1, 1), kAlignCenter);
  }
  const int g = touch.editGroup();
  const std::string size = "Size " + std::to_string(cfg.touchScale) + "%";
  const std::wstring hint = Menu::widen(g >= 0 ? std::string("Moving: ") + touchGroupName(g) + "      " + size +
                                                     "      Drag any control to move it"
                                               : "Drag any control to where your thumb rests      " + size);
  menu.text(hint, e.hint, (e.hint.bottom - e.hint.top) * 0.62f, false, colorF(1, 1, 1, 0.9f), kAlignCenter);
}

// ---------------------------------------------------------------------------
// Starting, stopping, and Android's lifecycle

void App::start(const std::wstring& dataDir, const std::wstring& sharedDir) {
  exeDir = dataDir;
  configPath = pathJoin(exeDir, L"smas_port.ini");
  saveDir = pathJoin(exeDir, L"saves");
  romDir = pathJoin(exeDir, L"roms");
  tmpDir = pathJoin(exeDir, L"tmp");
  shotDir = pathJoin(sharedDir, L"screenshots");
  msuDir = pathJoin(sharedDir, L"msu");
  logPath = pathJoin(sharedDir, kLogFileName);
  makeDir(saveDir);
  makeDir(romDir);
  makeDir(tmpDir);
  // (what an import or export left behind if the app was closed in its middle)
  std::vector<std::wstring> stale;
  if (listFiles(tmpDir, stale))
    for (const std::wstring& n : stale) removeFile(pathJoin(tmpDir, n));
  logf("%s %s (built on SuperMarioAllStars %s) starting", kAppName, kAppVersion, kUpstreamVersion);
  logf("device: %s", platform::deviceSummary().c_str());
  if (!cfg.load(configPath)) cfg.save(configPath);
  battery.setDir(saveDir, appVersionNumber());
  if (!audio.init(cfg.audioLatency)) {
    logf("audio device unavailable");
    menu.notify("Sound could not be started on this device", 5.0f);
  }
  input.init();
  menu.setCanvas(video.canvas());
  menu.touchUi = true;
  refreshInsets();
  platform::preferSixtyHz(cfg.screen60);

  const bool haveRom = loadRom();
  logf(haveRom ? "game file loaded (%s)" : "no game file yet%s", haveRom ? romInfo.headline.c_str() : "");
  achStore.load(pathJoin(exeDir, L"achievements.ini"));
  playing = gameOfIndex(cfg.lastGame);
  if (haveRom) romReady();
  applyConfig();
  inHub = true;
  checkResume();
  refreshHub();
  if (!achStore.noticeShown && haveRom) {
    achStore.noticeShown = true;
    achStore.save();
    if (cfg.achievements) achWelcome();
  }
}

void App::stop() {
  if (rewinding) endRewind();
  saveSram();
  saveResume(true);
  writer.flush();
  cfg.save(configPath);
  music.shutdown();
  logf("stopped");
}

void App::requestQuit() {
  onBackground();
  menu.closeAll();
  running = false;
}

// The app is leaving the screen (Home, another app, the screen locked).
// Android may end the process at any moment after this without another word,
// so everything is put on disk now: the games' own saves, the exact moment of
// play (for Continue Playing), the settings.
void App::onBackground() {
  logf("lifecycle: to the background");
  if (rewinding) endRewind();
  saveSram();
  saveResume(true);
  cfg.save(configPath);
  touch.releaseAll();
  input.clearKeys();
  for (auto& s : padSwallow) s = 0xffff;  // (nothing held carries over)
  // Back on screen, the game waits on the pause menu until Resume.
  if (cfg.pauseOnReturn && !inHub && snes && !menu.open() && !editingTouch && game && !game->levelLoading()) openMenu();
  audio.flush();
}

void App::onForeground() {
  logf("lifecycle: to the foreground");
  refreshInsets();
  input.rescanDevices();
  touch.releaseAll();
  input.clearKeys();
  audio.flush();
}

// Android is short of memory: the rewind buffer (the largest thing the app
// holds) is given up; it fills again as play goes on.
void App::onLowMemory() {
  logf("lifecycle: low memory (rewind buffer released)");
  if (rewinding) endRewind();
  rewind.clear();
}

void App::onTerminating() {
  logf("lifecycle: terminating");
  if (rewinding) endRewind();
  saveSram();
  saveResume(true);
  writer.flush();
  cfg.save(configPath);
}

// The graphics context was lost and made again (the platform has rebuilt the
// video layer): the menus draw on its new canvas.
void App::onDeviceReset() {
  logf("lifecycle: graphics context rebuilt");
  menu.setCanvas(video.canvas());
  refreshInsets();
}

void App::onResized(int w, int h) {
  video.resize(w, h);
  refreshInsets();
}
