// A very small test harness (no dependencies): TEST(name) { ... } with
// CHECK / CHECK_EQ / REQUIRE, a temporary folder per test, and a runner that
// can list tests, run those whose name contains a word, and reports each
// failure with its file and line.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {
struct Case {
  const char* name;
  void (*fn)();
};
std::vector<Case>& cases();
struct Registrar {
  Registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};
void fail(const char* file, int line, const std::string& what);
struct Abort {};  // thrown by REQUIRE: the rest of the test can't go on
int failures();
int checks();
void countCheck();

template <typename T>
std::string show(const T& v) {
  std::ostringstream o;
  o << v;
  return o.str();
}
inline std::string show(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s += c < 128 ? (char)c : '?';
  return s;
}
inline std::string show(bool b) { return b ? "true" : "false"; }
inline std::string show(uint8_t v) { return std::to_string((int)v); }

// A new empty folder (under the system's temporary one), removed with everything in it afterwards.
std::wstring tempDir();
}  // namespace check

#define TEST(name)                                              \
  static void test_##name();                                    \
  static check::Registrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    check::countCheck();                                               \
    if (!(cond)) check::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_MSG(cond, msg)                                                                      \
  do {                                                                                            \
    check::countCheck();                                                                          \
    if (!(cond)) check::fail(__FILE__, __LINE__, std::string("CHECK(" #cond "): ") + (msg));     \
  } while (0)

#define CHECK_EQ(a, b)                                                                                              \
  do {                                                                                                              \
    check::countCheck();                                                                                            \
    const auto va_ = (a);                                                                                           \
    const auto vb_ = (b);                                                                                           \
    if (!(va_ == vb_))                                                                                              \
      check::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b "): " + check::show(va_) + " != " + check::show(vb_)); \
  } while (0)

#define REQUIRE(cond)                                               \
  do {                                                              \
    check::countCheck();                                            \
    if (!(cond)) {                                                  \
      check::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")");        \
      throw check::Abort();                                         \
    }                                                               \
  } while (0)
