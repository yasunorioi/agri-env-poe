// config.h — NVS-backed runtime configuration
//
// All persisted state lives in Preferences namespace "env-cfg". Each loader
// pulls from NVS at boot; saveConfig() writes back the entire struct.
//
// String fields are size-bounded so the struct is plain-old-data; that lets
// us serialize it as JSON for the /api/config endpoint without round-trips.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

struct Config {
  // identity
  char     node_id[16];           // "env_node_01"
  char     hostname[32];          // "agri-env-01"

  // MQTT
  char     mqtt_host[64];         // e.g. "192.168.15.10" — empty disables MQTT
  uint16_t mqtt_port;             // 1883
  char     mqtt_user[32];         // empty = no auth
  char     mqtt_pass[32];
  char     mqtt_topic_prefix[64]; // "agri/env/01"
  uint16_t mqtt_interval_s;       // publish cadence

  // UECS-CCM
  bool     ccm_enabled;
  uint16_t ccm_interval_s;        // multicast cadence
  int16_t  ccm_room;              // 1
  int16_t  ccm_region;            // 11
  int16_t  ccm_order_temp;        // 1, 2, 3, 4 to distinguish channels
  int16_t  ccm_order_humid;
  int16_t  ccm_order_pressure;
  int16_t  ccm_order_co2;
  int16_t  ccm_priority;          // 29 is the conventional sensor priority
};

extern Config g_cfg;

inline void setConfigDefaults() {
  strlcpy(g_cfg.node_id,           "env_node_01",     sizeof(g_cfg.node_id));
  strlcpy(g_cfg.hostname,          "agri-env-01",     sizeof(g_cfg.hostname));
  strlcpy(g_cfg.mqtt_host,         "",                sizeof(g_cfg.mqtt_host));
  g_cfg.mqtt_port           = 1883;
  strlcpy(g_cfg.mqtt_user,         "",                sizeof(g_cfg.mqtt_user));
  strlcpy(g_cfg.mqtt_pass,         "",                sizeof(g_cfg.mqtt_pass));
  strlcpy(g_cfg.mqtt_topic_prefix, "agri/env/01",     sizeof(g_cfg.mqtt_topic_prefix));
  g_cfg.mqtt_interval_s     = 30;

  g_cfg.ccm_enabled         = false;
  g_cfg.ccm_interval_s      = 10;
  g_cfg.ccm_room            = 1;
  g_cfg.ccm_region          = 11;
  g_cfg.ccm_order_temp      = 1;
  g_cfg.ccm_order_humid     = 1;
  g_cfg.ccm_order_pressure  = 1;
  g_cfg.ccm_order_co2       = 1;
  g_cfg.ccm_priority        = 29;
}

inline void loadConfig() {
  setConfigDefaults();
  Preferences p;
  if (!p.begin("env-cfg", true)) return;

  p.getString("node_id",   g_cfg.node_id,           sizeof(g_cfg.node_id));
  p.getString("hostname",  g_cfg.hostname,          sizeof(g_cfg.hostname));
  p.getString("mq_host",   g_cfg.mqtt_host,         sizeof(g_cfg.mqtt_host));
  g_cfg.mqtt_port = p.getUShort("mq_port", g_cfg.mqtt_port);
  p.getString("mq_user",   g_cfg.mqtt_user,         sizeof(g_cfg.mqtt_user));
  p.getString("mq_pass",   g_cfg.mqtt_pass,         sizeof(g_cfg.mqtt_pass));
  p.getString("mq_pfx",    g_cfg.mqtt_topic_prefix, sizeof(g_cfg.mqtt_topic_prefix));
  g_cfg.mqtt_interval_s = p.getUShort("mq_int", g_cfg.mqtt_interval_s);

  g_cfg.ccm_enabled        = p.getBool("ccm_en",  g_cfg.ccm_enabled);
  g_cfg.ccm_interval_s     = p.getUShort("ccm_int", g_cfg.ccm_interval_s);
  g_cfg.ccm_room           = p.getShort("ccm_room", g_cfg.ccm_room);
  g_cfg.ccm_region         = p.getShort("ccm_reg",  g_cfg.ccm_region);
  g_cfg.ccm_order_temp     = p.getShort("ccm_ot",   g_cfg.ccm_order_temp);
  g_cfg.ccm_order_humid    = p.getShort("ccm_oh",   g_cfg.ccm_order_humid);
  g_cfg.ccm_order_pressure = p.getShort("ccm_op",   g_cfg.ccm_order_pressure);
  g_cfg.ccm_order_co2      = p.getShort("ccm_oc",   g_cfg.ccm_order_co2);
  g_cfg.ccm_priority       = p.getShort("ccm_pri",  g_cfg.ccm_priority);

  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("env-cfg", false)) return false;
  p.putString("node_id",   g_cfg.node_id);
  p.putString("hostname",  g_cfg.hostname);
  p.putString("mq_host",   g_cfg.mqtt_host);
  p.putUShort("mq_port",   g_cfg.mqtt_port);
  p.putString("mq_user",   g_cfg.mqtt_user);
  p.putString("mq_pass",   g_cfg.mqtt_pass);
  p.putString("mq_pfx",    g_cfg.mqtt_topic_prefix);
  p.putUShort("mq_int",    g_cfg.mqtt_interval_s);

  p.putBool  ("ccm_en",    g_cfg.ccm_enabled);
  p.putUShort("ccm_int",   g_cfg.ccm_interval_s);
  p.putShort ("ccm_room",  g_cfg.ccm_room);
  p.putShort ("ccm_reg",   g_cfg.ccm_region);
  p.putShort ("ccm_ot",    g_cfg.ccm_order_temp);
  p.putShort ("ccm_oh",    g_cfg.ccm_order_humid);
  p.putShort ("ccm_op",    g_cfg.ccm_order_pressure);
  p.putShort ("ccm_oc",    g_cfg.ccm_order_co2);
  p.putShort ("ccm_pri",   g_cfg.ccm_priority);
  p.end();
  return true;
}

// Render config as JSON for /api/config (and dashboard).
inline String configToJson() {
  JsonDocument doc;
  doc["node_id"]   = g_cfg.node_id;
  doc["hostname"]  = g_cfg.hostname;
  doc["mqtt"]["host"]   = g_cfg.mqtt_host;
  doc["mqtt"]["port"]   = g_cfg.mqtt_port;
  doc["mqtt"]["user"]   = g_cfg.mqtt_user;
  // mqtt.pass deliberately omitted
  doc["mqtt"]["prefix"] = g_cfg.mqtt_topic_prefix;
  doc["mqtt"]["interval_s"] = g_cfg.mqtt_interval_s;
  doc["ccm"]["enabled"]  = g_cfg.ccm_enabled;
  doc["ccm"]["interval_s"] = g_cfg.ccm_interval_s;
  doc["ccm"]["room"]     = g_cfg.ccm_room;
  doc["ccm"]["region"]   = g_cfg.ccm_region;
  doc["ccm"]["order_temp"]     = g_cfg.ccm_order_temp;
  doc["ccm"]["order_humid"]    = g_cfg.ccm_order_humid;
  doc["ccm"]["order_pressure"] = g_cfg.ccm_order_pressure;
  doc["ccm"]["order_co2"]      = g_cfg.ccm_order_co2;
  doc["ccm"]["priority"]       = g_cfg.ccm_priority;
  String out;
  serializeJson(doc, out);
  return out;
}
