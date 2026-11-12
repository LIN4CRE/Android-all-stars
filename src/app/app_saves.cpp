// SMAS Mobile: saved progress (see app.h and app/savestore.h).
//
// What is kept, and when:
//
//   the games' own save files   within five seconds of a game writing them,
//   (battery memory)            and whenever the app leaves the screen
//   the resume point            when the app leaves the screen, on Return to
//                               Hub and Exit, and once a minute of play
//   save states                 when the player saves one (nine slots)
//
// Nothing here ever deletes a save to make room for another: an older copy
// is replaced only by a newer one that has been written out and read back,
// and a damaged file is renamed, not removed.
#include <algorithm>
#include <cstring>
#include "app/app.h"
#include "app/platform.h"
#include "app/sys.h"
#include "core/crc32.h"
#include "core/fileio.h"

std::wstring App::statePath(int slot) const { return states::slotPath(saveDir, slot); }
std::wstring App::resumePath() const { return pathJoin(saveDir, L"resume.sst"); }

// "today, 18:30"; "yesterday, 09:05"; "12 Nov, 18:30"; "12 Nov 2025".
std::string App::whenText(long long t) const {
  if (t <= 0) return "at an unknown time";
  const long long now = timeNow();
  const std::string day = timeTextAt(t, "%Y%m%d");
  if (day == timeTextAt(now, "%Y%m%d")) return "today, " + timeTextAt(t, "%H:%M");
  if (day == timeTextAt(now - 86400, "%Y%m%d")) return "yesterday, " + timeTextAt(t, "%H:%M");
  if (timeTextAt(t, "%Y") == timeTextAt(now, "%Y")) return timeTextAt(t, "%d %b, %H:%M");
  return timeTextAt(t, "%d %b %Y");
}

std::string App::slotText(int slot) const {
  const long long t = fileTime(statePath(slot));
  if (t <= 0 || !states::slotUsed(saveDir, slot)) return "Empty";
  return whenText(t);
}

// ---------------------------------------------------------------------------
// The games' own save files

void App::loadSram() {
  if (!snes) return;
  battery.setDir(saveDir, appVersionNumber());
  const BatteryStore::LoadResult r = battery.load(snes->sram, romInfo.crc);
  snes->sramDirty = false;
  sramFailed = false;
  switch (r.origin) {
    case BatteryStore::Origin::Fresh: logf("saves: none yet"); break;
    case BatteryStore::Origin::Current: logf("saves: %s, written %s", r.source.c_str(), whenText(r.time).c_str()); break;
    case BatteryStore::Origin::Recovered:
      logf("saves: RECOVERED from %s (written %s); %s; %d damaged file(s) set aside, %d unreadable", r.source.c_str(),
           whenText(r.time).c_str(), r.problem.c_str(), r.setAside, r.unreadable);
      menu.notify("Your last save could not be read, so the one before it (" + whenText(r.time) +
                      ") is in use. The unreadable file was kept, not deleted.",
                  9.0f);
      break;
    case BatteryStore::Origin::Imported: {
      logf("saves: imported from %s", r.source.c_str());
      // (kept from now on as a checked save; the plain file is left as it is)
      std::string err;
      if (!battery.save(snes->sram, romInfo.crc, &err)) logf("saves: could not store the imported save: %s", err.c_str());
      break;
    }
    case BatteryStore::Origin::Damaged:
      logf("saves: NONE READABLE (%s); %d damaged file(s) set aside, %d unreadable", r.problem.c_str(), r.setAside,
           r.unreadable);
      menu.notify("Saved progress could not be read. The files were kept, not deleted: see Save Management > "
                  "Restore Points, or restore a backup.",
                  10.0f);
      break;
  }
  // The save as it is at the start of this session, kept as a restore point.
  if (!restorePointKept && (r.origin == BatteryStore::Origin::Current || r.origin == BatteryStore::Origin::Recovered ||
                            r.origin == BatteryStore::Origin::Imported)) {
    restorePointKept = battery.keepRestorePoint();
  }
}

void App::saveSram() {
  if (!snes) return;
  std::string err;
  if (battery.save(snes->sram, romInfo.crc, &err)) {
    snes->sramDirty = false;
    if (sramFailed) logf("saves: saving works again");
    sramFailed = false;
  } else if (!sramFailed) {  // (still dirty: tried again every five seconds, and when the app leaves the screen)
    sramFailed = true;
    logf("saves: SAVING FAILED: %s", err.c_str());
    menu.notify("Game progress could not be saved: " + err + ". The last good save is untouched; it will be tried "
                "again.",
                8.0f);
  }
}

// ---------------------------------------------------------------------------
// Save states

// A state into the console. The games' own save files stay as they are now:
// neither a save state nor Continue Playing ever takes back progress a game
// has saved (a state made before that save would otherwise overwrite it).
bool App::applyState(const std::vector<uint8_t>& st) {
  if (!snes) return false;
  std::vector<uint8_t> keep(snes->sram, snes->sram + sizeof(snes->sram));
  const bool dirty = snes->sramDirty;
  if (!snes->loadState(st)) return false;  // (the machine is as it was)
  std::memcpy(snes->sram, keep.data(), keep.size());
  snes->sramDirty = dirty;
  game->onStateLoaded();
  return true;
}

void App::saveState(int slot) {
  if (!snes || !game) return;
  // (In a co-op turn's middle the other player's variables are the game's:
  // saved there, the game's player came back as him. At the next frame's end.)
  if (game->coopMidTurn()) {
    pendingSave = slot;
    return;
  }
  pendingSave = -1;
  const std::vector<uint8_t> st = snes->saveState();
  std::string err;
  if (states::writeSlot(saveDir, slot, st, &err)) {
    menu.notify("State " + std::to_string(slot) + " saved");
  } else {
    logf("save state %d failed: %s", slot, err.c_str());
    menu.notify("State " + std::to_string(slot) + " could not be saved: " + err + ". The slot is as it was.", 6.0f);
  }
}

void App::loadState(int slot) {
  if (!snes || !game) return;
  pendingSave = -1;  // (the timeline it was asked in is gone)
  if (!states::slotUsed(saveDir, slot)) {
    menu.notify("State " + std::to_string(slot) + " is empty");
    return;
  }
  std::vector<uint8_t> d;
  std::string err;
  if (!states::readSlot(saveDir, slot, &d, &err)) {
    logf("load state %d refused: %s", slot, err.c_str());
    menu.notify("State " + std::to_string(slot) + " can't be loaded: " + err + ". Nothing was changed.", 6.0f);
    return;
  }
  // (the moment being left, for Undo Load State)
  std::vector<uint8_t> before = game->coopMidTurn() ? std::vector<uint8_t>() : snes->saveState();
  if (applyState(d)) {
    rewind.clear();  // (the past belongs to another timeline now)
    undoState = std::move(before);
    undoSlot = slot;
    // The state says whether its run counted for achievements.
    AchievementTracker& t = game->achievements();
    if (!cfg.achievements) t.counts = false;
    menu.notify("State " + std::to_string(slot) + " loaded");
    if (cfg.achievements && !t.counts)
      menu.notify("This state was saved while achievements weren't counting: they count again after "
                  "Reset Game", 5.0f);
    audio.flush();
  } else {
    logf("load state %d: the console refused it", slot);
    menu.notify("State " + std::to_string(slot) + " was made by a different version and can't be loaded. Nothing was "
                "changed.",
                6.0f);
  }
}

void App::undoLoadState() {
  if (!snes || undoState.empty()) return;
  pendingSave = -1;
  std::vector<uint8_t> st;
  st.swap(undoState);
  if (applyState(st)) {
    rewind.clear();
    AchievementTracker& t = game->achievements();
    if (!cfg.achievements) t.counts = false;
    menu.notify("Back to where you were before loading state " + std::to_string(undoSlot));
    audio.flush();
  } else {
    menu.notify("That moment could not be brought back");
  }
}

// ---------------------------------------------------------------------------
// The resume point ("Continue Playing")

void App::saveResume(bool now) {
  if (!cfg.autoResume || !snes || !game || inHub || rewinding) return;
  // (not at a moment that would not come back right: the level select's
  // out-of-sight run, the middle of a co-op turn. The last one kept stands.)
  if (game->levelLoading() || game->coopMidTurn()) return;
  const std::vector<uint8_t> st = snes->saveState();
  seal::Info info;
  info.kind = seal::kResume;
  info.romCrc = romInfo.crc;
  info.aux = (uint32_t)playing;
  info.generation = ++resumeGeneration;
  info.time = timeNow();
  info.appVersion = appVersionNumber();
  if (now) {
    writer.flush();  // (one writer of this file at a time)
    std::string err;
    if (!states::writeSealed(resumePath(), seal::kResume, info, st, &err)) {
      logf("resume point not saved: %s", err.c_str());
      return;
    }
  } else {
    // (during play: on another thread, so that no frame waits for the storage)
    writer.submit(resumePath(), seal::make(info, st.data(), st.size()));
  }
  haveResume = true;
  resumeInfo = info;
}

void App::checkResume() {
  writer.flush();
  seal::Info info;
  std::string err;
  haveResume = cfg.autoResume && states::readSealed(resumePath(), seal::kResume, &info, nullptr, &err);
  if (haveResume) {
    resumeInfo = info;
    resumeGeneration = std::max(resumeGeneration, info.generation);
  } else if (fileExists(resumePath()) && cfg.autoResume) {
    logf("resume point unusable: %s", err.c_str());
  }
}

void App::dropResume() {
  writer.flush();
  removeFile(resumePath());
  haveResume = false;
  refreshHub();
}

void App::continueGame() {
  if (!snes) {
    pushPage(pageGameFile());
    return;
  }
  writer.flush();
  seal::Info info;
  std::vector<uint8_t> st;
  std::string err;
  if (!states::readSealed(resumePath(), seal::kResume, &info, &st, &err)) {
    logf("continue: no usable resume point: %s", err.c_str());
    haveResume = false;
    refreshHub();
    menu.notify("There is nothing to continue from (" + err + "). Choose a game to start it.", 6.0f);
    return;
  }
  if (info.romCrc != 0 && info.romCrc != romInfo.crc) {
    menu.notify("That moment was kept with a different game file, so it can't be continued. Choose a game to start it.",
                7.0f);
    return;
  }
  reloadForMsu();
  if (!snes) return;
  GameId g = (GameId)info.aux;
  if (g != GameId::SMB1 && g != GameId::SMBLL && g != GameId::SMB2 && g != GameId::SMB3) g = GameId::Menu;
  pendingSave = -1;
  if (rewinding) endRewind();
  game->cancelWarps();
  if (!applyState(st)) {
    logf("continue: the console refused the resume point");
    menu.notify("That moment was kept by a different version and can't be continued. Choose a game to start it.", 7.0f);
    return;
  }
  playing = g;
  cfg.lastGame = indexOfGame(g);
  inHub = false;
  menu.closeAll();
  rewind.clear();
  undoState.clear();
  audio.flush();
  AchievementTracker& t = game->achievements();
  if (!cfg.achievements) t.counts = false;
  applyConfig();
  resumeTimer = 0;
  logf("continued: %s (kept %s)", SmasGame::gameName(g), whenText(info.time).c_str());
  // (it starts paused: time to get your thumbs in place)
  if (cfg.pauseOnReturn) openMenu();
}

// ---------------------------------------------------------------------------
// Backups

void App::exportBackup() {
  if (picking != Picking::None) return;
  if (snes) saveSram();
  writer.flush();
  std::vector<uint8_t> zip;
  std::string err;
  const BackupPaths paths{exeDir, saveDir};
  cfg.save(configPath);
  if (!buildBackup(paths, battery, kAppVersion, romInfo.crc, &zip, &exportSummary, &err)) {
    logf("backup: nothing made: %s", err.c_str());
    pushPage(pageText("No backup was made", "", "Nothing was written: " + err + "."));
    return;
  }
  makeDir(tmpDir);
  const std::wstring out = pathJoin(tmpDir, L"backup-out.zip");
  if (!roomToWrite(tmpDir, zip.size(), &err) || !writeFileSafely(out, zip.data(), zip.size())) {
    if (err.empty()) err = lastWriteError();
    pushPage(pageText("No backup was made", "", "The backup could not be prepared: " + err + ". Your saves are unchanged."));
    return;
  }
  if (platform::saveDocument(out, backupFileName(), "application/zip")) {
    picking = Picking::Export;
  } else {
    removeFile(out);
    menu.notify("The \"save as\" screen could not be opened on this device", 5.0f);
  }
}

// A backup the player chose (already copied to the app's temporary folder):
// checked completely, described, and restored only after a Yes.
void App::restoreBackupFile(const std::wstring& file, const std::string& shownName) {
  auto zip = std::make_shared<std::vector<uint8_t>>();
  const bool read = readFile(file, *zip, 64u << 20);
  removeFile(file);
  BackupSummary sum;
  std::string err;
  if (!read) err = "it could not be read";
  if (!read || !inspectBackup(*zip, &sum, &err)) {
    logf("restore: backup refused: %s", err.c_str());
    pushPage(pageText("That backup can't be used", shownName,
                      "Nothing was changed. The reason: " + err +
                          ".\n\nBackups are the .zip files made by Save Management > Back Up Saves, named "
                          "\"SMAS-Mobile-backup-...\". If this is one, it may have been damaged while it was copied: "
                          "copy it again from where you keep it."));
    return;
  }
  auto p = std::make_shared<MenuPage>();
  p->title = "Restore this backup?";
  p->subtitle = shownName;
  p->wide = true;
  p->rebuild = [this, zip, sum](MenuPage& pg) {
    pg.items.clear();
    std::string made = "Made " + (sum.created > 0 ? whenText(sum.created) : std::string("at an unknown time"));
    if (!sum.appVersion.empty()) made += " by SMAS Mobile " + sum.appVersion;
    menu.addText(pg, made + ". It holds " + sum.describe() + ".");
    menu.addText(pg, "Restoring replaces those things on this device with the backup's. What is here now is kept "
                     "first, as a safety copy (and the games' saves as a restore point), so this can be undone.");
    pg.items.push_back(MenuItem::separator());
    pg.items.push_back(MenuItem::action("Restore", [this, zip]() {
      BackupSummary done;
      std::wstring safety;
      std::string why;
      const BackupPaths paths{exeDir, saveDir};
      // (everything as it is now on disk first: it is what the safety copy keeps)
      if (snes) saveSram();
      writer.flush();
      cfg.save(configPath);
      const bool ok = restoreBackup(*zip, paths, battery, kAppVersion, romInfo.crc, &done, &safety, &why);
      // (what was restored is what the app now runs with)
      cfg.load(configPath);
      applyConfig();
      platform::preferSixtyHz(cfg.screen60);
      achStore.load(pathJoin(exeDir, L"achievements.ini"));
      if (snes) {
        const BatteryStore::LoadResult r = battery.load(snes->sram, romInfo.crc);
        (void)r;
        snes->sramDirty = false;
      }
      undoState.clear();
      checkResume();
      refreshHub();
      menu.closeAll();
      logf("restore: %s (%s)%s%s", ok ? "done" : "INCOMPLETE", done.describe().c_str(), ok ? "" : ": ", why.c_str());
      const std::string kept = safety.empty() ? std::string()
                                              : "\n\nWhat was here before is kept in the app as a safety copy; "
                                                "the games' saves from before are also under Save Management > "
                                                "Restore Points.";
      if (ok) pushPage(pageText("Backup restored", "", "Restored: " + done.describe() + "." + kept));
      else pushPage(pageText("The backup was only partly restored", "", "Restored: " + done.describe() + ". But " + why + kept));
    }, "Replaces the saves and settings on this device with the backup's, after keeping a safety copy."));
    pg.items.push_back(MenuItem::action("Cancel", [this]() { menu.pop(); }, "Nothing is changed."));
    // (Cancel is the one chosen to begin with)
    for (size_t i = 0; i < pg.items.size(); i++)
      if (pg.items[i].label == "Cancel") pg.sel = (int)i;
  };
  pushPage(p);
}

// ---------------------------------------------------------------------------
// Save Management

std::shared_ptr<MenuPage> App::pageSaves() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Save Management";
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    // (read from storage once, as the page is built: not on every refresh of the screen)
    const long long savedAt = battery.lastSavedTime();
    const std::string savedText = savedAt > 0 ? whenText(savedAt) : std::string(battery.exists() ? "Kept" : "Nothing saved yet");
    const std::string pointsText = std::to_string((int)battery.restorePoints().size());
    int used = 0;
    for (int s = 1; s <= states::kSlots; s++) used += states::slotUsed(saveDir, s) ? 1 : 0;
    const std::string slotsText = std::to_string(used) + " of " + std::to_string(states::kSlots) + " used";
    std::string roomText = "Unknown";
    unsigned long long free = 0;
    if (diskFree(saveDir, &free)) {
      char buf[48];
      if (free >= (1ull << 30)) snprintf(buf, sizeof buf, "%.1f GB", free / 1073741824.0);
      else snprintf(buf, sizeof buf, "%.0f MB", free / 1048576.0);
      roomText = std::string(buf) + (free < (16ull << 20) ? "  (very low: saving may fail)" : "");
    }
    pg.items.push_back(MenuItem::header("Backup"));
    MenuItem last = MenuItem::info("Last backup");
    last.valueText = [this]() { return cfg.lastBackup > 0 ? whenText(cfg.lastBackup) : std::string("Never"); };
    pg.items.push_back(last);
    pg.items.push_back(MenuItem::action("Back Up Saves", [this]() { exportBackup(); },
                                        "Makes one small .zip file holding your game progress, save states, the "
                                        "point you last stopped at, settings and achievements (never the game file), "
                                        "and asks where to keep it. Keep it off this phone."));
    pg.items.push_back(MenuItem::action("Restore Backup", [this]() { pickBackup(); },
                                        "Choose a backup file. It is checked completely and described before "
                                        "anything changes, and what is here now is kept as a safety copy first."));
    pg.items.push_back(MenuItem::header("Game progress"));
    MenuItem saved = MenuItem::info("Last saved");
    saved.valueText = [savedText]() { return savedText; };
    pg.items.push_back(saved);
    MenuItem rp = MenuItem::submenu("Restore Points", [this]() { menu.push(pageRestorePoints()); },
                                    "Earlier copies of the games' saves, one kept from the start of each session. "
                                    "Go back to one if a save was lost or overwritten by mistake.");
    rp.valueText = [pointsText]() { return pointsText; };
    pg.items.push_back(rp);
    pg.items.push_back(MenuItem::header("Save states"));
    MenuItem ss = MenuItem::submenu("Save State Slots", [this]() { menu.push(pageStateSlots()); },
                                    "The nine slots: when each was saved, deleting one, or bringing back what a "
                                    "slot held before it was last overwritten.");
    ss.valueText = [slotsText]() { return slotsText; };
    pg.items.push_back(ss);
    MenuItem cont = MenuItem::info("Continue Playing");
    cont.valueText = [this]() {
      if (!cfg.autoResume) return std::string("Off (Settings > Keep My Place)");
      return haveResume ? "Kept " + whenText(resumeInfo.time) : std::string("Nothing kept yet");
    };
    pg.items.push_back(cont);
    pg.items.push_back(MenuItem::header("Storage"));
    MenuItem room = MenuItem::info("Free storage");
    room.valueText = [roomText]() { return roomText; };
    pg.items.push_back(room);
    pg.items.push_back(MenuItem::submenu("How Saving Works", [this]() { menu.push(pageHelpTopic(1)); },
                                         "The three kinds of save, how they are kept safe, and what deletes them."));
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageRestorePoints() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Restore Points";
  p->subtitle = "Earlier copies of the games' own saves, newest first";
  p->wide = true;
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    const std::vector<BatteryStore::RestorePoint> pts = battery.restorePoints();
    if (pts.empty()) {
      menu.addText(pg, "There are none yet. One is kept at the start of each session in which there is a save, "
                       "and before a backup is restored.");
      pg.items.push_back(MenuItem::action("Back", [this]() { menu.pop(); }));
      return;
    }
    const bool inGame = !inHub && snes;
    for (const BatteryStore::RestorePoint& pt : pts) {
      MenuItem it = MenuItem::action("Saves as they were " + whenText(pt.time), [this, pt, inGame]() {
        if (inGame) {
          menu.notify("Return to the hub first: a game is running");
          return;
        }
        menu.confirm("Go back to these saves?", [this, pt]() {
          uint8_t buf[BatteryStore::kSize];
          if (!battery.readRestorePoint(pt, buf)) {
            menu.notify("That restore point is damaged and can't be used. Nothing was changed.", 6.0f);
            return;
          }
          battery.keepRestorePoint();  // (the saves being replaced: kept too)
          std::string err;
          if (!battery.save(buf, romInfo.crc, &err)) {
            menu.notify("The saves could not be written: " + err + ". Nothing was changed.", 6.0f);
            return;
          }
          if (snes) {
            std::memcpy(snes->sram, buf, sizeof buf);
            snes->sramDirty = false;
          }
          logf("saves: went back to restore point %s", pt.name.c_str());
          menu.notify("The games' saves are as they were " + whenText(pt.time), 5.0f);
          menu.refresh();
        }, "The saves in use now are kept as a restore point too, so you can come back.");
      }, "Replaces the games' own save files (all four games') with this earlier copy. Save states are not changed.");
      pg.items.push_back(it);
    }
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageStateSlots() {
  auto p = std::make_shared<MenuPage>();
  p->title = "Save State Slots";
  p->rebuild = [this](MenuPage& pg) {
    pg.items.clear();
    for (int s = 1; s <= states::kSlots; s++) {
      MenuItem it = MenuItem::submenu("Slot " + std::to_string(s), [this, s]() { menu.push(pageStateSlot(s)); },
                                      "Delete this state, or bring back what the slot held before.");
      it.valueText = [this, s]() { return slotText(s); };
      pg.items.push_back(it);
    }
  };
  return p;
}

std::shared_ptr<MenuPage> App::pageStateSlot(int slot) {
  auto p = std::make_shared<MenuPage>();
  p->title = "Slot " + std::to_string(slot);
  p->rebuild = [this, slot](MenuPage& pg) {
    pg.items.clear();
    const std::wstring cur = statePath(slot), prev = cur + L".prev";
    const bool used = states::slotUsed(saveDir, slot), hasPrev = fileSize(prev) > 0;
    pg.subtitle = used ? "Saved " + slotText(slot) : "Empty";
    if (hasPrev) {
      pg.items.push_back(MenuItem::action("Bring Back the Previous State", [this, cur, prev, slot]() {
        // (the two change places: nothing is lost either way)
        std::vector<uint8_t> a, b;
        std::string why;
        if (!readFile(prev, b, 8u << 20) || !states::valid(b, &why)) {
          menu.notify("The previous state can't be used: " + (why.empty() ? std::string("it could not be read") : why), 6.0f);
          return;
        }
        const bool haveCur = readFile(cur, a, 8u << 20) && states::valid(a);
        if (!writeFileSafely(cur, b.data(), b.size())) {
          menu.notify(std::string("It could not be written: ") + lastWriteError(), 6.0f);
          return;
        }
        if (haveCur) writeFileSafely(prev, a.data(), a.size());
        else removeFile(prev);
        menu.notify("Slot " + std::to_string(slot) + " holds its previous state again");
        menu.refresh();
      }, "What this slot held before it was last saved over (" + whenText(fileTime(prev)) +
             "). The two change places, so this can be undone."));
    }
    if (used) {
      pg.items.push_back(MenuItem::action("Delete This State", [this, cur, prev, slot]() {
        menu.confirm("Delete the state in slot " + std::to_string(slot) + "?", [this, cur, prev, slot]() {
          // (it becomes the slot's previous state, so a mistake can be undone)
          if (renameFile(cur, prev)) menu.notify("Slot " + std::to_string(slot) + " emptied");
          else menu.notify("It could not be deleted");
          menu.refresh();
        }, "It is kept as the slot's previous state until the slot is saved to again.");
      }, "Empties the slot. The state is kept as the slot's previous one until you save here again."));
    }
    if (!used && !hasPrev) menu.addText(pg, "Nothing has been saved in this slot. Save one from a game: MENU > Save State.");
    pg.items.push_back(MenuItem::action("Back", [this]() { menu.pop(); }));
  };
  return p;
}
