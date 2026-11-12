// Save backups: see backup.h.
#include "app/backup.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app/canvas.h"
#include "app/sys.h"
#include "core/crc32.h"
#include "core/fileio.h"
#include "core/zipfile.h"

const char kBackupManifestName[] = "smas-mobile-backup.txt";

namespace {
const char kMagicLine[] = "SMAS Mobile backup";
const char kSrm[] = "saves/smas.srm";
const char kResume[] = "saves/resume.sst";
const char kSettings[] = "settings/smas_port.ini";
const char kAchievements[] = "settings/achievements.ini";
constexpr int kSafetyCopies = 5;

std::string stateName(int slot) { return "saves/state" + std::to_string(slot) + ".sst"; }

// "saves/state7.sst": 7 (0 if it isn't one).
int stateSlot(const std::string& name) {
  if (name.size() != 16 || name.compare(0, 11, "saves/state") != 0 || name.compare(12, 4, ".sst") != 0) return 0;
  const int s = name[11] - '0';
  return s >= 1 && s <= states::kSlots ? s : 0;
}

// Settings files are short text: anything else isn't one.
bool plainText(const std::vector<uint8_t>& d) {
  if (d.empty() || d.size() > (256u << 10)) return false;
  for (uint8_t c : d)
    if (c == 0) return false;
  return true;
}

std::string hex8(uint32_t v) {
  char buf[16];
  snprintf(buf, sizeof buf, "%08X", v);
  return buf;
}

std::string manifest(const std::vector<ZipEntry>& entries, const std::string& appVersion, uint32_t romCrc, long long now) {
  std::string m;
  m += kMagicLine;
  m += "\nformat: " + std::to_string(kBackupFormat);
  m += "\napp_version: " + appVersion;
  m += "\ncreated: " + timeTextAt(now, "%Y-%m-%d %H:%M:%S");
  m += "\ncreated_unix: " + std::to_string(now);
  m += "\ngame_file_crc32: " + (romCrc ? hex8(romCrc) : std::string("none"));
  m += "\n\nThis archive holds saved progress from SMAS Mobile (publisher: LIN4CRE). It contains no game\n"
       "data. To use it: SMAS Mobile > Save Management > Restore Backup, and choose this file.\n\n"
       "saves/smas.srm is the cartridge's battery memory as a standard 8 KB .srm file.\n\ncontents:\n";
  for (const ZipEntry& e : entries) {
    char line[320];
    snprintf(line, sizeof line, "  %-28s %8zu bytes  crc32 %s\n", e.name.c_str(), e.data.size(),
             hex8(crc32(e.data.data(), e.data.size())).c_str());
    m += line;
  }
  return m;
}

// "key: value" from the manifest ("" if it isn't there).
std::string manifestValue(const std::string& m, const char* key) {
  const std::string k = std::string("\n") + key + ": ";
  size_t at = m.find(k);
  if (at == std::string::npos) return "";
  at += k.size();
  size_t end = m.find('\n', at);
  std::string v = m.substr(at, end == std::string::npos ? std::string::npos : end - at);
  while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
  return v;
}

struct Parsed {
  const ZipEntry* srm = nullptr;
  const ZipEntry* state[states::kSlots + 1] = {};
  const ZipEntry* resume = nullptr;
  const ZipEntry* settings = nullptr;
  const ZipEntry* achievements = nullptr;
};

// Every entry of a backup checked for what it claims to be. False (and why)
// if the archive is not a backup, or something in it is damaged.
bool parse(const std::vector<ZipEntry>& entries, Parsed* out, BackupSummary* s, std::string* err) {
  // The description file says it is a backup. It is at the top of the archive,
  // or (a backup that was extracted and zipped again as a folder) one folder down.
  const ZipEntry* man = nullptr;
  std::string prefix;
  const std::string manName = kBackupManifestName;
  for (const ZipEntry& e : entries) {
    const bool top = e.name == manName;
    const bool nested = e.name.size() > manName.size() + 1 &&
                        e.name.compare(e.name.size() - manName.size() - 1, std::string::npos, "/" + manName) == 0;
    if (!top && !nested) continue;
    if (man) {
      *err = "it has more than one description file (smas-mobile-backup.txt): not a single backup";
      return false;
    }
    man = &e;
    prefix = e.name.substr(0, e.name.size() - manName.size());
  }
  if (!man) {
    *err = "it is a zip archive, but not a SMAS Mobile backup (smas-mobile-backup.txt is missing)";
    return false;
  }
  const std::string m(man->data.begin(), man->data.end());
  if (m.compare(0, sizeof(kMagicLine) - 1, kMagicLine) != 0) {
    *err = "its description file is not a SMAS Mobile backup's";
    return false;
  }
  const int format = atoi(manifestValue(m, "format").c_str());
  if (format < 1) {
    *err = "its description file does not say which backup format it is";
    return false;
  }
  if (format > kBackupFormat) {
    *err = "it was made by a newer version of SMAS Mobile: update the app, then restore it";
    return false;
  }
  s->appVersion = manifestValue(m, "app_version");
  s->created = atoll(manifestValue(m, "created_unix").c_str());
  s->romCrc = (uint32_t)strtoul(manifestValue(m, "game_file_crc32").c_str(), nullptr, 16);
  // (the same entry twice: which one is meant can't be told, so neither is used)
  auto twice = [&](const void* have, const std::string& name) {
    if (have) *err = "it has two entries named \"" + name + "\": damaged, or not a single backup";
    return have != nullptr;
  };
  for (const ZipEntry& e : entries) {
    std::string why;
    if (&e == man) continue;
    if (e.name.compare(0, prefix.size(), prefix) != 0) {
      s->notes.push_back("ignored \"" + e.name + "\" (not part of a SMAS Mobile backup)");
      continue;
    }
    const std::string name = e.name.substr(prefix.size());
    if (name == kSrm) {
      if (twice(out->srm, name)) return false;
      if (e.data.size() != BatteryStore::kSize) {
        *err = "its game progress (saves/smas.srm) is " + std::to_string(e.data.size()) + " bytes, not 8192: damaged";
        return false;
      }
      out->srm = &e;
      s->progress = true;
    } else if (int slot = stateSlot(name)) {
      if (twice(out->state[slot], name)) return false;
      if (!states::valid(e.data, &why)) {
        *err = "its save state " + std::to_string(slot) + " can't be used: " + why;
        return false;
      }
      out->state[slot] = &e;
      s->states++;
      s->stateSlots.push_back(slot);
    } else if (name == kResume) {
      if (twice(out->resume, name)) return false;
      std::vector<uint8_t> state;
      seal::Error se = seal::open(e.data, seal::kResume, nullptr, &state);
      if (se != seal::Error::None || !states::valid(state, &why)) {
        *err = std::string("its resume state can't be used: ") + (se != seal::Error::None ? seal::errorText(se) : why.c_str());
        return false;
      }
      out->resume = &e;
      s->resume = true;
    } else if (name == kSettings) {
      if (twice(out->settings, name)) return false;
      if (!plainText(e.data)) {
        *err = "its settings file is not text: damaged";
        return false;
      }
      out->settings = &e;
      s->settings = true;
    } else if (name == kAchievements) {
      if (twice(out->achievements, name)) return false;
      if (!plainText(e.data)) {
        *err = "its achievements file is not text: damaged";
        return false;
      }
      out->achievements = &e;
      s->achievements = true;
    } else {
      s->notes.push_back("ignored \"" + e.name + "\" (not part of a SMAS Mobile backup)");
    }
  }
  std::sort(s->stateSlots.begin(), s->stateSlots.end());
  if (s->empty()) {
    *err = "it is a SMAS Mobile backup, but there are no saves or settings in it";
    return false;
  }
  return true;
}

// What there is to back up on this device, as archive entries.
void gather(const BackupPaths& paths, const BatteryStore& battery, std::vector<ZipEntry>* entries, BackupSummary* s) {
  auto add = [&](const std::string& name, std::vector<uint8_t> data, long long time) {
    ZipEntry e;
    e.name = name;
    e.data = std::move(data);
    e.time = time;
    entries->push_back(std::move(e));
  };
  std::vector<uint8_t> d;
  if (battery.exportPlain(&d)) {
    add(kSrm, d, battery.lastSavedTime());
    s->progress = true;
  }
  for (int slot = 1; slot <= states::kSlots; slot++) {
    if (!states::slotUsed(paths.saveDir, slot)) continue;
    std::string why;
    if (!states::readSlot(paths.saveDir, slot, &d, &why)) {
      s->notes.push_back("save state " + std::to_string(slot) + " was left out: " + why);
      continue;
    }
    add(stateName(slot), d, fileTime(states::slotPath(paths.saveDir, slot)));
    s->states++;
    s->stateSlots.push_back(slot);
  }
  const std::wstring resume = pathJoin(paths.saveDir, L"resume.sst");
  if (fileExists(resume)) {
    std::string why;
    if (states::readSealed(resume, seal::kResume, nullptr, nullptr, &why) && readFile(resume, d, 8u << 20)) {
      add(kResume, d, fileTime(resume));
      s->resume = true;
    } else {
      s->notes.push_back("the resume state was left out: " + why);
    }
  }
  const std::wstring ini = pathJoin(paths.dataDir, L"smas_port.ini");
  if (readFile(ini, d, 256u << 10) && plainText(d)) {
    add(kSettings, d, fileTime(ini));
    s->settings = true;
  }
  const std::wstring ach = pathJoin(paths.dataDir, L"achievements.ini");
  if (readFile(ach, d, 256u << 10) && plainText(d)) {
    add(kAchievements, d, fileTime(ach));
    s->achievements = true;
  }
}

// The entries as a backup archive, read back as a restore would read it: a
// backup that would not restore is never handed out.
bool pack(std::vector<ZipEntry> entries, const std::string& appVersion, uint32_t romCrc, long long now,
          std::vector<uint8_t>* zip, std::string* err) {
  const std::string m = manifest(entries, appVersion, romCrc, now);
  ZipEntry man;
  man.name = kBackupManifestName;
  man.data.assign(m.begin(), m.end());
  man.time = now;
  entries.insert(entries.begin(), std::move(man));
  *zip = zipWrite(entries);
  BackupSummary check;
  std::string why;
  if (!inspectBackup(*zip, &check, &why)) {
    if (err) *err = "the backup did not pass its own check (" + why + ")";
    return false;
  }
  return true;
}
}  // namespace

std::string BackupSummary::describe() const {
  std::vector<std::string> parts;
  if (progress) parts.push_back("game progress");
  if (states == 1) parts.push_back("1 save state");
  else if (states > 1) parts.push_back(std::to_string(states) + " save states");
  if (resume) parts.push_back("the point you last stopped at");
  if (settings) parts.push_back("settings and controls");
  if (achievements) parts.push_back("achievements");
  std::string s;
  for (size_t i = 0; i < parts.size(); i++) s += (i ? (i + 1 == parts.size() ? " and " : ", ") : "") + parts[i];
  return s.empty() ? "nothing" : s;
}

std::string backupFileName() { return "SMAS-Mobile-backup-" + timeText("%Y%m%d-%H%M") + ".zip"; }

bool buildBackup(const BackupPaths& paths, const BatteryStore& battery, const std::string& appVersion, uint32_t romCrc,
                 std::vector<uint8_t>* zip, BackupSummary* summary, std::string* err) {
  BackupSummary s;
  std::vector<ZipEntry> entries;
  gather(paths, battery, &entries, &s);
  if (s.empty()) {
    if (err) *err = "there is nothing to back up yet";
    return false;
  }
  const long long now = timeNow();
  if (!pack(std::move(entries), appVersion, romCrc, now, zip, err)) return false;
  s.created = now;
  s.appVersion = appVersion;
  s.romCrc = romCrc;
  s.bytes = zip->size();
  if (summary) *summary = s;
  return true;
}

bool inspectBackup(const std::vector<uint8_t>& zip, BackupSummary* summary, std::string* err) {
  std::string why;
  std::vector<ZipEntry> entries;
  BackupSummary s;
  Parsed parsed;
  if (!zipRead(zip, &entries, &why) || !parse(entries, &parsed, &s, &why)) {
    if (err) *err = why;
    return false;
  }
  s.bytes = zip.size();
  if (summary) *summary = s;
  return true;
}

bool restoreBackup(const std::vector<uint8_t>& zip, const BackupPaths& paths, BatteryStore& battery,
                   const std::string& appVersion, uint32_t romCrc, BackupSummary* restored, std::wstring* safetyCopy,
                   std::string* err) {
  std::string why;
  auto fail = [&](const std::string& w) {
    if (err) *err = w;
    return false;
  };
  // 1. All of it checked; nothing has been touched yet.
  std::vector<ZipEntry> entries;
  BackupSummary s;
  Parsed parsed;
  if (!zipRead(zip, &entries, &why) || !parse(entries, &parsed, &s, &why)) return fail(why);
  size_t need = 0;
  for (const ZipEntry& e : entries) need += e.data.size();
  if (!roomToWrite(paths.saveDir, need + zip.size(), &why)) return fail(why);

  // 2. What is there now, kept: as a backup of its own (restorable the same
  //    way), and the games' save files as a restore point as well. If there
  //    is something here and it can't be kept, the restore does not go ahead.
  if (safetyCopy) safetyCopy->clear();
  {
    std::vector<ZipEntry> here;
    BackupSummary hereSummary;
    gather(paths, battery, &here, &hereSummary);
    if (!hereSummary.empty()) {
      const char* kNotKept = "the saves on this device could not be kept safe first (";
      std::vector<uint8_t> current;
      if (!pack(std::move(here), appVersion, romCrc, timeNow(), &current, &why))
        return fail(kNotKept + why + "), so nothing was changed");
      const std::wstring dir = pathJoin(paths.saveDir, L"safety");
      makeDir(dir);
      const std::wstring stamp = widenUtf8(timeText("%Y%m%d-%H%M%S"));
      std::wstring name;
      for (int k = 0; k < 100; k++) {
        name = L"before-restore-" + stamp + (k ? L"-" + std::to_wstring(k) : L"") + L".zip";
        if (!fileExists(pathJoin(dir, name))) break;
      }
      const std::wstring path = pathJoin(dir, name);
      if (!writeFileSafely(path, current.data(), current.size()))
        return fail(std::string(kNotKept) + lastWriteError() + "), so nothing was changed");
      if (safetyCopy) *safetyCopy = path;
      // (the newest few are kept: each is a copy of saves that were replaced on purpose)
      std::vector<std::wstring> names;
      if (listFiles(dir, names)) {
        std::vector<std::wstring> copies;
        for (const std::wstring& n : names)
          if (n.compare(0, 15, L"before-restore-") == 0 && n != name) copies.push_back(n);
        // oldest first: by when they were written, then by name
        std::sort(copies.begin(), copies.end(), [&](const std::wstring& a, const std::wstring& b) {
          const long long ta = fileTime(pathJoin(dir, a)), tb = fileTime(pathJoin(dir, b));
          return ta != tb ? ta < tb : a < b;
        });
        for (size_t i = 0; i + (kSafetyCopies - 1) < copies.size(); i++) removeFile(pathJoin(dir, copies[i]));
      }
    }
  }
  battery.keepRestorePoint();

  // 3. The backup's saves and settings, each written whole or not at all.
  BackupSummary done;
  std::vector<std::string> problems;
  if (parsed.srm) {
    if (battery.save(parsed.srm->data.data(), romCrc, &why)) done.progress = true;
    else problems.push_back("game progress: " + why);
  }
  for (int slot = 1; slot <= states::kSlots; slot++) {
    if (!parsed.state[slot]) continue;
    if (states::writeSlot(paths.saveDir, slot, parsed.state[slot]->data, &why)) {
      done.states++;
      done.stateSlots.push_back(slot);
    } else {
      problems.push_back("save state " + std::to_string(slot) + ": " + why);
    }
  }
  auto put = [&](const ZipEntry* e, const std::wstring& path, bool* flag, const char* what) {
    if (!e) return;
    if (writeFileSafely(path, e->data.data(), e->data.size())) *flag = true;
    else problems.push_back(std::string(what) + ": " + lastWriteError());
  };
  put(parsed.resume, pathJoin(paths.saveDir, L"resume.sst"), &done.resume, "the resume state");
  put(parsed.settings, pathJoin(paths.dataDir, L"smas_port.ini"), &done.settings, "settings");
  put(parsed.achievements, pathJoin(paths.dataDir, L"achievements.ini"), &done.achievements, "achievements");
  done.created = s.created;
  done.appVersion = s.appVersion;
  done.romCrc = s.romCrc;
  done.bytes = zip.size();
  done.notes = s.notes;
  if (restored) *restored = done;
  if (!problems.empty()) {
    std::string all = "some of it could not be written (";
    for (size_t i = 0; i < problems.size(); i++) all += (i ? "; " : "") + problems[i];
    return fail(all + "). What was there before is in the safety copy.");
  }
  return true;
}
