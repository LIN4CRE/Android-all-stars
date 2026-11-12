// The test runner: see check.h.
//   smas_tests            run everything
//   smas_tests word...    run the tests whose name contains any of the words
//   smas_tests --list     list the tests
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include "check.h"

namespace check {
namespace {
int g_failures = 0, g_checks = 0;
std::vector<std::string> g_temp;

void removeTree(const std::string& path) {
  if (DIR* d = opendir(path.c_str())) {
    while (dirent* e = readdir(d)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      const std::string p = path + "/" + e->d_name;
      struct stat st {};
      if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) removeTree(p);
      else unlink(p.c_str());
    }
    closedir(d);
  }
  rmdir(path.c_str());
}
}  // namespace

std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}
void fail(const char* file, int line, const std::string& what) {
  g_failures++;
  fprintf(stderr, "    FAILED %s:%d: %s\n", file, line, what.c_str());
}
int failures() { return g_failures; }
int checks() { return g_checks; }
void countCheck() { g_checks++; }

std::wstring tempDir() {
  const char* base = getenv("TMPDIR");
  std::string tmpl = std::string(base && *base ? base : "/tmp") + "/smas-test-XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back(0);
  if (!mkdtemp(buf.data())) {
    perror("mkdtemp");
    exit(2);
  }
  g_temp.push_back(buf.data());
  return std::wstring(buf.data(), buf.data() + strlen(buf.data()));
}

void cleanTemp() {
  for (const std::string& t : g_temp) removeTree(t);
  g_temp.clear();
}
}  // namespace check

int main(int argc, char** argv) {
  std::vector<std::string> words;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--list")) {
      for (const check::Case& c : check::cases()) printf("%s\n", c.name);
      return 0;
    }
    words.push_back(argv[i]);
  }
  int ran = 0, failed = 0;
  for (const check::Case& c : check::cases()) {
    bool want = words.empty();
    for (const std::string& w : words) want = want || strstr(c.name, w.c_str()) != nullptr;
    if (!want) continue;
    const int before = check::failures();
    printf("[ RUN  ] %s\n", c.name);
    fflush(stdout);
    try {
      c.fn();
    } catch (const check::Abort&) {
    } catch (const std::exception& e) {
      check::fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
    } catch (...) {
      check::fail(__FILE__, __LINE__, "unknown exception");
    }
    check::cleanTemp();
    ran++;
    if (check::failures() != before) {
      failed++;
      printf("[ FAIL ] %s\n", c.name);
    } else {
      printf("[  OK  ] %s\n", c.name);
    }
    fflush(stdout);
  }
  printf("\n%d test(s) run, %d failed, %d check(s)\n", ran, failed, check::checks());
  if (ran == 0) {
    fprintf(stderr, "no test matched\n");
    return 2;
  }
  return failed ? 1 : 0;
}
