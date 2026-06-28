// agri-env-poe — ENV III (SHT30 + QMP6988) + SCD41 CO2 node on M5 ATOM PoE.
// agriha MQTT publisher, web UI, mDNS + OTA. Most plumbing lives in
// agri-node-poe-core; this sketch wires the three I²C sensors in.
// UECS-CCM was dropped in 0.4.0 (MQTT-only; see mqtt_pub.h).

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <SHT3X.h>
#include <QMP6988.h>
#include <SensirionI2cScd4x.h>
#include <AgriNode.h>

#include "config.h"
#include "sensors.h"
#include "mqtt_pub.h"
#include "ccm_pub.h"

const char *FW_NAME     = "agri-env-poe";
const char *FW_VERSION  = "0.10.0";
const char *FW_REPO     = "yasunorioi/agri-env-poe";
const char *FW_BIN_NAME = "agri-env-poe.bin";

// globals declared extern in headers
AppConfig g_cfg;

float    g_temp_c        = NAN;
float    g_humid_pct     = NAN;
float    g_pressure_hpa  = NAN;
uint16_t g_co2_ppm       = 0;
float    g_co2_temp_c    = NAN;
float    g_co2_humid_pct = NAN;
bool g_sht30_ok = false;
bool g_qmp_ok   = false;
bool g_scd41_ok = false;
uint32_t g_last_sensor_ms = 0;
SHT3X   g_sht;
QMP6988 g_qmp;
SensirionI2cScd4x g_scd;

// ---- Dashboard / Config hooks --------------------------------------------
static String renderDashboardSensors() {
  String s; s.reserve(480);
  char buf[12];
  s = F("<h3>Sensors</h3><table>");
  // Label every value by its source sensor — this node has two temp/RH
  // sources (SHT30 primary, SCD41 secondary) and it must be obvious which.
  if (g_sht30_ok) {
    dtostrf(g_temp_c, 1, 2, buf);
    s += "<tr><th>Temp (SHT30)</th><td>"; s += buf; s += " °C</td></tr>";
    dtostrf(g_humid_pct, 1, 1, buf);
    s += "<tr><th>Humidity (SHT30)</th><td>"; s += buf; s += " %</td></tr>";
    dtostrf(airHd(g_temp_c, g_humid_pct), 1, 2, buf);
    s += "<tr><th>飽差 (HD)</th><td>"; s += buf; s += " g/m³</td></tr>";
  }
  if (g_qmp_ok) {
    dtostrf(g_pressure_hpa, 1, 2, buf);
    s += "<tr><th>Pressure (QMP6988)</th><td>"; s += buf; s += " hPa</td></tr>";
  }
  if (g_scd41_ok) {
    s += "<tr><th>CO₂ (SCD41)</th><td>"; s += g_co2_ppm; s += " ppm</td></tr>";
    if (!isnan(g_co2_temp_c)) {
      dtostrf(g_co2_temp_c, 1, 2, buf);
      s += "<tr><th>Temp (SCD41)</th><td>"; s += buf; s += " °C</td></tr>";
      dtostrf(g_co2_humid_pct, 1, 1, buf);
      s += "<tr><th>Humidity (SCD41)</th><td>"; s += buf; s += " %</td></tr>";
    }
  }
  if (!g_sht30_ok && !g_qmp_ok && !g_scd41_ok)
    s += "<tr><th>Sensor</th><td>NONE detected</td></tr>";
  s += F("</table>");
  return s;
}

static void addStatusFields(JsonObject doc) {
  if (g_sht30_ok) {
    // SHT30 = primary temp/RH (drives InAirTemp/InAirHumid/飽差).
    doc["temp_c"]    = g_temp_c;
    doc["humid_pct"] = g_humid_pct;
    doc["hd_gm3"]    = airHd(g_temp_c, g_humid_pct);
  }
  if (g_qmp_ok)   doc["pressure_hpa"] = g_pressure_hpa;
  if (g_scd41_ok) {
    doc["co2_ppm"] = g_co2_ppm;
    // SCD41's own (secondary) temp/RH — InAirTempSCD41/InAirHumidSCD41.
    if (!isnan(g_co2_temp_c)) {
      doc["scd_temp_c"]    = g_co2_temp_c;
      doc["scd_humid_pct"] = g_co2_humid_pct;
    }
  }
  // CCM識別子 (wire type names) — exposed for inspection / verification.
  JsonObject ct = doc["ccm_types"].to<JsonObject>();
  ct["temp"]  = g_cfg.ccm_type_temp;
  ct["humid"] = g_cfg.ccm_type_humid;
  ct["hd"]    = g_cfg.ccm_type_hd;
  ct["press"] = g_cfg.ccm_type_press;
  ct["co2"]   = g_cfg.ccm_type_co2;
}

// Project-owned Config rows: the per-sensor CCM識別子 (UECS type names).
// Appended to the core Config page's CCM table. Empty input = datum off.
static String renderCcmTypeRows() {
  String s; s.reserve(520);
  auto row = [&](const char *label, const char *name, const char *val) {
    s += "<tr><th>"; s += label; s += "</th><td><input name="; s += name;
    s += " value='"; s += val; s += "'></td></tr>";
  };
  row("CCM識別子: 気温 (SHT30)",   "ct_temp",  g_cfg.ccm_type_temp);
  row("CCM識別子: 湿度 (SHT30)",   "ct_humid", g_cfg.ccm_type_humid);
  row("CCM識別子: 飽差",           "ct_hd",    g_cfg.ccm_type_hd);
  row("CCM識別子: 気圧 (QMP6988)", "ct_press", g_cfg.ccm_type_press);
  row("CCM識別子: CO₂ (SCD41)",    "ct_co2",   g_cfg.ccm_type_co2);
  return s;
}

static void applyCcmTypeForm(const String &body) {
  agri::parseFormStr(body, "ct_temp",  g_cfg.ccm_type_temp,  sizeof(g_cfg.ccm_type_temp));
  agri::parseFormStr(body, "ct_humid", g_cfg.ccm_type_humid, sizeof(g_cfg.ccm_type_humid));
  agri::parseFormStr(body, "ct_hd",    g_cfg.ccm_type_hd,    sizeof(g_cfg.ccm_type_hd));
  agri::parseFormStr(body, "ct_press", g_cfg.ccm_type_press, sizeof(g_cfg.ccm_type_press));
  agri::parseFormStr(body, "ct_co2",   g_cfg.ccm_type_co2,   sizeof(g_cfg.ccm_type_co2));
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  agri::Led::begin();
  loadConfig();
  Serial.printf("[CFG] node=%s mqtt_host=%s prefix=%s\n",
                g_cfg.common.node_id,
                g_cfg.common.mqtt_host[0] ? g_cfg.common.mqtt_host : "(unset)",
                g_cfg.common.mqtt_topic_prefix);

  sensorsBegin();

  agri::Network::begin(g_cfg.common.hostname);
  agri::Network::waitForLease();

  // SNTP for the {value,unit,ts} payloads (UTC epoch; non-blocking, ts=0
  // until the first sync lands).
  configTime(0, 0, "pool.ntp.org");

  agri::MQTT::begin();
  agri::ccmBegin();   // optional ArSprout CCM export (gated by ccm_enabled)

  agri::WebHooks hooks;
  hooks.nodeTitle             = [](){ return FW_NAME; };
  hooks.renderDashboardSensors= renderDashboardSensors;
  hooks.renderConfigSensorRows= renderCcmTypeRows;
  hooks.applyConfigSensorForm = applyCcmTypeForm;
  hooks.addStatusFields       = addStatusFields;
  hooks.saveConfig            = [](){ saveConfig(); };
  agri::WebUI::begin(g_cfg.common, hooks, FW_NAME, FW_VERSION);

  agri::mdnsBegin(g_cfg.common.hostname);
  agri::otaBegin(g_cfg.common.hostname);

  agri::OTA::begin(FW_REPO, FW_BIN_NAME, FW_VERSION);
  agri::OTA::checkLatest();

  Serial.println("[BOOT] ready");
}

void loop() {
  agri::otaHandle();
  agri::OTA::poll();
  agri::WebUI::handle(agri::Network::link_up, agri::Network::have_lease);

  uint32_t now = millis();

  static uint32_t lastSensorPoll = 0;
  if (now - lastSensorPoll >= 2000) {
    lastSensorPoll = now;
    sensorsPoll();
  }

  if (agri::networkUp() && agri::MQTT::hasHost(g_cfg.common)) {
    if (!agri::MQTT::connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; agri::MQTT::reconnect(g_cfg.common); }
    } else {
      agri::MQTT::loop();
      static uint32_t lastPub = 0;
      uint32_t interval = (uint32_t)g_cfg.common.mqtt_interval_s * 1000UL;
      if (now - lastPub >= interval) {
        lastPub = now;
        if (mqttPublishState()) agri::Led::flashPublish();
      }
    }
  }

  if (agri::networkUp() && g_cfg.common.ccm_enabled) {
    static uint32_t lastCcm = 0;
    uint32_t interval = (uint32_t)g_cfg.common.ccm_interval_s * 1000UL;
    if (now - lastCcm >= interval) {
      lastCcm = now;
      if (ccmPublish()) agri::Led::flashPublish();
    }
  }

  agri::LedState desired;
  if (!agri::networkUp())                                                desired = agri::LED_NO_LINK;
  else if (agri::MQTT::hasHost(g_cfg.common) && !agri::MQTT::connected()) desired = agri::LED_NO_MQTT;
  else                                                                   desired = agri::LED_OK;
  agri::Led::set(desired);

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d  SHT:T=%.2f H=%.1f  P=%.2f CO2=%u  SCD:T=%.2f H=%.1f  up=%lus\n",
                  agri::Network::link_up, agri::Network::have_lease,
                  agri::MQTT::connected(),
                  g_temp_c, g_humid_pct, g_pressure_hpa, (unsigned)g_co2_ppm,
                  g_co2_temp_c, g_co2_humid_pct,
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
