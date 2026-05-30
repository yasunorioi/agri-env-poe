// config.h — agri-env-poe NVS-backed config. Wraps the library's
// CommonConfig with the 4 CCM channel orders (temp / humid / pressure / co2).

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AgriCommonConfig.h>

struct AppConfig {
  agri::CommonConfig common;
  int16_t            ccm_order_temp;
  int16_t            ccm_order_humid;
  int16_t            ccm_order_pressure;
  int16_t            ccm_order_co2;
};

extern AppConfig g_cfg;

inline void setDefaults() {
  agri::commonDefaults(g_cfg.common,
                       "env_node_01", "agri-env-01",
                       "agri/env/01",
                       /*default_ccm_region=*/11);
  g_cfg.ccm_order_temp     = 1;
  g_cfg.ccm_order_humid    = 1;
  g_cfg.ccm_order_pressure = 1;
  g_cfg.ccm_order_co2      = 1;
}

inline void loadConfig() {
  setDefaults();
  Preferences p;
  if (!p.begin("env-cfg", true)) return;
  agri::commonLoad(g_cfg.common, p);
  g_cfg.ccm_order_temp     = p.getShort("ccm_ot", g_cfg.ccm_order_temp);
  g_cfg.ccm_order_humid    = p.getShort("ccm_oh", g_cfg.ccm_order_humid);
  g_cfg.ccm_order_pressure = p.getShort("ccm_op", g_cfg.ccm_order_pressure);
  g_cfg.ccm_order_co2      = p.getShort("ccm_oc", g_cfg.ccm_order_co2);
  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("env-cfg", false)) return false;
  agri::commonSave(g_cfg.common, p);
  p.putShort("ccm_ot", g_cfg.ccm_order_temp);
  p.putShort("ccm_oh", g_cfg.ccm_order_humid);
  p.putShort("ccm_op", g_cfg.ccm_order_pressure);
  p.putShort("ccm_oc", g_cfg.ccm_order_co2);
  p.end();
  return true;
}
