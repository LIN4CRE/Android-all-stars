// A small OS layer for the app: paths, files and folders, links. Paths are
// std::wstring everywhere (UTF-8 when a file is opened): sys_posix.cpp.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

std::wstring pathJoin(const std::wstring& dir, const std::wstring& name);
std::wstring pathDir(const std::wstring& path);   // all but the last part ("." if none)
std::wstring pathName(const std::wstring& path);  // the last part

FILE* openFile(const std::wstring& path, const char* mode);
bool readFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxBytes = 64u << 20);
bool readText(const std::wstring& path, std::string& out);
bool fileExists(const std::wstring& path);
// When it was last written, local time (false if it doesn't exist).
bool fileModified(const std::wstring& path, int* day, int* month, int* hour, int* minute);
void makeDir(const std::wstring& path);
// Its size in bytes (-1 if it doesn't exist), and when it was last written (seconds since 1970; 0 if it doesn't).
long long fileSize(const std::wstring& path);
long long fileTime(const std::wstring& path);
// Removes a file (true if it's gone, or wasn't there); renames one over another.
bool removeFile(const std::wstring& path);
bool renameFile(const std::wstring& from, const std::wstring& to);
// The storage left for new files in a folder, in bytes; false if it can't be told.
bool diskFree(const std::wstring& dir, unsigned long long* bytes);
// The names of the files in a folder (not folders); false if it can't be read.
bool listFiles(const std::wstring& dir, std::vector<std::wstring>& names);
// ... and of the folders in it.
bool listDirs(const std::wstring& dir, std::vector<std::wstring>& names);

// Local time now, strftime-formatted.
std::string timeText(const char* fmt);

// The time now, seconds since 1970 (UTC); and a time as local text, strftime-formatted.
long long timeNow();
std::string timeTextAt(long long t, const char* fmt);
