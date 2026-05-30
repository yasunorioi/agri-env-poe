// mqtt_pub.h — env JSON payload published to <prefix>/state.

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline bool mqttPublishState() {
  if (!agri::MQTT::hasHost(g_cfg.common) || !agri::MQTT::connected()) return false;

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["node"] = g_cfg.common.node_id;
  root["uptime_s"] = millis() / 1000;
  if (g_sht30_ok) {
    root["temp_c"]    = g_temp_c;
    root["humid_pct"] = g_humid_pct;
  }
  if (g_qmp_ok)   root["pressure_hpa"]  = g_pressure_hpa;
  if (g_scd41_ok) {
    root["co2_ppm"]       = g_co2_ppm;
    root["co2_temp_c"]    = g_co2_temp_c;
    root["co2_humid_pct"] = g_co2_humid_pct;
  }

  char payload[320];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  char topic[96];
  snprintf(topic, sizeof(topic), "%s/state", g_cfg.common.mqtt_topic_prefix);
  bool ok = agri::MQTT::mqtt.publish(topic, (const uint8_t*)payload, (unsigned)n, false);
  Serial.printf("[MQTT] %s %s (%u bytes)\n", topic, ok ? "OK" : "FAIL", (unsigned)n);
  return ok;
}
