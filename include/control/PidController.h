#pragma once

#include <algorithm>

#include "control/LinearDerivativeFilter.h"

class PidController {
 public:
  static constexpr float kDefaultKp = 4.0f;
  static constexpr float kDefaultKi = 0.02f;
  static constexpr float kDefaultKd = 50.0f;
  static constexpr float kOutputMin = 0.0f;
  static constexpr float kOutputMax = 100.0f;
  static constexpr int kDefaultDerivativeN = LinearDerivativeFilter::kDefaultN;
  static constexpr int kMaxDerivativeN = LinearDerivativeFilter::kMaxN;
  // Caps the time delta used for integral/derivative math. Bounds the
  // one-shot integral jump after any stall (sensor fault, blocking flash
  // write, etc.) to ki*error*kMaxDtSec instead of an unbounded value.
  static constexpr float kMaxDtSec = 2.0f;
  // Minimum spacing between samples fed to the derivative filter. 0 means
  // push on every compute() call (legacy behavior). A slow-moving system
  // (e.g. an espresso boiler at <1 deg C/sec) benefits from decimating to
  // ~1000ms: combined with derivativeN, this stretches the regression window
  // to derivativeN seconds, averaging out sensor noise/quantization that a
  // sample-rate window would chase.
  static constexpr unsigned long kDefaultDerivativeIntervalMs = 0;

  // Which signal feeds the derivative term: the raw probe reading, or the
  // median+slew-filtered one. The error driving P/I always uses the
  // filtered signal regardless of this setting.
  enum class DerivativeSource { kRaw, kFiltered };

  // "raw"/"filtered" -> DerivativeSource; anything else (including nullptr)
  // defaults to kFiltered.
  static DerivativeSource parseSource(const char* name);
  static const char* sourceName(DerivativeSource source);

  explicit PidController(float kp = kDefaultKp, float ki = kDefaultKi, float kd = kDefaultKd,
                         int derivativeN = kDefaultDerivativeN);

  // Returns output duty cycle [0, 100]. Call on each new temperature sample.
  // `filteredInput` drives the P and I terms (error = setpoint -
  // filteredInput); the D term is computed from `rawInput` or
  // `filteredInput` depending on derivativeSource().
  float compute(float filteredInput, float rawInput, float setpoint, unsigned long nowMs);

  // Feeds the derivative filter without computing P/I/output — call this on
  // each new sample while the PID is disabled so derivativePerSec() and the
  // filter's window stay warm/up to date, and compute() doesn't start cold
  // (with an empty window) once the PID re-enables.
  void updateDerivativeOnly(float filteredInput, float rawInput, unsigned long nowMs);

  void reset();

  // Control integrator behavior: set whether integrator should accumulate or reset
  void setIntegratorEnabled(bool enabled) { integratorEnabled_ = enabled; }
  bool isIntegratorEnabled() const { return integratorEnabled_; }
  // Reset integrator only (used when disabling integrator)
  void resetIntegrator() { integral_ = 0.0f; }

  float kp() const { return kp_; }
  float ki() const { return ki_; }
  float kd() const { return kd_; }
  float output() const { return output_; }
  int derivativeN() const { return derivativeFilter_.n(); }
  void setDerivativeN(int n) { derivativeFilter_.setN(n); }
  DerivativeSource derivativeSource() const { return derivativeSource_; }
  void setDerivativeSource(DerivativeSource source) { derivativeSource_ = source; }
  unsigned long derivativeIntervalMs() const { return derivativeIntervalMs_; }
  void setDerivativeIntervalMs(unsigned long ms) { derivativeIntervalMs_ = ms; }

  // Diagnostics from the most recent compute(): the raw error (the signal
  // accumulated into the integral) and each term's contribution to the
  // output (all in duty-percent units, same scale as output()). pTerm+iTerm
  // +dTerm == output() (modulo the final clamp to [kOutputMin, kOutputMax]).
  float error() const { return lastError_; }
  float pTerm() const { return lastPTerm_; }
  float iTerm() const { return integral_; }
  float dTerm() const { return lastDTerm_; }
  // Raw temperature slope (deg C/sec, positive = warming) feeding the D
  // term, i.e. dTerm() == -kd_ * derivativePerSec(). Held between
  // derivative-filter pushes, same as dTerm().
  float derivativePerSec() const { return lastDPerSec_; }

  void setTunings(float kp, float ki, float kd);

 private:
  // Decimates pushes to derivativeFilter_ by derivativeIntervalMs_, updating
  // lastDPerSec_/lastDerivPushMs_. Returns the held or freshly-computed
  // slope. Shared by compute() and updateDerivativeOnly().
  float updateDerivative(float derivInput, unsigned long nowMs);

  float kp_, ki_, kd_;
  float integral_;
  float output_;
  float lastError_;
  float lastPTerm_;
  float lastDTerm_;
  unsigned long lastTimeMs_;
  bool initialized_;
  bool integratorEnabled_;
  DerivativeSource derivativeSource_;
  LinearDerivativeFilter derivativeFilter_;
  unsigned long derivativeIntervalMs_;
  unsigned long lastDerivPushMs_;
  bool derivStarted_;
  float lastDPerSec_;
};
