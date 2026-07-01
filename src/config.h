// config.h — agri-env-poe NVS-backed config. CommonConfig + per-sensor
// CCM 識別子 (wire type names) + SCD41 校正状態 (ASC enable, FRC target,
// FRC 結果) を持つ。FRC 進行中の状態 (WARMING_UP 等) は RAM 専用で
// main.cpp 側にある。

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AgriCommonConfig.h>

// FRC 結果ステート (NVS scd_frc_state 用)。値は永続化される。
enum FrcResultState : uint8_t {
  FRC_RESULT_NEVER  = 0,
  FRC_RESULT_OK     = 1,
  FRC_RESULT_FAILED = 2,
};

struct AppConfig {
  agri::CommonConfig common;
  // CCM識別子 = the <DATA type=...> wire name per measurement (UECS type,
  // e.g. InAirTemp). Per-sensor, so it lives here, not in core's envelope
  // (core owns room/region/priority/ntype). Mainly for inspection; editable
  // so the wire type can be matched to an ArSprout receive-CCM without a
  // reflash. Empty = that datum is not sent.
  char ccm_type_temp[16];
  char ccm_type_humid[16];
  char ccm_type_hd[16];
  char ccm_type_press[16];
  char ccm_type_co2[16];
  // SCD41 校正状態
  bool     scd_asc;            // Automatic Self-Calibration
  uint16_t scd_frc_target;     // FRC target ppm (year-1-once)
  long     scd_frc_ts;         // 最終 FRC unix epoch (0 = SNTP 未同期で実施)
  int16_t  scd_frc_corr;       // 最終 correction ppm (signed)
  uint8_t  scd_frc_state;      // FrcResultState
};

extern AppConfig g_cfg;

inline void setDefaults() {
  agri::commonDefaults(g_cfg.common,
                       "env_node_01", "agri-env-01",
                       /*mqtt prefix = house scope*/ "agriha/2",
                       /*default_ccm_region = 別棟 ArSprout region*/13);
  // ArSprout receive CCMs are configured at priority 1; match it (core
  // default 29 is fine for spec managers). CCM itself stays off by default —
  // enable per-site via /config (ccm_en) when feeding an ArSprout.
  g_cfg.common.ccm_priority = 1;
  strlcpy(g_cfg.ccm_type_temp,  "InAirTemp",     sizeof(g_cfg.ccm_type_temp));
  strlcpy(g_cfg.ccm_type_humid, "InAirHumid",    sizeof(g_cfg.ccm_type_humid));
  strlcpy(g_cfg.ccm_type_hd,    "InAirHD",       sizeof(g_cfg.ccm_type_hd));
  strlcpy(g_cfg.ccm_type_press, "InAirPressure", sizeof(g_cfg.ccm_type_press));
  strlcpy(g_cfg.ccm_type_co2,   "InAirCO2",      sizeof(g_cfg.ccm_type_co2));
  g_cfg.scd_asc        = false;   // 温室用途では OFF 既定 (年1回 FRC で補正)
  g_cfg.scd_frc_target = 400;
  g_cfg.scd_frc_ts     = 0;
  g_cfg.scd_frc_corr   = 0;
  g_cfg.scd_frc_state  = FRC_RESULT_NEVER;
}

inline void loadConfig() {
  setDefaults();
  Preferences p;
  if (!p.begin("env-cfg", true)) return;
  agri::commonLoad(g_cfg.common, p);
  // String-default overload: a pre-existing NVS without these keys keeps the
  // setDefaults() value instead of being wiped to "" (same guard as ccm_nt).
  auto loadType = [&](const char *key, char *dst, size_t n) {
    String v = p.getString(key, dst);
    strlcpy(dst, v.c_str(), n);
  };
  loadType("ct_temp",  g_cfg.ccm_type_temp,  sizeof(g_cfg.ccm_type_temp));
  loadType("ct_humid", g_cfg.ccm_type_humid, sizeof(g_cfg.ccm_type_humid));
  loadType("ct_hd",    g_cfg.ccm_type_hd,    sizeof(g_cfg.ccm_type_hd));
  loadType("ct_press", g_cfg.ccm_type_press, sizeof(g_cfg.ccm_type_press));
  loadType("ct_co2",   g_cfg.ccm_type_co2,   sizeof(g_cfg.ccm_type_co2));
  g_cfg.scd_asc        = p.getBool ("scd_asc",       g_cfg.scd_asc);
  g_cfg.scd_frc_target = p.getUShort("scd_frc_tgt",  g_cfg.scd_frc_target);
  g_cfg.scd_frc_ts     = p.getLong ("scd_frc_ts",    g_cfg.scd_frc_ts);
  g_cfg.scd_frc_corr   = p.getShort("scd_frc_corr",  g_cfg.scd_frc_corr);
  g_cfg.scd_frc_state  = p.getUChar("scd_frc_state", g_cfg.scd_frc_state);
  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("env-cfg", false)) return false;
  agri::commonSave(g_cfg.common, p);
  p.putString("ct_temp",  g_cfg.ccm_type_temp);
  p.putString("ct_humid", g_cfg.ccm_type_humid);
  p.putString("ct_hd",    g_cfg.ccm_type_hd);
  p.putString("ct_press", g_cfg.ccm_type_press);
  p.putString("ct_co2",   g_cfg.ccm_type_co2);
  p.putBool  ("scd_asc",       g_cfg.scd_asc);
  p.putUShort("scd_frc_tgt",   g_cfg.scd_frc_target);
  p.putLong  ("scd_frc_ts",    g_cfg.scd_frc_ts);
  p.putShort ("scd_frc_corr",  g_cfg.scd_frc_corr);
  p.putUChar ("scd_frc_state", g_cfg.scd_frc_state);
  p.end();
  return true;
}
