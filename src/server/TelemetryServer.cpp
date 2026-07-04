#include "server/TelemetryServer.h"

#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <esp_heap_caps.h>

TelemetryServer::TelemetryServer(const char* ssid,
                                 const char* password,
                                 const char* hostname,
                                 uint16_t port,
                                 TemperatureSensor& temperatureSensor,
                                 PressureSensor& pressureSensor,
                                 SsrController& ssrController,
                                 AppConfig& config)
    : ssid_(ssid),
      password_(password),
      hostname_(hostname),
      fsMounted_(false),
      sensor_(temperatureSensor),
      pressureSensor_(pressureSensor),
      ssr_(ssrController),
      config_(config),
      server_(port) {
  history_ = static_cast<TelemetryPoint*>(
      heap_caps_malloc(kHistorySize * sizeof(TelemetryPoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pressureHistory_ = static_cast<PressurePoint*>(
      heap_caps_malloc(kHistorySize * sizeof(PressurePoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!history_ || !pressureHistory_) {
    Serial.println("ERROR: Failed to allocate history buffers in PSRAM");
  }
}

TelemetryServer::~TelemetryServer() {
  heap_caps_free(history_);
  heap_caps_free(pressureHistory_);
}

void TelemetryServer::begin() {
  fsMounted_ = beginFileSystem();
  if (fsMounted_) {
    Serial.println("INFO: SPIFFS mounted successfully");
  } else {
    Serial.println("ERROR: SPIFFS is not mounted; web UI files are unavailable");
    Serial.println("INFO: Run Upload Filesystem Image to push data/ files");
  }

  if (!startWiFi()) {
    Serial.println("ERROR: WiFi initialization failed, HTTP server not started");
    return;
  }

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/styles.css", HTTP_GET, [this]() { handleStyles(); });
  server_.on("/app.js", HTTP_GET, [this]() { handleScript(); });
  server_.on("/api/temperature", HTTP_GET, [this]() { handleTemperatureApi(); });
  server_.on("/api/temperature/history", HTTP_GET, [this]() { handleTemperatureHistory(); });
  server_.on("/api/temperature/history.csv", HTTP_GET, [this]() { handleTemperatureHistoryCsv(); });
  server_.on("/api/pressure", HTTP_GET, [this]() { handlePressureApi(); });
  server_.on("/api/pressure/history", HTTP_GET, [this]() { handlePressureHistory(); });
  server_.on("/api/ssr", HTTP_GET,  [this]() { handleSsrApi(); });
  server_.on("/api/ssr", HTTP_POST, [this]() { handleSsrApi(); });
  server_.on("/api/pid", HTTP_GET,  [this]() { handlePidApi(); });
  server_.on("/api/pid", HTTP_POST, [this]() { handlePidApi(); });
  server_.on("/api/sensor", HTTP_GET,  [this]() { handleSensorApi(); });
  server_.on("/api/sensor", HTTP_POST, [this]() { handleSensorApi(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();
  Serial.println("INFO: HTTP server started");
}

void TelemetryServer::handleClient() { server_.handleClient(); }

bool TelemetryServer::startWiFi() {
  static const unsigned long kConnectTimeoutMs = 20000;
  const unsigned long startMs = millis();

  Serial.printf("Trying to connect to %s\n", ssid_);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_);
  WiFi.begin(ssid_, password_);

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs >= kConnectTimeoutMs) {
      Serial.println();
      Serial.println("ERROR: WiFi connection timeout");
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("INFO: WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool TelemetryServer::beginFileSystem() {
  if (SPIFFS.begin(false)) {
    return true;
  }

  Serial.println("ERROR: SPIFFS mount failed");
  return false;
}

void TelemetryServer::handleRoot() {
  if (!fsMounted_) {
    server_.send(503, "text/plain", "SPIFFS not mounted. Upload filesystem image.");
    return;
  }
  serveFile("/index.html", "text/html");
}

void TelemetryServer::handleStyles() {
  if (!fsMounted_) {
    server_.send(503, "text/plain", "SPIFFS not mounted. Upload filesystem image.");
    return;
  }
  serveFile("/styles.css", "text/css");
}

void TelemetryServer::handleScript() {
  if (!fsMounted_) {
    server_.send(503, "text/plain", "SPIFFS not mounted. Upload filesystem image.");
    return;
  }
  serveFile("/app.js", "application/javascript");
}

void TelemetryServer::handleTemperatureApi() {
  const TemperatureTelemetry& t = sensor_.telemetry();

  char payload[220];
  const int length = snprintf(payload,
                              sizeof(payload),
                              "{\"timestamp\":%lu,\"probe\":%.2f,\"internal\":%.2f,\"compensated\":%.2f,"
                              "\"median\":%.2f,\"count\":%d,\"valid\":%s,\"fault\":%u}",
                              t.timestamp,
                              t.probeC,
                              t.internalC,
                              t.compensatedC,
                              t.medianC,
                              t.sampleCount,
                              t.valid ? "true" : "false",
                              t.fault);

  if (length < 0) {
    server_.send(500, "text/plain", "json format error");
    return;
  }

  server_.send(200, "application/json", payload);
}

void TelemetryServer::update() {
  const TemperatureTelemetry& t = sensor_.telemetry();
  if (history_ && t.valid && t.timestamp != lastRecordedTimestamp_ && !std::isnan(t.filteredC)) {
    lastRecordedTimestamp_ = t.timestamp;
    history_[historyHead_] = {t.timestamp, t.probeC, t.medianC, t.filteredC, ssr_.pidOutput(),
                              ssr_.pidError(), ssr_.pidPTerm(), ssr_.pidITerm(), ssr_.pidDTerm(),
                              ssr_.isPidEnabled(), ssr_.isIntegratorEnabled()};
    historyHead_ = (historyHead_ + 1) % kHistorySize;
    if (historyCount_ < kHistorySize) historyCount_++;
  }

  const PressureTelemetry& p = pressureSensor_.telemetry();
  if (pressureHistory_ && p.valid && p.timestamp != lastPressureTimestamp_) {
    lastPressureTimestamp_ = p.timestamp;
    pressureHistory_[pressureHistoryHead_] = {p.timestamp, p.bar, p.medianBar};
    pressureHistoryHead_ = (pressureHistoryHead_ + 1) % kHistorySize;
    if (pressureHistoryCount_ < kHistorySize) pressureHistoryCount_++;
  }
}

void TelemetryServer::handleTemperatureHistory() {
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"interval\":250,\"points\":[");

  const int oldest = (historyCount_ < kHistorySize) ? 0 : historyHead_;
  // Stream only the most-recent N points — see kMaxHistoryPointsPerResponse.
  const int count = std::min(historyCount_, kMaxHistoryPointsPerResponse);
  const int start = (oldest + (historyCount_ - count)) % kHistorySize;
  char batch[2048];
  int pos = 0;

  for (int i = 0; i < count; i++) {
    const int idx = (start + i) % kHistorySize;
    pos += snprintf(batch + pos, sizeof(batch) - pos, "%s[%lu,%.2f,%.2f,%.2f,%.1f,%.2f,%.1f,%.1f,%.1f,%s,%s]",
                    i > 0 ? "," : "",
                    history_[idx].timestamp,
                    history_[idx].probeC,
                    history_[idx].medianC,
                    history_[idx].filteredC,
                    history_[idx].dutyPercent,
                    history_[idx].error,
                    history_[idx].pTerm,
                    history_[idx].iTerm,
                    history_[idx].dTerm,
                    history_[idx].pidEnabled ? "true" : "false",
                    history_[idx].integratorEnabled ? "true" : "false");
    if (pos > static_cast<int>(sizeof(batch)) - 80 || i == count - 1) {
      server_.sendContent(batch, pos);
      pos = 0;
    }
  }

  server_.sendContent("]}");
  server_.sendContent("");  // end chunked transfer
}

void TelemetryServer::handleTemperatureHistoryCsv() {
  server_.sendHeader("Content-Disposition", "attachment; filename=\"temperature_history.csv\"");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  server_.sendContent("timestamp_ms,probe_c,median_c,filtered_c,duty_pct,error_c,p_term,i_term,d_term,pid_enabled,integrator_enabled\n");

  const int start = (historyCount_ < kHistorySize) ? 0 : historyHead_;
  char batch[2048];
  int pos = 0;

  for (int i = 0; i < historyCount_; i++) {
    const int idx = (start + i) % kHistorySize;
    pos += snprintf(batch + pos, sizeof(batch) - pos, "%lu,%.2f,%.2f,%.2f,%.1f,%.2f,%.1f,%.1f,%.1f,%s,%s\n",
                    history_[idx].timestamp,
                    history_[idx].probeC,
                    history_[idx].medianC,
                    history_[idx].filteredC,
                    history_[idx].dutyPercent,
                    history_[idx].error,
                    history_[idx].pTerm,
                    history_[idx].iTerm,
                    history_[idx].dTerm,
                    history_[idx].pidEnabled ? "true" : "false",
                    history_[idx].integratorEnabled ? "true" : "false");
    if (pos > static_cast<int>(sizeof(batch)) - 64 || i == historyCount_ - 1) {
      server_.sendContent(batch, pos);
      pos = 0;
    }
  }

  server_.sendContent("");  // end chunked transfer
}

void TelemetryServer::handlePressureApi() {
  const PressureTelemetry& p = pressureSensor_.telemetry();

  char payload[180];
  const int length = snprintf(payload,
                              sizeof(payload),
                              "{\"timestamp\":%lu,\"bar\":%.2f,\"median\":%.2f,"
                              "\"voltage\":%.3f,\"count\":%d,\"valid\":%s}",
                              p.timestamp,
                              p.bar,
                              p.medianBar,
                              p.voltage,
                              p.sampleCount,
                              p.valid ? "true" : "false");

  if (length < 0) {
    server_.send(500, "text/plain", "json format error");
    return;
  }

  server_.send(200, "application/json", payload);
}

void TelemetryServer::handlePressureHistory() {
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"interval\":50,\"points\":[");

  const int start = (pressureHistoryCount_ < kHistorySize) ? 0 : pressureHistoryHead_;
  char batch[2048];
  int pos = 0;

  for (int i = 0; i < pressureHistoryCount_; i++) {
    const int idx = (start + i) % kHistorySize;
    pos += snprintf(batch + pos, sizeof(batch) - pos, "%s[%lu,%.2f,%.2f]",
                    i > 0 ? "," : "",
                    pressureHistory_[idx].timestamp,
                    pressureHistory_[idx].bar,
                    pressureHistory_[idx].medianBar);
    if (pos > static_cast<int>(sizeof(batch)) - 80 || i == pressureHistoryCount_ - 1) {
      server_.sendContent(batch, pos);
      pos = 0;
    }
  }

  server_.sendContent("]}");
  server_.sendContent("");  // end chunked transfer
}

void TelemetryServer::handleSsrApi() {
  if (server_.method() == HTTP_POST) {
    if (server_.hasArg("setpoint")) {
      const float sp = server_.arg("setpoint").toFloat();
      if (sp >= 0.0f && sp <= 160.0f) {
        ssr_.setSetpoint(sp);
        config_.ssr.setpointC = sp;
        config_.saveToYaml();
      } else {
        server_.send(400, "text/plain", "setpoint out of range (0-160)");
        return;
      }
    }
  }

  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"on\":%s,\"setpoint\":%.1f,\"pid_output\":%.1f}",
           ssr_.isOn() ? "true" : "false",
           ssr_.setpoint(),
           ssr_.pidOutput());
  server_.send(200, "application/json", payload);
}

void TelemetryServer::handlePidApi() {
  if (server_.method() == HTTP_POST) {
    if (server_.hasArg("reset")) {
      // Standalone action: clear integrator/derivative memory without
      // touching tunings (e.g. to recover from a wound-up integral holding
      // the output near 100% after a long cold-start or setpoint jump —
      // a small Ki makes that wind-up slow to clear on its own).
      ssr_.resetPid();
    } else {
      float kp = ssr_.kp();
      float ki = ssr_.ki();
      float kd = ssr_.kd();
      unsigned long windowMs = ssr_.windowMs();
      int derivativeN = ssr_.derivativeN();
      float dcCapPercent = ssr_.dcCapPercent();
      unsigned long derivativeIntervalMs = ssr_.derivativeIntervalMs();
      float dcCapDisableSlope = ssr_.dcCapDisableSlopeCPerSec();
      unsigned long dcCapDisableHoldMs = ssr_.dcCapDisableHoldMs();
      if (server_.hasArg("kp")) kp = server_.arg("kp").toFloat();
      if (server_.hasArg("ki")) ki = server_.arg("ki").toFloat();
      if (server_.hasArg("kd")) kd = server_.arg("kd").toFloat();
      if (server_.hasArg("window_ms")) {
        windowMs = static_cast<unsigned long>(server_.arg("window_ms").toInt());
      }
      if (server_.hasArg("derivative_k")) derivativeN = server_.arg("derivative_k").toInt();
      if (server_.hasArg("dc_cap_percent")) dcCapPercent = server_.arg("dc_cap_percent").toFloat();
      if (server_.hasArg("derivative_interval_ms")) {
        derivativeIntervalMs = static_cast<unsigned long>(server_.arg("derivative_interval_ms").toInt());
      }
      if (server_.hasArg("dc_cap_disable_slope")) {
        dcCapDisableSlope = server_.arg("dc_cap_disable_slope").toFloat();
      }
      if (server_.hasArg("dc_cap_disable_hold_ms")) {
        dcCapDisableHoldMs = static_cast<unsigned long>(server_.arg("dc_cap_disable_hold_ms").toInt());
      }

      bool derivativeSourceValid = true;
      PidController::DerivativeSource derivativeSource = ssr_.derivativeSource();
      if (server_.hasArg("derivative_source")) {
        const String src = server_.arg("derivative_source");
        if (src == "raw" || src == "filtered") {
          derivativeSource = PidController::parseSource(src.c_str());
        } else {
          derivativeSourceValid = false;
        }
      }

      // Negative gains would invert the control direction (heat MORE as temp
      // rises) — reject them, along with absurd magnitudes from typos/garbage.
      if (kp < 0.0f || kp > 100.0f ||
          ki < 0.0f || ki > 10.0f ||
          kd < 0.0f || kd > 1000.0f ||
          windowMs < 250 || windowMs > 10000 ||
          derivativeN < 2 || derivativeN > PidController::kMaxDerivativeN ||
          dcCapPercent < 1.0f || dcCapPercent > 100.0f ||
          derivativeIntervalMs > 10000 ||
          dcCapDisableSlope < -20.0f || dcCapDisableSlope > 0.0f ||
          dcCapDisableHoldMs > 60000 ||
          !derivativeSourceValid) {
        server_.send(400, "text/plain",
                     "out of range (kp 0-100, ki 0-10, kd 0-1000, window_ms 250-10000, "
                     "derivative_k 2-50, derivative_source raw|filtered, dc_cap_percent 1-100, "
                     "derivative_interval_ms 0-10000, dc_cap_disable_slope -20-0, "
                     "dc_cap_disable_hold_ms 0-60000)");
        return;
      }

      ssr_.setPidTunings(kp, ki, kd);
      ssr_.setWindowMs(windowMs);
      ssr_.setDerivativeN(derivativeN);
      ssr_.setDerivativeSource(derivativeSource);
      ssr_.setDerivativeIntervalMs(derivativeIntervalMs);
      ssr_.setDcCapPercent(dcCapPercent);
      ssr_.setDcCapDisableSlopeCPerSec(dcCapDisableSlope);
      ssr_.setDcCapDisableHoldMs(dcCapDisableHoldMs);
      config_.pid.kp = kp;
      config_.pid.ki = ki;
      config_.pid.kd = kd;
      config_.pid.windowMs = windowMs;
      config_.pid.derivativeN = derivativeN;
      config_.pid.derivativeSource = PidController::sourceName(derivativeSource);
      config_.pid.derivativeIntervalMs = derivativeIntervalMs;
      config_.pid.dcCapPercent = dcCapPercent;
      config_.pid.dcCapDisableSlopeCPerSec = dcCapDisableSlope;
      config_.pid.dcCapDisableHoldMs = dcCapDisableHoldMs;
      config_.saveToYaml();
    }
  }

  char payload[400];
  snprintf(payload, sizeof(payload),
           "{\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f,\"output\":%.1f,"
           "\"window_ms\":%lu,\"derivative_k\":%d,\"derivative_source\":\"%s\","
           "\"derivative_interval_ms\":%lu,"
           "\"dc_cap_percent\":%.1f,"
           "\"dc_cap_disable_slope\":%.2f,\"dc_cap_disable_hold_ms\":%lu,"
           "\"dc_cap_bypassed\":%s,"
           "\"pid_enabled\":%s,\"integrator_enabled\":%s,"
           "\"error\":%.2f,\"d_term\":%.1f,\"slope_c_per_sec\":%.3f}",
           ssr_.kp(), ssr_.ki(), ssr_.kd(), ssr_.pidOutput(),
           ssr_.windowMs(), ssr_.derivativeN(), PidController::sourceName(ssr_.derivativeSource()),
           ssr_.derivativeIntervalMs(),
           ssr_.dcCapPercent(),
           ssr_.dcCapDisableSlopeCPerSec(), ssr_.dcCapDisableHoldMs(),
           ssr_.isDcCapBypassed() ? "true" : "false",
           ssr_.isPidEnabled() ? "true" : "false",
           ssr_.isIntegratorEnabled() ? "true" : "false",
           ssr_.pidError(), ssr_.pidDTerm(), ssr_.pidSlopeCPerSec());
  server_.send(200, "application/json", payload);
}

void TelemetryServer::handleSensorApi() {
  if (server_.method() == HTTP_POST) {
    unsigned long sampleIntervalMs = sensor_.sampleIntervalMs();
    int medianWindow = sensor_.medianWindow();
    if (server_.hasArg("sample_interval_ms")) {
      sampleIntervalMs = static_cast<unsigned long>(server_.arg("sample_interval_ms").toInt());
    }
    if (server_.hasArg("median_window")) {
      medianWindow = server_.arg("median_window").toInt();
    }

    if (sampleIntervalMs < 20 || sampleIntervalMs > 1000 ||
        medianWindow < 1 || medianWindow > 10) {
      server_.send(400, "text/plain", "out of range (sample_interval_ms 20-1000, median_window 1-10)");
      return;
    }

    sensor_.setSampleIntervalMs(sampleIntervalMs);
    sensor_.setMedianWindow(medianWindow);
    config_.temperature.sampleIntervalMs = sampleIntervalMs;
    config_.temperature.medianWindow = medianWindow;
    config_.saveToYaml();
  }

  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"sample_interval_ms\":%lu,\"median_window\":%d,\"median_latency_ms\":%lu}",
           sensor_.sampleIntervalMs(), sensor_.medianWindow(),
           sensor_.sampleIntervalMs() * static_cast<unsigned long>(sensor_.medianWindow()));
  server_.send(200, "application/json", payload);
}

void TelemetryServer::handleNotFound() {
  server_.send(404, "text/plain", "Not found");
}

void TelemetryServer::serveFile(const char* path, const char* contentType) {
  if (!fsMounted_) {
    server_.send(503, "text/plain", "SPIFFS not mounted. Upload filesystem image.");
    return;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    server_.send(404, "text/plain", "Asset not found");
    return;
  }

  server_.streamFile(file, contentType);
  file.close();
}
