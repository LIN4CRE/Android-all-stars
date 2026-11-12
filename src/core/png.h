// Minimal PNG writer (uncompressed deflate) for screenshots and debugging.
#pragma once
#include <cstdint>
#include <string>

// pixels: 0x00RRGGBB, `stride` in pixels
bool writePng(const std::string& path, const uint32_t* pixels, int w, int h, int stride);
