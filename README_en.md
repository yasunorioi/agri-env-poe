# agri-env-poe

[🇯🇵 日本語](README_ja.md) · **English**

A greenhouse environment sensor node built with the [M5Stack ATOM PoE Kit](https://docs.m5stack.com/en/atom/atom_poe) +
[ENV III](https://docs.m5stack.com/en/unit/envIII) (SHT30 + QMP6988) + [SCD41](https://sensirion.com/products/catalog/SCD41). It publishes temperature, humidity,
pressure, and CO₂ over the agriha-schema MQTT, and can also emit ArSprout-
compatible UECS-CCM broadcasts when needed. The common foundation is
[`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core).

## Hardware

- **MCU**: [M5Stack ATOM Lite](https://docs.m5stack.com/en/core/ATOM%20Lite) (ESP32-PICO-D4)
- **Ethernet (PoE)**: [M5Stack ATOM PoE Base](https://docs.m5stack.com/en/atom/Atomic%20PoE%20Base) (Wiznet W5500 on SPI)
- **I²C**: M5 Grove (G26 SDA / G32 SCL) → Grove I²C HUB
  - **M5Stack ENV III Unit**: SHT30 (0x44) / QMP6988 (0x70)
  - **M5Stack CO2 Unit**: Sensirion SCD41 (0x62)

## Main features

- DHCP / PoE plug-and-play (automatic DHCP re-acquisition even when the cable is plugged in later)
- **agriha-schema MQTT publisher** — retained to `<prefix>/sensor/<Type>`, with a
  one-value-per-topic payload of `{"value":<num>,"unit":"<unit>","ts":<unix_s>}`
  (`ts=0` before SNTP sync)
- **Vapor pressure deficit InAirHD** — derived from the SHT30 T/RH via the Tetens equation and published automatically
- **SCD41 pressure compensation** — automatically feeds in the current pressure from QMP6988
  (thinned by whichever comes first, ±0.5 hPa or 5 minutes; periodic measurement is not stopped)
- **SCD41 CO₂ calibration UI** — ASC on/off + FRC executed with a single button
  (3-minute warmup → `performForcedRecalibration` → result saved to NVS)
- **Optional UECS-CCM output** — turn on `ccm_en` from `/config` only when you want to
  mix into an existing ArSprout greenhouse. The CCM ids (`CCM識別子`, wire type names such
  as `InAirTemp`) are stored on the node in NVS, and can be changed without a reflash to match
  the ArSprout receive-CCM configuration
- **Web UI** with 3 pages (Dashboard / Config / About) + JSON API
  (`/api/status`, `/api/dashboard`)
- **ArduinoOTA** over Ethernet (hostname `agri-env-XX`)
- **GitHub Release self-update** — checks for the latest version at boot,
  one-click update from the Dashboard
- **Status LED**:
  | Color | Meaning |
  |---|---|
  | Blue | Booting |
  | Red | Link down / DHCP failure |
  | Yellow | MQTT host configured but not connected |
  | Green | Normal |
  | Blinking white | Moment of transmission |

## MQTT topics (agriha schema)

`<prefix>` is the agriha house partition (e.g. `agriha/2` = separate building). All topics retained.

| Topic | Unit | Sensor |
|---|---|---|
| `<prefix>/sensor/InAirTemp`       | °C   | SHT30 (ENV III) — primary temperature |
| `<prefix>/sensor/InAirHumid`      | %    | SHT30 (ENV III) — primary humidity |
| `<prefix>/sensor/InAirHD`         | g/m³ | derived from SHT30 T/RH (vapor pressure deficit) |
| `<prefix>/sensor/InAirPressure`   | hPa  | QMP6988 (ENV III) |
| `<prefix>/sensor/InAirCO2`        | ppm  | SCD41 |
| `<prefix>/sensor/InAirTempSCD41`  | °C   | SCD41 built-in (reference value) |
| `<prefix>/sensor/InAirHumidSCD41` | %    | SCD41 built-in (reference value) |

SHT30 provides the primary temperature/humidity; since the SCD41's built-in T/RH is
less accurate than the SHT30, it is separated under a different type name
(the same reasoning as the [DS18B20](https://www.switch-science.com/products/10979) sensor-typed precedent).

## Optional UECS-CCM output

Enabled with `ccm_enabled=ON`. In Config, matching the receive-CCM definitions on the ArSprout side:

- **CCM id (CCM識別子)** (per-sensor): the wire type names corresponding to temperature/humidity/VPD/pressure/CO₂
  (defaults `InAirTemp` / `InAirHumid` / `InAirHD` / `InAirPressure` / `InAirCO2`).
  Leaving one blank means that datum is not sent
- **node-type suffix**: `common.ccm_ntype` (default `cMC`) is appended to the end of the type name
  → the actual transmission is, for example, `InAirTemp.cMC`
- **room / region / priority**: from `CommonConfig`. Defaults region=13
  (separate-building ArSprout), priority=1 (aligned with the receiving side)
- order is fixed at 1

The destination is core's `CCM_BROADCAST` (limited broadcast) — because ArSprout only picks up
broadcast, not multicast.

## Persistent configuration (NVS)

`Preferences` namespace `env-cfg`. Edited from the Web UI's `/config`:

- **Common** (`agri::CommonConfig`): Node ID / hostname / MQTT host, port, user,
  pass, topic prefix, interval / CCM enable, interval, room, region, priority,
  ntype
- **CCM id (CCM識別子)**: `ct_temp` / `ct_humid` / `ct_hd` / `ct_press` / `ct_co2`
  (16 bytes each, blank disables the datum)
- **SCD41 calibration**: `scd_asc` (ASC on/off) / `scd_frc_tgt` (FRC target ppm) /
  `scd_frc_now` (trigger flag) / `scd_frc_ts,corr,state` (last FRC result)

Defaults are MQTT host blank (not set), CCM disabled, ASC OFF, FRC target 400 ppm.

## Build & flash

```
pio run -e m5atom-poe -t upload                                # first time USB-C
pio run -e m5atom-poe -t upload --upload-port agri-env-01.local  # OTA
```

> 🛠 **Build environment (shared Windows / Linux) / Linux first-time setup (udev, etc.)** →
> [agri-node-poe-core/docs/cross-platform-build.md](https://github.com/yasunorioi/agri-node-poe-core/blob/main/docs/cross-platform-build.md)

After flashing, access the UI at `http://agri-env-01.local/`.
Subsequent updates can also be done with the **Update** button at the top of the Dashboard (via GitHub Release).

## CO₂ calibration (roughly once a year)

Because the SCD41 uses the NDIR method it has aging drift, so periodic calibration is required.

**ASC (Automatic Self-Calibration)**: ON at shipping. A mechanism where the SCD41 applies internal
correction on the assumption that it is "continuously exposed to 400 ppm outdoor air for at least
4 hours once a week." **Since this assumption is not guaranteed in a greenhouse environment, OFF is
recommended** (this node's default is also OFF).

**FRC (Forced Recalibration)**: a manual operation that, at any timing, tells the sensor "I am now in
outdoor air (400 ppm)" and applies correction. About once a year is sufficient.

Procedure:

1. Place the sensor node outdoors (or in a location with the same well-ventilated outdoor air)
2. Open `/config` in a browser
3. Check `Perform FRC now` in the SCD41 section → Save
4. It runs automatically 3 minutes later (during this time the sensor continues normal periodic measurement)
5. The correction value (±ppm) is shown in the SCD41 section of the Dashboard / the `Last FRC` row of Config

If `Last FRC` shows `failed`, it may be insufficient exposure time (the SCD41 started operating in less
than 3 minutes) or the measured concentration was not stable. Wait about 5 minutes outdoors and try again.

## Version summary

- v0.2: migrated to arduino-esp32 3.x + ESP-IDF lwIP
- v0.3: split common processing out into `agri-node-poe-core`
- v0.4: dropped UECS-CCM → single agriha MQTT (from `<prefix>/state` to one-value-per-topic)
- v0.5: GitHub Release self-update
- v0.7: revived optional CCM export for ArSprout (`ccm_en` gate)
- v0.8: InAirHD (vapor pressure deficit) derivation & publish
- v0.9: publish SCD41 built-in T/RH as InAirTempSCD41 / InAirHumidSCD41
- v0.10: made CCM ids (wire type names) configurable in NVS
- v0.11: SCD41 pressure compensation (QMP6988 → SCD41) and ASC/FRC calibration UI

## Related projects

- [`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core) — common foundation library
- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe) — rainfall sibling
- [`agri-flow-poe`](https://github.com/yasunorioi/agri-flow-poe) — flow-rate sibling
- [`agri-solar-poe`](https://github.com/yasunorioi/agri-solar-poe) — solar-radiation sibling
- [`ccm_rp2350_relay`](https://github.com/yasunorioi/ccm_rp2350_relay) — control relay
- [`OGMS`](https://github.com/yasunorioi/OGMS) (`~/agri-relay`) — irrigation/CO2/condensation control
