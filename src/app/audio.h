// XAudio2 output with resampling (32040 Hz -> device rate) and dynamic rate
// control to keep latency stable while video drives the frame pacing.
#pragma once
#ifndef _WIN32
#include "platform/android/audio_sdl.h"
#else
#include <cstdint>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

class Audio {
public:
  bool init(int latencyMs);
  void shutdown();
  // Push interleaved stereo samples at the SNES rate.
  void push(const int16_t* samples, size_t frames, double inputRate);
  void setVolume(float v);
  void setLatency(int ms) { latencyMs_ = ms; }
  void flush();
  int queuedFrames() const;
  bool ok() const { return voice_ != nullptr; }
  // Recent output level (for UI meters), 0..1
  float level() const { return level_; }
  // Everything pushed, as it comes (the recording mode's sound: see autotest_win.cpp).
  void (*tap)(void* ctx, const int16_t* samples, size_t frames) = nullptr;
  void* tapCtx = nullptr;

private:
  IXAudio2* xa_ = nullptr;
  IXAudio2MasteringVoice* master_ = nullptr;
  IXAudio2SourceVoice* voice_ = nullptr;
  int rate_ = 48000;
  int latencyMs_ = 64;
  static constexpr int kBuffers = 16;
  std::vector<int16_t> bufs_[kBuffers];
  int nextBuf_ = 0;
  uint64_t submitted_ = 0;
  double pos_ = 0.0;         // fractional read position into history
  int16_t hist_[4][2] = {};  // last 4 input frames for cubic interpolation
  std::vector<int16_t> out_;
  float level_ = 0;
  float volume_ = 1.0f;
};
#endif
