// mqtt_pub.h — PubSubClient-backed publisher. One JSON document per cadence
// containing all live sensor values; topic is "<prefix>/state".

#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors.h"

extern EthernetClient g_ethClient;
extern PubSubClient   g_mqtt;

inline bool mqttHasHost() {
  return g_cfg.mqtt_host[0] != '\0';
}

inline bool mqttReconnect() {
  if (!mqttHasHost()) return false;
  if (g_mqtt.connected()) return true;
  g_mqtt.setServer(g_cfg.mqtt_host, g_cfg.mqtt_port);
  bool ok;
  if (g_cfg.mqtt_user[0]) {
    ok = g_mqtt.connect(g_cfg.node_id, g_cfg.mqtt_user, g_cfg.mqtt_pass);
  } else {
    ok = g_mqtt.connect(g_cfg.node_id);
  }
  Serial.printf("[MQTT] connect(%s:%u) = %s\n",
                g_cfg.mqtt_host, g_cfg.mqtt_port, ok ? "OK" : "FAIL");
  return ok;
}

inline bool mqttPublishState() {
  if (!mqttHasHost() || !g_mqtt.connected()) return false;

  JsonDocument doc;
  doc["node"] = g_cfg.node_id;
  doc["uptime_s"] = millis() / 1000;
  if (g_sht30_ok) {
    doc["temp_c"]    = g_temp_c;
    doc["humid_pct"] = g_humid_pct;
  }
  if (g_qmp_ok) {
    doc["pressure_hpa"] = g_pressure_hpa;
  }
  if (g_scd41_ok) {
    doc["co2_ppm"]      = g_co2_ppm;
    doc["co2_temp_c"]   = g_co2_temp_c;
    doc["co2_humid_pct"] = g_co2_humid_pct;
  }

  char payload[320];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  char topic[96];
  snprintf(topic, sizeof(topic), "%s/state", g_cfg.mqtt_topic_prefix);

  bool ok = g_mqtt.publish(topic, (const uint8_t*)payload, (unsigned)n, false);
  Serial.printf("[MQTT] %s %s (%u bytes)\n", topic, ok ? "OK" : "FAIL", (unsigned)n);
  return ok;
}
