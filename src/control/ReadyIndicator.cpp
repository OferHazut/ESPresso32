#include "control/ReadyIndicator.h"

#include <Arduino.h>

ReadyIndicator::ReadyIndicator(int pin) : pin_(pin) {}

void ReadyIndicator::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void ReadyIndicator::update(float filteredC, float setpointC, unsigned long nowMs) {
  const bool ready = hysteresis_.update(filteredC, setpointC, nowMs);
  digitalWrite(pin_, ready ? HIGH : LOW);
}
