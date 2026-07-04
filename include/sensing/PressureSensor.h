#pragma once

#include <Arduino.h>

#include "models/PressureTelemetry.h"
#include "sensing/SensingBase.h"

class PressureSensor : public BaseSensor {
 public:
  static constexpr int kDefaultAdcPin = 4;  // ADC1_CH3 on ESP32-S3
  static constexpr unsigned long kDefaultSampleIntervalMs = 50;  // 20 Hz
  static constexpr int kDefaultMedianWindow = 10;

  // Transducer: 0.5–4.5 V maps to 0–maxBar (before voltage divider)
  static constexpr float kSensorMinV = 0.5f;
  static constexpr float kSensorMaxV = 4.5f;
  static constexpr float kDefaultMaxBar = 16.0f;  // 1.6 MPa

  // Voltage divider scales 5 V → 3.3 V
  static constexpr float kDefaultDividerRatio = 3.3f / 5.0f;

  PressureSensor(int adcPin = kDefaultAdcPin,
                 unsigned long intervalMs = kDefaultSampleIntervalMs,
                 int medianWindow = kDefaultMedianWindow,
                 float maxBar = kDefaultMaxBar,
                 float dividerRatio = kDefaultDividerRatio);

  bool begin();
  const PressureTelemetry& telemetry() const;

 protected:
  bool readReading(float& readingOut) override;
  void onReadSuccess(float reading,
                     float median,
                     int sampleCount,
                     unsigned long timestampMs) override;
  void onReadFailure() override;

 private:
  float adcToBar(int rawAdc);

  int adcPin_;
  float maxBar_;
  float dividerRatio_;
  PressureTelemetry latest_;
  float lastVoltage_;
  bool hasReportedReady_;
  int readCount_;
};
