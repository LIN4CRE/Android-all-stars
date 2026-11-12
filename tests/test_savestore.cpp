// Saved progress on disk (app/savestore.h): checksums, the copies kept of
// earlier saves, recovery from damaged and half-written files, failed writes,
// restore points, save-state slots, and the background writer.
#include <cstring>
#include "app/savestore.h"
#include "check.h"
#include "fixtures.h"

using fx::Faults;

namespace {
const uint32_t kVer = 10000;
std::wstring sav(const std::wstring& dir, const wchar_t* suffix = L"") {
  return pathJoin(dir, std::wstring(L"smas.sav") + suffix);
}
// A store on a folder with `n` saves made in it, the contents sram(1) .. sram(n).
void makeSaves(const std::wstring& dir, int n) {
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize, 0);
  s.load(mem.data(), 0);
  for (int i = 1; i <= n; i++) {
    std::string err;
    CHECK_MSG(s.save(fx::sram(i).data(), 0x11111111, &err), err);
  }
}
BatteryStore::LoadResult loadInto(const std::wstring& dir, std::vector<uint8_t>* mem) {
  BatteryStore s;
  s.setDir(dir, kVer);
  mem->assign(BatteryStore::kSize, 0xEE);
  return s.load(mem->data(), 0);
}
const std::vector<uint8_t> kUntouched(BatteryStore::kSize, 0xEE);
}  // namespace

// ---- the seal -------------------------------------------------------------

TEST(seal_round_trip) {
  seal::Info in;
  in.kind = seal::kBattery;
  in.romCrc = 0x925637c7;
  in.aux = 3;
  in.generation = 41;
  in.time = 1794500000LL;
  in.appVersion = 10203;
  const auto payload = fx::pattern(1000, 5);
  const auto file = seal::make(in, payload.data(), payload.size());
  CHECK_EQ(file.size(), seal::kHeaderSize + payload.size());
  seal::Info out;
  std::vector<uint8_t> got;
  CHECK(seal::open(file, seal::kBattery, &out, &got) == seal::Error::None);
  CHECK(got == payload);
  CHECK_EQ(out.kind, in.kind);
  CHECK_EQ(out.romCrc, in.romCrc);
  CHECK_EQ(out.aux, in.aux);
  CHECK_EQ(out.generation, in.generation);
  CHECK_EQ(out.time, in.time);
  CHECK_EQ(out.appVersion, in.appVersion);
  // (the header alone can be asked for)
  CHECK(seal::open(file, seal::kBattery, nullptr, nullptr) == seal::Error::None);
}

TEST(seal_detects_every_flipped_bit) {
  seal::Info in;
  in.kind = seal::kResume;
  in.generation = 7;
  in.time = 1794500000LL;
  const auto payload = fx::pattern(300, 9);
  const auto file = seal::make(in, payload.data(), payload.size());
  int undetected = 0;
  for (size_t i = 0; i < file.size(); i++) {
    for (int bit = 0; bit < 8; bit++) {
      auto bad = file;
      bad[i] ^= (uint8_t)(1 << bit);
      std::vector<uint8_t> got;
      if (seal::open(bad, seal::kResume, nullptr, &got) == seal::Error::None) undetected++;
    }
  }
  CHECK_EQ(undetected, 0);
}

TEST(seal_detects_every_truncation_and_extension) {
  seal::Info in;
  in.kind = seal::kBattery;
  const auto payload = fx::pattern(200, 2);
  const auto file = seal::make(in, payload.data(), payload.size());
  for (size_t n = 0; n < file.size(); n++) {
    std::vector<uint8_t> cut(file.begin(), file.begin() + n);
    CHECK(seal::open(cut, seal::kBattery, nullptr, nullptr) != seal::Error::None);
  }
  auto longer = file;
  longer.push_back(0);
  CHECK(seal::open(longer, seal::kBattery, nullptr, nullptr) == seal::Error::BadLength);
}

TEST(seal_says_what_is_wrong) {
  seal::Info in;
  in.kind = seal::kBattery;
  const auto payload = fx::pattern(64, 2);
  const auto file = seal::make(in, payload.data(), payload.size());
  CHECK(seal::open(file, seal::kResume, nullptr, nullptr) == seal::Error::WrongKind);
  CHECK(seal::open({}, seal::kBattery, nullptr, nullptr) == seal::Error::TooShort);
  auto other = file;
  other[0] = 'X';
  CHECK(seal::open(other, seal::kBattery, nullptr, nullptr) == seal::Error::BadMagic);
  auto newer = file;
  newer[7] = 2;  // (a later format: not guessed at)
  CHECK(seal::open(newer, seal::kBattery, nullptr, nullptr) == seal::Error::BadVersion);
  auto body = file;
  body[seal::kHeaderSize + 10] ^= 0x40;
  CHECK(seal::open(body, seal::kBattery, nullptr, nullptr) == seal::Error::BadPayload);
  auto head = file;
  head[30] ^= 1;
  CHECK(seal::open(head, seal::kBattery, nullptr, nullptr) == seal::Error::BadHeader);
  // (every reason has words for the log)
  for (int e = 0; e <= (int)seal::Error::WrongKind; e++) CHECK(strlen(seal::errorText((seal::Error)e)) > 3);
}

TEST(seal_read_from_disk) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"x.sav");
  CHECK(seal::read(p, seal::kBattery, nullptr, nullptr) == seal::Error::Missing);
  seal::Info in;
  in.kind = seal::kBattery;
  const auto payload = fx::pattern(500, 1);
  fx::spit(p, seal::make(in, payload.data(), payload.size()));
  std::vector<uint8_t> got;
  CHECK(seal::read(p, seal::kBattery, nullptr, &got) == seal::Error::None);
  CHECK(got == payload);
  CHECK(seal::read(p, seal::kBattery, nullptr, &got, 100) == seal::Error::BadLength);  // (longer than one can be)
}

// ---- the games' save files ------------------------------------------------

TEST(battery_fresh_folder) {
  const std::wstring dir = pathJoin(check::tempDir(), L"saves");
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Fresh);
  CHECK(mem == kUntouched);
  CHECK_EQ(r.setAside, 0);
  BatteryStore s;
  s.setDir(dir, kVer);
  CHECK(!s.exists());
  std::vector<uint8_t> plain;
  CHECK(!s.exportPlain(&plain));
  CHECK(!s.keepRestorePoint());  // (nothing to keep yet)
}

TEST(battery_save_and_load) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 1);
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Current);
  CHECK(mem == fx::sram(1));
  CHECK(r.time > 1700000000LL);
  CHECK_EQ(r.source, std::string("smas.sav"));
  // (what is on disk is sealed, not the bare memory)
  CHECK_EQ(fileSize(sav(dir)), (long long)(seal::kHeaderSize + BatteryStore::kSize));
  CHECK(!fileExists(sav(dir, L".new")));
}

TEST(battery_keeps_the_two_saves_before) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 4);
  auto payloadOf = [&](const wchar_t* suffix, uint32_t* gen) {
    seal::Info i;
    std::vector<uint8_t> p;
    CHECK(seal::read(sav(dir, suffix), seal::kBattery, &i, &p) == seal::Error::None);
    *gen = i.generation;
    return p;
  };
  uint32_t g0 = 0, g1 = 0, g2 = 0;
  CHECK(payloadOf(L"", &g0) == fx::sram(4));
  CHECK(payloadOf(L".1", &g1) == fx::sram(3));
  CHECK(payloadOf(L".2", &g2) == fx::sram(2));
  CHECK(g0 > g1 && g1 > g2);
}

TEST(battery_unchanged_memory_is_not_rewritten) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize, 0);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  Faults faults;
  CHECK(s.save(fx::sram(1).data(), 0));
  CHECK_EQ(faults.f.writes, 0);  // (the storage is not worn by saving the same thing every five seconds)
  CHECK(!fileExists(sav(dir, L".1")));
  // ... and neither after the app starts again with the same save
  BatteryStore t;
  t.setDir(dir, kVer);
  t.load(mem.data(), 0);
  CHECK(t.save(mem.data(), 0));
  CHECK_EQ(faults.f.writes, 0);
}

TEST(battery_generation_continues_across_sessions) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 2);
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(9).data(), 0));
  seal::Info a, b;
  CHECK(seal::read(sav(dir), seal::kBattery, &a, nullptr) == seal::Error::None);
  CHECK(seal::read(sav(dir, L".1"), seal::kBattery, &b, nullptr) == seal::Error::None);
  CHECK_EQ(a.generation, b.generation + 1);
  CHECK_EQ(a.appVersion, kVer);
}

TEST(battery_damaged_save_falls_back_to_the_one_before) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  const auto damagedBytes = [&] {
    fx::flipBit(sav(dir), seal::kHeaderSize + 1234, 3);
    return fx::slurp(sav(dir));
  }();
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Recovered);
  CHECK(mem == fx::sram(2));  // (the save before: whole, never the damaged one)
  CHECK_EQ(r.source, std::string("smas.sav.1"));
  CHECK_EQ(r.setAside, 1);
  CHECK(r.problem.find("damaged") != std::string::npos);
  // The damaged file is kept under another name, byte for byte: never deleted.
  bool found = false;
  for (const std::wstring& n : fx::names(dir))
    if (n.find(L"smas.sav.damaged-") == 0) found = fx::slurp(pathJoin(dir, n)) == damagedBytes;
  CHECK(found);
  CHECK(!fileExists(sav(dir)));
}

TEST(battery_recovery_then_saving_goes_on) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  fx::truncateTo(sav(dir), 100);
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  CHECK(s.load(mem.data(), 0).origin == BatteryStore::Origin::Recovered);
  CHECK(mem == fx::sram(2));
  // The recovered progress is saved again as the newest (it is not "already on disk" as smas.sav).
  std::string err;
  CHECK_MSG(s.save(mem.data(), 0, &err), err);
  CHECK(s.save(fx::sram(7).data(), 0));
  std::vector<uint8_t> again;
  const auto r = loadInto(dir, &again);
  CHECK(r.origin == BatteryStore::Origin::Current);
  CHECK(again == fx::sram(7));
}

TEST(battery_every_kind_of_damage_to_the_newest_is_survived) {
  // Each case: three saves, the newest damaged somehow, and the one before must come back.
  struct Case {
    const char* what;
    void (*damage)(const std::wstring&);
  } cases[] = {
      {"empty file", [](const std::wstring& p) { fx::truncateTo(p, 0); }},
      {"header only", [](const std::wstring& p) { fx::truncateTo(p, seal::kHeaderSize); }},
      {"cut in the middle", [](const std::wstring& p) { fx::truncateTo(p, 4000); }},
      {"one byte short", [](const std::wstring& p) { fx::truncateTo(p, seal::kHeaderSize + BatteryStore::kSize - 1); }},
      {"bit flipped in the header", [](const std::wstring& p) { fx::flipBit(p, 20, 0); }},
      {"bit flipped in the magic", [](const std::wstring& p) { fx::flipBit(p, 2, 5); }},
      {"bit flipped at the very end", [](const std::wstring& p) { fx::flipBit(p, seal::kHeaderSize + BatteryStore::kSize - 1, 7); }},
      {"zeroed", [](const std::wstring& p) { fx::spit(p, std::vector<uint8_t>(seal::kHeaderSize + BatteryStore::kSize, 0)); }},
      {"garbage", [](const std::wstring& p) { fx::spit(p, fx::pattern(seal::kHeaderSize + BatteryStore::kSize, 77)); }},
      {"a plain .srm in its place", [](const std::wstring& p) { fx::spit(p, fx::sram(50)); }},
  };
  for (const Case& c : cases) {
    const std::wstring dir = check::tempDir();
    makeSaves(dir, 3);
    c.damage(sav(dir));
    std::vector<uint8_t> mem;
    const auto r = loadInto(dir, &mem);
    CHECK_MSG(r.origin == BatteryStore::Origin::Recovered, c.what);
    CHECK_MSG(mem == fx::sram(2), c.what);
    CHECK_MSG(fx::hasFileStarting(dir, L"smas.sav.damaged-"), c.what);
  }
}

TEST(battery_two_damaged_copies_leave_the_third) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  fx::flipBit(sav(dir), 60);
  fx::truncateTo(sav(dir, L".1"), 10);
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Recovered);
  CHECK(mem == fx::sram(1));
  CHECK_EQ(r.setAside, 2);
}

TEST(battery_all_copies_damaged_uses_a_restore_point) {
  const std::wstring dir = check::tempDir();
  {
    BatteryStore s;
    s.setDir(dir, kVer);
    std::vector<uint8_t> mem(BatteryStore::kSize);
    s.load(mem.data(), 0);
    REQUIRE(s.save(fx::sram(1).data(), 0));
    REQUIRE(s.keepRestorePoint());
    REQUIRE(s.save(fx::sram(2).data(), 0));
    REQUIRE(s.save(fx::sram(3).data(), 0));
  }
  for (const wchar_t* n : {L"", L".1", L".2"})
    if (fileExists(sav(dir, n))) fx::flipBit(sav(dir, n), 100);
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Recovered);
  CHECK(mem == fx::sram(1));
  CHECK(r.source.find("history/") == 0);
}

TEST(battery_nothing_readable_starts_fresh_and_keeps_the_evidence) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  for (const wchar_t* n : {L"", L".1", L".2"}) fx::flipBit(sav(dir, n), 100);
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Damaged);
  CHECK(mem == kUntouched);  // (nothing damaged is ever given to the game)
  CHECK_EQ(r.setAside, 3);
  int kept = 0;
  for (const std::wstring& n : fx::names(dir)) kept += n.find(L".damaged-") != std::wstring::npos;
  CHECK_EQ(kept, 3);
  // A new save can be started beside them.
  BatteryStore s;
  s.setDir(dir, kVer);
  s.load(mem.data(), 0);
  CHECK(s.save(fx::sram(5).data(), 0));
  CHECK(loadInto(dir, &mem).origin == BatteryStore::Origin::Current);
  CHECK(mem == fx::sram(5));
}

TEST(battery_interrupted_save_is_completed) {
  // The app was stopped after the new save was written and checked, before it took the last one's place.
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 2);
  seal::Info cur;
  REQUIRE(seal::read(sav(dir), seal::kBattery, &cur, nullptr) == seal::Error::None);
  seal::Info next = cur;
  next.generation = cur.generation + 1;
  const auto newest = fx::sram(33);
  fx::spit(sav(dir, L".new"), seal::make(next, newest.data(), newest.size()));
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Current);
  CHECK(mem == newest);
  CHECK(!fileExists(sav(dir, L".new")));
  // (and the others moved down as the save itself would have moved them)
  std::vector<uint8_t> p;
  CHECK(seal::read(sav(dir), seal::kBattery, nullptr, &p) == seal::Error::None);
  CHECK(p == newest);
  CHECK(seal::read(sav(dir, L".1"), seal::kBattery, nullptr, &p) == seal::Error::None);
  CHECK(p == fx::sram(2));
  CHECK(seal::read(sav(dir, L".2"), seal::kBattery, nullptr, &p) == seal::Error::None);
  CHECK(p == fx::sram(1));
}

TEST(battery_interrupted_save_at_every_step_of_the_rotation) {
  // save() moves .1 to .2, the save to .1, the new one into place. Stopped
  // after any of those steps, the newest whole save is what comes back.
  for (int step = 0; step <= 3; step++) {
    const std::wstring dir = check::tempDir();
    makeSaves(dir, 3);  // smas.sav = 3, .1 = 2, .2 = 1
    seal::Info cur;
    REQUIRE(seal::read(sav(dir), seal::kBattery, &cur, nullptr) == seal::Error::None);
    seal::Info next = cur;
    next.generation = cur.generation + 1;
    const auto newest = fx::sram(44);
    fx::spit(sav(dir, L".new"), seal::make(next, newest.data(), newest.size()));
    if (step >= 1) CHECK(renameFile(sav(dir, L".1"), sav(dir, L".2")));
    if (step >= 2) CHECK(renameFile(sav(dir), sav(dir, L".1")));
    if (step >= 3) CHECK(renameFile(sav(dir, L".new"), sav(dir)));
    std::vector<uint8_t> mem;
    const auto r = loadInto(dir, &mem);
    CHECK_MSG(r.origin == BatteryStore::Origin::Current, "step " + std::to_string(step));
    CHECK_MSG(mem == newest, "step " + std::to_string(step));
    CHECK_EQ(r.setAside, 0);
    // The save before the newest is still there as well.
    std::vector<uint8_t> p;
    bool haveBefore = false;
    for (const wchar_t* n : {L".1", L".2"})
      if (seal::read(sav(dir, n), seal::kBattery, nullptr, &p) == seal::Error::None && p == fx::sram(3)) haveBefore = true;
    CHECK_MSG(haveBefore, "step " + std::to_string(step));
  }
}

TEST(battery_half_written_new_file_is_ignored) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 2);
  fx::spit(sav(dir, L".new"), fx::pattern(3000, 4));  // (never finished)
  std::vector<uint8_t> mem;
  const auto r = loadInto(dir, &mem);
  CHECK(r.origin == BatteryStore::Origin::Current);
  CHECK(mem == fx::sram(2));
  CHECK(!fileExists(sav(dir, L".new")));
}

TEST(battery_an_older_new_file_does_not_replace_a_newer_save) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  seal::Info old;
  REQUIRE(seal::read(sav(dir, L".2"), seal::kBattery, &old, nullptr) == seal::Error::None);
  const auto stale = fx::sram(99);
  fx::spit(sav(dir, L".new"), seal::make(old, stale.data(), stale.size()));
  std::vector<uint8_t> mem;
  CHECK(loadInto(dir, &mem).origin == BatteryStore::Origin::Current);
  CHECK(mem == fx::sram(3));
}

TEST(battery_storage_full_changes_nothing) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  REQUIRE(s.save(fx::sram(2).data(), 0));
  const auto before = fx::snapshot(dir);
  for (long long room : {0LL, 47LL, 48LL, 5000LL, 8239LL}) {
    Faults faults;
    faults.f.failWriteAfter = room;
    std::string err;
    CHECK(!s.save(fx::sram(3).data(), 0, &err));
    CHECK_MSG(err.find("No space") != std::string::npos, err);
    CHECK(fx::snapshot(dir) == before);  // (every file exactly as it was; nothing half-written beside them)
  }
  // The failure is not remembered as a success: with room again the save is written.
  std::string err;
  CHECK_MSG(s.save(fx::sram(3).data(), 0, &err), err);
  std::vector<uint8_t> got;
  CHECK(loadInto(dir, &got).origin == BatteryStore::Origin::Current);
  CHECK(got == fx::sram(3));
}

TEST(battery_failed_flush_or_rename_changes_nothing) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  const auto before = fx::snapshot(dir);
  {
    Faults faults;
    faults.f.failFsync = true;
    std::string err;
    CHECK(!s.save(fx::sram(2).data(), 0, &err));
    CHECK(!err.empty());
  }
  CHECK(fx::snapshot(dir) == before);
  {
    Faults faults;
    faults.f.failRename = true;
    CHECK(!s.save(fx::sram(2).data(), 0));
  }
  CHECK(fx::snapshot(dir) == before);
  std::vector<uint8_t> got;
  CHECK(loadInto(dir, &got).origin == BatteryStore::Origin::Current);
  CHECK(got == fx::sram(1));
}

TEST(battery_imports_a_plain_srm) {
  const std::wstring dir = check::tempDir();
  const auto plain = fx::sram(8);
  fx::spit(pathJoin(dir, L"smas.srm"), plain);
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize, 0);
  const auto r = s.load(mem.data(), 0);
  CHECK(r.origin == BatteryStore::Origin::Imported);
  CHECK(mem == plain);
  // The first save seals it (even though the memory hasn't changed), and the .srm is left as it was.
  CHECK(s.save(mem.data(), 0));
  CHECK(fileExists(sav(dir)));
  CHECK(fx::slurp(pathJoin(dir, L"smas.srm")) == plain);
  std::vector<uint8_t> again;
  CHECK(loadInto(dir, &again).origin == BatteryStore::Origin::Current);  // (the sealed one wins from now on)
  CHECK(again == plain);
}

TEST(battery_a_short_srm_is_not_imported) {
  const std::wstring dir = check::tempDir();
  fx::spit(pathJoin(dir, L"smas.srm"), fx::pattern(100, 1));
  std::vector<uint8_t> mem;
  CHECK(loadInto(dir, &mem).origin == BatteryStore::Origin::Fresh);
  CHECK(mem == kUntouched);
}

TEST(battery_export_plain_is_the_newest) {
  const std::wstring dir = check::tempDir();
  makeSaves(dir, 3);
  BatteryStore s;
  s.setDir(dir, kVer);
  CHECK(s.exists());
  std::vector<uint8_t> plain;
  CHECK(s.exportPlain(&plain));
  CHECK(plain == fx::sram(3));
  // (with the newest damaged: the one before, not garbage)
  fx::flipBit(sav(dir), 500);
  CHECK(s.exportPlain(&plain));
  CHECK(plain == fx::sram(2));
}

// ---- restore points --------------------------------------------------------

TEST(restore_points_keep_the_save_as_it_was) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  CHECK(s.keepRestorePoint());
  REQUIRE(s.save(fx::sram(2).data(), 0));
  REQUIRE(s.save(fx::sram(3).data(), 0));
  REQUIRE(s.save(fx::sram(4).data(), 0));  // (sram(1) has left the three newest copies by now)
  const auto pts = s.restorePoints();
  REQUIRE(pts.size() == 1);
  std::vector<uint8_t> got(BatteryStore::kSize);
  CHECK(s.readRestorePoint(pts[0], got.data()));
  CHECK(got == fx::sram(1));
  CHECK(pts[0].time > 1700000000LL);
  CHECK(pts[0].name.find("smas-") == 0);
}

TEST(restore_points_are_not_duplicated) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  CHECK(s.keepRestorePoint());
  CHECK(s.keepRestorePoint());  // (the same save again: nothing new to keep)
  CHECK_EQ(s.restorePoints().size(), (size_t)1);
  REQUIRE(s.save(fx::sram(2).data(), 0));
  CHECK(s.keepRestorePoint());
  CHECK_EQ(s.restorePoints().size(), (size_t)2);
}

TEST(restore_points_newest_first_and_capped) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  const int made = BatteryStore::kMaxRestorePoints + 6;
  for (int i = 1; i <= made; i++) {
    REQUIRE(s.save(fx::sram(i).data(), 0));
    REQUIRE(s.keepRestorePoint());
  }
  const auto pts = s.restorePoints();
  REQUIRE(pts.size() == (size_t)BatteryStore::kMaxRestorePoints);
  // The newest are the ones kept, in order, newest first (all made within the same second or two).
  std::vector<uint8_t> got(BatteryStore::kSize);
  for (int k = 0; k < (int)pts.size(); k++) {
    CHECK(s.readRestorePoint(pts[k], got.data()));
    CHECK_MSG(got == fx::sram(made - k), "restore point " + std::to_string(k));
  }
}

TEST(restore_points_damaged_ones_are_not_offered) {
  const std::wstring dir = check::tempDir();
  BatteryStore s;
  s.setDir(dir, kVer);
  std::vector<uint8_t> mem(BatteryStore::kSize);
  s.load(mem.data(), 0);
  REQUIRE(s.save(fx::sram(1).data(), 0));
  REQUIRE(s.keepRestorePoint());
  auto pts = s.restorePoints();
  REQUIRE(pts.size() == 1);
  const BatteryStore::RestorePoint p = pts[0];
  fx::flipBit(p.path, 300);
  CHECK(s.restorePoints().empty());
  std::vector<uint8_t> got(BatteryStore::kSize, 0xEE);
  CHECK(!s.readRestorePoint(p, got.data()));
  CHECK(got == kUntouched);
  // (files in the folder that are not restore points are left alone and not listed)
  fx::spit(pathJoin(pathJoin(dir, L"history"), L"notes.txt"), fx::pattern(10, 1));
  CHECK(s.restorePoints().empty());
}

// ---- save-state slots -------------------------------------------------------

TEST(states_valid_accepts_only_whole_states) {
  std::string why;
  CHECK(states::valid(fx::stateShaped(5000, 1), &why));
  CHECK(!states::valid({}, &why));
  CHECK(!why.empty());
  CHECK(!states::valid(fx::pattern(5000, 1), &why));
  auto bad = fx::stateShaped(5000, 1);
  bad[4000] ^= 1;
  CHECK(!states::valid(bad, &why));
  CHECK(why.find("damaged") != std::string::npos);
  auto cut = fx::stateShaped(5000, 1);
  cut.resize(cut.size() - 1);
  CHECK(!states::valid(cut));
  CHECK(!states::valid(fx::stateShaped(100, 1, 4), &why));  // (a later format)
  CHECK(why.find("newer") != std::string::npos);
  CHECK(!states::valid(fx::stateShaped(100, 1, 0)));
}

TEST(states_slot_round_trip) {
  const std::wstring dir = check::tempDir();
  const auto a = fx::stateShaped(40000, 1);
  CHECK(!states::slotUsed(dir, 3));
  std::string err;
  CHECK_MSG(states::writeSlot(dir, 3, a, &err), err);
  CHECK(states::slotUsed(dir, 3));
  CHECK(!states::slotUsed(dir, 4));
  std::vector<uint8_t> got;
  CHECK(states::readSlot(dir, 3, &got, &err));
  CHECK(got == a);
  CHECK(!states::readSlot(dir, 4, &got, &err));
  CHECK(err.find("empty") != std::string::npos);
  CHECK(states::slotPath(dir, 3) == pathJoin(dir, L"state3.sst"));
}

TEST(states_overwriting_a_slot_keeps_what_it_held) {
  const std::wstring dir = check::tempDir();
  const auto a = fx::stateShaped(40000, 1), b = fx::stateShaped(41000, 2), c = fx::stateShaped(39000, 3);
  REQUIRE(states::writeSlot(dir, 1, a));
  CHECK(!fileExists(states::slotPath(dir, 1) + L".prev"));
  REQUIRE(states::writeSlot(dir, 1, b));
  CHECK(fx::slurp(states::slotPath(dir, 1)) == b);
  CHECK(fx::slurp(states::slotPath(dir, 1) + L".prev") == a);
  REQUIRE(states::writeSlot(dir, 1, c));
  CHECK(fx::slurp(states::slotPath(dir, 1) + L".prev") == b);
}

TEST(states_refuses_to_store_what_is_not_a_state) {
  const std::wstring dir = check::tempDir();
  const auto a = fx::stateShaped(40000, 1);
  REQUIRE(states::writeSlot(dir, 2, a));
  std::string err;
  CHECK(!states::writeSlot(dir, 2, fx::pattern(40000, 9), &err));
  CHECK(!err.empty());
  CHECK(fx::slurp(states::slotPath(dir, 2)) == a);  // (the slot is as it was)
}

TEST(states_damaged_slot_is_refused_with_a_reason) {
  const std::wstring dir = check::tempDir();
  REQUIRE(states::writeSlot(dir, 5, fx::stateShaped(40000, 1)));
  fx::flipBit(states::slotPath(dir, 5), 20000);
  std::vector<uint8_t> got;
  std::string err;
  CHECK(!states::readSlot(dir, 5, &got, &err));
  CHECK(err.find("damaged") != std::string::npos);
  fx::truncateTo(states::slotPath(dir, 5), 5);
  CHECK(!states::readSlot(dir, 5, &got, &err));
}

TEST(states_failed_write_leaves_the_slot_as_it_was) {
  const std::wstring dir = check::tempDir();
  const auto a = fx::stateShaped(40000, 1), b = fx::stateShaped(40000, 2);
  REQUIRE(states::writeSlot(dir, 1, a));
  for (int kind = 0; kind < 3; kind++) {
    Faults faults;
    if (kind == 0) faults.f.failWriteAfter = 20000;
    if (kind == 1) faults.f.failFsync = true;
    if (kind == 2) faults.f.failRename = true;
    std::string err;
    CHECK(!states::writeSlot(dir, 1, b, &err));
    CHECK(!err.empty());
    CHECK(fx::slurp(states::slotPath(dir, 1)) == a);
  }
  // (a damaged slot is not kept as ".prev" of the new one: only whole states are)
  fx::flipBit(states::slotPath(dir, 1), 300);
  REQUIRE(states::writeSlot(dir, 1, b));
  CHECK(!fileExists(states::slotPath(dir, 1) + L".prev"));
}

TEST(states_sealed_round_trip) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"resume.sst");
  const auto st = fx::stateShaped(50000, 4);
  seal::Info in;
  in.aux = 2;
  in.romCrc = 0x925637c7;
  in.generation = 5;
  std::string err;
  CHECK_MSG(states::writeSealed(p, seal::kResume, in, st, &err), err);
  seal::Info out;
  std::vector<uint8_t> got;
  CHECK(states::readSealed(p, seal::kResume, &out, &got, &err));
  CHECK(got == st);
  CHECK_EQ(out.aux, 2u);
  CHECK_EQ(out.generation, 5u);
  CHECK(out.time > 1700000000LL);  // (stamped when written)
  // Damaged, or of another kind: refused, with words.
  CHECK(!states::readSealed(p, seal::kBattery, &out, &got, &err));
  fx::flipBit(p, 30000);
  CHECK(!states::readSealed(p, seal::kResume, &out, &got, &err));
  CHECK(!err.empty());
  CHECK(!states::readSealed(pathJoin(dir, L"none.sst"), seal::kResume, &out, &got, &err));
  CHECK_EQ(err, std::string("missing"));
  // What is not a state is not sealed as one.
  CHECK(!states::writeSealed(p, seal::kResume, in, fx::pattern(500, 1), &err));
}

TEST(room_to_write) {
  const std::wstring dir = check::tempDir();
  std::string err;
  CHECK(roomToWrite(dir, 8192, &err));
  CHECK(!roomToWrite(dir, (size_t)1 << 60, &err));  // (an exabyte: no phone has it)
  CHECK(err.find("not enough free storage") != std::string::npos);
  CHECK(roomToWrite(L"/no/such/folder/at/all", 8192, &err));  // (can't be told: the write itself will say)
}

// ---- the background writer ---------------------------------------------------

TEST(async_writer_writes) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"resume.sst");
  AsyncWriter w;
  const auto d = fx::pattern(300000, 1);
  w.submit(p, d);
  w.flush();
  CHECK(fx::slurp(p) == d);
  CHECK_EQ(w.written(), 1);
  CHECK_EQ(w.failed(), 0);
  w.flush();  // (nothing waiting: returns at once)
}

TEST(async_writer_last_one_wins) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"resume.sst");
  AsyncWriter w;
  for (int i = 1; i <= 40; i++) w.submit(p, fx::pattern(100000, i));
  w.flush();
  CHECK(fx::slurp(p) == fx::pattern(100000, 40));
  CHECK(w.written() >= 1 && w.written() <= 40);
  CHECK(!fileExists(p + L".tmp"));
}

TEST(async_writer_finishes_before_it_is_destroyed) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"resume.sst");
  const auto d = fx::pattern(200000, 3);
  {
    AsyncWriter w;
    w.submit(p, d);
  }
  CHECK(fx::slurp(p) == d);
  { AsyncWriter unused; }  // (never used: no thread to wait for)
}

TEST(async_writer_counts_failures) {
  const std::wstring dir = check::tempDir();
  AsyncWriter w;
  w.submit(pathJoin(pathJoin(dir, L"missing"), L"x.sst"), fx::pattern(100, 1));
  w.flush();
  CHECK_EQ(w.failed(), 1);
  CHECK_EQ(w.written(), 0);
}
