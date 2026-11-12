// Atomic file replacement (core/fileio.h): a save file is either its old
// contents, whole, or its new contents, whole, whatever goes wrong on the way.
#include <cstring>
#include "app/sys.h"
#include "check.h"
#include "core/fileio.h"

namespace {
std::vector<uint8_t> bytes(size_t n, uint8_t seed) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i * 7);
  return v;
}
std::vector<uint8_t> slurp(const std::wstring& p) {
  std::vector<uint8_t> v;
  CHECK(readFile(p, v));
  return v;
}
// Installs faults for the life of the object.
struct Faults {
  FileIoFaults f;
  Faults() { g_fileIoFaults = &f; }
  ~Faults() { g_fileIoFaults = nullptr; }
};
}  // namespace

TEST(fileio_writes_a_new_file) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"a.bin");
  const auto d = bytes(5000, 3);
  CHECK(writeFileSafely(p, d.data(), d.size()));
  CHECK(slurp(p) == d);
  CHECK_EQ(std::string(lastWriteError()), std::string(""));
  CHECK(!fileExists(p + L".tmp"));
}

TEST(fileio_replaces_a_file_whole) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"a.bin");
  const auto a = bytes(9000, 1), b = bytes(100, 9);
  CHECK(writeFileSafely(p, a.data(), a.size()));
  CHECK(writeFileSafely(p, b.data(), b.size()));
  CHECK(slurp(p) == b);  // (shorter than before: nothing of the old one is left behind it)
}

TEST(fileio_empty_file) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"empty.bin");
  CHECK(writeFileSafely(p, nullptr, 0));
  CHECK_EQ(fileSize(p), 0LL);
}

TEST(fileio_storage_full_keeps_the_old_file) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"save.bin");
  const auto old = bytes(8192, 1), neu = bytes(8192, 200);
  REQUIRE(writeFileSafely(p, old.data(), old.size()));
  // The device fills up at every possible point of the write.
  for (long long room : {0LL, 1LL, 100LL, 4096LL, 8191LL}) {
    Faults faults;
    faults.f.failWriteAfter = room;
    CHECK(!writeFileSafely(p, neu.data(), neu.size()));
    CHECK_MSG(strstr(lastWriteError(), "No space") != nullptr, lastWriteError());
    CHECK(slurp(p) == old);
    CHECK(!fileExists(p + L".tmp"));  // (nothing half-written is left lying about)
  }
  // ... and with room again it goes through.
  CHECK(writeFileSafely(p, neu.data(), neu.size()));
  CHECK(slurp(p) == neu);
}

TEST(fileio_failed_flush_keeps_the_old_file) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"save.bin");
  const auto old = bytes(300, 1), neu = bytes(300, 2);
  REQUIRE(writeFileSafely(p, old.data(), old.size()));
  {
    Faults faults;
    faults.f.failFsync = true;
    CHECK(!writeFileSafely(p, neu.data(), neu.size()));
    CHECK_MSG(strstr(lastWriteError(), "flush") != nullptr, lastWriteError());
    CHECK_EQ(faults.f.renames, 0);  // (never renamed over the real file)
  }
  CHECK(slurp(p) == old);
  CHECK(!fileExists(p + L".tmp"));
}

TEST(fileio_failed_rename_keeps_the_old_file) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"save.bin");
  const auto old = bytes(300, 1), neu = bytes(300, 2);
  REQUIRE(writeFileSafely(p, old.data(), old.size()));
  {
    Faults faults;
    faults.f.failRename = true;
    CHECK(!writeFileSafely(p, neu.data(), neu.size()));
    CHECK_MSG(strstr(lastWriteError(), "replace") != nullptr, lastWriteError());
  }
  CHECK(slurp(p) == old);
  CHECK(!fileExists(p + L".tmp"));
}

TEST(fileio_flushes_before_renaming) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"save.bin");
  const auto d = bytes(70000, 5);
  Faults faults;
  CHECK(writeFileSafely(p, d.data(), d.size()));
  CHECK_EQ(faults.f.fsyncs, 1);
  CHECK_EQ(faults.f.renames, 1);
  CHECK(faults.f.writes >= 1);
}

TEST(fileio_missing_folder_fails_cleanly) {
  const std::wstring dir = check::tempDir(), p = pathJoin(pathJoin(dir, L"no-such-folder"), L"a.bin");
  const auto d = bytes(10, 1);
  CHECK(!writeFileSafely(p, d.data(), d.size()));
  CHECK_MSG(strstr(lastWriteError(), "create") != nullptr, lastWriteError());
  CHECK(!fileExists(p));
}

TEST(fileio_names_beyond_ascii) {
  const std::wstring dir = check::tempDir();
  // (two, three and four byte UTF-8 sequences)
  const std::wstring p = pathJoin(dir, L"sp\u00e9cial-\u65e5\u672c-\U0001F600.bin");
  const auto d = bytes(64, 7);
  CHECK(writeFileSafely(p, d.data(), d.size()));
  CHECK(slurp(p) == d);
  std::vector<std::wstring> names;
  CHECK(listFiles(dir, names));
  REQUIRE(names.size() == 1);
  CHECK(names[0] == pathName(p));
}

TEST(sys_paths) {
  CHECK(pathJoin(L"/a/b", L"c") == L"/a/b/c");
  CHECK(pathJoin(L"/a/b/", L"c") == L"/a/b/c");
  CHECK(pathJoin(L"", L"c") == L"c");
  CHECK(pathDir(L"/a/b/c") == L"/a/b");
  CHECK(pathDir(L"c") == L".");
  CHECK(pathName(L"/a/b/c.txt") == L"c.txt");
  CHECK(pathName(L"c.txt") == L"c.txt");
}

TEST(sys_files) {
  const std::wstring dir = check::tempDir(), p = pathJoin(dir, L"f.bin");
  CHECK_EQ(fileSize(p), -1LL);
  CHECK(!fileExists(p));
  CHECK(removeFile(p));  // (not there: nothing to do)
  const auto d = bytes(1234, 1);
  REQUIRE(writeFileSafely(p, d.data(), d.size()));
  CHECK_EQ(fileSize(p), 1234LL);
  CHECK(fileTime(p) > 1700000000LL);
  std::vector<uint8_t> v;
  CHECK(!readFile(p, v, 1000));  // (longer than the caller will take: refused, not truncated)
  CHECK(readFile(p, v, 1234));
  CHECK(renameFile(p, p + L".moved"));
  CHECK(!fileExists(p));
  CHECK(fileExists(p + L".moved"));
  makeDir(pathJoin(dir, L"sub"));
  std::vector<std::wstring> files, dirs;
  CHECK(listFiles(dir, files));
  CHECK(listDirs(dir, dirs));
  CHECK_EQ(files.size(), (size_t)1);
  CHECK_EQ(dirs.size(), (size_t)1);
  CHECK_EQ(fileSize(pathJoin(dir, L"sub")), -1LL);  // (a folder is not a file)
  unsigned long long freeBytes = 0;
  CHECK(diskFree(dir, &freeBytes));
  CHECK(removeFile(p + L".moved"));
}
