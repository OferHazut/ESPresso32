#include "control/PidController.h"

#include <cstring>

PidController::DerivativeSource PidController::parseSource(const char* name) {
  if (name != nullptr && std::strcmp(name, "raw") == 0) return DerivativeSource::kRaw;
  return DerivativeSource::kFiltered;
}

const char* PidController::sourceName(DerivativeSource source) {
  return source == DerivativeSource::kFiltered ? "filtered" : "raw";
}

PidController::PidController(float kp, float ki, float kd, int derivativeN)
    : kp_(kp), ki_(ki), kd_(kd), integral_(0.0f), output_(0.0f),
      lastError_(0.0f), lastPTerm_(0.0f), lastDTerm_(0.0f),
      lastTimeMs_(0), initialized_(false), integratorEnabled_(true),
      derivativeSource_(DerivativeSource::kFiltered), derivativeFilter_(derivativeN),
      derivativeIntervalMs_(kDefaultDerivativeIntervalMs), lastDerivPushMs_(0),
      derivStarted_(false), lastDPerSec_(0.0f) {}

float PidController::updateDerivative(float derivInput, unsigned long nowMs) {
  // Decimate the derivative filter's input: pushing every call makes its
  // window span derivativeN sample periods, which can be too short to
  // outrun sensor noise on a slow-moving system. Spacing pushes by
  // derivativeIntervalMs_ stretches that window to derivativeN *
  // derivativeIntervalMs_. Holding lastDPerSec_ between pushes keeps the
  // slope/dTerm defined in the meantime.
  if (!derivStarted_ || nowMs - lastDerivPushMs_ >= derivativeIntervalMs_) {
    lastDPerSec_ = derivativeFilter_.push(derivInput, nowMs);
    lastDerivPushMs_ = nowMs;
    derivStarted_ = true;
  }
  return lastDPerSec_;
}

void PidController::updateDerivativeOnly(float filteredInput, float rawInput, unsigned long nowMs) {
  const float derivInput = (derivativeSource_ == DerivativeSource::kRaw) ? rawInput : filteredInput;
  updateDerivative(derivInput, nowMs);
}

float PidController::compute(float filteredInput, float rawInput, float setpoint, unsigned long nowMs) {
  const float derivInput = (derivativeSource_ == DerivativeSource::kRaw) ? rawInput : filteredInput;

  if (!initialized_) {
    lastTimeMs_ = nowMs;
    updateDerivative(derivInput, nowMs);
    initialized_ = true;
    return output_;
  }

  float dt = static_cast<float>(nowMs - lastTimeMs_) / 1000.0f;
  if (dt < 0.001f) return output_;
  if (dt > kMaxDtSec) dt = kMaxDtSec;
  lastTimeMs_ = nowMs;

  const float error = setpoint - filteredInput;
  lastError_ = error;
  lastPTerm_ = kp_ * error;

  // Integral with clamping anti-windup. Only accumulate if integratorEnabled.
  if (integratorEnabled_) {
    integral_ = std::clamp(integral_ + ki_ * error * dt, kOutputMin, kOutputMax);
  }

  const float dPerSec = updateDerivative(derivInput, nowMs);
  const float dTerm = -kd_ * dPerSec;
  lastDTerm_ = dTerm;

  const float raw = lastPTerm_ + integral_ + dTerm;
  output_ = std::clamp(raw, kOutputMin, kOutputMax);
  return output_;
}

void PidController::reset() {
  integral_ = 0.0f;
  output_ = 0.0f;
  initialized_ = false;
  lastDPerSec_ = 0.0f;
  derivStarted_ = false;
  derivativeFilter_.reset();
}

void PidController::setTunings(float kp, float ki, float kd) {
  kp_ = kp;
  ki_ = ki;
  kd_ = kd;
}
