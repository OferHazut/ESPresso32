#include "sensing/PressureSensor.h"

#include <cmath>

PressureSensor::PressureSensor(int adcPin,
                               unsigned long intervalMs,
                               int medianWindow,
                               float maxBar,
                               float dividerRatio)
    : BaseSensor(intervalMs, medianWindow),
      adcPin_(adcPin),
      maxBar_(maxBar),
      dividerRatio_(dividerRatio),
      lastVoltage_(NAN),
      hasReportedReady_(false),
      readCount_(0) {}

bool PressureSensor::begin() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // full 0–3.3 V range
  pinMode(adcPin_, INPUT);
  return true;
}

const PressureTelemetry& PressureSensor::telemetry() const { return latest_; }

float PressureSensor::adcToBar(int rawAdc) {
  // ADC value → voltage at ESP32 pin
  const float pinVoltage = (rawAdc / 4095.0f) * 3.3f;

  // Undo voltage divider to get actual sensor output
  const float sensorVoltage = pinVoltage / dividerRatio_;
  lastVoltage_ = sensorVoltage;

  // Linear map: 0.5 V → 0 bar, 4.5 V → maxBar
  const float bar = (sensorVoltage - kSensorMinV)
                    / (kSensorMaxV - kSensorMinV) * maxBar_;
  return bar;
}

bool PressureSensor::readReading(float& readingOut) {
  const int rawAdc = analogRead(adcPin_);
  const float bar = adcToBar(rawAdc);

  if (std::isnan(bar)) {
    latest_.valid = false;
    return false;
  }

  readingOut = bar;
  return true;
}

void PressureSensor::onReadSuccess(float reading,
                                   float median,
                                   int sampleCount,
                                   unsigned long timestampMs) {
  latest_.timestamp = timestampMs;
  latest_.bar = reading;
  latest_.medianBar = median;
  latest_.voltage = lastVoltage_;
  latest_.sampleCount = sampleCount;
  latest_.valid = true;

  if (!hasReportedReady_) {
    Serial.println("INFO: Pressure sensor read is working");
    hasReportedReady_ = true;
  }

  if (++readCount_ % 20 == 0) {
    Serial.printf("Pressure: %.2f bar | Median: %.2f bar | Sensor: %.3f V (%d samples)\n",
                  latest_.bar, latest_.medianBar, latest_.voltage, latest_.sampleCount);
  }
}

void PressureSensor::onReadFailure() {
  Serial.println("ERROR: Failed to read pressure sensor ADC");
}
