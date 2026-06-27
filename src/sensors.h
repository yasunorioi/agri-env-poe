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

// ---- driver instances ----------------------------------------------------
extern SHT3X         g_sht;
extern QMP6988       g_qmp;
extern SensirionI2cScd4x g_scd;

inline void sensorsBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_FREQ_HZ);

  g_sht30_ok = g_sht.begin(&Wire, 0x44, PIN_SDA, PIN_SCL, I2C_FREQ_HZ);
  Serial.printf("[SENS] SHT30: %s\n", g_sht30_ok ? "OK" : "MISSING");

  g_qmp_ok = g_qmp.begin(&Wire, 0x70, PIN_SDA, PIN_SCL, I2C_FREQ_HZ);
  Serial.printf("[SENS] QMP6988: %s\n", g_qmp_ok ? "OK" : "MISSING");

  g_scd.begin(Wire, SCD41_I2C_ADDR_62);
  // SCD4x periodic measurement — first sample ~5s later
  uint16_t err = g_scd.stopPeriodicMeasurement();
  delay(500);
  err = g_scd.startPeriodicMeasurement();
  g_scd41_ok = (err == 0);
  Serial.printf("[SENS] SCD41: %s (err=%u)\n", g_scd41_ok ? "OK" : "MISSING", err);
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
  if (g_scd41_ok) {
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
