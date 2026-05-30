// agri-env-poe — M5Stack ATOM PoE environmental node
//   ENV III (SHT30 + QMP6988) + SCD41 CO2  →  MQTT + UECS-CCM multicast
//
// Hardware
//   M5Stack ATOM Lite + ATOM PoE base (W5500 on SPI)
//   Grove I2C HUB on G26 (SDA) / G32 (SCL)
//     - ENV III: SHT30 0x44, QMP6988 0x70
//     - SCD41:   0x62
//
// Built on arduino-esp32 3.x (via the pioarduino fork). W5500 runs through
// the ESP-IDF spi_w5500 driver + lwIP so ESPmDNS and ArduinoOTA work.

#include <Arduino.h>
#include <SPI.h>
#include <ETH.h>
#include <Network.h>
#include <NetworkUdp.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <Wire.h>
#include <SHT3X.h>
#include <QMP6988.h>
#include <SensirionI2cScd4x.h>

#include "config.h"
#include "sensors.h"
#include "ccm_pub.h"
#include "mqtt_pub.h"
#include "web_ui.h"
#include "led_state.h"

const char *FW_NAME    = "agri-env-poe";
const char *FW_VERSION = "0.2.0";

// W5500 SPI pinout (M5 ATOM PoE base)
static const int PIN_W5500_SCK  = 22;
static const int PIN_W5500_MISO = 23;
static const int PIN_W5500_MOSI = 33;
static const int PIN_W5500_CS   = 19;
static const int PIN_W5500_IRQ  = -1;
static const int PIN_W5500_RST  = -1;

// globals declared extern in headers
Config g_cfg;

float    g_temp_c        = NAN;
float    g_humid_pct     = NAN;
float    g_pressure_hpa  = NAN;
uint16_t g_co2_ppm       = 0;
float    g_co2_temp_c    = NAN;
float    g_co2_humid_pct = NAN;
bool g_sht30_ok = false;
bool g_qmp_ok   = false;
bool g_scd41_ok = false;
uint32_t g_last_sensor_ms = 0;
SHT3X   g_sht;
QMP6988 g_qmp;
SensirionI2cScd4x g_scd;

NetworkUDP    g_ccmUDP;
NetworkClient g_ethClient;
PubSubClient  g_mqtt(g_ethClient);
NetworkServer g_webServer(80);

CRGB g_led[NEOPIXEL_NUM];
LedState g_led_state = LED_BOOT;

static bool g_link_up    = false;
static bool g_have_lease = false;

static void onNetworkEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(g_cfg.hostname);
      Serial.println("[ETH] start");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      g_link_up = true;
      Serial.println("[ETH] link UP");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      g_link_up = false;
      g_have_lease = false;
      Serial.println("[ETH] link DOWN");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      g_have_lease = true;
      Serial.printf("[ETH] IP %s\n", ETH.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      g_have_lease = false;
      Serial.println("[ETH] lost IP");
      break;
    default: break;
  }
}

static void ethernetBegin() {
  SPI.begin(PIN_W5500_SCK, PIN_W5500_MISO, PIN_W5500_MOSI);
  if (!ETH.begin(ETH_PHY_W5500, 1,
                 PIN_W5500_CS, PIN_W5500_IRQ, PIN_W5500_RST,
                 SPI, 20)) {
    Serial.println("[ETH] ETH.begin failed");
  }
}

static void mdnsBegin() {
  if (!MDNS.begin(g_cfg.hostname)) {
    Serial.println("[MDNS] begin failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  Serial.printf("[MDNS] %s.local\n", g_cfg.hostname);
}

static void otaBegin() {
  ArduinoOTA.setHostname(g_cfg.hostname);
  ArduinoOTA.onStart([]{ Serial.println("[OTA] start"); });
  ArduinoOTA.onEnd  ([]{ Serial.println("[OTA] end");   });
  ArduinoOTA.onError([](ota_error_t e){ Serial.printf("[OTA] err %u\n", e); });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  ledBegin();
  g_led_state = LED_BOOT;
  ledApply();

  loadConfig();
  Serial.printf("[CFG] node=%s mqtt_host=%s ccm=%s\n",
                g_cfg.node_id,
                g_cfg.mqtt_host[0] ? g_cfg.mqtt_host : "(unset)",
                g_cfg.ccm_enabled ? "on" : "off");

  sensorsBegin();

  Network.onEvent(onNetworkEvent);
  ethernetBegin();

  uint32_t t0 = millis();
  while (!g_have_lease && millis() - t0 < 8000) delay(50);
  if (!g_have_lease) Serial.println("[NET] no DHCP yet — continuing");

  ccmBegin();
  g_webServer.begin();
  g_mqtt.setBufferSize(512);
  g_mqtt.setKeepAlive(30);
  mdnsBegin();
  otaBegin();

  Serial.println("[BOOT] ready");
}

void loop() {
  ArduinoOTA.handle();
  handleWebClient(g_link_up, g_have_lease);

  static uint32_t lastSensorPoll = 0;
  uint32_t now = millis();
  if (now - lastSensorPoll >= 2000) {
    lastSensorPoll = now;
    sensorsPoll();
  }

  if (g_link_up && g_have_lease && mqttHasHost()) {
    if (!g_mqtt.connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; mqttReconnect(); }
    } else {
      g_mqtt.loop();
      static uint32_t lastPub = 0;
      uint32_t interval = (uint32_t)g_cfg.mqtt_interval_s * 1000UL;
      if (now - lastPub >= interval) {
        lastPub = now;
        if (mqttPublishState()) ledFlashPublish();
      }
    }
  }

  if (g_link_up && g_have_lease && g_cfg.ccm_enabled) {
    static uint32_t lastCcm = 0;
    uint32_t interval = (uint32_t)g_cfg.ccm_interval_s * 1000UL;
    if (now - lastCcm >= interval) {
      lastCcm = now;
      if (ccmPublish()) ledFlashPublish();
    }
  }

  LedState desired;
  if (!g_link_up)                                desired = LED_NO_LINK;
  else if (!g_have_lease)                        desired = LED_NO_LEASE;
  else if (mqttHasHost() && !g_mqtt.connected()) desired = LED_NO_MQTT;
  else                                           desired = LED_OK;
  if (desired != g_led_state) { g_led_state = desired; ledApply(); }

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d  T=%.2f H=%.1f P=%.2f CO2=%u  up=%lus\n",
                  g_link_up, g_have_lease, g_mqtt.connected(),
                  g_temp_c, g_humid_pct, g_pressure_hpa, (unsigned)g_co2_ppm,
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
