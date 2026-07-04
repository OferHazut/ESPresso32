#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <memory>

#include "config/AppConfig.h"
#include "control/ReadyIndicator.h"
#include "control/SsrController.h"
#include "sensing/PressureSensor.h"
#include "sensing/TemperatureSensor.h"
#include "server/TelemetryServer.h"

namespace {
// Pressure sensing/monitoring is disabled for now (hardware not connected).
// Flip to true to re-enable — code is otherwise untouched.
constexpr bool kPressureEnabled = false;

// Watchdog settings.
constexpr int kTaskWdtTimeoutSec = 8;
constexpr unsigned long kPidStuckResetMs = 20UL * 60UL * 1000UL;  // 20 minutes
constexpr float kPidStuckTempMarginC = 8.0f;  // if current temp remains this far below setpoint

AppConfig gConfig;
std::unique_ptr<TemperatureSensor> gTemperatureSensor;
std::unique_ptr<PressureSensor> gPressureSensor;
std::unique_ptr<SsrController> gSsrController;
std::unique_ptr<ReadyIndicator> gReadyIndicator;
std::unique_ptr<TelemetryServer> gTelemetryServer;
unsigned long gPidStuckSinceMs = 0;
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("Espresso32 Monitor starting");
  Serial.printf("INFO: Reset reason: %d\n", esp_reset_reason());

  if (esp_task_wdt_init(kTaskWdtTimeoutSec, true) == ESP_OK) {
    esp_task_wdt_add(nullptr);
    Serial.printf("INFO: Task watchdog enabled (%d seconds)\n", kTaskWdtTimeoutSec);
  } else {
    Serial.println("WARN: Failed to initialize task watchdog");
  }

  if (gConfig.loadFromYaml("/config.yaml")) {
    Serial.println("INFO: Configuration loaded from /config.yaml");
  } else {
    Serial.println("WARN: Using built-in default configuration");
  }

  Serial.println("INFO: Initializing temperature probe");
  gTemperatureSensor.reset(new TemperatureSensor(gConfig.temperature.clkPin,
                                                 gConfig.temperature.csPin,
                                                 gConfig.temperature.doPin,
                                                 gConfig.temperature.sampleIntervalMs,
                                                 gConfig.temperature.medianWindow));

  if (!gTemperatureSensor->begin()) {
    Serial.println("ERROR: Temperature probe initialization failed (MAX31855)");
    while (true) {
      delay(50);
    }
  }
  Serial.println("INFO: Temperature probe initialized");

  Serial.println("INFO: Initializing SSR controller");
  gSsrController.reset(new SsrController(
      gConfig.ssr.pin, gConfig.ssr.setpointC,
      gConfig.pid.windowMs, gConfig.pid.kp, gConfig.pid.ki, gConfig.pid.kd,
      gConfig.pid.derivativeN));
  gSsrController->begin();
  gSsrController->setDcCapPercent(gConfig.pid.dcCapPercent);
  gSsrController->setDerivativeSource(PidController::parseSource(gConfig.pid.derivativeSource.c_str()));
  gSsrController->setDerivativeIntervalMs(gConfig.pid.derivativeIntervalMs);
  gSsrController->setDcCapDisableSlopeCPerSec(gConfig.pid.dcCapDisableSlopeCPerSec);
  gSsrController->setDcCapDisableHoldMs(gConfig.pid.dcCapDisableHoldMs);
  // Explicit, self-documenting: guarantees a clean integrator/derivative
  // state on every boot, independent of how the controller was constructed.
  gSsrController->resetPid();
  Serial.printf("INFO: SSR controller initialized (pin %d, setpoint %.1f C, Kp=%.2f Ki=%.3f Kd=%.2f, derivative_n=%d (%s, interval=%lums), DC cap %.0f%%)\n",
                gConfig.ssr.pin, gConfig.ssr.setpointC,
                gConfig.pid.kp, gConfig.pid.ki, gConfig.pid.kd, gConfig.pid.derivativeN,
                gConfig.pid.derivativeSource.c_str(), gConfig.pid.derivativeIntervalMs, gConfig.pid.dcCapPercent);

  Serial.println("INFO: Initializing ready indicator");
  gReadyIndicator.reset(new ReadyIndicator());
  gReadyIndicator->begin();

  // Constructed unconditionally — TelemetryServer holds a reference to it —
  // but only initialized/sampled when kPressureEnabled is true.
  gPressureSensor.reset(new PressureSensor(gConfig.pressure.adcPin,
                                           gConfig.pressure.sampleIntervalMs,
                                           gConfig.pressure.medianWindow,
                                           gConfig.pressure.maxBar,
                                           gConfig.pressure.dividerRatio));

  if (kPressureEnabled) {
    Serial.println("INFO: Initializing pressure sensor");
    if (!gPressureSensor->begin()) {
      Serial.println("ERROR: Pressure sensor initialization failed");
      while (true) {
        delay(50);
      }
    }
    Serial.println("INFO: Pressure sensor initialized");
  } else {
    Serial.println("INFO: Pressure sensor disabled (kPressureEnabled = false)");
  }

  if (gConfig.server.enabled) {
    gTelemetryServer.reset(new TelemetryServer(gConfig.wifi.ssid.c_str(),
                                               gConfig.wifi.password.c_str(),
                                               gConfig.wifi.hostname.c_str(),
                                               gConfig.server.port,
                                               *gTemperatureSensor,
                                               *gPressureSensor,
                                               *gSsrController,
                                               gConfig));
    Serial.println("INFO: Starting telemetry server");
    gTelemetryServer->begin();
  } else {
    Serial.println("INFO: Telemetry server disabled by configuration");
  }
}

void loop() {
  const unsigned long now = millis();
  const bool newSample = gTemperatureSensor ? gTemperatureSensor->update(now) : false;
  if (gSsrController && gTemperatureSensor) {
    const bool valid = gTemperatureSensor->telemetry().valid;
    const float filteredC = valid ? gTemperatureSensor->lastFilteredC() : NAN;
    gSsrController->update(filteredC, gTemperatureSensor->lastProbeC(), newSample, now);
    if (gReadyIndicator) {
      gReadyIndicator->update(filteredC, gSsrController->setpoint(), now);
    }
  }
  if (kPressureEnabled && gPressureSensor) {
    gPressureSensor->update(now);
  }
  if (gTelemetryServer) {
    gTelemetryServer->update();
    gTelemetryServer->handleClient();
  }

  if (gSsrController && gTemperatureSensor) {
    const auto& telemetry = gTemperatureSensor->telemetry();
    const float pidOutput = gSsrController->pidOutput();
    if (telemetry.valid && !std::isnan(telemetry.filteredC) &&
        pidOutput >= 99.5f &&
        telemetry.filteredC <= gSsrController->setpoint() - kPidStuckTempMarginC) {
      if (gPidStuckSinceMs == 0) {
        gPidStuckSinceMs = now;
      } else if (now - gPidStuckSinceMs >= kPidStuckResetMs) {
        Serial.println("ERROR: PID stuck at 100% with temperature far below setpoint; restarting");
        delay(100);
        esp_restart();
      }
    } else {
      gPidStuckSinceMs = 0;
    }
  }

  esp_task_wdt_reset();
  delay(10);
}
