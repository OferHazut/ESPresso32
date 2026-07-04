#pragma once

#include "control/ReadyHysteresis.h"

// Drives a "ready" LED: on once the temperature has settled within
// ReadyHysteresis::kReadyBandC of the setpoint for
// ReadyHysteresis::kReadyHoldMs, off again once it drifts beyond
// ReadyHysteresis::kOffBandC. See ReadyHysteresis for the hysteresis logic.
class ReadyIndicator {
 public:
  // GPIO2 — free on this board (clk/cs/do = 11/12/13, SSR = 10, pressure ADC = 4).
  static constexpr int kDefaultPin = 2;

  explicit ReadyIndicator(int pin = kDefaultPin);

  void begin();

  // Call on each loop with the latest filtered temperature, setpoint, and
  // millis(). Drives the LED pin directly.
  void update(float filteredC, float setpointC, unsigned long nowMs);

  bool isOn() const { return hysteresis_.ready(); }

 private:
  int pin_;
  ReadyHysteresis hysteresis_;
};
