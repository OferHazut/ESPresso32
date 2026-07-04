#pragma once

#include <cmath>

// Hysteresis state machine for a "ready" indicator: becomes ready once the
// temperature has stayed within +-kReadyBandC of the setpoint for at least
// kReadyHoldMs continuously, and stays ready until the temperature drifts
// beyond +-kOffBandC. The gap between the two bands is a dead zone that
// prevents flicker for temperatures that hover near the edge of the ready
// band.
class ReadyHysteresis {
 public:
  static constexpr float kReadyBandC = 1.0f;
  static constexpr float kOffBandC = 2.0f;
  static constexpr unsigned long kReadyHoldMs = 5000;

  // Feeds a new sample and returns the updated ready state. A NaN
  // temperature is treated as out of range: it resets the hold timer and
  // turns ready off.
  bool update(float filteredC, float setpointC, unsigned long nowMs) {
    if (std::isnan(filteredC)) {
      inBand_ = false;
      ready_ = false;
      return ready_;
    }

    const float diff = std::fabs(filteredC - setpointC);
    if (diff <= kReadyBandC) {
      if (!inBand_) {
        inBand_ = true;
        bandEnteredMs_ = nowMs;
      }
      if (nowMs - bandEnteredMs_ >= kReadyHoldMs) {
        ready_ = true;
      }
    } else {
      inBand_ = false;
      if (diff > kOffBandC) {
        ready_ = false;
      }
    }
    return ready_;
  }

  bool ready() const { return ready_; }

 private:
  bool ready_ = false;
  bool inBand_ = false;
  unsigned long bandEnteredMs_ = 0;
};
