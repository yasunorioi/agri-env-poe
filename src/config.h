// config.h — agri-env-poe NVS-backed config. Just the library's
// CommonConfig now: the per-sensor CCM channel orders went away with CCM
// itself (MQTT-only node since 0.4.0).

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AgriCommonConfig.h>

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
  p.end();
  return true;
}
