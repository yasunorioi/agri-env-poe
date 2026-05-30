// ccm_pub.h — UECS-CCM packet builder + UDP multicast publisher.
//
// Packet shape (matches ccm_rp2350_relay / OGMS conventions):
//   <UECS ver="1.00-E10">
//     <DATA type="InAirTemp.cMC" room="1" region="11" order="1"
//           priority="29" lv="S" cast="uni">23.5</DATA>
//     ...
//   </UECS>

#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include "config.h"
#include "sensors.h"

static const IPAddress CCM_MULTICAST(224, 0, 0, 1);
static const uint16_t  CCM_PORT      = 16520;
static const char* const UECS_VERSION = "1.00-E10";

extern EthernetUDP g_ccmUDP;

inline void ccmBegin() {
  // Arduino Ethernet's EthernetUDP doesn't expose a multicast TX bind; we
  // just open the UDP socket and use beginPacket() with the multicast IP.
  g_ccmUDP.begin(CCM_PORT);
}

inline String buildOneDatum(const char *type, int order, int region,
                            const char *value) {
  String s;
  s.reserve(160);
  s  = "<DATA type=\"";
  s += type;
  s += ".cMC\" room=\"";
  s += g_cfg.ccm_room;
  s += "\" region=\"";
  s += region;
  s += "\" order=\"";
  s += order;
  s += "\" priority=\"";
  s += g_cfg.ccm_priority;
  s += "\" lv=\"S\" cast=\"uni\">";
  s += value;
  s += "</DATA>";
  return s;
}

inline bool ccmPublish() {
  if (!g_cfg.ccm_enabled) return false;

  String xml;
  xml.reserve(640);
  xml  = "<UECS ver=\"";
  xml += UECS_VERSION;
  xml += "\">";

  char buf[16];

  if (g_sht30_ok && !isnan(g_temp_c)) {
    dtostrf(g_temp_c, 1, 2, buf);
    xml += buildOneDatum("InAirTemp", g_cfg.ccm_order_temp, g_cfg.ccm_region, buf);
  }
  if (g_sht30_ok && !isnan(g_humid_pct)) {
    dtostrf(g_humid_pct, 1, 1, buf);
    xml += buildOneDatum("InAirHumid", g_cfg.ccm_order_humid, g_cfg.ccm_region, buf);
  }
  if (g_qmp_ok && !isnan(g_pressure_hpa)) {
    dtostrf(g_pressure_hpa, 1, 2, buf);
    // UECS convention: pressure type name varies by deployment. InAirPressure
    // is the most readable; switch via the type table if your central uses
    // something else.
    xml += buildOneDatum("InAirPressure", g_cfg.ccm_order_pressure, g_cfg.ccm_region, buf);
  }
  if (g_scd41_ok && g_co2_ppm > 0) {
    snprintf(buf, sizeof(buf), "%u", (unsigned)g_co2_ppm);
    xml += buildOneDatum("InAirCO2", g_cfg.ccm_order_co2, g_cfg.ccm_region, buf);
  }

  xml += "</UECS>";

  if (!g_ccmUDP.beginPacket(CCM_MULTICAST, CCM_PORT)) return false;
  g_ccmUDP.write((const uint8_t*)xml.c_str(), xml.length());
  bool ok = g_ccmUDP.endPacket();
  if (ok) Serial.printf("[CCM] TX %u bytes\n", (unsigned)xml.length());
  return ok;
}
