// SMAS Mobile: the hub, the game file (import and validation), Help and About
// (see app.h).
#include <algorithm>
#include <cstring>
#include "app/app.h"
#include "app/msupack.h"
#include "app/platform.h"
#include "app/sys.h"
#include "core/crc32.h"
#include "core/fileio.h"
#include "game/achievements.h"

namespace {
const wchar_t kGameFileName[] = L"game.sfc";  // the imported game file's name in the app's "roms" folder
const wchar_t kCandidateName[] = L"import-candidate.bin";
const wchar_t kBackupInName[] = L"backup-in.zip";
const wchar_t kBackupOutName[] = L"backup-out.zip";
const wchar_t kLogOutName[] = L"log-out.txt";
constexpr long long kMaxBackupBytes = 64ll << 20;
}  // namespace

GameId App::gameOfIndex(int i) {
  static const GameId kGames[5] = {GameId::SMB1, GameId::SMBLL, GameId::SMB2, GameId::SMB3, GameId::Menu};
  return kGames[std::clamp(i, 0, 4)];
}

int App::indexOfGame(GameId g) {
  switch (g) {
    case GameId::SMB1: return 0;
    case GameId::SMBLL: return 1;
    case GameId::SMB2: return 2;
    case GameId::SMB3: return 3;
    default: return 4;
  }
}

ColorF App::gameAccent(GameId g) {
  switch (g) {
    case GameId::SMB1: return colorF(0.93f, 0.33f, 0.27f);   // red
    case GameId::SMBLL: return colorF(0.36f, 0.76f, 0.40f);  // green
    case GameId::SMB2: return colorF(0.66f, 0.52f, 0.95f);   // purple
    case GameId::SMB3: return colorF(0.96f, 0.62f, 0.20f);   // orange
    default: return colorF(0.91f, 0.72f, 0.29f);             // gold
  }
}

// ---------------------------------------------------------------------------
// The game file
//
// SMAS Mobile contains no game. The player chooses their own copy of Super
// Mario All-Stars (USA) with the system's file picker; it is copied into the
// app's temporary folder, checked there (app/romcheck.h), and only a file that
// is the game is moved into the app's private "roms" folder. It never leaves
// the device, and nothing else is ever read from outside the app's storage.

// The stored game file: the imported one, or (from older versions, which kept
// the file's own name) any file there that is the game, the verified one first.
bool App::findRom(std::wstring* out) {
  const std::wstring fixed = pathJoin(romDir, kGameFileName);
  if (fileExists(fixed) && checkGameFile(fixed).accepted()) {
    *out = fixed;
    return true;
  }
  std::vector<std::wstring> names;
  if (!listFiles(romDir, names)) return false;
  std::sort(names.begin(), names.end());
  std::wstring fallback;
  for (const std::wstring& n : names) {
    if (n == kGameFileName || (n.size() > 4 && n.compare(n.size() - 4, 4, L".tmp") == 0)) continue;
    const std::wstring p = pathJoin(romDir, n);
    const RomCheck rc = checkGameFile(p);
    if (rc.verdict == RomCheck::Verified) {
      *out = p;
      return true;
    }
    if (rc.accepted() && fallback.empty()) fallback = p;
  }
  if (fallback.empty()) return false;
  *out = fallback;
  return true;
}

bool App::loadRom() {
  std::wstring path;
  if (!findRom(&path)) return false;
  std::vector<uint8_t> data;
  if (!readFile(path, data, kMaxGameFileBytes)) return false;
  const RomCheck rc = checkGameFile(data);
  if (!rc.accepted()) return false;
  romPath = path;
  romInfo = rc;
  game.reset();  // (before the Snes it points at: its destructor uses it)
  snes = std::make_unique<Snes>();
  // (the console's copy patched for an MSU-1 pack if one is chosen)
  applyMsu(data);
  if (!snes->loadRom(data)) {
    snes.reset();
    return false;
  }
  game = std::make_unique<SmasGame>(*snes);
  game->install();
  snes->reset(true);
  loadSram();
  return true;
}

// The chosen MSU-1 pack (see app/msupack.h): its patch applied to the
// console's ROM and the chip attached, or why not (msuStatus).
void App::applyMsu(std::vector<uint8_t>& rom) {
  if (msuDir.empty()) msuDir = pathJoin(exeDir, L"msu");
  msuLoaded = cfg.msuPack;
  msuStatus.clear();
  msuBase.clear();
  if (cfg.msuPack.empty()) return;
  MsuPack pk;
  std::string err;
  std::vector<uint8_t> patch;
  if (findMsuPack(msuDir, cfg.msuPack, pk, &err)) {
    if (!readFile(pk.patch, patch, 16u << 20)) {
      err = "its patch can't be read";
    } else {
      std::vector<uint8_t> patched = rom;
      if ((patched.size() & 0x3ff) == 0x200) patched.erase(patched.begin(), patched.begin() + 0x200);
      if (applyRomPatch(patch, patched, &err)) {
        rom.swap(patched);
        snes->msu = std::make_unique<Msu1>();
        snes->msu->open(pk.base);
        snes->msu->setGain(cfg.msuVolume / 100.0f * cfg.musicVolume / 100.0f);
        msuBase = pk.base;
        msuStatus = "Playing " + narrow(cfg.msuPack) + ": " + std::to_string(pk.tracks) + " tracks, patched with " +
                    narrow(pathName(pk.patch)) + ".";
        logf("MSU-1 pack in use: %d tracks", pk.tracks);
        return;
      }
    }
  }
  msuStatus = "Not used: " + err + ".";
  logf("MSU-1 pack not used: %s", err.c_str());
}

// Another pack chosen: the console is rebuilt with it (outside a game: the
// hub shows). Save files are kept; a game's save states belong with the pack
// they were made with.
void App::reloadForMsu() {
  if (!snes || cfg.msuPack == msuLoaded) return;
  saveSram();
  if (loadRom()) romReady();
}

// After the game file is loaded (at start, or just imported).
void App::romReady() {
  music.init(snes.get(), game.get(), &audio);
  music.setMsu(msuBase, narrow(msuLoaded), cfg.msuVolume / 100.0f);  // (the Music Player plays the pack too)
  achNewRun(cfg.achievements);
  applyConfig();
  if (romInfo.verdict != RomCheck::Verified) logf("game file: %s", romInfo.headline.c_str());
}

void App::pickGameFile() {
  if (picking != Picking::None) return;
  makeDir(tmpDir);
  const std::wstring to = pathJoin(tmpDir, kCandidateName);
  removeFile(to);
  // (one byte more than a game file can be, so that "too large" is told apart from "the largest allowed")
  if (platform::pickDocument(platform::PickKind::GameFile, to, (long long)kMaxGameFileBytes + 1)) {
    picking = Picking::GameFile;
  } else {
    menu.notify("The file picker could not be opened on this device", 5.0f);
  }
}

// The log (what the app did: no personal information), saved where the
// player chooses, to attach to a report. A copy is handed over, so that it is
// whole while the app goes on writing the log itself.
void App::exportLog() {
  if (picking != Picking::None) return;
  if (g_log) fflush(g_log);
  std::vector<uint8_t> text;
  if (logPath.empty() || !readFile(logPath, text, 8u << 20) || text.empty()) {
    menu.notify("There is no log file yet", 4.0f);
    return;
  }
  makeDir(tmpDir);
  const std::wstring out = pathJoin(tmpDir, kLogOutName);
  if (!writeFileSafely(out, text.data(), text.size())) {
    menu.notify(std::string("The log could not be copied: ") + lastWriteError(), 6.0f);
    return;
  }
  if (platform::saveDocument(out, "smas-mobile-log-" + timeText("%Y%m%d-%H%M") + ".txt", "text/plain")) {
    picking = Picking::LogExport;
  } else {
    removeFile(out);
    menu.notify("The \"save as\" screen could not be opened on this device", 5.0f);
  }
}

void App::pickBackup() {
  if (picking != Picking::None) return;
  makeDir(tmpDir);
  const std::wstring to = pathJoin(tmpDir, kBackupInName);
  removeFile(to);
  if (platform::pickDocument(platform::PickKind::Backup, to, kMaxBackupBytes + 1)) picking = Picking::Backup;
  else menu.notify("The file picker could not be opened on this device", 5.0f);
}

// The system picker runs over the app (paused meanwhile); when it closes, what
// was chosen has been copied into the app's temporary folder.
void App::pollPick() {
  if (picking == Picking::None) return;
  std::string name, error;
  const platform::PickResult r = platform::pollDocument(&name, &error);
  if (r == platform::PickResult::Pending) return;
  const Picking what = picking;
  picking = Picking::None;
  if (what == Picking::LogExport) {
    removeFile(pathJoin(tmpDir, kLogOutName));
    if (r == platform::PickResult::Done) menu.notify("Log file saved", 4.0f);
    else if (r != platform::PickResult::Cancelled) menu.notify("The log file could not be saved there: choose another place", 6.0f);
    return;
  }
  const std::wstring in = pathJoin(tmpDir, what == Picking::GameFile ? kCandidateName : kBackupInName);
  const std::wstring out = pathJoin(tmpDir, kBackupOutName);
  if (what == Picking::Export) {
    removeFile(out);
    if (r == platform::PickResult::Done) {
      cfg.lastBackup = timeNow();
      cfg.save(configPath);
      logf("backup exported (%zu bytes: %s)", exportSummary.bytes, exportSummary.describe().c_str());
      pushPage(pageText("Backup saved", name,
                        "It holds " + exportSummary.describe() +
                            ".\n\nKeep it somewhere other than this phone (your cloud storage, a computer): then a "
                            "lost, broken or reset phone does not take your progress with it.\n\nTo use it: Save "
                            "Management > Restore Backup."));
    } else if (r != platform::PickResult::Cancelled) {
      logf("backup export failed: %s", error.c_str());
      pushPage(pageText("The backup was not saved", "",
                        "The place you chose could not be written to" + (error.empty() ? std::string() : " (" + error + ")") +
                            ". Your saves are unchanged. Try again and choose another place, such as Downloads."));
    }
    refreshHub();
    return;
  }
  if (r == platform::PickResult::Cancelled) {
    removeFile(in);
    return;
  }
  if (r == platform::PickResult::TooLarge) {
    removeFile(in);
    if (what == Picking::GameFile) {
      RomCheck rc;
      rc.verdict = RomCheck::TooLarge;
      rc.headline = "That file is too large to be the game";
      rc.explain = "SMAS Mobile needs your own copy of Super Mario All-Stars (USA) for the Super Nintendo: a file of "
                   "2 MB, usually named \"Super Mario All-Stars (USA).sfc\".";
      pushPage(pageImportResult(rc, name, false));
    } else {
      pushPage(pageText("That file is not a SMAS Mobile backup", name,
                        "It is far too large to be one. Backups made by Save Management > Back Up Saves are small "
                        ".zip files named \"SMAS-Mobile-backup-...\"."));
    }
    return;
  }
  if (r == platform::PickResult::Failed) {
    removeFile(in);
    logf("picked file could not be read: %s", error.c_str());
    pushPage(pageText("That file could not be read", name,
                      "Android could not give the app that file" + (error.empty() ? std::string() : " (" + error + ")") +
                          ". If it is in cloud storage, download it to the phone first, then choose it again."));
    return;
  }
  if (what == Picking::GameFile) importGameFile(in, name);
  else restoreBackupFile(in, name);
}

void App::importGameFile(const std::wstring& candidate, const std::string& shownName) {
  const RomCheck rc = checkGameFile(candidate);
  // (the file's own name is not logged: it is the player's business)
  logf("import: %s: %s (%zu bytes, crc %08X)", rc.accepted() ? "accepted" : "refused", rc.headline.c_str(), rc.size, rc.crc);
  if (!rc.accepted()) {
    removeFile(candidate);
    pushPage(pageImportResult(rc, shownName, false));
    return;
  }
  // The game: into place under a fixed name, without a copier header, whole
  // or not at all; the one in use (if any) stays until this one is in place.
  std::vector<uint8_t> data;
  std::string why;
  bool ok = readFile(candidate, data, kMaxGameFileBytes);
  if (!ok) why = "the copy in the app's temporary folder could not be read";
  if (ok && rc.copierHeader) data.erase(data.begin(), data.begin() + 0x200);
  if (ok && !roomToWrite(romDir, data.size(), &why)) ok = false;
  const std::wstring dest = pathJoin(romDir, kGameFileName);
  if (ok && !writeFileSafely(dest, data.data(), data.size())) {
    ok = false;
    why = lastWriteError();
  }
  // (read back from the storage: only then does it count)
  if (ok && !checkGameFile(dest).accepted()) {
    ok = false;
    why = "it could not be read back from storage";
    removeFile(dest);
  }
  removeFile(candidate);
  if (!ok) {
    logf("import: could not store the game file: %s", why.c_str());
    pushPage(pageText("The game file could not be stored", shownName,
                      "It is the right file, but the app could not keep its copy: " + why +
                          ". Free some storage and import it again. Nothing else was changed."));
    return;
  }
  // (files earlier versions kept under their own names: no longer needed)
  std::vector<std::wstring> names;
  if (listFiles(romDir, names))
    for (const std::wstring& n : names)
      if (n != kGameFileName) removeFile(pathJoin(romDir, n));
  if (snes) saveSram();
  if (!loadRom()) {
    pushPage(pageText("The game file could not be started", shownName,
                      "It was stored, but the console could not be started with it. Close the app and open it again; if "
                      "that does not help, import the file again."));
    refreshHub();
    return;
  }
  romReady();
  checkResume();
  refreshHub();
  hub.focus(haveResume ? Hub::kContinue : Hub::kPlaySMB1);
  pushPage(pageImportResult(romInfo, shownName, true));
}

void App::removeGameFile() {
  if (snes) saveSram();
  music.stop();
  game.reset();
  snes.reset();
  std::vector<std::wstring> names;
  if (listFiles(romDir, names))
    for (const std::wstring& n : names) removeFile(pathJoin(romDir, n));
  romPath.clear();
  romInfo = RomCheck();
  inHub = true;
  logf("game file removed (saves kept)");
  refreshHub();
}

// ---------------------------------------------------------------------------
// The hub

// A game chosen (or Reset Game): the console is reset and driven through the
// All-Stars menu to the game's save files, out of sight (see
// SmasGame::requestTitle), as a new run for achievements.
void App::launchGame(GameId g) {
  pendingSave = -1;  // (a save waiting for a co-op turn's end: not into another game)
  if (!snes) {
    pushPage(pageGameFile());
    return;
  }
  reloadForMsu();  // (a pack chosen in a game: in use from this start)
  if (!snes) return;
  playing = g;
  cfg.lastGame = indexOfGame(g);
  inHub = false;
  menu.closeAll();
  if (rewinding) endRewind();
  undoState.clear();
  if (g == GameId::Menu) {
    // (the cartridge as at power-on, its title and game select)
    saveSram();
    game->cancelWarps();
    snes->reset(false);
  } else {
    game->requestTitle(g, false);
  }
  rewind.clear();
  audio.flush();
  achNewRun(cfg.achievements);
  applyConfig();
  cfg.save(configPath);
  resumeTimer = 45.0;  // (this game's first resume point soon: a quarter of a minute in)
  logf("game started: %s", SmasGame::gameName(g));
  if (!menuHintShown) {
    menuHintShown = true;
    menu.notify(touchShown() ? "Tap MENU (top left) to pause, save or change settings"
                : cfg.selectMode == 0 ? "Press Select for the menu"
                                      : "Hold Select for the menu",
                4.0f, true);
  }
}

// Out of the game, to the hub. The exact moment is kept: Continue Playing
// comes back to it.
void App::returnToHub() {
  pendingSave = -1;
  if (rewinding) endRewind();
  if (snes) {
    saveSram();
    saveResume(true);
    game->cancelWarps();
  }
  rewind.clear();
  undoState.clear();
  audio.flush();
  menu.closeAll();
  inHub = true;
  reloadForMsu();
  checkResume();
  refreshHub();
  hub.focus(haveResume ? Hub::kContinue : (Hub::Action)(Hub::kPlaySMB1 + indexOfGame(playing)));
}

void App::refreshHub() {
  Hub::State s;
  s.haveGame = snes != nullptr;
  s.gameVerified = s.haveGame && romInfo.verdict == RomCheck::Verified;
  s.gameLine = !s.haveGame ? "Not imported yet" : s.gameVerified ? "Verified" : "Different version";
  s.haveResume = s.haveGame && haveResume;
  if (s.haveResume) {
    GameId g = (GameId)resumeInfo.aux;
    const char* name = g == GameId::SMB1 || g == GameId::SMBLL || g == GameId::SMB2 || g == GameId::SMB3
                           ? SmasGame::gameName(g)
                           : "Super Mario All-Stars";
    s.resumeLine = std::string(name) + "  \xE2\x80\xA2  where you stopped " + whenText(resumeInfo.time);
  }
  const long long saved = battery.lastSavedTime();
  s.savesLine = saved > 0 ? "Saved " + whenText(saved) : battery.exists() ? "Saves kept safe" : "Backups, save states";
  int got = 0;
  for (const AchievementDef& d : achievementDefs()) got += achStore.unlocked(d.id) ? 1 : 0;
  s.achievementsLine = std::to_string(got) + " of " + std::to_string((int)achievementDefs().size());
  s.version = kAppVersion;
  s.confirmRight = cfg.confirmRight;
  s.touch = touchActive;
  hub.setState(s);
}

void App::hubAction(Hub::Action a) {
  switch (a) {
    case Hub::kContinue: continueGame(); break;
    case Hub::kImport:
      if (snes) pushPage(pageGameFile());
      else pickGameFile();
      break;
    case Hub::kPlaySMB1:
    case Hub::kPlaySMBLL:
    case Hub::kPlaySMB2:
    case Hub::kPlaySMB3:
    case Hub::kPlayAllStars: launchGame(gameOfIndex(a - Hub::kPlaySMB1)); break;
    case Hub::kGameFile: pushPage(pageGameFile()); break;
    case Hub::kSaves: pushPage(pageSaves()); break;
    case Hub::kControls: pushPage(pageControls()); break;
    case Hub::kDisplay: pushPage(pageDisplay()); break;
    case Hub::kAudio: pushPage(pageAudio()); break;
    case Hub::kAchievements: pushPage(pageAchievements()); break;
    case Hub::kMusic:
      if (snes) pushPage(pageMusicPlayer());
      else pushPage(pageGameFile());
      break;
    case Hub::kHelp: pushPage(pageHelp()); break;
    case Hub::kAbout: pushPage(pageAbout()); break;
    case Hub::kUpdates: pushPage(pageUpdates()); break;
    case Hub::kExit:
      menuAnim = 0;
      menu.confirm("Close SMAS Mobile?", [this]() { requestQuit(); }, "Everything is saved first.");
      break;
    default: break;
  }
}

// A display refresh on the hub: its tiles (or a menu page over it).
void App::frameHub(double dt) {
  const bool menuOpen = menu.open();
  MenuInput mi = menuInput();
  if (menuOpen) {
    if (menu.top() && !menu.capturing) menu.update(mi, (float)dt);
    menuAnim = std::min(1.0f, menuAnim + (float)dt * 8.0f);
    hubTap = Hub::kNone;
  } else {
    Hub::State s = hub.state();
    if (s.touch != touchActive) {
      s.touch = touchActive;
      hub.setState(s);
    }
    Hub::Action act = hub.update(mi, (float)dt);
    if (act == Hub::kNone) act = hubTap;  // (a tap)
    hubTap = Hub::kNone;
    if (act != Hub::kNone) hubAction(act);
  }
  if (menuOpen && !menu.open()) {
    cfg.save(configPath);
    refreshHub();  // (what the pages changed: the saves' line, the game file's)
  }
  if (snes && inHub) music.update(dt, menu.open());
}

// ---------------------------------------------------------------------------
// Pages

// A page of explanation: the text as rows that scroll (drag, or up and down).
std::shared_ptr<MenuPage> App::pageText(const std::string& title, const std::string& subtitle, const std::string& text) {
  auto p = std::make_shared<MenuPage>();
  p->title = title;
  p->subtitle = subtitle;
  p->wide = true;
  p->rebuild = [this, text](MenuPage& pg) {
    pg.items.clear();
    menu.addText(pg, text);
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("OK", [this]() { menu.pop(); }));
  };
  Menu::makeScrollable(*p);
  return p;
}

std::shared_ptr<MenuPage> App::pageImportResult(const RomCheck& rc, const std::string& shownName, bool imported) {
  auto p = std::make_shared<MenuPage>();
  p->title = imported ? "Game file imported" : "That file can't be used";
  p->subtitle = shownName;
  p->wide = true;
  p->rebuild = [this, rc, imported](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::header(rc.headline));
    menu.addText(pg, rc.explain);
    if (imported) {
      menu.addText(pg, "It is now stored inside the app (the original file is not needed any more, and was not "
                       "changed). It never leaves this device.");
      pg.items.push_back(MenuItem::separator());
      pg.items.push_back(MenuItem::action("Choose a Game", [this]() { menu.closeAll(); },
                                          "Back to the hub: the four games are ready."));
    } else {
      pg.items.push_back(MenuItem::separator());
      pg.items.push_back(MenuItem::action("Choose Another File", [this]() {
        menu.pop();
        pickGameFile();
      }, "Opens the file picker again."));
      pg.items.push_back(MenuItem::submenu("Which File Do I Need?", [this]() { menu.push(pageHelpTopic(0)); },
                                           "What SMAS Mobile needs, and where such a file comes from."));
      pg.items.push_back(MenuItem::action("Back", [this]() { menu.pop(); }));
    }
  };
  Menu::makeScrollable(*p);
  return p;
}

std::shared_ptr<MenuPage> App::pageGameFile() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Game File";
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    if (snes) {
      pg.subtitle = romInfo.headline;
      MenuItem st = MenuItem::info("Status");
      st.valueText = [this]() {
        return std::string(romInfo.verdict == RomCheck::Verified ? "Verified: exactly the supported version"
                                                                 : "Works, but not the exact supported version");
      };
      pg.items.push_back(st);
      MenuItem crc = MenuItem::info("Checksum (CRC-32)");
      crc.valueText = [this]() {
        char buf[16];
        snprintf(buf, sizeof buf, "%08X", romInfo.crc);
        return std::string(buf);
      };
      pg.items.push_back(crc);
      if (romInfo.verdict != RomCheck::Verified) menu.addText(pg, romInfo.explain);
      pg.items.push_back(MenuItem::separator());
      pg.items.push_back(MenuItem::action("Replace Game File", [this]() { pickGameFile(); },
                                          "Choose another copy. The one in use stays unless the new file is accepted. "
                                          "Your saves are kept."));
      pg.items.push_back(MenuItem::action("Remove Game File", [this]() {
        menu.confirm("Remove the game file from the app?", [this]() {
          removeGameFile();
          menu.closeAll();
        }, "Your saves, save states and settings are kept. To play again, import the file again.");
      }, "Deletes the app's copy of the game file. Saves are kept."));
    } else {
      pg.subtitle = "No game file has been imported yet";
      menu.addText(pg, "SMAS Mobile does not contain any game. It runs your own copy of Super Mario All-Stars (USA) "
                       "for the Super Nintendo: one file of 2 MB, usually named \"Super Mario All-Stars (USA).sfc\" "
                       "(.smc works too).");
      menu.addText(pg, "Copy that file to this phone (Downloads is fine), then choose it below. The app checks it, "
                       "keeps its own copy, and says clearly if it is not the right file.");
      pg.items.push_back(MenuItem::separator());
      pg.items.push_back(MenuItem::action("Import Game File", [this]() { pickGameFile(); },
                                          "Opens Android's file picker. Choose the .sfc file itself (extract it "
                                          "first if it is inside a .zip)."));
    }
    pg.items.push_back(MenuItem::submenu("Which File Do I Need?", [this]() { menu.push(pageHelpTopic(0)); },
                                         "What SMAS Mobile needs, and where such a file comes from."));
  };
  return p;
}

namespace {
struct HelpTopic {
  const char* title;
  const char* text;
};
const HelpTopic kHelp[] = {
    {"The game file",
     "SMAS Mobile is a player, not a game: it contains no game data. To play you need your own copy of Super Mario "
     "All-Stars (USA) for the Super Nintendo, as one file of exactly 2 MB (2,097,152 bytes), usually named \"Super "
     "Mario All-Stars (USA).sfc\". A .smc file works too, with or without a 512-byte copier header.\n\n"
     "Such a file is made by copying (\"dumping\") a cartridge you own with a cartridge reader. SMAS Mobile cannot "
     "supply one, and nobody connected with it will.\n\n"
     "Import it: put the file on the phone (the Downloads folder is fine), open Game File on the hub, choose Import "
     "Game File and pick it. If it is inside a .zip, extract it first with the phone's Files app.\n\n"
     "The app checks the file before keeping it and tells you which of these it is: the exact supported version "
     "(\"verified\"); another version of Super Mario All-Stars (it runs, but widescreen, cheats and the level select "
     "are tuned for the USA version); or not the game at all, with the reason: an archive, a save file, another "
     "game, the Super Mario All-Stars + Super Mario World cartridge (not supported), an incomplete download.\n\n"
     "The app keeps its own copy in its private storage. Your original file is not changed and is no longer needed. "
     "The file never leaves the device: the app has no network access at all."},
    {"Saving your progress",
     "There are three kinds of save, and all of them are kept automatically.\n\n"
     "1. The games' own save files. Each game saves as it did on the cartridge (for example Save & Continue or Save & "
     "Quit from a game's pause screen). The app writes that to storage within five seconds, and again whenever you "
     "leave the app.\n\n"
     "2. Your place. When you leave the app, return to the hub, or once a minute while you play, the exact moment is "
     "kept. Continue Playing on the hub takes you back to it, even if Android closed the app in the meantime.\n\n"
     "3. Save states. MENU > Save State keeps the exact moment in one of nine slots; Load State returns to it. "
     "Loading a state never undoes progress a game has saved itself, and Undo Load State takes you back if you "
     "loaded by mistake. Each slot also keeps its previous contents.\n\n"
     "Safety: every save is written completely before it replaces the old one, is checked when it is read, and the "
     "two saves before it are kept. If a save file is ever found damaged it is set aside (never deleted) and the "
     "newest good one is used. Save Management > Restore Points lists earlier saves you can go back to.\n\n"
     "Back up: Save Management > Back Up Saves makes one small .zip file with all of it. Keep it off the phone. "
     "Uninstalling the app or clearing its data deletes its saves, so back up first. Installing a newer version over "
     "the top keeps everything."},
    {"Touch controls",
     "While you play, the screen shows a Super Nintendo pad. On the left, from the bottom: the D-pad, Select, L, and "
     "MENU in the top corner. On the right: A B X Y, Start, R. Rewind and fast forward appear at the top right when "
     "achievements are off.\n\n"
     "Several fingers work at once: hold B to run, slide your thumb to A to jump, steer with the other thumb.\n\n"
     "Make them yours in Controls > On-Screen Controls: Move the Controls lets you drag each one where your thumbs "
     "rest; Size makes them larger or smaller; Opacity makes them more or less see-through; Vibrate On Press gives "
     "a tick you can feel.\n\n"
     "By default the picture is the original 4:3 in the middle of the screen with the controls at its sides. On a "
     "phone's wide screen they fit beside the picture, so your thumbs do not cover the game (on a squarer screen, "
     "such as a tablet's, they overlap its edges). For a full widescreen picture under the controls, set Picture "
     "With Touch Controls to As Aspect Ratio.\n\n"
     "The controls hide when you use a controller or keyboard and come back when you touch the screen."},
    {"Controllers",
     "Bluetooth and USB controllers work: connect the controller in Android's settings first, then open the app. "
     "Xbox, PlayStation, Switch Pro and most other pads are recognised and need no setup: the buttons are by "
     "position (bottom face button is B, right is A, as on a Super Nintendo pad).\n\n"
     "Controls shows what is connected. A pad listed as (generic) or (basic mode) is one Android does not describe "
     "fully: it works, but you may need to assign its buttons in Player 1 Controller Buttons (choose a row, press "
     "the button).\n\n"
     "Select opens the menu by default (Controls > Select Button changes that). Menu Buttons chooses whether the "
     "bottom or the right face button confirms in menus.\n\n"
     "Two players: connect two controllers and set Player 2 Uses. Two-Player Co-op is in Settings.\n\n"
     "If a controller is not seen: reconnect it, check it works in Android's own menus, then reopen the app. If its "
     "buttons do the wrong things, assign them in Controls."},
    {"If something is wrong",
     "\"That file can't be used\": the message says what the file is. The usual causes are a .zip that needs "
     "extracting first, a different game, or a download that did not finish (check that the file is exactly 2 MB).\n\n"
     "The picture stutters or the sound crackles: set Display > Aspect Ratio to 4:3, Graphics > Filter to Nearest, "
     "and raise Audio > Latency. Close other apps. On a 120 Hz screen, if movement looks uneven, set the phone's "
     "display to 60 Hz for this app. Show FPS in Display should read about 60.\n\n"
     "No sound: check the phone's media volume and Audio > Volume.\n\n"
     "The screen went black after returning to the app: wait a second (the picture is rebuilt); if it stays black, "
     "close the app from the recent-apps list and open it again. Your place is kept: choose Continue Playing.\n\n"
     "A save looks wrong or old: Save Management > Restore Points lists earlier saves; choose one to go back to it. "
     "The save it replaces is kept as a restore point too, so nothing is lost by trying.\n\n"
     "Moving to a new phone: Save Management > Back Up Saves here, copy the .zip to the new phone, install SMAS "
     "Mobile there, import your game file, then Save Management > Restore Backup.\n\n"
     "Reporting a problem: the app keeps a short log of what it did (the device model and Android version, what "
     "was loaded and saved, errors; no names, no file names, nothing personal). Help > Save the Log File puts a copy "
     "where you choose; attach it to your report."},
};
}  // namespace

std::shared_ptr<MenuPage> App::pageHelpTopic(int topic) {
  topic = std::clamp(topic, 0, (int)(sizeof(kHelp) / sizeof(kHelp[0])) - 1);
  auto p = std::make_shared<MenuPage>();
  p->title = kHelp[topic].title;
  p->wide = true;
  p->rebuild = [this, topic](MenuPage& pg) {
    pg.items.clear();
    menu.addText(pg, kHelp[topic].text);
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Back", [this]() { menu.pop(); }));
  };
  Menu::makeScrollable(*p);
  return p;
}

std::shared_ptr<MenuPage> App::pageHelp() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Help";
  p->subtitle = "Short guides. Everything works offline.";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    static const char* const kWhat[] = {
        "What SMAS Mobile needs to play, and how to import it.",
        "The three kinds of save, how they are kept safe, and backing up.",
        "The on-screen pad: moving, sizing and using it.",
        "Bluetooth and USB controllers, two players.",
        "Stutter, no sound, a black screen, an old save, a new phone.",
    };
    for (int i = 0; i < (int)(sizeof(kHelp) / sizeof(kHelp[0])); i++)
      pg.items.push_back(MenuItem::submenu(kHelp[i].title, [this, i]() { menu.push(pageHelpTopic(i)); }, kWhat[i]));
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Save the Log File", [this]() { exportLog(); },
                                        "For reporting a problem: saves a copy of the app's log (what it did; nothing "
                                        "personal) where you choose, to attach to your report."));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageUpdates() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Updates";
  p->subtitle = std::string("This is version ") + kAppVersion;
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    menu.addText(pg, "SMAS Mobile never goes online: it has no network permission, so it cannot check for updates "
                     "itself. New versions are published on its GitHub releases page.");
    menu.addText(pg, "To update, download the new .apk there and install it over this one. Do not uninstall first: "
                     "uninstalling deletes your saves. Installing over the top keeps them. Back up first to be sure "
                     "(Save Management > Back Up Saves).");
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Open the Releases Page", [this]() {
      platform::openUrl(kReleasesUrl);
      menu.notify("Opened in your browser");
    }, "Opens github.com/LIN4CRE/Android-all-stars/releases in your web browser."));
    pg.items.push_back(MenuItem::action("Back", [this]() { menu.pop(); }));
  };
  Menu::makeScrollable(*p);
  return p;
}

std::shared_ptr<MenuPage> App::pageLicences() {
  return pageText(
      "Licences", "Open-source software in SMAS Mobile",
      "SMAS Mobile\nCopyright (c) 2026 LIN4CRE. MIT License.\n\n"
      "Super Mario All-Stars port (the emulation core, the game enhancements and the menu toolkit this app is built "
      "on)\nCopyright (c) 2026 Darhkfox. MIT License: permission is granted, free of charge, to use, copy, modify, "
      "merge, publish and distribute the software, provided the copyright notice and the permission notice are "
      "included in all copies. The software is provided \"as is\", without warranty of any kind.\n\n"
      "SDL 2 (window, input, audio)\nCopyright (C) 1997-2025 Sam Lantinga. zlib License.\n\n"
      "Luckiest Guy (the title font)\nCopyright (c) 2010 by Brian J. Bonislawsky DBA Astigmatic (AOETI). Apache "
      "License, Version 2.0.\n\n"
      "stb_image (image decoding, and the decompressor used for backups)\nSean Barrett and contributors. Public "
      "domain / MIT License.\n\n"
      "The complete licence texts are in the app's source repository (LICENSE, NOTICE.md and "
      "THIRD_PARTY_NOTICES.md) at github.com/LIN4CRE/Android-all-stars.\n\n"
      "Super Mario All-Stars, Super Mario Bros. and Mario are trademarks of Nintendo. They are named here only to "
      "say what the app is compatible with. No Nintendo code, graphics, music or other game data is included.");
}

std::shared_ptr<MenuPage> App::pageAbout() {
  auto p = std::make_shared<MenuPage>();
  p->title = "About";
  p->subtitle = std::string(kAppName) + " " + kAppVersion + "  \xE2\x80\xA2  published by " + kPublisher;
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    pg.items.push_back(MenuItem::header("What this is"));
    menu.addText(pg, "An unofficial Android app for playing your own copy of Super Mario All-Stars (USA), with "
                     "widescreen, touch controls, save states and achievements.");
    menu.addText(pg, std::string("It is built on the open-source Super Mario All-Stars port by Darhkfox (version ") +
                         kUpstreamVersion + ", MIT licence), adapted for Android phones and tablets by LIN4CRE.");
    pg.items.push_back(MenuItem::header("What this is not"));
    menu.addText(pg, "Not a Nintendo product. SMAS Mobile is not affiliated with, endorsed, sponsored or approved "
                     "by Nintendo. Super Mario All-Stars and Mario are trademarks of Nintendo.");
    menu.addText(pg, "No game is included. You supply your own game file; the app only reads it, on this device.");
    pg.items.push_back(MenuItem::header("Privacy"));
    menu.addText(pg, "The app has no network permission: it cannot send or receive anything. It collects nothing, "
                     "contains no advertising and no analytics. Your game file, saves and settings stay in the "
                     "app's own storage unless you export a backup yourself.");
    pg.items.push_back(MenuItem::header("More"));
    pg.items.push_back(MenuItem::submenu("Licences", [this]() { menu.push(pageLicences()); },
                                         "The open-source software in the app, and its licences."));
    pg.items.push_back(MenuItem::action("Source Code", [this]() {
      platform::openUrl(kSourceUrl);
      menu.notify("Opened in your browser");
    }, "github.com/LIN4CRE/Android-all-stars, in your web browser."));
    pg.items.push_back(MenuItem::action("The Original Port", [this]() {
      platform::openUrl(kUpstreamUrl);
      menu.notify("Opened in your browser");
    }, "github.com/Darhkfox/SuperMarioAllStars: the project this app is built on, in your web browser."));
    MenuItem ver = MenuItem::info("Version");
    ver.valueText = []() { return std::string(kAppVersion) + " (port " + kUpstreamVersion + ")"; };
    pg.items.push_back(ver);
    MenuItem dev = MenuItem::info("Device");
    dev.valueText = []() { return platform::deviceSummary(); };
    pg.items.push_back(dev);
  };
  Menu::makeScrollable(*p);
  return p;
}
