// Writing files without losing the previous version on failure.
#pragma once
#include <cstddef>
#include <string>

// Writes `n` bytes to `path` through a temporary file that replaces it only
// once all of it has been written: a failed write (disk full, drive removed)
// leaves any previous file as it was. False on any failure.
bool writeFileSafely(const std::wstring& path, const void* data, size_t n);
// `path` in UTF-8 (on Windows, the ANSI code page if it isn't valid UTF-8).
bool writeFileSafely(const std::string& path, const void* data, size_t n);
