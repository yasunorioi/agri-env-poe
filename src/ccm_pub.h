// ccm_pub.h — optional UECS-CCM export (off by default).
//
// agri-env is agriha-MQTT-native; this path exists only for the "drop our
// sensor into an existing ArSprout greenhouse" use case. Enable via /config
// (ccm_en). Core sends to the limited broadcast address, which is what
// ArSprout actually receives (see AgriCCM.h CCM_BROADCAST).
//
// CCM type/room/region/order must match the receive CCM configured in
// ArSprout (e.g. InAirTemp 1/13/1). order is fixed at 1; room/region/
// priority come from CommonConfig.

#pragma once

#include <Arduino.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline bool ccmPublishOne(const char *type, float v, int decimals) {
  char buf[16];
  dtostrf(v, 1, decimals, buf);
  // Append the configured ArSprout node-type suffix: "<Type>.<ntype>"
  // (e.g. InAirTemp.cMC). Empty ntype = bare type.
  String fullType = type;
  if (g_cfg.common.ccm_ntype[0]) { fullType += '.'; fullType += g_cfg.common.ccm_ntype; }
  String xml = agri::ccmEnvelopeOpen();
  xml += agri::ccmDatum(fullType.c_str(),
                        g_cfg.common.ccm_room,
                        g_cfg.common.ccm_region,
                        /*order=*/1,
                        g_cfg.common.ccm_priority,
                        buf);
  xml += agri::ccmEnvelopeClose();
  return agri::ccmSend(xml);
}

inline bool ccmPublish() {
  if (!g_cfg.common.ccm_enabled) return false;
  bool any = false;
  if (g_sht30_ok && !isnan(g_temp_c)) {
    any |= ccmPublishOne("InAirTemp",  g_temp_c,      2);
    any |= ccmPublishOne("InAirHumid", g_humid_pct,   1);
  }
  if (g_qmp_ok && !isnan(g_pressure_hpa))
    any |= ccmPublishOne("InAirPressure", g_pressure_hpa, 2);
  if (g_scd41_ok && g_co2_ppm > 0)
    any |= ccmPublishOne("InAirCO2", (float)g_co2_ppm, 0);
  return any;
}
