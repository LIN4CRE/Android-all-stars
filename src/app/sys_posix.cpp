// sys.h on POSIX systems (Android, and the tests on a computer): paths are UTF-8 underneath.
#include "app/sys.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "app/canvas.h"

std::wstring pathJoin(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty()) return name;
  return dir.back() == L'/' ? dir + name : dir + L"/" + name;
}

std::wstring pathDir(const std::wstring& path) {
  size_t s = path.find_last_of(L'/');
  return s == std::wstring::npos ? L"." : path.substr(0, s);
}

std::wstring pathName(const std::wstring& path) {
  size_t s = path.find_last_of(L'/');
  return s == std::wstring::npos ? path : path.substr(s + 1);
}

FILE* openFile(const std::wstring& path, const char* mode) { return fopen(narrowUtf8(path).c_str(), mode); }

bool readFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxBytes) {
  FILE* f = openFile(path, "rb");
  if (!f) return false;
  fseeko(f, 0, SEEK_END);
  off_t n = ftello(f);
  fseeko(f, 0, SEEK_SET);
  if (n < 0 || (unsigned long long)n > maxBytes) {
    fclose(f);
    return false;
  }
  out.resize((size_t)n);
  size_t r = n ? fread(out.data(), 1, (size_t)n, f) : 0;
  fclose(f);
  return r == (size_t)n;
}

bool readText(const std::wstring& path, std::string& out) {
  std::vector<uint8_t> d;
  if (!readFile(path, d)) return false;
  out.assign(d.begin(), d.end());
  return true;
}

bool fileExists(const std::wstring& path) {
  struct stat st;
  return stat(narrowUtf8(path).c_str(), &st) == 0;
}

bool fileModified(const std::wstring& path, int* day, int* month, int* hour, int* minute) {
  struct stat st;
  if (stat(narrowUtf8(path).c_str(), &st) != 0) return false;
  time_t t = st.st_mtime;
  tm lt;
  localtime_r(&t, &lt);
  *day = lt.tm_mday;
  *month = lt.tm_mon + 1;
  *hour = lt.tm_hour;
  *minute = lt.tm_min;
  return true;
}

void makeDir(const std::wstring& path) { mkdir(narrowUtf8(path).c_str(), 0755); }

long long fileSize(const std::wstring& path) {
  struct stat st;
  if (stat(narrowUtf8(path).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
  return (long long)st.st_size;
}

long long fileTime(const std::wstring& path) {
  struct stat st;
  if (stat(narrowUtf8(path).c_str(), &st) != 0) return 0;
  return (long long)st.st_mtime;
}

bool removeFile(const std::wstring& path) { return unlink(narrowUtf8(path).c_str()) == 0 || errno == ENOENT; }

bool renameFile(const std::wstring& from, const std::wstring& to) {
  return std::rename(narrowUtf8(from).c_str(), narrowUtf8(to).c_str()) == 0;
}

bool diskFree(const std::wstring& dir, unsigned long long* bytes) {
  struct statvfs sv;
  if (statvfs(narrowUtf8(dir).c_str(), &sv) != 0) return false;
  *bytes = (unsigned long long)sv.f_bavail * (unsigned long long)sv.f_frsize;
  return true;
}

bool listFiles(const std::wstring& dir, std::vector<std::wstring>& names) {
  DIR* d = opendir(narrowUtf8(dir).c_str());
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    std::wstring n = widenUtf8(e->d_name);
    struct stat st;
    if (stat(narrowUtf8(pathJoin(dir, n)).c_str(), &st) == 0 && S_ISREG(st.st_mode)) names.push_back(n);
  }
  closedir(d);
  return true;
}

bool listDirs(const std::wstring& dir, std::vector<std::wstring>& names) {
  DIR* d = opendir(narrowUtf8(dir).c_str());
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    std::wstring n = widenUtf8(e->d_name);
    struct stat st;
    if (stat(narrowUtf8(pathJoin(dir, n)).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) names.push_back(n);
  }
  closedir(d);
  return true;
}

long long timeNow() { return (long long)time(nullptr); }

std::string timeTextAt(long long when, const char* fmt) {
  time_t t = (time_t)when;
  tm lt;
  localtime_r(&t, &lt);
  char buf[64];
  if (strftime(buf, sizeof buf, fmt, &lt) == 0) buf[0] = 0;
  return buf;
}

std::string timeText(const char* fmt) { return timeTextAt(timeNow(), fmt); }
