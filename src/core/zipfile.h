// A small, strict reader and writer of .zip archives, for save backups: a
// backup is an ordinary zip the player can open on any computer.
//
// Written: entries stored as they are (no compression: saves are small),
// each with its CRC-32. Read: stored or deflated entries (a backup zipped
// again by a file manager still restores), every offset and length checked
// against the file, every entry checked against its CRC-32, with limits on
// how many entries and how many bytes are accepted. Encrypted, split and
// zip64 archives are refused. Entry names are data: nothing here touches the
// file system.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ZipEntry {
  std::string name;  // UTF-8, '/' separated
  std::vector<uint8_t> data;
  long long time = 0;  // last modified, seconds since 1970 (0: now)
};

struct ZipLimits {
  int maxEntries = 64;
  size_t maxEntryBytes = 8u << 20;
  size_t maxTotalBytes = 48u << 20;
};

std::vector<uint8_t> zipWrite(const std::vector<ZipEntry>& entries);
bool zipRead(const std::vector<uint8_t>& file, std::vector<ZipEntry>* entries, std::string* err,
             const ZipLimits& limits = ZipLimits());

// Raw "deflate" data (as in a zip) into `out`, which must come out exactly
// `outLen` bytes long. (The decoder is stb_image's: see app/imageload.cpp.)
bool inflateRaw(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);
