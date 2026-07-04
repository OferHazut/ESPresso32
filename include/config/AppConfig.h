#pragma once

#include <Arduino.h>

#include "control/DcCapOverride.h"
#include "control/PidController.h"
#include "control/SsrController.h"
#include "sensing/PressureSensor.h"
#include "sensing/TemperatureSensor.h"

class AppConfig {
 public:
  struct WifiConfig {
    String ssid = "";
    String password = "";
    String hostname = "esp-temp-monitor";
  };

  struct ServerConfig {
    uint16_t port = 80;
    bool enabled = true;
  };

  struct TemperatureConfig {
    int clkPin = TemperatureSensor::kDefaultClkPin;
    int csPin = TemperatureSensor::kDefaultCsPin;
    int doPin = TemperatureSensor::kDefaultDoPin;
    unsigned long sampleIntervalMs = TemperatureSensor::kDefaultSampleIntervalMs;
    int medianWindow = TemperatureSensor::kDefaultMedianWindow;
  };

  struct SsrConfig {
    int pin = SsrController::kDefaultPin;
    float setpointC = SsrController::kDefaultSetpointC;
  };

  struct PidConfig {
    float kp = PidController::kDefaultKp;
    float ki = PidController::kDefaultKi;
    float kd = PidController::kDefaultKd;
    unsigned long windowMs = SsrController::kDefaultWindowMs;
    int derivativeN = PidController::kDefaultDerivativeN;
    String derivativeSource = "filtered";
    // 1000ms: with derivativeN samples spaced this far apart, the derivative
    // regression spans derivativeN seconds — long enough to average out
    // sensor noise on a slow boiler (vs. derivativeN sample periods).
    unsigned long derivativeIntervalMs = 1000;
    float dcCapPercent = SsrController::kDefaultDcCapPercent;
    // While the temperature has been falling faster than this (deg C/sec,
    // negative) for at least dcCapDisableHoldMs, the DC cap is bypassed so
    // the PID can recover quickly from a fast drop (e.g. a flush).
    float dcCapDisableSlopeCPerSec = DcCapOverride::kDefaultThresholdCPerSec;
    unsigned long dcCapDisableHoldMs = DcCapOverride::kDefaultHoldMs;
  };

  struct PressureConfig {
    int adcPin = PressureSensor::kDefaultAdcPin;
    unsigned long sampleIntervalMs = PressureSensor::kDefaultSampleIntervalMs;
    int medianWindow = PressureSensor::kDefaultMedianWindow;
    float maxBar = PressureSensor::kDefaultMaxBar;
    float dividerRatio = PressureSensor::kDefaultDividerRatio;
  };

  bool loadFromYaml(const char* path = "/config.yaml");
  bool saveToYaml(const char* path = "/config.yaml");

  WifiConfig wifi;
  ServerConfig server;
  TemperatureConfig temperature;
  PressureConfig pressure;
  SsrConfig ssr;
  PidConfig pid;

 private:
  static String trimComment(const String& line);
  static String stripQuotes(const String& value);
  bool applyValue(const String& section, const String& key, const String& value);
};
