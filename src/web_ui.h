// web_ui.h — minimal 3-page HTTP server (Dashboard / Config / About)
// + JSON API. Hand-rolled because Arduino-Ethernet's EthernetServer is
// already small enough to use directly.

#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors.h"
#include "mqtt_pub.h"

// arduino-libraries/Ethernet on ESP32 leaves Server::begin(uint16_t) pure
// virtual (the lib only has begin() with no port). Provide the missing
// override so the class is instantiable.
class EthernetServerWrap : public EthernetServer {
 public:
  explicit EthernetServerWrap(uint16_t port) : EthernetServer(port) {}
  using EthernetServer::begin;
  void begin(uint16_t port) override { (void)port; EthernetServer::begin(); }
};

extern EthernetServerWrap g_webServer;
extern const char *FW_VERSION;
extern const char *FW_NAME;

// ---------- helpers --------------------------------------------------------
inline String urlDecode(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

inline void parseFormField(const String &body, const char *key, char *dst, size_t dstSz) {
  String needle = String(key) + "=";
  int p = body.indexOf(needle);
  if (p < 0) return;
  p += needle.length();
  int e = body.indexOf('&', p);
  String v = (e < 0) ? body.substring(p) : body.substring(p, e);
  String dec = urlDecode(v);
  strlcpy(dst, dec.c_str(), dstSz);
}

inline int parseFormInt(const String &body, const char *key, int defv) {
  String needle = String(key) + "=";
  int p = body.indexOf(needle);
  if (p < 0) return defv;
  p += needle.length();
  int e = body.indexOf('&', p);
  String v = (e < 0) ? body.substring(p) : body.substring(p, e);
  return v.toInt();
}

inline bool parseFormBool(const String &body, const char *key) {
  String needle = String(key) + "=";
  int p = body.indexOf(needle);
  return p >= 0;  // checkbox: present at all = true
}

// ---------- page builders --------------------------------------------------
inline String pageHead(const char *title) {
  String s;
  s.reserve(600);
  s  = F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=UTF-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>");
  s += title;
  s += F("</title><style>"
         "body{font-family:sans-serif;margin:16px;background:#0f1011;color:#f7f8f8}"
         "h2{color:#5e6ad2}h3{color:#d0d6e0}"
         ".sec{background:#191a1b;border-radius:6px;padding:12px;margin:8px 0}"
         "table{border-collapse:collapse;width:100%;margin:6px 0}"
         "th,td{border:1px solid #2e2e2e;padding:5px 8px}"
         "th{background:#191a1b;color:#d0d6e0}"
         "input,select{padding:5px;background:#1a1a1f;color:#eee;border:1px solid #3e3e44;border-radius:3px}"
         "input[type=submit]{background:#1976d2;color:#fff;border:none;padding:8px 20px;cursor:pointer}"
         "a{color:#d0d6e0}"
         "</style></head><body>");
  s += F("<h2>agri-env-poe</h2><p>"
         "<a href='/'>Dashboard</a> | <a href='/config'>Config</a> | <a href='/about'>About</a>"
         "</p>");
  return s;
}

inline String pageDashboard() {
  String s = pageHead("Dashboard");
  s += F("<div class=sec id=sens>Loading…</div>"
         "<div class=sec id=net>Loading…</div>"
         "<script>"
         "async function tick(){"
         "let r=await fetch('/api/status');let d=await r.json();"
         "let s='<h3>Sensors</h3><table>'"
         "+'<tr><th>Temp</th><td>'+(d.temp_c?.toFixed(2)??'—')+' °C</td></tr>'"
         "+'<tr><th>Humidity</th><td>'+(d.humid_pct?.toFixed(1)??'—')+' %</td></tr>'"
         "+'<tr><th>Pressure</th><td>'+(d.pressure_hpa?.toFixed(2)??'—')+' hPa</td></tr>'"
         "+'<tr><th>CO₂</th><td>'+(d.co2_ppm??'—')+' ppm</td></tr>'"
         "+'</table>';"
         "document.getElementById('sens').innerHTML=s;"
         "let n='<h3>Network</h3><table>'"
         "+'<tr><th>IP</th><td>'+d.ip+'</td></tr>'"
         "+'<tr><th>Link</th><td>'+d.link+'</td></tr>'"
         "+'<tr><th>MQTT</th><td>'+(d.mqtt_connected?'connected':d.mqtt_host?'not connected':'not configured')+'</td></tr>'"
         "+'<tr><th>CCM</th><td>'+(d.ccm_enabled?'enabled':'disabled')+'</td></tr>'"
         "+'<tr><th>Uptime</th><td>'+d.uptime_s+' s</td></tr>'"
         "+'</table>';"
         "document.getElementById('net').innerHTML=n;"
         "}tick();setInterval(tick,3000);"
         "</script></body></html>");
  return s;
}

inline String pageConfig() {
  String s = pageHead("Config");
  s += F("<div class=sec><h3>MQTT</h3><form method=POST action='/config'>"
         "<table>");
  auto row = [&](const char *label, const String &input) {
    s += "<tr><th>"; s += label; s += "</th><td>"; s += input; s += "</td></tr>";
  };
  row("Node ID",     "<input name=node_id value='"   + String(g_cfg.node_id)   + "'>");
  row("Hostname",    "<input name=hostname value='"  + String(g_cfg.hostname)  + "'>");
  row("MQTT Host",   "<input name=mq_host value='"   + String(g_cfg.mqtt_host) + "'>");
  row("MQTT Port",   "<input type=number name=mq_port value='" + String(g_cfg.mqtt_port) + "'>");
  row("MQTT User",   "<input name=mq_user value='"   + String(g_cfg.mqtt_user) + "'>");
  row("MQTT Pass",   "<input type=password name=mq_pass value='" + String(g_cfg.mqtt_pass) + "'>");
  row("Topic Prefix","<input name=mq_pfx value='"    + String(g_cfg.mqtt_topic_prefix) + "'>");
  row("MQTT interval (s)", "<input type=number name=mq_int value='" + String(g_cfg.mqtt_interval_s) + "'>");
  s += F("</table><h3>UECS-CCM</h3><table>");
  row("CCM enabled", String("<input type=checkbox name=ccm_en ") + (g_cfg.ccm_enabled ? "checked" : "") + ">");
  row("CCM interval (s)", "<input type=number name=ccm_int value='" + String(g_cfg.ccm_interval_s) + "'>");
  row("Room",     "<input type=number name=ccm_room value='" + String(g_cfg.ccm_room) + "'>");
  row("Region",   "<input type=number name=ccm_reg value='" + String(g_cfg.ccm_region) + "'>");
  row("Order (Temp)",     "<input type=number name=ccm_ot value='" + String(g_cfg.ccm_order_temp) + "'>");
  row("Order (Humid)",    "<input type=number name=ccm_oh value='" + String(g_cfg.ccm_order_humid) + "'>");
  row("Order (Pressure)", "<input type=number name=ccm_op value='" + String(g_cfg.ccm_order_pressure) + "'>");
  row("Order (CO2)",      "<input type=number name=ccm_oc value='" + String(g_cfg.ccm_order_co2) + "'>");
  row("Priority", "<input type=number name=ccm_pri value='" + String(g_cfg.ccm_priority) + "'>");
  s += F("</table><p><input type=submit value='Save'></p></form></div></body></html>");
  return s;
}

inline String pageAbout() {
  String s = pageHead("About");
  uint8_t mac[6];
  Ethernet.MACAddress(mac);
  char macStr[20];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  s += F("<div class=sec><h3>About</h3><table>");
  s += "<tr><th>Firmware</th><td>"; s += FW_NAME; s += " "; s += FW_VERSION; s += "</td></tr>";
  s += "<tr><th>Node ID</th><td>";  s += g_cfg.node_id; s += "</td></tr>";
  s += "<tr><th>MAC</th><td>";      s += macStr; s += "</td></tr>";
  s += "<tr><th>IP</th><td>";       s += Ethernet.localIP().toString(); s += "</td></tr>";
  s += "<tr><th>Uptime</th><td>";   s += String(millis() / 1000); s += " s</td></tr>";
  s += F("</table></div></body></html>");
  return s;
}

inline String statusJson(bool linkUp, bool haveLease) {
  JsonDocument doc;
  if (g_sht30_ok) {
    doc["temp_c"]    = g_temp_c;
    doc["humid_pct"] = g_humid_pct;
  }
  if (g_qmp_ok)   doc["pressure_hpa"] = g_pressure_hpa;
  if (g_scd41_ok) doc["co2_ppm"]      = g_co2_ppm;
  doc["uptime_s"]   = millis() / 1000;
  doc["ip"]         = Ethernet.localIP().toString();
  doc["link"]       = linkUp ? (haveLease ? "up" : "no-lease") : "down";
  doc["mqtt_host"]  = g_cfg.mqtt_host[0] ? g_cfg.mqtt_host : "";
  doc["mqtt_connected"] = g_mqtt.connected();
  doc["ccm_enabled"]    = g_cfg.ccm_enabled;
  String out;
  serializeJson(doc, out);
  return out;
}

// ---------- request loop ---------------------------------------------------
inline void sendResponse(EthernetClient &c, int status, const char *contentType,
                         const String &body) {
  c.print("HTTP/1.0 "); c.print(status); c.println(status == 200 ? " OK" : (status == 303 ? " See Other" : " Error"));
  c.print("Content-Type: "); c.println(contentType);
  c.print("Content-Length: "); c.println(body.length());
  c.println("Connection: close");
  c.println();
  c.print(body);
}

inline void sendRedirect(EthernetClient &c, const char *location) {
  c.println("HTTP/1.0 303 See Other");
  c.print("Location: "); c.println(location);
  c.println("Content-Length: 0");
  c.println("Connection: close");
  c.println();
}

inline void handleWebClient(bool linkUp, bool haveLease) {
  EthernetClient client = g_webServer.available();
  if (!client) return;

  // Read request line
  String reqLine;
  uint32_t t0 = millis();
  while (client.connected() && millis() - t0 < 500) {
    if (client.available()) {
      char ch = client.read();
      if (ch == '\n') break;
      if (ch != '\r') reqLine += ch;
    }
  }

  int sp1 = reqLine.indexOf(' ');
  int sp2 = (sp1 >= 0) ? reqLine.indexOf(' ', sp1 + 1) : -1;
  if (sp1 < 0 || sp2 < 0) { client.stop(); return; }
  String method = reqLine.substring(0, sp1);
  String path   = reqLine.substring(sp1 + 1, sp2);

  // Drain headers, read body length
  int contentLen = 0;
  while (client.connected() && millis() - t0 < 1000) {
    String h;
    while (client.available()) {
      char ch = client.read();
      if (ch == '\n') break;
      if (ch != '\r') h += ch;
    }
    if (h.length() == 0) break;
    if (h.startsWith("Content-Length:")) {
      contentLen = h.substring(15).toInt();
    }
  }

  String body;
  if (method == "POST" && contentLen > 0) {
    body.reserve(contentLen);
    while ((int)body.length() < contentLen && millis() - t0 < 2000) {
      if (client.available()) body += (char)client.read();
    }
  }

  if (method == "GET" && path == "/") {
    sendResponse(client, 200, "text/html; charset=utf-8", pageDashboard());
  } else if (method == "GET" && path == "/config") {
    sendResponse(client, 200, "text/html; charset=utf-8", pageConfig());
  } else if (method == "POST" && path == "/config") {
    parseFormField(body, "node_id",  g_cfg.node_id,           sizeof(g_cfg.node_id));
    parseFormField(body, "hostname", g_cfg.hostname,          sizeof(g_cfg.hostname));
    parseFormField(body, "mq_host",  g_cfg.mqtt_host,         sizeof(g_cfg.mqtt_host));
    g_cfg.mqtt_port = (uint16_t)parseFormInt(body, "mq_port", g_cfg.mqtt_port);
    parseFormField(body, "mq_user",  g_cfg.mqtt_user,         sizeof(g_cfg.mqtt_user));
    parseFormField(body, "mq_pass",  g_cfg.mqtt_pass,         sizeof(g_cfg.mqtt_pass));
    parseFormField(body, "mq_pfx",   g_cfg.mqtt_topic_prefix, sizeof(g_cfg.mqtt_topic_prefix));
    g_cfg.mqtt_interval_s = (uint16_t)parseFormInt(body, "mq_int", g_cfg.mqtt_interval_s);
    g_cfg.ccm_enabled         = parseFormBool(body, "ccm_en");
    g_cfg.ccm_interval_s      = (uint16_t)parseFormInt(body, "ccm_int", g_cfg.ccm_interval_s);
    g_cfg.ccm_room            = (int16_t)parseFormInt(body, "ccm_room", g_cfg.ccm_room);
    g_cfg.ccm_region          = (int16_t)parseFormInt(body, "ccm_reg",  g_cfg.ccm_region);
    g_cfg.ccm_order_temp      = (int16_t)parseFormInt(body, "ccm_ot",   g_cfg.ccm_order_temp);
    g_cfg.ccm_order_humid     = (int16_t)parseFormInt(body, "ccm_oh",   g_cfg.ccm_order_humid);
    g_cfg.ccm_order_pressure  = (int16_t)parseFormInt(body, "ccm_op",   g_cfg.ccm_order_pressure);
    g_cfg.ccm_order_co2       = (int16_t)parseFormInt(body, "ccm_oc",   g_cfg.ccm_order_co2);
    g_cfg.ccm_priority        = (int16_t)parseFormInt(body, "ccm_pri",  g_cfg.ccm_priority);
    saveConfig();
    sendRedirect(client, "/config");
  } else if (method == "GET" && path == "/about") {
    sendResponse(client, 200, "text/html; charset=utf-8", pageAbout());
  } else if (method == "GET" && path == "/api/status") {
    sendResponse(client, 200, "application/json", statusJson(linkUp, haveLease));
  } else if (method == "GET" && path == "/api/config") {
    sendResponse(client, 200, "application/json", configToJson());
  } else {
    sendResponse(client, 404, "text/plain", "not found\n");
  }

  client.flush();
  client.stop();
}
