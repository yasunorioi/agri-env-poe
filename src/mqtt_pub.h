// mqtt_pub.h — agriha-native sensor publisher.
//
// One value per topic (mqtt-topics.md §0.2): <prefix>/sensor/<Type> with the
// unified {value,unit,ts} JSON payload (§0.4), retained (§0.5). prefix is the
// house scope, e.g. "agriha/2" for the 別棟. Replaces both the old
// <prefix>/state blob and the UECS-CCM broadcast (CCM dropped — ArSprout
// never ingested our packets; pi4's ccm_mqtt_bridge covers ArSprout's own
// sensors instead).

#pragma once

#include <Arduino.h>
#include <time.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

// UNIX seconds if SNTP has synced, else 0 (consumers treat 0 as "no clock").
inline long mqttNow() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (long)t : 0;
}

inline bool mqttPublishOne(const char *type, const char *unit,
                           const char *valueStr) {
  char topic[96];
  snprintf(topic, sizeof(topic), "%s/sensor/%s",
           g_cfg.common.mqtt_topic_prefix, type);
  char payload[96];
  int n = snprintf(payload, sizeof(payload),
                   "{\"value\":%s,\"unit\":\"%s\",\"ts\":%ld}",
                   valueStr, unit, mqttNow());
  bool ok = agri::MQTT::mqtt.publish(topic, (const uint8_t*)payload,
                                     (unsigned)n, /*retain=*/true);
  Serial.printf("[MQTT] %s %s %s\n", topic, valueStr, ok ? "OK" : "FAIL");
  return ok;
}

inline bool mqttPublishState() {
  if (!agri::MQTT::hasHost(g_cfg.common) || !agri::MQTT::connected()) return false;

  bool any = false;
  char buf[16];
  if (g_sht30_ok && !isnan(g_temp_c)) {
    dtostrf(g_temp_c, 1, 2, buf);
    any |= mqttPublishOne("InAirTemp", "C", buf);
    dtostrf(g_humid_pct, 1, 1, buf);
    any |= mqttPublishOne("InAirHumid", "%", buf);
  }
  if (g_qmp_ok && !isnan(g_pressure_hpa)) {
    dtostrf(g_pressure_hpa, 1, 2, buf);
    any |= mqttPublishOne("InAirPressure", "hPa", buf);
  }
  if (g_scd41_ok && g_co2_ppm > 0) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_co2_ppm);
    any |= mqttPublishOne("InAirCO2", "ppm", buf);
  }
  return any;
}
