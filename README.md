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
- **SCD41 pressure compensation** — QMP6988 の現在気圧を SCD41 に
  自動投入 (±0.5 hPa or 5 分間隔)。
- **SCD41 CO₂ calibration UI** — ASC on/off + ボタン1つで FRC 実行
  (3分自動待機 → 校正)。詳細は下記「CO₂ 校正」セクション。
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
- SCD41: ASC enable, FRC target ppm, FRC trigger (および最終 FRC 結果)

Defaults: MQTT host empty (disabled), CCM disabled. Configure via the
web UI at `http://<device-ip>/config`.

## Build / flash

```
pio run -e m5atom-poe -t upload    # USB-C
pio run -e m5atom-poe -t upload \
    --upload-port agri-env-01.local    # OTA after first boot
```

## CO₂ 校正 (年1回目安)

SCD41 は NDIR 方式の経年ドリフトがあるため、定期的な校正が必要。

**ASC (Automatic Self-Calibration)**: 出荷時 ON。SCD41 が「週1回 400ppm の外気に
4時間以上連続曝露される」前提で内部補正する仕組み。**温室環境ではこの前提が
保証されないため OFF 推奨** (本ノードの既定も OFF)。

**FRC (Forced Recalibration)**: 任意のタイミングで「いま外気 (400 ppm) に居る」と
センサに教えて補正をかける手動操作。年1回程度で十分。

手順:

1. センサーノードを屋外 (または十分換気された外気と同じ場所) に置く
2. ブラウザで `/config` を開く
3. SCD41 セクションの `Perform FRC now` にチェック → Save
4. 3分後に自動実行 (この間センサは普通に periodic 計測を続ける)
5. Dashboard の SCD41 セクション / Config の `Last FRC` 行に補正値 (±ppm) が表示

`Last FRC` が `failed` の場合は曝露時間不足 (3分未満で SCD41 が動作開始した) か
測定濃度が安定していない可能性。屋外で 5 分ほど待ってからやり直す。

## Sibling projects

- [`ccm_rp2350_relay`](https://github.com/yasunorioi/ccm_rp2350_relay)
- [`OGMS`](https://github.com/yasunorioi/OGMS) (`~/agri-relay`)
- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe)
