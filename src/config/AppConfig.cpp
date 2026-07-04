#include "config/AppConfig.h"

#include <SPIFFS.h>
#include <cstdlib>

String AppConfig::trimComment(const String& line) {
  // Track quote state so a '#' inside "..."/'...' (e.g. a WiFi password)
  // isn't mistaken for the start of a comment and truncated.
  bool inSingleQuote = false;
  bool inDoubleQuote = false;
  for (unsigned int i = 0; i < line.length(); ++i) {
    const char c = line.charAt(i);
    if (c == '\'' && !inDoubleQuote) {
      inSingleQuote = !inSingleQuote;
    } else if (c == '"' && !inSingleQuote) {
      inDoubleQuote = !inDoubleQuote;
    } else if (c == '#' && !inSingleQuote && !inDoubleQuote) {
      return line.substring(0, i);
    }
  }
  return line;
}

String AppConfig::stripQuotes(const String& value) {
  if (value.length() < 2) {
    return value;
  }

  const char first = value.charAt(0);
  const char last = value.charAt(value.length() - 1);
  if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
    return value.substring(1, value.length() - 1);
  }
  return value;
}

bool AppConfig::applyValue(const String& section, const String& key, const String& value) {
  const String normalized = stripQuotes(value);

  if (section == "wifi") {
    if (key == "ssid") {
      wifi.ssid = normalized;
      return true;
    }
    if (key == "password") {
      wifi.password = normalized;
      return true;
    }
    if (key == "hostname") {
      wifi.hostname = normalized;
      return true;
    }
    return false;
  }

  if (section == "server") {
    if (key == "port") {
      server.port = static_cast<uint16_t>(normalized.toInt());
      return true;
    }
    if (key == "enabled") {
      String lower = normalized;
      lower.toLowerCase();
      server.enabled = (lower == "true" || lower == "1");
      return true;
    }
    return false;
  }

  if (section == "temperature") {
    if (key == "clk_pin") {
      temperature.clkPin = normalized.toInt();
      return true;
    }
    if (key == "cs_pin") {
      temperature.csPin = normalized.toInt();
      return true;
    }
    if (key == "do_pin") {
      temperature.doPin = normalized.toInt();
      return true;
    }
    if (key == "sample_interval_ms") {
      temperature.sampleIntervalMs = static_cast<unsigned long>(std::strtoul(normalized.c_str(), nullptr, 10));
      return true;
    }
    if (key == "median_window") {
      temperature.medianWindow = normalized.toInt();
      return true;
    }
    return false;
  }

  if (section == "pressure") {
    if (key == "adc_pin") {
      pressure.adcPin = normalized.toInt();
      return true;
    }
    if (key == "sample_interval_ms") {
      pressure.sampleIntervalMs = static_cast<unsigned long>(std::strtoul(normalized.c_str(), nullptr, 10));
      return true;
    }
    if (key == "median_window") {
      pressure.medianWindow = normalized.toInt();
      return true;
    }
    if (key == "max_bar") {
      pressure.maxBar = normalized.toFloat();
      return true;
    }
    if (key == "divider_ratio") {
      pressure.dividerRatio = normalized.toFloat();
      return true;
    }
    return false;
  }

  if (section == "ssr") {
    if (key == "pin") {
      ssr.pin = normalized.toInt();
      return true;
    }
    if (key == "setpoint_c") {
      ssr.setpointC = normalized.toFloat();
      return true;
    }
    return false;
  }

  if (section == "pid") {
    if (key == "kp") {
      pid.kp = normalized.toFloat();
      return true;
    }
    if (key == "ki") {
      pid.ki = normalized.toFloat();
      return true;
    }
    if (key == "kd") {
      pid.kd = normalized.toFloat();
      return true;
    }
    if (key == "window_ms") {
      pid.windowMs = static_cast<unsigned long>(std::strtoul(normalized.c_str(), nullptr, 10));
      return true;
    }
    if (key == "derivative_k") {
      pid.derivativeN = normalized.toInt();
      return true;
    }
    if (key == "derivative_source") {
      pid.derivativeSource = PidController::sourceName(PidController::parseSource(normalized.c_str()));
      return true;
    }
    if (key == "derivative_interval_ms") {
      pid.derivativeIntervalMs = std::strtoul(normalized.c_str(), nullptr, 10);
      return true;
    }
    if (key == "dc_cap_percent") {
      pid.dcCapPercent = normalized.toFloat();
      return true;
    }
    if (key == "dc_cap_disable_slope") {
      pid.dcCapDisableSlopeCPerSec = normalized.toFloat();
      return true;
    }
    if (key == "dc_cap_disable_hold_ms") {
      pid.dcCapDisableHoldMs = std::strtoul(normalized.c_str(), nullptr, 10);
      return true;
    }
    return false;
  }

  return false;
}

bool AppConfig::saveToYaml(const char* path) {
  if (!SPIFFS.begin(false)) return false;

  // Write to a temp file and rename over the original so a power loss
  // mid-write can't leave /config.yaml truncated/corrupt — the old file
  // stays intact until the new one is fully written and renamed in.
  const String tmpPath = String(path) + ".tmp";

  File file = SPIFFS.open(tmpPath.c_str(), FILE_WRITE);
  if (!file) return false;

  file.printf("wifi:\n");
  file.printf("  ssid: \"%s\"\n", wifi.ssid.c_str());
  file.printf("  password: \"%s\"\n", wifi.password.c_str());
  file.printf("  hostname: \"%s\"\n", wifi.hostname.c_str());
  file.printf("\n");
  file.printf("server:\n");
  file.printf("  port: %u\n", server.port);
  file.printf("  enabled: %s\n", server.enabled ? "true" : "false");
  file.printf("\n");
  file.printf("temperature:\n");
  file.printf("  clk_pin: %d\n", temperature.clkPin);
  file.printf("  cs_pin: %d\n", temperature.csPin);
  file.printf("  do_pin: %d\n", temperature.doPin);
  file.printf("  sample_interval_ms: %lu\n", temperature.sampleIntervalMs);
  file.printf("  median_window: %d\n", temperature.medianWindow);
  file.printf("\n");
  file.printf("pressure:\n");
  file.printf("  adc_pin: %d\n", pressure.adcPin);
  file.printf("  sample_interval_ms: %lu\n", pressure.sampleIntervalMs);
  file.printf("  median_window: %d\n", pressure.medianWindow);
  file.printf("  max_bar: %.1f\n", pressure.maxBar);
  file.printf("  divider_ratio: %.2f\n", pressure.dividerRatio);
  file.printf("\n");
  file.printf("ssr:\n");
  file.printf("  pin: %d\n", ssr.pin);
  file.printf("  setpoint_c: %.1f\n", ssr.setpointC);
  file.printf("\n");
  file.printf("pid:\n");
  file.printf("  kp: %.3f\n", pid.kp);
  file.printf("  ki: %.3f\n", pid.ki);
  file.printf("  kd: %.3f\n", pid.kd);
  file.printf("  window_ms: %lu\n", pid.windowMs);
  file.printf("  derivative_k: %d\n", pid.derivativeN);
  file.printf("  derivative_source: \"%s\"\n", pid.derivativeSource.c_str());
  file.printf("  derivative_interval_ms: %lu\n", pid.derivativeIntervalMs);
  file.printf("  dc_cap_percent: %.1f\n", pid.dcCapPercent);
  file.printf("  dc_cap_disable_slope: %.2f\n", pid.dcCapDisableSlopeCPerSec);
  file.printf("  dc_cap_disable_hold_ms: %lu\n", pid.dcCapDisableHoldMs);

  file.close();

  // Atomically replace the live config with the freshly written one.
  if (SPIFFS.exists(path)) {
    SPIFFS.remove(path);
  }
  return SPIFFS.rename(tmpPath.c_str(), path);
}

bool AppConfig::loadFromYaml(const char* path) {
  if (!SPIFFS.begin(false)) {
    Serial.println("WARN: SPIFFS mount failed, using built-in defaults");
    return false;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    Serial.println("WARN: config.yaml not found, using built-in defaults");
    return false;
  }

  String currentSection;
  while (file.available()) {
    String rawLine = file.readStringUntil('\n');
    rawLine.trim();
    if (rawLine.isEmpty()) {
      continue;
    }

    String line = trimComment(rawLine);
    line.trim();
    if (line.isEmpty()) {
      continue;
    }

    if (line.endsWith(":") && line.indexOf(':') == line.length() - 1) {
      currentSection = line.substring(0, line.length() - 1);
      currentSection.trim();
      continue;
    }

    const int separator = line.indexOf(':');
    if (separator < 0) {
      continue;
    }

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();
    applyValue(currentSection, key, value);
  }

  file.close();
  return true;
}
