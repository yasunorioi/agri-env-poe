// agri-env-poe — ENV III (SHT30 + QMP6988) + SCD41 CO2 node on M5 ATOM PoE.
// MQTT + UECS-CCM publishers, web UI, mDNS + OTA. Most plumbing lives in
// agri-node-poe-core; this sketch wires the three I²C sensors in.

#include <Arduino.h>
#include <Wire.h>
#include <SHT3X.h>
#include <QMP6988.h>
#include <SensirionI2cScd4x.h>
#include <AgriNode.h>

#include "config.h"
#include "sensors.h"
#include "mqtt_pub.h"
#include "ccm_pub.h"

const char *FW_NAME    = "agri-env-poe";
const char *FW_VERSION = "0.3.0";

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
  String s; s.reserve(360);
  char buf[12];
  s = F("<h3>Sensors</h3><table>");
  if (g_sht30_ok) {
    dtostrf(g_temp_c, 1, 2, buf);
    s += "<tr><th>Temp</th><td>"; s += buf; s += " °C</td></tr>";
    dtostrf(g_humid_pct, 1, 1, buf);
    s += "<tr><th>Humidity</th><td>"; s += buf; s += " %</td></tr>";
  }
  if (g_qmp_ok) {
    dtostrf(g_pressure_hpa, 1, 2, buf);
    s += "<tr><th>Pressure</th><td>"; s += buf; s += " hPa</td></tr>";
  }
  if (g_scd41_ok) {
    s += "<tr><th>CO₂</th><td>"; s += g_co2_ppm; s += " ppm</td></tr>";
  }
  if (!g_sht30_ok && !g_qmp_ok && !g_scd41_ok)
    s += "<tr><th>Sensor</th><td>NONE detected</td></tr>";
  s += F("</table>");
  return s;
}

static String renderConfigSensorRows() {
  String s;
  auto row = [&](const char *label, const char *name, int16_t val) {
    s += "<tr><th>"; s += label;
    s += "</th><td><input type=number name="; s += name;
    s += " value='"; s += val; s += "'></td></tr>";
  };
  row("Order (Temp)",     "ccm_ot", g_cfg.ccm_order_temp);
  row("Order (Humid)",    "ccm_oh", g_cfg.ccm_order_humid);
  row("Order (Pressure)", "ccm_op", g_cfg.ccm_order_pressure);
  row("Order (CO2)",      "ccm_oc", g_cfg.ccm_order_co2);
  return s;
}

static void applyConfigSensorForm(const String &body) {
  g_cfg.ccm_order_temp     = (int16_t)agri::parseFormInt(body, "ccm_ot", g_cfg.ccm_order_temp);
  g_cfg.ccm_order_humid    = (int16_t)agri::parseFormInt(body, "ccm_oh", g_cfg.ccm_order_humid);
  g_cfg.ccm_order_pressure = (int16_t)agri::parseFormInt(body, "ccm_op", g_cfg.ccm_order_pressure);
  g_cfg.ccm_order_co2      = (int16_t)agri::parseFormInt(body, "ccm_oc", g_cfg.ccm_order_co2);
}

static void addStatusFields(JsonObject doc) {
  if (g_sht30_ok) {
    doc["temp_c"]    = g_temp_c;
    doc["humid_pct"] = g_humid_pct;
  }
  if (g_qmp_ok)   doc["pressure_hpa"] = g_pressure_hpa;
  if (g_scd41_ok) doc["co2_ppm"]      = g_co2_ppm;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  agri::Led::begin();
  loadConfig();
  Serial.printf("[CFG] node=%s mqtt_host=%s ccm=%s\n",
                g_cfg.common.node_id,
                g_cfg.common.mqtt_host[0] ? g_cfg.common.mqtt_host : "(unset)",
                g_cfg.common.ccm_enabled ? "on" : "off");

  sensorsBegin();

  agri::Network::begin(g_cfg.common.hostname);
  agri::Network::waitForLease();

  agri::ccmBegin();
  agri::MQTT::begin();

  agri::WebHooks hooks;
  hooks.nodeTitle             = [](){ return FW_NAME; };
  hooks.renderDashboardSensors= renderDashboardSensors;
  hooks.renderConfigSensorRows= renderConfigSensorRows;
  hooks.applyConfigSensorForm = applyConfigSensorForm;
  hooks.addStatusFields       = addStatusFields;
  hooks.saveConfig            = [](){ saveConfig(); };
  agri::WebUI::begin(g_cfg.common, hooks, FW_NAME, FW_VERSION);

  agri::mdnsBegin(g_cfg.common.hostname);
  agri::otaBegin(g_cfg.common.hostname);

  Serial.println("[BOOT] ready");
}

void loop() {
  agri::otaHandle();
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
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d  T=%.2f H=%.1f P=%.2f CO2=%u  up=%lus\n",
                  agri::Network::link_up, agri::Network::have_lease,
                  agri::MQTT::connected(),
                  g_temp_c, g_humid_pct, g_pressure_hpa, (unsigned)g_co2_ppm,
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
