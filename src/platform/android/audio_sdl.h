// Sound on Android: SDL's audio queue, with the same resampling (32040 Hz ->
// the device's rate) and latency control as the Windows output (app/audio.h).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class Audio {
public:
  bool init(int latencyMs);
  void shutdown();
  // Push interleaved stereo samples at the SNES rate.
  void push(const int16_t* samples, size_t frames, double inputRate);
  void setVolume(float v) { volume_ = v; }
  void setLatency(int ms) { latencyMs_ = ms; }
  void flush();
  void setPaused(bool paused);
  int queuedFrames() const;
  bool ok() const { return dev_ != 0; }
  float level() const { return level_; }
  int deviceFrames() const { return deviceFrames_; }
  int rate() const { return rate_; }
  // (for the log: times the queue ran dry, and its lowest level, since the last call)
  void takeStats(int* underruns, int* minQueued) {
    *underruns = underruns_;
    *minQueued = minQueued_ == 1 << 30 ? 0 : minQueued_;
    underruns_ = 0;
    minQueued_ = 1 << 30;
  }

private:
  uint32_t dev_ = 0;
  int rate_ = 48000;
  int latencyMs_ = 64;
  int deviceFrames_ = 512;
  int underruns_ = 0, minQueued_ = 1 << 30;
  double pos_ = 0.0;
  int16_t hist_[4][2] = {};
  std::vector<int16_t> out_;
  float level_ = 0;
  float volume_ = 1.0f;
};
