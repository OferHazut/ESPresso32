#pragma once

#include <Arduino.h>

struct TemperatureTelemetry {
  unsigned long timestamp = 0;
  float probeC = NAN;
  float internalC = NAN;
  float compensatedC = NAN;
  float medianC = NAN;
  float filteredC = NAN;  // EMA-smoothed physical estimate
  int sampleCount = 0;
  bool valid = false;
  uint8_t fault = 0;
};
