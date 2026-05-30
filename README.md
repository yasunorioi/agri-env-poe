# agri-env-poe

UECS-CCM / MQTT environmental sensor node on the M5Stack ATOM PoE Kit.

Reads SHT30 (temperature/humidity), QMP6988 (barometric pressure) and
SCD41 (CO₂) over a single Grove I²C bus and publishes to either MQTT,
UECS-CCM multicast, or both.

## Hardware

- **MCU**: M5Stack ATOM Lite (ESP32-PICO-D4)
- **Ethernet (PoE)**: M5Stack ATOM PoE base (Wiznet W5500 on SPI)
- **I²C bus**: Grove (G26 SDA / G32 SCL) via a Grove I²C HUB
  - ENV III Unit: SHT30 (0x44) + QMP6988 (0x70)
  - SCD41 Unit: 0x62

## Features

- DHCP / PoE plug-and-play with automatic recovery if the cable is
  plugged in late (Arduino-Ethernet's `Ethernet.begin()` is restart-safe).
- MQTT publisher (PubSubClient) — one JSON document per cadence at
  `<prefix>/state`.
- UECS-CCM multicast publisher (UDP 224.0.0.1:16520, XML format
  identical to ccm_rp2350_relay / OGMS).
- Embedded HTTP UI (3 pages: Dashboard / Config / About) for setting
  MQTT host + topic, enabling CCM and per-channel room/region/order, and
  watching live sensor values.
- ArduinoOTA over Ethernet (hostname `agri-env-XX` by default).
- Single-pixel WS2812 LED status:
  | colour | meaning |
  |---|---|
  | blue | booting |
  | red | no Ethernet link / no DHCP lease |
  | yellow | MQTT host configured but not connected |
  | green | all good |
  | white blink | publish in progress |

## Persisted configuration

NVS namespace `env-cfg` (via Preferences). Fields:

- Node ID, hostname
- MQTT host / port / user / password / topic prefix / interval
- UECS-CCM enable, interval, room, region, per-channel order
  (temperature / humidity / pressure / CO₂), priority

Defaults: MQTT host empty (disabled), CCM disabled. Configure via the
web UI at `http://<device-ip>/config`.

## Build / flash

```
pio run -e m5atom-poe -t upload    # USB-C
pio run -e m5atom-poe -t upload \
    --upload-port agri-env-01.local    # OTA after first boot
```

## Sibling projects

- [`ccm_rp2350_relay`](https://github.com/yasunorioi/ccm_rp2350_relay)
- [`OGMS`](https://github.com/yasunorioi/OGMS) (`~/agri-relay`)
- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe)
