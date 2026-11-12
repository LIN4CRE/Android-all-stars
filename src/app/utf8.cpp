#include "app/canvas.h"

std::wstring widenUtf8(const std::string& s) {
  std::wstring w;
  w.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint8_t c = (uint8_t)s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) cp = c, n = 1;
    else if ((c & 0xe0) == 0xc0) cp = c & 0x1f, n = 2;
    else if ((c & 0xf0) == 0xe0) cp = c & 0x0f, n = 3;
    else if ((c & 0xf8) == 0xf0) cp = c & 0x07, n = 4;
    else cp = 0xfffd, n = 1;
    if (i + n > s.size()) {
      cp = 0xfffd;
      n = (int)(s.size() - i);
    } else {
      for (int k = 1; k < n; k++) cp = cp << 6 | ((uint8_t)s[i + k] & 0x3f);
    }
    i += n;
    if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
      cp -= 0x10000;
      w += (wchar_t)(0xd800 + (cp >> 10));
      w += (wchar_t)(0xdc00 + (cp & 0x3ff));
    } else {
      w += (wchar_t)cp;
    }
  }
  return w;
}

std::string narrowUtf8(const std::wstring& w) {
  std::string s;
  s.reserve(w.size());
  for (size_t i = 0; i < w.size(); i++) {
    uint32_t cp = (uint32_t)w[i];
    if (sizeof(wchar_t) == 2 && cp >= 0xd800 && cp < 0xdc00 && i + 1 < w.size()) {
      uint32_t lo = (uint32_t)w[i + 1];
      if (lo >= 0xdc00 && lo < 0xe000) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
        i++;
      }
    }
    if (cp < 0x80) {
      s += (char)cp;
    } else if (cp < 0x800) {
      s += (char)(0xc0 | cp >> 6);
      s += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      s += (char)(0xe0 | cp >> 12);
      s += (char)(0x80 | ((cp >> 6) & 0x3f));
      s += (char)(0x80 | (cp & 0x3f));
    } else {
      s += (char)(0xf0 | cp >> 18);
      s += (char)(0x80 | ((cp >> 12) & 0x3f));
      s += (char)(0x80 | ((cp >> 6) & 0x3f));
      s += (char)(0x80 | (cp & 0x3f));
    }
  }
  return s;
}
