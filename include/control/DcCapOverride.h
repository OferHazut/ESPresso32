#pragma once

#include <cmath>

// Tracks whether the temperature has been falling faster than
// thresholdCPerSec (a negative slope, deg C/sec) for at least holdMs
// continuously. While that holds, the PID's DC cap should be bypassed
// (full output allowed) to recover quickly from a fast drop, e.g. a flush.
// The cap re-activates immediately once the slope is no longer below
// threshold — no extra hold time on the way back up.
class DcCapOverride {
 public:
  static constexpr float kDefaultThresholdCPerSec = -1.0f;
  static constexpr unsigned long kDefaultHoldMs = 3000;

  explicit DcCapOverride(float thresholdCPerSec = kDefaultThresholdCPerSec,
                         unsigned long holdMs = kDefaultHoldMs)
      : thresholdCPerSec_(thresholdCPerSec), holdMs_(holdMs) {}

  // Feeds the latest temperature slope (deg C/sec) and returns true if the
  // DC cap should be bypassed. NaN is treated as "not below threshold".
  bool update(float slopeCPerSec, unsigned long nowMs) {
    if (std::isnan(slopeCPerSec) || slopeCPerSec >= thresholdCPerSec_) {
      below_ = false;
      bypass_ = false;
      return bypass_;
    }
    if (!below_) {
      below_ = true;
      belowSinceMs_ = nowMs;
    }
    if (nowMs - belowSinceMs_ >= holdMs_) {
      bypass_ = true;
    }
    return bypass_;
  }

  bool bypass() const { return bypass_; }

  float thresholdCPerSec() const { return thresholdCPerSec_; }
  void setThresholdCPerSec(float v) { thresholdCPerSec_ = v; }
  unsigned long holdMs() const { return holdMs_; }
  void setHoldMs(unsigned long ms) { holdMs_ = ms; }

 private:
  float thresholdCPerSec_;
  unsigned long holdMs_;
  bool below_ = false;
  bool bypass_ = false;
  unsigned long belowSinceMs_ = 0;
};
