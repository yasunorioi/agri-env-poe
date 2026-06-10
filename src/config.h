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
}

inline void loadConfig() {
  setDefaults();
  Preferences p;
  if (!p.begin("env-cfg", true)) return;
  agri::commonLoad(g_cfg.common, p);
  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("env-cfg", false)) return false;
  agri::commonSave(g_cfg.common, p);
  p.end();
  return true;
}
