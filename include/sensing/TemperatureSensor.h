#pragma once

#include <MAX31855.h>
#include <Arduino.h>

#include "models/TemperatureTelemetry.h"
#include "sensing/SensingBase.h"

class TemperatureSensor : public BaseSensor {
 public:
  static constexpr int kDefaultClkPin = 11;
  static constexpr int kDefaultCsPin = 12;
  static constexpr int kDefaultDoPin = 13;
  static constexpr unsigned long kDefaultSampleIntervalMs = 100;  // matches MAX31855 max conversion time — no duplicate reads
  static constexpr int kDefaultMedianWindow = 5;                  // 5 × 100 ms = 500 ms history
  static constexpr unsigned long kDefaultTelemetryIntervalMs = 250;
  // Time constant (seconds) of the exponential moving average applied to the
  // median. Smooths the ~0.25-0.5 deg C quantization/noise on the raw probe
  // reading into a continuous value, at the cost of this much lag during
  // fast transients (e.g. a flush dropping ~3 deg C/sec).
  static constexpr float kEmaTimeConstantSec = 1.5f;
  // Minimum samples before the rate-limited filter is seeded.
  // Ensures the seed comes from a proper median, not a single transient.
  static constexpr int kMinSeedSamples = 5;
  // Plausible seed range: room temp through max operating temp.
  // Readings outside this range are treated as startup noise.
  static constexpr float kSeedMinC = -10.0f;
  static constexpr float kSeedMaxC = 160.0f;

  TemperatureSensor(
      int clkPin = kDefaultClkPin,
      int csPin = kDefaultCsPin,
      int doPin = kDefaultDoPin,
      unsigned long intervalMs = kDefaultSampleIntervalMs,
      int medianWindow = kDefaultMedianWindow);

  bool begin();
  const TemperatureTelemetry& telemetry() const;

  // Updated every sample (at sampleIntervalMs() cadence), unlike telemetry()
  // which is decimated to kDefaultTelemetryIntervalMs. Used to drive the PID
  // at full sample rate.
  float lastFilteredC() const { return filteredC_; }
  float lastProbeC() const { return lastProbeC_; }

 protected:
  bool readReading(float& readingOut) override;
  void onReadSuccess(float reading,
                     float median,
                     int sampleCount,
                     unsigned long timestampMs) override;
  void onReadFailure() override;

 private:
  MAX31855 thermocouple_;
  SamplingTimer telemetryTimer_;
  TemperatureTelemetry latest_;
  float lastProbeC_;
  float lastInternalC_;
  float filteredC_;          // EMA-smoothed running estimate
  unsigned long lastFilterMs_;
  bool hasReportedProbeReady_;
  int readCount_;
};
