// ccm_pub.h — env (Temp/Humid/Pressure/CO2) UECS-CCM publisher.

#pragma once

#include <Arduino.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline bool ccmPublish() {
  if (!g_cfg.common.ccm_enabled) return false;

  String xml = agri::ccmEnvelopeOpen();
  char buf[16];

  if (g_sht30_ok && !isnan(g_temp_c)) {
    dtostrf(g_temp_c, 1, 2, buf);
    xml += agri::ccmDatum("InAirTemp",
                          g_cfg.common.ccm_room, g_cfg.common.ccm_region,
                          g_cfg.ccm_order_temp, g_cfg.common.ccm_priority, buf);
  }
  if (g_sht30_ok && !isnan(g_humid_pct)) {
    dtostrf(g_humid_pct, 1, 1, buf);
    xml += agri::ccmDatum("InAirHumid",
                          g_cfg.common.ccm_room, g_cfg.common.ccm_region,
                          g_cfg.ccm_order_humid, g_cfg.common.ccm_priority, buf);
  }
  if (g_qmp_ok && !isnan(g_pressure_hpa)) {
    dtostrf(g_pressure_hpa, 1, 2, buf);
    xml += agri::ccmDatum("InAirPressure",
                          g_cfg.common.ccm_room, g_cfg.common.ccm_region,
                          g_cfg.ccm_order_pressure, g_cfg.common.ccm_priority, buf);
  }
  if (g_scd41_ok && g_co2_ppm > 0) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_co2_ppm);
    xml += agri::ccmDatum("InAirCO2",
                          g_cfg.common.ccm_room, g_cfg.common.ccm_region,
                          g_cfg.ccm_order_co2, g_cfg.common.ccm_priority, buf);
  }

  xml += agri::ccmEnvelopeClose();
  return agri::ccmSend(xml);
}
