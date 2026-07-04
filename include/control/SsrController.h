#pragma once

#include <Arduino.h>

#include "control/DcCapOverride.h"
#include "control/PidController.h"

class SsrController {
 public:
  static constexpr int kDefaultPin = 10;
  // 105°C boiler setpoint — targets ~92-95°C at the puck after the typical
  // 8-12°C boiler-to-grouphead drop (see brew-temperature notes).
  static constexpr float kDefaultSetpointC = 105.0f;
  // 3000ms balances SSR/heating-element longevity (fewer switching cycles)
  // against temperature ripple — see PID tuning notes for the trade-off math.
  static constexpr unsigned long kDefaultWindowMs = 3000;
  // Temperature threshold: when temp < setpoint - kTempThresholdC, PID is off and DC = 100%
  // Also used for integral: only integrate when temp is within ±kIntegratorRangeC of setpoint
  static constexpr float kTempThresholdC = 20.0f;
  static constexpr float kIntegratorRangeC = 5.0f;
  static constexpr float kDefaultDcCapPercent = 50.0f;

  explicit SsrController(int pin = kDefaultPin,
                         float setpointC = kDefaultSetpointC,
                         unsigned long windowMs = kDefaultWindowMs,
                         float kp = PidController::kDefaultKp,
                         float ki = PidController::kDefaultKi,
                         float kd = PidController::kDefaultKd,
                         int derivativeN = PidController::kDefaultDerivativeN);

  void begin();
  // `filteredTempC`/`rawTempC` are the latest median+slew-filtered and raw
  // probe readings; `hasNewSample` is true only on loop iterations where a
  // fresh sample arrived (PID compute is gated on this). `nowMs` is millis().
  // NaN `filteredTempC` forces the SSR off immediately, every loop.
  void update(float filteredTempC, float rawTempC, bool hasNewSample, unsigned long nowMs);

  bool isOn() const { return on_; }
  float setpoint() const { return setpointC_; }
  float pidOutput() const { return pid_.output(); }
  void setSetpoint(float c) { setpointC_ = c; }

  float kp() const { return pid_.kp(); }
  float ki() const { return pid_.ki(); }
  float kd() const { return pid_.kd(); }
  void setPidTunings(float kp, float ki, float kd) {
    pid_.setTunings(kp, ki, kd);
    pid_.reset();
  }
  // Clears the integrator/derivative memory without changing tunings — use
  // when the integral has wound up (e.g. after a long cold-start or a big
  // setpoint jump) and is holding the output near 100% long after the
  // temperature caught up. Ki being small makes that wind-up slow to clear
  // on its own; this gives an immediate way out.
  void resetPid() { pid_.reset(); }

  unsigned long windowMs() const { return windowMs_; }
  void setWindowMs(unsigned long ms) { windowMs_ = ms; }

  int derivativeN() const { return pid_.derivativeN(); }
  void setDerivativeN(int n) { pid_.setDerivativeN(n); }
  PidController::DerivativeSource derivativeSource() const { return pid_.derivativeSource(); }
  void setDerivativeSource(PidController::DerivativeSource source) { pid_.setDerivativeSource(source); }
  unsigned long derivativeIntervalMs() const { return pid_.derivativeIntervalMs(); }
  void setDerivativeIntervalMs(unsigned long ms) { pid_.setDerivativeIntervalMs(ms); }

  // Check if PID is enabled (temp >= setpoint - kTempThresholdC)
  bool isPidEnabled() const { return pidEnabled_; }
  // Check if integrator is active (temp within ±kIntegratorRangeC of setpoint)
  bool isIntegratorEnabled() const { return pid_.isIntegratorEnabled(); }

  float dcCapPercent() const { return dcCapPercent_; }
  void setDcCapPercent(float cap) { dcCapPercent_ = cap; }

  // While the temperature has been falling faster than
  // dcCapDisableSlopeCPerSec (deg C/sec, negative) for at least
  // dcCapDisableHoldMs, the DC cap is bypassed (full output allowed) so the
  // PID can recover quickly from a fast drop (e.g. a flush).
  float dcCapDisableSlopeCPerSec() const { return dcCapOverride_.thresholdCPerSec(); }
  void setDcCapDisableSlopeCPerSec(float v) { dcCapOverride_.setThresholdCPerSec(v); }
  unsigned long dcCapDisableHoldMs() const { return dcCapOverride_.holdMs(); }
  void setDcCapDisableHoldMs(unsigned long ms) { dcCapOverride_.setHoldMs(ms); }
  bool isDcCapBypassed() const { return dcCapOverride_.bypass(); }

  // Current temperature slope feeding the D term (deg C/sec, positive = warming).
  float pidSlopeCPerSec() const { return pid_.derivativePerSec(); }

  // PID diagnostics: error feeding the integral, and each term's
  // contribution to the output (duty-percent units).
  float pidError() const { return pid_.error(); }
  float pidPTerm() const { return pid_.pTerm(); }
  float pidITerm() const { return pid_.iTerm(); }
  float pidDTerm() const { return pid_.dTerm(); }

 private:
  int pin_;
  float setpointC_;
  unsigned long windowMs_;
  bool on_ = false;
  unsigned long windowStartMs_ = 0;
  // On-time for the current window, sampled once when the window starts and
  // held fixed for its whole duration — see update() for why this matters.
  unsigned long lockedOnTimeMs_ = 0;
  // millis() of the last on/off transition — used to log actual elapsed
  // cycle time alongside the expected/locked values for diagnostics.
  unsigned long lastTransitionMs_ = 0;
  PidController pid_;
  bool pidEnabled_ = false;
  float dcCapPercent_ = kDefaultDcCapPercent;
  DcCapOverride dcCapOverride_;
};
