// sensors.h — ENV III (SHT30 + QMP6988) and SCD41 CO2 reads, globals exposed
// to the publishers and the dashboard.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SHT3X.h>
#include <QMP6988.h>
#include <SensirionI2cScd4x.h>

// I2C on M5 ATOM Grove
static const int PIN_SDA = 26;
static const int PIN_SCL = 32;
static const long I2C_FREQ_HZ = 100000;  // SCD41 max is 100 kHz

// ---- live values ---------------------------------------------------------
extern float    g_temp_c;
extern float    g_humid_pct;
extern float    g_pressure_hpa;
extern uint16_t g_co2_ppm;
extern float    g_co2_temp_c;
extern float    g_co2_humid_pct;

extern bool g_sht30_ok;
extern bool g_qmp_ok;
extern bool g_scd41_ok;
extern uint32_t g_last_sensor_ms;

// SCD41 圧力補正トラッキング。QMP6988 の現在値を setAmbientPressure() に流すが、
// I2C 負荷を抑えるため ±0.5 hPa (=50 Pa) 変化 or 5 分経過の早い方で間引く。
// _ms == 0 は「まだ一度も投入していない」状態 (起動直後)。
extern uint32_t g_scd_amb_p_pa;
extern uint32_t g_scd_amb_p_ms;

// FRC / ASC apply 中は SCD41 が IDLE モードに入る (periodic 停止)。この間は
// getDataReadyStatus / readMeasurement / setAmbientPressure が全部エラーになるので
// sensorsPoll() ごとスキップする。
extern bool g_scd_busy;

// ---- driver instances ----------------------------------------------------
extern SHT3X         g_sht;
extern QMP6988       g_qmp;
extern SensirionI2cScd4x g_scd;

// asc_enabled: NVS から渡された ASC 状態。boot 時に stop → setASC → persist → start で
// sensor を NVS に追従させる (これで「NVS が真実」になり、UI と実状態が一致する)。
inline void sensorsBegin(bool asc_enabled) {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_FREQ_HZ);

  g_sht30_ok = g_sht.begin(&Wire, 0x44, PIN_SDA, PIN_SCL, I2C_FREQ_HZ);
  Serial.printf("[SENS] SHT30: %s\n", g_sht30_ok ? "OK" : "MISSING");

  g_qmp_ok = g_qmp.begin(&Wire, 0x70, PIN_SDA, PIN_SCL, I2C_FREQ_HZ);
  Serial.printf("[SENS] QMP6988: %s\n", g_qmp_ok ? "OK" : "MISSING");

  g_scd.begin(Wire, SCD41_I2C_ADDR_62);
  uint16_t err = g_scd.stopPeriodicMeasurement();
  delay(500);
  // ASC を NVS に合わせて persistSettings()。EEPROM 書き込みは ~800ms かかる。
  uint16_t asc_err = g_scd.setAutomaticSelfCalibrationEnabled(asc_enabled ? 1 : 0);
  if (asc_err == 0) g_scd.persistSettings();
  delay(800);
  err = g_scd.startPeriodicMeasurement();
  g_scd41_ok = (err == 0);
  Serial.printf("[SENS] SCD41: %s (err=%u, asc=%s)\n",
                g_scd41_ok ? "OK" : "MISSING", err, asc_enabled ? "on" : "off");
}

inline void sensorsPoll() {
  uint32_t now = millis();

  if (g_sht30_ok && g_sht.update()) {
    g_temp_c    = g_sht.cTemp;
    g_humid_pct = g_sht.humidity;
  }
  if (g_qmp_ok) {
    g_pressure_hpa = g_qmp.calcPressure() / 100.0f;  // Pa -> hPa
  }
  if (g_scd41_ok && !g_scd_busy) {
    bool data_ready = false;
    if (g_scd.getDataReadyStatus(data_ready) == 0 && data_ready) {
      uint16_t co2 = 0;
      float temp = NAN, humid = NAN;
      if (g_scd.readMeasurement(co2, temp, humid) == 0 && co2 > 0) {
        g_co2_ppm      = co2;
        g_co2_temp_c   = temp;
        g_co2_humid_pct = humid;
      }
    }

    // 圧力補正: QMP6988 の hPa を Pa にしてセンサに投入 (periodic 中で OK)。
    // 70-120 kPa の有効範囲外は QMP 異常とみなして skip。
    if (g_qmp_ok && !isnan(g_pressure_hpa)) {
      uint32_t pa = (uint32_t)(g_pressure_hpa * 100.0f + 0.5f);
      if (pa >= 70000 && pa <= 120000) {
        bool first   = (g_scd_amb_p_ms == 0);
        bool stale   = !first && (now - g_scd_amb_p_ms) >= 300000UL;     // 5 min
        int32_t d    = (int32_t)pa - (int32_t)g_scd_amb_p_pa;
        bool changed = !first && (d >= 50 || d <= -50);                   // 0.5 hPa
        if (first || stale || changed) {
          if (g_scd.setAmbientPressure(pa) == 0) {
            g_scd_amb_p_pa = pa;
            g_scd_amb_p_ms = now;
          }
        }
      }
    }
  }

  g_last_sensor_ms = now;
}

// Saturation deficit / 室内飽差 (g/m³) derived from temperature (°C) and RH (%).
// Matches the farm's earlier CCM node (calc_vpd): Tetens saturation vapour
// pressure es (hPa) -> saturated vapour density 217*es/T_K, then
// HD = density * (1 - RH/100). Exposed as ArSprout CCM type "InAirHD".
inline float airHd(float t_c, float rh_pct) {
  float es = 6.1078f * powf(10.0f, (7.5f * t_c) / (t_c + 237.3f));
  float hd = 217.0f * es * (1.0f - rh_pct / 100.0f) / (t_c + 273.15f);
  return hd < 0.0f ? 0.0f : hd;
}
