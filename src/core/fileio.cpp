// Atomic, durable file replacement: see fileio.h.
#include "fileio.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

FileIoFaults* g_fileIoFaults = nullptr;

namespace {
thread_local char t_error[96] = "";

void setError(const char* what, int err) { snprintf(t_error, sizeof t_error, "%s: %s", what, strerror(err)); }

// write() until everything is written: it may write less than asked for, and
// may be interrupted by a signal.
bool writeFully(int fd, const uint8_t* p, size_t n) {
  while (n > 0) {
    size_t chunk = n;
    if (FileIoFaults* f = g_fileIoFaults) {
      f->writes++;
      if (f->failWriteAfter >= 0) {
        if (f->failWriteAfter == 0) {
          errno = ENOSPC;
          return false;
        }
        if ((long long)chunk > f->failWriteAfter) chunk = (size_t)f->failWriteAfter;
        f->failWriteAfter -= (long long)chunk;
      }
    }
    ssize_t w = write(fd, p, chunk);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (w == 0) {
      errno = EIO;
      return false;
    }
    p += w;
    n -= (size_t)w;
  }
  return true;
}

// Flushes a folder, so that a rename inside it is on the storage device.
// (Best effort: some file systems refuse it, and the data itself is safe.)
void syncFolderOf(const std::string& path) {
  size_t s = path.find_last_of('/');
  std::string dir = s == std::string::npos ? "." : s == 0 ? "/" : path.substr(0, s);
  int fd = open(dir.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  fsync(fd);
  close(fd);
}
}  // namespace

const char* lastWriteError() { return t_error; }

bool writeFileSafely(const std::string& path, const void* data, size_t n) {
  t_error[0] = 0;
  const std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    setError("cannot create the file", errno);
    return false;
  }
  bool ok = writeFully(fd, (const uint8_t*)data, n);
  if (!ok) setError("cannot write the file", errno);
  if (ok) {
    FileIoFaults* f = g_fileIoFaults;
    if (f) f->fsyncs++;
    if ((f && f->failFsync) || fsync(fd) != 0) {
      if (f && f->failFsync) errno = EIO;
      setError("cannot flush the file to storage", errno);
      ok = false;
    }
  }
  if (close(fd) != 0 && ok) {
    setError("cannot close the file", errno);
    ok = false;
  }
  if (ok) {
    FileIoFaults* f = g_fileIoFaults;
    if (f) f->renames++;
    if ((f && f->failRename) || std::rename(tmp.c_str(), path.c_str()) != 0) {
      if (f && f->failRename) errno = EACCES;
      setError("cannot replace the file", errno);
      ok = false;
    }
  }
  if (!ok) {
    unlink(tmp.c_str());  // (the real file is untouched)
    return false;
  }
  syncFolderOf(path);
  return true;
}

// (a path of code points, to UTF-8)
bool writeFileSafely(const std::wstring& path, const void* data, size_t n) {
  std::string s;
  for (wchar_t wc : path) {
    uint32_t c = (uint32_t)wc;
    if (c < 0x80) {
      s += (char)c;
    } else if (c < 0x800) {
      s += (char)(0xc0 | c >> 6);
      s += (char)(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
      s += (char)(0xe0 | c >> 12);
      s += (char)(0x80 | ((c >> 6) & 0x3f));
      s += (char)(0x80 | (c & 0x3f));
    } else {
      s += (char)(0xf0 | c >> 18);
      s += (char)(0x80 | ((c >> 12) & 0x3f));
      s += (char)(0x80 | ((c >> 6) & 0x3f));
      s += (char)(0x80 | (c & 0x3f));
    }
  }
  return writeFileSafely(s, data, n);
}
