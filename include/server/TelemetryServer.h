#pragma once

#include <WebServer.h>

#include "config/AppConfig.h"
#include "control/SsrController.h"
#include "sensing/PressureSensor.h"
#include "sensing/TemperatureSensor.h"

struct TelemetryPoint {
  unsigned long timestamp;
  float probeC;
  float medianC;
  float filteredC;
  float dutyPercent;
  float error;   // setpoint - input, the signal accumulated into the integral
  float pTerm;   // proportional term's contribution to output (duty-% units)
  float iTerm;   // integral term's contribution to output (duty-% units)
  float dTerm;   // derivative term's contribution to output (duty-% units)
  bool pidEnabled;  // whether PID control is active
  bool integratorEnabled;  // whether integrator is accumulating
};

struct PressurePoint {
  unsigned long timestamp;
  float bar;
  float medianBar;
};

class TelemetryServer {
 public:
  // 10 minutes * 60 s * 20 Hz = 12 000 points (~144 KB per array, allocated in PSRAM)
  static constexpr int kHistorySize = 12000;
  // Caps how many of the most-recent points a single history response
  // streams. Sending the full buffer blocks handleClient() — and therefore
  // SsrController::update() — long enough to risk swallowing short on-pulses
  // at low duty cycles. The webapp only ever displays up to a 10-minute
  // window (~2400 points at the ~250ms recording rate), so this costs nothing.
  static constexpr int kMaxHistoryPointsPerResponse = 3000;

  TelemetryServer(const char* ssid,
                  const char* password,
                  const char* hostname,
                  uint16_t port,
                  TemperatureSensor& temperatureSensor,
                  PressureSensor& pressureSensor,
                  SsrController& ssrController,
                  AppConfig& config);

  ~TelemetryServer();
  void begin();
  void handleClient();
  void update();  // call from loop to capture new readings into history

 private:
  bool startWiFi();
  bool beginFileSystem();
  void handleRoot();
  void handleStyles();
  void handleScript();
  void handleTemperatureApi();
  void handleTemperatureHistory();
  void handleTemperatureHistoryCsv();
  void handlePressureApi();
  void handlePressureHistory();
  void handleSsrApi();
  void handlePidApi();
  void handleSensorApi();
  void handleNotFound();
  void serveFile(const char* path, const char* contentType);

  const char* ssid_;
  const char* password_;
  const char* hostname_;
  bool fsMounted_;
  TemperatureSensor& sensor_;
  PressureSensor& pressureSensor_;
  SsrController& ssr_;
  AppConfig& config_;
  WebServer server_;

  TelemetryPoint* history_ = nullptr;
  int historyHead_ = 0;
  int historyCount_ = 0;
  unsigned long lastRecordedTimestamp_ = 0;

  PressurePoint* pressureHistory_ = nullptr;
  int pressureHistoryHead_ = 0;
  int pressureHistoryCount_ = 0;
  unsigned long lastPressureTimestamp_ = 0;
};
