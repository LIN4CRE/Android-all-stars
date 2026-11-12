// Saved progress on disk: see savestore.h.
#include "app/savestore.h"
#include <algorithm>
#include <cstring>
#include "app/canvas.h"
#include "app/sys.h"
#include "core/crc32.h"
#include "core/fileio.h"

namespace {
void put32(std::vector<uint8_t>& o, uint32_t v) {
  for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& o, uint64_t v) {
  for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint64_t get64(const uint8_t* p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }

// "SMASMOB" and the format's version.
const uint8_t kMagic[8] = {'S', 'M', 'A', 'S', 'M', 'O', 'B', 1};

std::string megabytes(unsigned long long bytes) {
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f MB", bytes / 1048576.0);
  return buf;
}
}  // namespace

// ---------------------------------------------------------------------------
namespace seal {
const char* errorText(Error e) {
  switch (e) {
    case Error::None: return "intact";
    case Error::Missing: return "missing";
    case Error::Unreadable: return "the storage could not read it";
    case Error::TooShort: return "cut short";
    case Error::BadMagic: return "not a SMAS Mobile save";
    case Error::BadVersion: return "made by a newer version of the app";
    case Error::BadHeader: return "its header is damaged";
    case Error::BadLength: return "its length is wrong (incomplete)";
    case Error::BadPayload: return "its contents are damaged";
    case Error::WrongKind: return "another kind of save";
  }
  return "unreadable";
}

std::vector<uint8_t> make(const Info& info, const void* payload, size_t n) {
  std::vector<uint8_t> o;
  o.reserve(kHeaderSize + n);
  o.insert(o.end(), kMagic, kMagic + 8);
  put32(o, info.kind);
  put32(o, (uint32_t)n);
  put32(o, crc32(payload, n));
  put32(o, info.romCrc);
  put32(o, info.aux);
  put32(o, info.generation);
  put64(o, (uint64_t)info.time);
  put32(o, info.appVersion);
  put32(o, crc32(o.data(), o.size()));
  const uint8_t* p = (const uint8_t*)payload;
  o.insert(o.end(), p, p + n);
  return o;
}

Error open(const std::vector<uint8_t>& f, uint32_t wantKind, Info* info, std::vector<uint8_t>* payload) {
  if (f.size() < kHeaderSize) return Error::TooShort;
  if (std::memcmp(f.data(), kMagic, 7) != 0) return Error::BadMagic;
  if (f[7] != kMagic[7]) return Error::BadVersion;
  if (get32(&f[44]) != crc32(f.data(), 44)) return Error::BadHeader;
  const uint32_t kind = get32(&f[8]), n = get32(&f[12]);
  if (kind != wantKind) return Error::WrongKind;
  if (f.size() - kHeaderSize != n) return Error::BadLength;
  if (get32(&f[16]) != crc32(f.data() + kHeaderSize, n)) return Error::BadPayload;
  if (info) {
    info->kind = kind;
    info->romCrc = get32(&f[20]);
    info->aux = get32(&f[24]);
    info->generation = get32(&f[28]);
    info->time = (long long)get64(&f[32]);
    info->appVersion = get32(&f[40]);
  }
  if (payload) payload->assign(f.begin() + kHeaderSize, f.end());
  return Error::None;
}

Error read(const std::wstring& path, uint32_t wantKind, Info* info, std::vector<uint8_t>* payload, size_t maxBytes) {
  const long long size = fileSize(path);
  if (size < 0) return Error::Missing;
  if ((unsigned long long)size > maxBytes) return Error::BadLength;  // (far too long to be one)
  std::vector<uint8_t> f;
  if (!readFile(path, f, maxBytes)) return Error::Unreadable;
  return open(f, wantKind, info, payload);
}
}  // namespace seal

bool roomToWrite(const std::wstring& dir, size_t bytes, std::string* err) {
  unsigned long long free = 0;
  if (!diskFree(dir, &free)) return true;
  const unsigned long long need = (unsigned long long)bytes * 2 + (256u << 10);
  if (free >= need) return true;
  if (err) *err = "not enough free storage (" + megabytes(need) + " needed, " + megabytes(free) + " free)";
  return false;
}

// ---------------------------------------------------------------------------
// The games' save files.

void BatteryStore::setDir(const std::wstring& saveDir, uint32_t appVersion) {
  dir_ = saveDir;
  appVersion_ = appVersion;
  generation_ = 0;
  haveLast_ = false;
  lastTime_ = 0;
  makeDir(dir_);
  // Every save written from here on counts on from the newest one there is,
  // whether or not the saves are ever loaded (a backup restored before a game
  // file is imported must still outrank what was there).
  for (const wchar_t* n : {L"", L".new", L".1", L".2"}) {
    const Candidate k = probe(path(n));
    if (k.err != seal::Error::None) continue;
    if (k.info.generation >= generation_) {
      generation_ = k.info.generation;
      lastTime_ = k.info.time;
    }
  }
  for (const RestorePoint& p : restorePoints()) generation_ = std::max(generation_, p.generation);
}

std::wstring BatteryStore::path(const wchar_t* suffix) const { return pathJoin(dir_, std::wstring(L"smas.sav") + suffix); }

BatteryStore::Candidate BatteryStore::probe(const std::wstring& p) const {
  Candidate c;
  c.path = p;
  c.present = fileExists(p);
  if (!c.present) return c;
  c.err = seal::read(p, seal::kBattery, &c.info, &c.payload, 1u << 16);
  if (c.err == seal::Error::None && c.payload.size() != kSize) c.err = seal::Error::BadLength;
  return c;
}

// A damaged file out of the way, under a name nothing reads or writes: kept
// for whoever wants to look at it, never deleted here.
int BatteryStore::setAside(const std::wstring& p) const {
  const std::wstring stamp = widenUtf8(timeText("%Y%m%d-%H%M%S"));
  for (int k = 0; k < 100; k++) {
    std::wstring to = p + L".damaged-" + stamp + (k ? L"-" + std::to_wstring(k) : L"");
    if (fileExists(to)) continue;
    return renameFile(p, to) ? 1 : 0;
  }
  return 0;
}

BatteryStore::LoadResult BatteryStore::load(uint8_t* sram, uint32_t) {
  LoadResult r;
  // (the last save; one written but not yet moved into place when the app
  // was stopped; the two before)
  const wchar_t* kNames[4] = {L"", L".new", L".1", L".2"};
  Candidate c[4];
  int best = -1;
  for (int i = 0; i < 4; i++) {
    c[i] = probe(path(kNames[i]));
    if (c[i].err == seal::Error::None && (best < 0 || c[i].info.generation > c[best].info.generation)) best = i;
  }
  if (c[0].present && c[0].err != seal::Error::None) r.problem = std::string("smas.sav: ") + seal::errorText(c[0].err);
  // Damaged ones aside first: their names are free for what follows. (One the
  // storage would not read is not known to be damaged: it stays where it is.)
  for (int i = 0; i < 4; i++) {
    if (!c[i].present || c[i].err == seal::Error::None) continue;
    if (c[i].err == seal::Error::Unreadable) r.unreadable++;
    else r.setAside += setAside(c[i].path);
  }
  auto use = [&](const Candidate& k, const std::string& name) {
    std::memcpy(sram, k.payload.data(), kSize);
    generation_ = std::max(generation_, k.info.generation);
    lastCrc_ = crc32(k.payload.data(), kSize);
    haveLast_ = true;
    lastTime_ = r.time = k.info.time;
    r.source = name;
  };
  if (best == 1) {
    // The newest save was written and checked but the app stopped before it
    // was moved into place: finish that now.
    if (fileExists(path(L".1"))) renameFile(path(L".1"), path(L".2"));
    if (fileExists(path(L""))) renameFile(path(L""), path(L".1"));
    renameFile(path(L".new"), path(L""));
    use(c[1], "smas.sav (completed)");
    r.origin = Origin::Current;
    return r;
  }
  if (best >= 0) {
    use(c[best], narrowUtf8(pathName(c[best].path)));
    r.origin = best == 0 ? Origin::Current : Origin::Recovered;
    return r;
  }
  // None of them: the newest restore point that is whole.
  for (const RestorePoint& p : restorePoints()) {
    Candidate k = probe(p.path);
    if (k.err != seal::Error::None) continue;
    use(k, "history/" + p.name);
    r.origin = Origin::Recovered;
    return r;
  }
  // A plain battery file (another emulator's, or an older version's).
  std::vector<uint8_t> plain;
  if (readFile(pathJoin(dir_, L"smas.srm"), plain, 1u << 16) && plain.size() >= kSize) {
    std::memcpy(sram, plain.data(), kSize);
    haveLast_ = false;  // (not yet a sealed save: the next save writes one)
    lastTime_ = r.time = fileTime(pathJoin(dir_, L"smas.srm"));
    r.source = "smas.srm";
    r.origin = Origin::Imported;
    return r;
  }
  haveLast_ = false;
  r.origin = r.setAside + r.unreadable > 0 ? Origin::Damaged : Origin::Fresh;
  return r;
}

bool BatteryStore::exists() const {
  for (const wchar_t* n : {L"", L".new", L".1", L".2"})
    if (probe(path(n)).err == seal::Error::None) return true;
  return false;
}

bool BatteryStore::exportPlain(std::vector<uint8_t>* out) const {
  Candidate bestC;
  bool have = false;
  for (const wchar_t* n : {L"", L".new", L".1", L".2"}) {
    Candidate k = probe(path(n));
    if (k.err != seal::Error::None) continue;
    if (!have || k.info.generation > bestC.info.generation) bestC = std::move(k), have = true;
  }
  if (!have) return false;
  *out = bestC.payload;
  return true;
}

bool BatteryStore::save(const uint8_t* sram, uint32_t romCrc, std::string* err) {
  const uint32_t c = crc32(sram, kSize);
  if (haveLast_ && c == lastCrc_ && fileExists(path(L""))) return true;  // already on disk
  std::string why;
  if (!roomToWrite(dir_, seal::kHeaderSize + kSize, &why)) {
    if (err) *err = why;
    return false;
  }
  seal::Info info;
  info.kind = seal::kBattery;
  info.romCrc = romCrc;
  info.generation = generation_ + 1;
  info.time = timeNow();
  info.appVersion = appVersion_;
  const std::vector<uint8_t> bytes = seal::make(info, sram, kSize);
  // 1. Beside the last save, whole or not at all.
  if (!writeFileSafely(path(L".new"), bytes.data(), bytes.size())) {
    if (err) *err = lastWriteError();
    return false;
  }
  // 2. Read back from the storage and checked: only then does it count.
  Candidate back = probe(path(L".new"));
  if (back.err != seal::Error::None || back.payload.size() != kSize || std::memcmp(back.payload.data(), sram, kSize) != 0) {
    removeFile(path(L".new"));
    if (err) *err = "the save could not be read back from storage";
    return false;
  }
  // 3. The saves before it move down one, and it takes the last save's place.
  //    (Stopped anywhere in here, the newest whole save is still found: see load.)
  if (fileExists(path(L".1"))) renameFile(path(L".1"), path(L".2"));
  if (fileExists(path(L""))) renameFile(path(L""), path(L".1"));
  renameFile(path(L".new"), path(L""));
  generation_ = info.generation;
  lastCrc_ = c;
  haveLast_ = true;
  lastTime_ = info.time;
  return true;
}

std::vector<BatteryStore::RestorePoint> BatteryStore::restorePoints() const {
  std::vector<RestorePoint> out;
  const std::wstring hist = pathJoin(dir_, L"history");
  std::vector<std::wstring> names;
  if (!listFiles(hist, names)) return out;
  for (const std::wstring& n : names) {
    if (n.size() < 9 || n.compare(0, 5, L"smas-") != 0 || n.compare(n.size() - 4, 4, L".sav") != 0) continue;
    Candidate k = probe(pathJoin(hist, n));
    if (k.err != seal::Error::None) continue;
    RestorePoint p;
    p.path = k.path;
    p.name = narrowUtf8(n);
    p.time = k.info.time;
    p.generation = k.info.generation;
    p.crc = crc32(k.payload.data(), k.payload.size());
    out.push_back(p);
  }
  // Newest first: by the save's own count (each save's is one more than the
  // last, whatever the clock said), then by when it was written.
  std::sort(out.begin(), out.end(), [](const RestorePoint& a, const RestorePoint& b) {
    if (a.generation != b.generation) return a.generation > b.generation;
    if (a.time != b.time) return a.time > b.time;
    return a.name > b.name;
  });
  return out;
}

bool BatteryStore::readRestorePoint(const RestorePoint& p, uint8_t* sram) const {
  Candidate k = probe(p.path);
  if (k.err != seal::Error::None) return false;
  std::memcpy(sram, k.payload.data(), kSize);
  return true;
}

bool BatteryStore::keepRestorePoint() {
  Candidate cur = probe(path(L""));
  if (cur.err != seal::Error::None) return false;
  const uint32_t c = crc32(cur.payload.data(), cur.payload.size());
  std::vector<RestorePoint> pts = restorePoints();
  if (!pts.empty() && pts[0].crc == c) return true;  // the newest one is this save already
  const std::wstring hist = pathJoin(dir_, L"history");
  makeDir(hist);
  const std::vector<uint8_t> bytes = seal::make(cur.info, cur.payload.data(), cur.payload.size());
  const std::wstring stamp = widenUtf8(timeText("%Y%m%d-%H%M%S"));
  std::wstring name;
  for (int k = 0; k < 100; k++) {
    name = L"smas-" + stamp + (k ? L"-" + std::to_wstring(k) : L"") + L".sav";
    if (!fileExists(pathJoin(hist, name))) break;
  }
  if (!writeFileSafely(pathJoin(hist, name), bytes.data(), bytes.size())) return false;
  // The oldest make way (copies of saves kept elsewhere too: nothing is lost with them).
  pts = restorePoints();
  for (size_t i = kMaxRestorePoints; i < pts.size(); i++) removeFile(pts[i].path);
  return true;
}

// ---------------------------------------------------------------------------
// Save states.
namespace states {
bool valid(const std::vector<uint8_t>& d, std::string* why) {
  auto no = [&](const char* w) {
    if (why) *why = w;
    return false;
  };
  if (d.size() < 12) return no("it is empty or cut short");
  if (d.size() > (8u << 20)) return no("it is too large to be a save state");
  if (get32(d.data()) != 0x53534d53u) return no("it is not a save state of this app");
  const uint32_t ver = get32(d.data() + 4);
  if (ver < 1 || ver > 3) return no("it was made by a newer version of the app");
  if (ver >= 2 && get32(d.data() + d.size() - 4) != crc32(d.data(), d.size() - 4))
    return no("it is damaged (its checksum does not match)");
  return true;
}

std::wstring slotPath(const std::wstring& saveDir, int slot) {
  return pathJoin(saveDir, L"state" + std::to_wstring(slot) + L".sst");
}

bool slotUsed(const std::wstring& saveDir, int slot) { return fileSize(slotPath(saveDir, slot)) > 0; }

bool readSlot(const std::wstring& saveDir, int slot, std::vector<uint8_t>* bytes, std::string* err) {
  const std::wstring p = slotPath(saveDir, slot);
  if (!fileExists(p)) {
    if (err) *err = "the slot is empty";
    return false;
  }
  if (!readFile(p, *bytes, 8u << 20)) {
    if (err) *err = "the file could not be read";
    return false;
  }
  return valid(*bytes, err);
}

bool writeSlot(const std::wstring& saveDir, int slot, const std::vector<uint8_t>& bytes, std::string* err) {
  if (!valid(bytes, err)) return false;
  if (!roomToWrite(saveDir, bytes.size(), err)) return false;
  const std::wstring p = slotPath(saveDir, slot);
  // What the slot holds now, if it is a whole state, is kept beside it.
  std::vector<uint8_t> old;
  if (fileExists(p) && readFile(p, old, 8u << 20) && valid(old)) writeFileSafely(p + L".prev", old.data(), old.size());
  if (!writeFileSafely(p, bytes.data(), bytes.size())) {
    if (err) *err = lastWriteError();
    return false;
  }
  std::vector<uint8_t> back;
  if (!readFile(p, back, 8u << 20) || back != bytes) {
    // (as it was, if what it held was kept)
    if (!old.empty()) writeFileSafely(p, old.data(), old.size());
    if (err) *err = "the state could not be read back from storage";
    return false;
  }
  return true;
}

bool writeSealed(const std::wstring& path, uint32_t kind, const seal::Info& info, const std::vector<uint8_t>& state,
                 std::string* err) {
  if (!valid(state, err)) return false;
  if (!roomToWrite(pathDir(path), state.size() + seal::kHeaderSize, err)) return false;
  seal::Info i = info;
  i.kind = kind;
  if (i.time == 0) i.time = timeNow();
  const std::vector<uint8_t> bytes = seal::make(i, state.data(), state.size());
  if (!writeFileSafely(path, bytes.data(), bytes.size())) {
    if (err) *err = lastWriteError();
    return false;
  }
  return true;
}

bool readSealed(const std::wstring& path, uint32_t kind, seal::Info* info, std::vector<uint8_t>* state, std::string* err) {
  std::vector<uint8_t> payload;
  seal::Error e = seal::read(path, kind, info, &payload);
  if (e != seal::Error::None) {
    if (err) *err = seal::errorText(e);
    return false;
  }
  if (!valid(payload, err)) return false;
  if (state) state->swap(payload);
  return true;
}
}  // namespace states

// ---------------------------------------------------------------------------
AsyncWriter::~AsyncWriter() {
  {
    std::lock_guard<std::mutex> lock(m_);
    quit_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void AsyncWriter::submit(const std::wstring& path, std::vector<uint8_t> bytes) {
  {
    std::lock_guard<std::mutex> lock(m_);
    path_ = path;
    bytes_ = std::move(bytes);
    pending_ = true;
    if (!started_) {
      started_ = true;
      worker_ = std::thread([this]() { run(); });
    }
  }
  cv_.notify_all();
}

void AsyncWriter::flush() {
  std::unique_lock<std::mutex> lock(m_);
  idle_.wait(lock, [this]() { return !pending_ && !busy_; });
}

int AsyncWriter::written() const {
  std::lock_guard<std::mutex> lock(m_);
  return written_;
}

int AsyncWriter::failed() const {
  std::lock_guard<std::mutex> lock(m_);
  return failed_;
}

void AsyncWriter::run() {
  std::unique_lock<std::mutex> lock(m_);
  for (;;) {
    cv_.wait(lock, [this]() { return pending_ || quit_; });
    if (!pending_) break;  // (quitting, nothing left to write)
    std::wstring path;
    std::vector<uint8_t> bytes;
    path.swap(path_);
    bytes.swap(bytes_);
    pending_ = false;
    busy_ = true;
    lock.unlock();
    const bool ok = writeFileSafely(path, bytes.data(), bytes.size());
    lock.lock();
    busy_ = false;
    (ok ? written_ : failed_)++;
    idle_.notify_all();
  }
}
