// Save backups: the player's progress in one ordinary .zip file, made and
// restored entirely on the device (the app has no network access at all).
//
// A backup holds
//   smas-mobile-backup.txt     what it is, when it was made, by which version
//   saves/smas.srm             the games' own save files: the cartridge's
//                              battery memory as a standard 8 KB .srm, which
//                              other Super Nintendo emulators read too
//   saves/state1.sst .. 9      save states
//   saves/resume.sst           the automatic "continue where I left off" state
//   settings/smas_port.ini     settings, touch layout, controller bindings
//   settings/achievements.ini  achievements earned
// and never the game file itself.
//
// Restoring checks everything first (the archive, each entry's checksum, each
// save's own checksum) and changes nothing unless the backup is sound; then
// keeps the saves it is about to replace as a safety copy before it writes.
// Only the entries named above are ever written, each to its own fixed place:
// nothing in an archive can direct a file anywhere else. (A backup that was
// extracted and zipped again inside a folder is understood as well.)
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "app/savestore.h"

struct BackupPaths {
  std::wstring dataDir;  // the app's private folder (settings live here)
  std::wstring saveDir;  // its "saves" folder
};

struct BackupSummary {
  bool progress = false;      // the games' save files
  int states = 0;             // save states
  std::vector<int> stateSlots;
  bool resume = false;
  bool settings = false;
  bool achievements = false;
  long long created = 0;      // seconds since 1970 (0: not said)
  std::string appVersion;     // "1.0.0"
  uint32_t romCrc = 0;        // the game file in use when it was made (0: none)
  size_t bytes = 0;           // the archive's size
  std::vector<std::string> notes;  // anything left out or ignored, and why
  bool empty() const { return !progress && states == 0 && !resume && !settings && !achievements; }
  std::string describe() const;  // "game progress, 3 save states, settings"
};

constexpr int kBackupFormat = 1;
extern const char kBackupManifestName[];  // "smas-mobile-backup.txt"

// Everything worth keeping, as a zip. False (and why) if there is nothing to back up or it can't be read.
bool buildBackup(const BackupPaths& paths, const BatteryStore& battery, const std::string& appVersion, uint32_t romCrc,
                 std::vector<uint8_t>* zip, BackupSummary* summary, std::string* err);
// What a backup holds, after checking all of it. False (and why) if it is not a sound SMAS Mobile backup.
bool inspectBackup(const std::vector<uint8_t>& zip, BackupSummary* summary, std::string* err);
// Checks it, keeps what is there now as a safety copy (`safetyCopy`: its
// path; empty if there was nothing to keep), then writes the backup's saves
// and settings. False (and why) with nothing changed if the backup is unsound
// or the safety copy can't be made. False, too, if some part could not be
// written afterwards (storage full): `restored` then says which parts were,
// and what was there before is in the safety copy.
bool restoreBackup(const std::vector<uint8_t>& zip, const BackupPaths& paths, BatteryStore& battery,
                   const std::string& appVersion, uint32_t romCrc, BackupSummary* restored, std::wstring* safetyCopy,
                   std::string* err);
// The name a new backup is offered under: "SMAS-Mobile-backup-20261112-1830.zip".
std::string backupFileName();
