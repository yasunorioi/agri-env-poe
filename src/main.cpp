// agri-env-poe — ENV III (SHT30 + QMP6988) + SCD41 CO2 node on M5 ATOM PoE.
// agriha MQTT publisher, web UI, mDNS + OTA. Most plumbing lives in
// agri-node-poe-core; this sketch wires the three I²C sensors in.
// UECS-CCM was dropped in 0.4.0 (MQTT-only; see mqtt_pub.h).
// 0.6.0 adds SCD41 pressure compensation (QMP6988→SCD41) and an FRC/ASC UI.

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
const char *FW_VERSION  = "0.11.0";
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
uint32_t g_scd_amb_p_pa   = 0;
uint32_t g_scd_amb_p_ms   = 0;
bool     g_scd_busy       = false;
SHT3X   g_sht;
QMP6988 g_qmp;
SensirionI2cScd4x g_scd;

// FRC ランタイムステート (RAM のみ。永続化結果は g_cfg.scd_frc_state)
enum FrcRuntimeState : uint8_t {
  FRC_RT_IDLE,
  FRC_RT_WARMING_UP,
  FRC_RT_EXECUTING,
};
FrcRuntimeState g_frc_state         = FRC_RT_IDLE;
uint32_t        g_frc_started_ms    = 0;
uint16_t        g_frc_pending_target = 400;

// ---- Dashboard / Config hooks --------------------------------------------
static String renderDashboardSensors() {
  String s; s.reserve(900);
  char buf[16];
  s = F("<h3>Sensors</h3>");

  if (!g_sht30_ok && !g_qmp_ok && !g_scd41_ok) {
    s += F("<table><tr><th>Sensors</th><td>NONE detected</td></tr></table>");
    return s;
  }

  // SHT30: agriha 公開温湿度のソース (SCD41 内蔵より高精度なので publish 対象)
  // + 飽差 (HD) を SHT30 の T/RH から導出して同ブロックに表示。
  if (g_sht30_ok) {
    s += F("<h4 style='margin:8px 0 4px;color:#9aa'>ENV III — SHT30 (0x44)</h4><table>");
    dtostrf(g_temp_c, 1, 2, buf);
    s += "<tr><th>Temp</th><td>"; s += buf;
    s += F(" °C <span style='color:#7c7;font-size:85%'>&rarr; InAirTemp</span></td></tr>");
    dtostrf(g_humid_pct, 1, 1, buf);
    s += "<tr><th>Humidity</th><td>"; s += buf;
    s += F(" % <span style='color:#7c7;font-size:85%'>&rarr; InAirHumid</span></td></tr>");
    dtostrf(airHd(g_temp_c, g_humid_pct), 1, 2, buf);
    s += "<tr><th>飽差 (HD)</th><td>"; s += buf;
    s += F(" g/m³ <span style='color:#7c7;font-size:85%'>&rarr; InAirHD</span></td></tr>");
    s += F("</table>");
  }

  // QMP6988: 公開気圧 + SCD41 補正用の二役
  if (g_qmp_ok) {
    s += F("<h4 style='margin:8px 0 4px;color:#9aa'>ENV III — QMP6988 (0x70)</h4><table>");
    dtostrf(g_pressure_hpa, 1, 2, buf);
    s += "<tr><th>Pressure</th><td>"; s += buf;
    s += F(" hPa <span style='color:#7c7;font-size:85%'>&rarr; InAirPressure</span></td></tr>");
    s += F("</table>");
  }

  // SCD41: 公開 CO2 + 内蔵 T/RH (参照)、圧力補正の最終投入状態
  if (g_scd41_ok) {
    s += F("<h4 style='margin:8px 0 4px;color:#9aa'>SCD41 (0x62)</h4><table>");
    s += "<tr><th>CO&#8322;</th><td>"; s += g_co2_ppm;
    s += F(" ppm <span style='color:#7c7;font-size:85%'>&rarr; InAirCO2</span></td></tr>");
    // SCD41 内蔵 T/RH は v0.9.0 から publish 対象 (InAirTempSCD41 / InAirHumidSCD41)
    if (!isnan(g_co2_temp_c)) {
      dtostrf(g_co2_temp_c, 1, 2, buf);
      s += "<tr><th>Temp (internal)</th><td>"; s += buf;
      s += F(" °C <span style='color:#7c7;font-size:85%'>&rarr; InAirTempSCD41</span></td></tr>");
    }
    if (!isnan(g_co2_humid_pct)) {
      dtostrf(g_co2_humid_pct, 1, 1, buf);
      s += "<tr><th>Humidity (internal)</th><td>"; s += buf;
      s += F(" % <span style='color:#7c7;font-size:85%'>&rarr; InAirHumidSCD41</span></td></tr>");
    }
    if (g_qmp_ok) {
      s += F("<tr><th>Ambient pressure</th><td>");
      if (g_scd_amb_p_ms == 0) {
        s += F("<span style='color:#bba'>not fed yet</span>");
      } else {
        uint32_t ago_s = (millis() - g_scd_amb_p_ms) / 1000;
        dtostrf(g_scd_amb_p_pa / 100.0f, 1, 2, buf);
        s += buf; s += F(" hPa, "); s += ago_s; s += F(" s ago");
      }
      s += F(" <span style='color:#999;font-size:85%'>(QMP6988 &rarr; SCD41)</span></td></tr>");
    }
    s += F("</table>");
  }
  return s;
}

// ---- FRC display helpers -------------------------------------------------
static String formatFrcTime(long ts) {
  if (ts <= 0) return F("no clock at FRC time");
  char buf[40];
  time_t t = (time_t)ts;
  struct tm tm_;
  gmtime_r(&t, &tm_);
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02dZ",
           tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
           tm_.tm_hour, tm_.tm_min, tm_.tm_sec);
  return String(buf);
}

static String renderFrcRuntimeState() {
  switch (g_frc_state) {
    case FRC_RT_IDLE:
      return F("<span style='color:#888'>idle</span>");
    case FRC_RT_WARMING_UP: {
      uint32_t elapsed = (millis() - g_frc_started_ms) / 1000;
      if (elapsed >= 180)
        return F("<span style='color:#fc6'>ready, executing on next tick…</span>");
      uint32_t rem = 180 - elapsed;
      char buf[96];
      snprintf(buf, sizeof(buf),
        "<span style='color:#fc6'>warming up: %lu:%02lu remaining (target %u ppm)</span>",
        (unsigned long)(rem / 60), (unsigned long)(rem % 60),
        (unsigned)g_frc_pending_target);
      return String(buf);
    }
    case FRC_RT_EXECUTING:
      return F("<span style='color:#fc6'>executing…</span>");
  }
  return F("?");
}

static String renderLastFrc() {
  switch (g_cfg.scd_frc_state) {
    case FRC_RESULT_NEVER:
      return F("<span style='color:#888'>(never run)</span>");
    case FRC_RESULT_OK: {
      String s = F("<span style='color:#7c7'>");
      s += (int)g_cfg.scd_frc_corr >= 0 ? "+" : "";
      s += (int)g_cfg.scd_frc_corr;
      s += F(" ppm, target ");
      s += g_cfg.scd_frc_target;
      s += F(" ppm, ");
      s += formatFrcTime(g_cfg.scd_frc_ts);
      s += F("</span>");
      return s;
    }
    case FRC_RESULT_FAILED: {
      String s = F("<span style='color:#f88'>failed at ");
      s += formatFrcTime(g_cfg.scd_frc_ts);
      s += F(" (sensor not warmed up long enough at the target concentration?)</span>");
      return s;
    }
  }
  return F("?");
}

// ---- Config form: SCD41 calibration rows ---------------------------------
// hooks.renderConfigSensorRows で CCM 識別子 UI (renderCcmTypeRows) と併用。
// 実配線は setup() のラムダで両方連結する。
static String renderScdCalibrationRows() {
  String s; s.reserve(900);
  s += F("<tr><th colspan=2 style='background:#1a1a1f;color:#9ad;text-align:left;padding:8px'>"
         "SCD41 &mdash; CO&#8322; Calibration</th></tr>");

  s += F("<tr><th>ASC enabled</th><td><label><input type=checkbox name=scd_asc");
  if (g_cfg.scd_asc) s += F(" checked");
  s += F("> Automatic Self-Calibration "
         "<span style='color:#888'>(温室では OFF 推奨)</span></label></td></tr>");

  s += F("<tr><th>FRC target (ppm)</th>"
         "<td><input type=number name=scd_frc_tgt min=350 max=2000 value='");
  s += g_cfg.scd_frc_target;
  s += F("'> <span style='color:#888'>外気 = 400 ppm</span></td></tr>");

  s += F("<tr><th>Perform FRC now</th><td><label><input type=checkbox name=scd_frc_now>"
         " 屋外に置いてからチェックして Save。3分後に自動実行</label></td></tr>");

  s += F("<tr><th>Current FRC state</th><td>");
  s += renderFrcRuntimeState();
  s += F("</td></tr>");

  s += F("<tr><th>Last FRC</th><td>");
  s += renderLastFrc();
  s += F("</td></tr>");

  return s;
}

// ---- Config form: apply SCD41 fields -------------------------------------
static void applyScdCalibrationForm(const String &body) {
  bool     new_asc     = agri::parseFormBool(body, "scd_asc");
  uint16_t new_target  = (uint16_t)agri::parseFormInt(body, "scd_frc_tgt", g_cfg.scd_frc_target);
  bool     frc_now     = agri::parseFormBool(body, "scd_frc_now");

  if (new_target < 350 || new_target > 2000) new_target = g_cfg.scd_frc_target;
  g_cfg.scd_frc_target = new_target;

  if (new_asc != g_cfg.scd_asc) {
    g_cfg.scd_asc = new_asc;
    if (g_scd41_ok) {
      Serial.printf("[ASC] applying %s\n", new_asc ? "ON" : "OFF");
      g_scd_busy = true;
      g_scd.stopPeriodicMeasurement();
      delay(500);
      g_scd.setAutomaticSelfCalibrationEnabled(new_asc ? 1 : 0);
      delay(50);
      g_scd.persistSettings();
      delay(800);
      g_scd.startPeriodicMeasurement();
      g_scd_busy = false;
    }
  }

  if (frc_now && g_scd41_ok &&
      g_frc_state != FRC_RT_WARMING_UP && g_frc_state != FRC_RT_EXECUTING) {
    g_frc_state          = FRC_RT_WARMING_UP;
    g_frc_started_ms     = millis();
    g_frc_pending_target = new_target;
    Serial.printf("[FRC] scheduled (target=%u, 3-min warmup)\n", (unsigned)new_target);
  }
}

// ---- FRC tick: drives WARMING_UP → EXECUTING transition ------------------
static void frcTick() {
  if (g_frc_state != FRC_RT_WARMING_UP) return;
  if (millis() - g_frc_started_ms < 180000UL) return;

  g_frc_state = FRC_RT_EXECUTING;
  Serial.printf("[FRC] executing (target=%u)\n", (unsigned)g_frc_pending_target);

  g_scd_busy = true;
  g_scd.stopPeriodicMeasurement();
  delay(500);
  uint16_t correction = 0xFFFF;
  int16_t err = g_scd.performForcedRecalibration(g_frc_pending_target, correction);
  delay(50);
  g_scd.startPeriodicMeasurement();
  g_scd_busy = false;

  g_cfg.scd_frc_ts = mqttNow();
  if (err == 0 && correction != 0xFFFF) {
    g_cfg.scd_frc_corr  = (int16_t)((int32_t)correction - 0x8000);
    g_cfg.scd_frc_state = FRC_RESULT_OK;
    Serial.printf("[FRC] OK: correction=%+d ppm\n", (int)g_cfg.scd_frc_corr);
  } else {
    g_cfg.scd_frc_state = FRC_RESULT_FAILED;
    Serial.printf("[FRC] FAILED: err=%d raw=0x%04x\n", err, (unsigned)correction);
  }
  saveConfig();
  g_frc_state = FRC_RT_IDLE;
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

  g_frc_pending_target = g_cfg.scd_frc_target;
  sensorsBegin(g_cfg.scd_asc);

  agri::Network::begin(g_cfg.common.hostname);
  agri::Network::waitForLease();

  // SNTP for the {value,unit,ts} payloads (UTC epoch; non-blocking, ts=0
  // until the first sync lands).
  configTime(0, 0, "pool.ntp.org");

  agri::MQTT::begin();
  agri::ccmBegin();   // optional ArSprout CCM export (gated by ccm_enabled)

  agri::WebHooks hooks;
  hooks.nodeTitle              = [](){ return FW_NAME; };
  hooks.renderDashboardSensors = renderDashboardSensors;
  // Config page sensor block = CCM 識別子 + SCD41 校正 の連結。
  hooks.renderConfigSensorRows = [](){ return renderCcmTypeRows() + renderScdCalibrationRows(); };
  hooks.applyConfigSensorForm  = [](const String &body){
    applyCcmTypeForm(body);
    applyScdCalibrationForm(body);
  };
  hooks.addStatusFields        = addStatusFields;
  hooks.saveConfig             = [](){ saveConfig(); };
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

  frcTick();

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
