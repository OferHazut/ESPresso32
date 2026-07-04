#include "sensing/TemperatureSensor.h"

#include <cmath>

TemperatureSensor::TemperatureSensor(int clkPin,
                                     int csPin,
                                     int doPin,
                                     unsigned long intervalMs,
                                     int medianWindow)
    : BaseSensor(intervalMs, medianWindow),
      thermocouple_(csPin, doPin, clkPin),  // Rob Tillaart SW SPI: (select, miso, clock)
      telemetryTimer_(kDefaultTelemetryIntervalMs),
      lastProbeC_(NAN),
      lastInternalC_(NAN),
      filteredC_(NAN),
      lastFilterMs_(0),
      hasReportedProbeReady_(false),
      readCount_(0) {}

bool TemperatureSensor::begin() {
  thermocouple_.begin();

  return true;
}

const TemperatureTelemetry& TemperatureSensor::telemetry() const { return latest_; }

bool TemperatureSensor::readReading(float& readingOut) {
  // Single SPI read — probe, internal, and status all come from one transaction
  const uint8_t status = thermocouple_.read();

  if (status != STATUS_OK) {
    latest_.fault = status;
    latest_.valid = false;
    return false;
  }

  // getTemperature() returns cold-junction-compensated probe temp (from buffered read)
  const float probeC = thermocouple_.getTemperature();
  const float internalC = thermocouple_.getInternal();

  if (std::isnan(probeC)) {
    latest_.valid = false;
    return false;
  }

  lastProbeC_ = probeC;
  lastInternalC_ = internalC;
  readingOut = probeC;
  return true;
}

void TemperatureSensor::onReadSuccess(float reading,
                                      float median,
                                      int sampleCount,
                                      unsigned long timestampMs) {
  // Always update the rate-limited filter at full sample rate (100 Hz).
  // Small dt per step keeps the rate clamp accurate.
  if (std::isnan(filteredC_)) {
    // Seed only once we have enough samples and a plausible value.
    if (sampleCount >= kMinSeedSamples &&
        median >= kSeedMinC && median <= kSeedMaxC) {
      filteredC_ = median;
      lastFilterMs_ = timestampMs;
    }
  } else {
    const float dt = (timestampMs - lastFilterMs_) / 1000.0f;
    if (dt > 5.0f) {
      filteredC_ = median;
    } else if (dt > 0.0f) {
      // Discrete RC low-pass: alpha -> dt/tau for dt << tau, but stays
      // bounded (< 1) for larger dt instead of overshooting.
      const float alpha = dt / (kEmaTimeConstantSec + dt);
      filteredC_ += alpha * (median - filteredC_);
    }
    lastFilterMs_ = timestampMs;
  }

  // Publish latest_ at the telemetry rate (250 ms), not the sample rate.
  if (!telemetryTimer_.isDue(timestampMs)) {
    return;
  }

  latest_.timestamp = timestampMs;
  latest_.probeC = lastProbeC_;
  latest_.internalC = lastInternalC_;
  latest_.compensatedC = lastProbeC_;  // chip already compensates
  latest_.medianC = median;
  latest_.filteredC = filteredC_;
  latest_.sampleCount = sampleCount;
  latest_.valid = true;
  latest_.fault = 0;

  if (!hasReportedProbeReady_) {
    Serial.println("INFO: Temperature probe read is working");
    hasReportedProbeReady_ = true;
  }

  // Log once per second (4 telemetry updates/s at 250 ms interval)
  if (++readCount_ % 4 == 0) {
    Serial.printf("Probe: %.2f C | Internal: %.2f C | Median: %.2f C (%d samples)\n",
                  latest_.probeC, latest_.internalC, latest_.medianC, latest_.sampleCount);
  }
}

void TemperatureSensor::onReadFailure() {
  if (latest_.fault != 0) {
    Serial.print("FAULT DETECTED: ");
    if (latest_.fault & STATUS_OPEN_CIRCUIT) {
      Serial.print("OPEN CIRCUIT ");
    }
    if (latest_.fault & STATUS_SHORT_TO_GND) {
      Serial.print("SHORT TO GND ");
    }
    if (latest_.fault & STATUS_SHORT_TO_VCC) {
      Serial.print("SHORT TO VCC ");
    }
    Serial.println();
    return;
  }

  Serial.println("ERROR: Failed to read probe temperature");
}
