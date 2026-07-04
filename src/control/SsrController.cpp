#include "control/SsrController.h"

#include <cmath>

SsrController::SsrController(int pin, float setpointC, unsigned long windowMs,
                               float kp, float ki, float kd, int derivativeN)
    : pin_(pin), setpointC_(setpointC), windowMs_(windowMs),
      on_(false), windowStartMs_(0), pid_(kp, ki, kd, derivativeN),
      pidEnabled_(false), dcCapPercent_(kDefaultDcCapPercent) {}
void SsrController::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
  windowStartMs_ = millis();
  lockedOnTimeMs_ = 0;
  lastTransitionMs_ = windowStartMs_;
}

void SsrController::update(float filteredTempC, float rawTempC, bool hasNewSample, unsigned long nowMs) {
  if (std::isnan(filteredTempC)) {
    if (on_) {
      on_ = false;
      digitalWrite(pin_, LOW);
    }
    return;
  }

  const unsigned long now = nowMs;

  // Determine if PID should be enabled: when temp >= setpoint - kTempThresholdC
  // When PID is off (temp < setpoint - 20°C), use 100% DC
  const float tempThreshold = setpointC_ - kTempThresholdC;
  pidEnabled_ = (filteredTempC >= tempThreshold);

  float duty;
  if (!pidEnabled_) {
    // PID disabled: use 100% DC directly
    duty = 100.0f;
    // Reset PID state when disabled to avoid integrator windup
    pid_.resetIntegrator();
    pid_.setIntegratorEnabled(false);
    // Keep the derivative filter warm so the D term/slope readout aren't
    // starting cold (empty window) once the PID re-enables.
    if (hasNewSample) {
      pid_.updateDerivativeOnly(filteredTempC, rawTempC, now);
    }
  } else {
    // PID enabled: compute the control output on fresh samples only; otherwise
    // hold the last output (the window/lock logic below only samples this once
    // per windowMs_ anyway).
    duty = hasNewSample ? pid_.compute(filteredTempC, rawTempC, setpointC_, now) : pid_.output();

    // Determine if integrator should be active: only when temp is within ±5°C of setpoint
    const float integratorMinTemp = setpointC_ - kIntegratorRangeC;
    const float integratorMaxTemp = setpointC_ + kIntegratorRangeC;
    const bool shouldIntegrate = (filteredTempC >= integratorMinTemp && filteredTempC <= integratorMaxTemp);

    if (shouldIntegrate != pid_.isIntegratorEnabled()) {
      pid_.setIntegratorEnabled(shouldIntegrate);
      if (!shouldIntegrate) {
        // When exiting integrator range, reset the integrator to prevent windup
        pid_.resetIntegrator();
      }
    }

    // Cap duty cycle to dcCapPercent_ when PID is on, unless the temperature
    // has been falling fast enough for long enough to warrant a quick
    // recovery (e.g. after a flush) — see DcCapOverride.
    const bool bypassCap = dcCapOverride_.update(pid_.derivativePerSec(), now);
    if (!bypassCap && duty > dcCapPercent_) {
      duty = dcCapPercent_;
    }
  }

  // Guard against NaN/Inf reaching the cast below: casting a non-finite
  // float to unsigned long is undefined behavior (can yield a huge
  // lockedOnTimeMs_, locking the SSR on indefinitely, or fault on some
  // platforms). Fail safe to 0% rather than propagate a bad value.
  if (!std::isfinite(duty)) {
    Serial.println("ERROR: PID output is not finite; forcing duty to 0");
    duty = 0.0f;
    pid_.reset();
  }

  // Roll over to a new window when the current one expires, and lock the
  // on-time for that window at this instant. Locking is essential: the PID
  // output drifts slightly on every call, and comparing `elapsed` against a
  // continuously-recomputed threshold for the whole window causes the SSR to
  // toggle on/off multiple times per window instead of exactly once. Sampling
  // once per window and holding it fixed is standard time-proportional control.
  if (now - windowStartMs_ >= windowMs_) {
    windowStartMs_ += windowMs_;
    // Catch up if we fell behind (e.g. long delay).
    if (now - windowStartMs_ >= windowMs_) windowStartMs_ = now;
    lockedOnTimeMs_ = static_cast<unsigned long>(duty * static_cast<float>(windowMs_) / 100.0f);
  }

  const unsigned long elapsed = now - windowStartMs_;
  const bool shouldBeOn = (elapsed < lockedOnTimeMs_);

  if (shouldBeOn != on_) {
    on_ = shouldBeOn;
    const int expected = on_ ? HIGH : LOW;
    digitalWrite(pin_, expected);

    // Verify the GPIO actually reflects what we just commanded, and log the
    // cycle timing — period length, locked on-time, and how long the
    // previous state actually lasted vs. what was expected. This is the
    // direct way to tell a software/timing issue (duty>0 but the locked
    // on-time rounded too small to survive a polling gap — see the minimum
    // on-time discussion) apart from a hardware/wiring problem (pin commanded
    // but never actually changes).
    const int actual = digitalRead(pin_);
    Serial.printf("SSR -> %s | window+%lums, locked %lu/%lums (period), prev state held %lums%s\n",
                  on_ ? "ON" : "OFF", elapsed, lockedOnTimeMs_, windowMs_,
                  now - lastTransitionMs_,
                  (actual != expected) ? "  <-- PIN MISMATCH: wrote != read, check wiring/driver" : "");
    lastTransitionMs_ = now;
  }
}
