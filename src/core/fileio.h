// Writing files without ever losing the previous version.
//
// SMAS Mobile keeps the player's progress in a handful of small files, and a
// phone can be switched off, run out of storage or kill the app at any moment.
// Every file that matters is therefore written like this:
//
//   1. the new contents go to "<name>.tmp", in the same folder;
//   2. the data is flushed to the storage device (fsync);
//   3. the temporary file is renamed over the real one (atomic on POSIX);
//   4. the folder itself is flushed, so the rename survives a power cut.
//
// At no point does the real file hold a partly written version: it is either
// the old contents, whole, or the new contents, whole. A failed write (storage
// full, I/O error) leaves the old file exactly as it was and returns false.
#pragma once
#include <cstddef>
#include <string>

bool writeFileSafely(const std::wstring& path, const void* data, size_t n);
// `path` in UTF-8.
bool writeFileSafely(const std::string& path, const void* data, size_t n);

// Why the last writeFileSafely on this thread failed ("" if it succeeded):
// "no space left on device" and the like, for the log and the messages.
const char* lastWriteError();

// Faults for the tests (tests/): the real write path is exercised with a
// device that fills up, a flush that fails or a rename that fails. All off
// (the default) in the app.
struct FileIoFaults {
  long long failWriteAfter = -1;  // bytes accepted before writes fail with ENOSPC (-1: never)
  bool failFsync = false;
  bool failRename = false;
  int writes = 0, fsyncs = 0, renames = 0;  // counted while a FileIoFaults is installed
};
extern FileIoFaults* g_fileIoFaults;
