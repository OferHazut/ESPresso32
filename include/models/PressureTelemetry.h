#pragma once

#include <Arduino.h>

struct PressureTelemetry {
  unsigned long timestamp = 0;
  float bar = NAN;       // pressure in bar
  float medianBar = NAN; // median-filtered pressure
  float voltage = NAN;   // sensor voltage (before divider)
  int sampleCount = 0;
  bool valid = false;
};
