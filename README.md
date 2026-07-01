# agri-env-poe

M5Stack ATOM PoE Kit + ENV III (SHT30 + QMP6988) + SCD41 を使った温室環境
センサーノード。気温・湿度・気圧・CO₂ を agriha スキーマ MQTT で配信し、
必要に応じて ArSprout 互換の UECS-CCM ブロードキャストにも出せる。共通基盤
は [`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core)。

## ハードウェア

- **MCU**: M5Stack ATOM Lite (ESP32-PICO-D4)
- **Ethernet (PoE)**: M5Stack ATOM PoE Base (Wiznet W5500 on SPI)
- **I²C**: M5 Grove (G26 SDA / G32 SCL) → Grove I²C HUB
  - **M5Stack ENV III Unit**: SHT30 (0x44) / QMP6988 (0x70)
  - **M5Stack CO2 Unit**: Sensirion SCD41 (0x62)

## 主な機能

- DHCP / PoE プラグアンドプレイ (ケーブル後挿しでも DHCP 自動再取得)
- **agriha スキーマ MQTT publisher** — `<prefix>/sensor/<Type>` に retained、
  payload は `{"value":<num>,"unit":"<unit>","ts":<unix_s>}` の1値1トピック
  (SNTP 同期前は `ts=0`)
- **飽差 InAirHD** — SHT30 の T/RH から Tetens 式で導出して自動 publish
- **SCD41 圧力補正** — QMP6988 の現在気圧を自動投入
  (±0.5 hPa or 5 分の早い方で間引き、periodic 計測は止めない)
- **SCD41 CO₂ 校正 UI** — ASC on/off + ボタン1つで FRC 実行
  (3 分ウォームアップ → `performForcedRecalibration` → NVS に結果保存)
- **Optional UECS-CCM 出力** — ArSprout の既存温室に混ぜたいときだけ
  `/config` から `ccm_en` を ON。CCM 識別子 (`InAirTemp` などのワイヤ型名)
  はノード側で NVS に保存され、ArSprout の受信 CCM 設定と合わせて
  再フラッシュ無しで変更可能
- **Web UI** 3 ページ (Dashboard / Config / About) + JSON API
  (`/api/status`, `/api/dashboard`)
- **ArduinoOTA** over Ethernet (hostname `agri-env-XX`)
- **GitHub Release self-update** — 起動時に最新版チェック、
  Dashboard から1クリック更新
- **状態 LED**:
  | 色 | 意味 |
  |---|---|
  | 青 | 起動中 |
  | 赤 | リンク断 / DHCP 失敗 |
  | 黄 | MQTT host 設定済みだが未接続 |
  | 緑 | 正常 |
  | 白点滅 | 送信瞬間 |

## MQTT トピック (agriha スキーマ)

`<prefix>` は agriha のハウス分割 (例 `agriha/2` = 別棟)。全トピック retained。

| トピック | 単位 | センサー |
|---|---|---|
| `<prefix>/sensor/InAirTemp`       | °C   | SHT30 (ENV III) — 主温度 |
| `<prefix>/sensor/InAirHumid`      | %    | SHT30 (ENV III) — 主湿度 |
| `<prefix>/sensor/InAirHD`         | g/m³ | SHT30 の T/RH から導出 (飽差) |
| `<prefix>/sensor/InAirPressure`   | hPa  | QMP6988 (ENV III) |
| `<prefix>/sensor/InAirCO2`        | ppm  | SCD41 |
| `<prefix>/sensor/InAirTempSCD41`  | °C   | SCD41 内蔵 (参考値) |
| `<prefix>/sensor/InAirHumidSCD41` | %    | SCD41 内蔵 (参考値) |

SHT30 が主温湿度、SCD41 内蔵の T/RH は精度で SHT30 に劣るため別型名で分離
(DS18B20 の sensor-typed precedent と同じ考え)。

## Optional UECS-CCM 出力

`ccm_enabled=ON` で有効化。ArSprout 側の受信 CCM 定義と合わせて Config で:

- **CCM 識別子** (per-sensor): 気温/湿度/飽差/気圧/CO₂ に対応するワイヤ型名
  (既定 `InAirTemp` / `InAirHumid` / `InAirHD` / `InAirPressure` / `InAirCO2`)。
  空欄にするとその datum は送信しない
- **node-type suffix**: `common.ccm_ntype` (既定 `cMC`) を型名末尾に追加
  → 実際の送出は例えば `InAirTemp.cMC`
- **room / region / priority**: `CommonConfig` から。既定 region=13
  (別棟 ArSprout), priority=1 (受信側と揃える)
- order は1固定

宛先は core の `CCM_BROADCAST` (limited broadcast) — ArSprout は multicast
ではなく broadcast しか拾わないため。

## 永続化設定 (NVS)

`Preferences` ネームスペース `env-cfg`。Web UI の `/config` から編集:

- **共通** (`agri::CommonConfig`): Node ID / hostname / MQTT host, port, user,
  pass, topic prefix, interval / CCM enable, interval, room, region, priority,
  ntype
- **CCM 識別子**: `ct_temp` / `ct_humid` / `ct_hd` / `ct_press` / `ct_co2`
  (各 16 bytes、空欄で datum 無効)
- **SCD41 校正**: `scd_asc` (ASC on/off) / `scd_frc_tgt` (FRC target ppm) /
  `scd_frc_now` (トリガフラグ) / `scd_frc_ts,corr,state` (最終 FRC 結果)

既定は MQTT host 空 (未設定)、CCM disabled、ASC OFF、FRC target 400 ppm。

## ビルド・書き込み

```
pio run -e m5atom-poe -t upload                                # 初回 USB-C
pio run -e m5atom-poe -t upload --upload-port agri-env-01.local  # OTA
```

書き込み後は `http://agri-env-01.local/` で UI にアクセス。
以降の更新は Dashboard 上部の **Update** ボタン (GitHub Release 経由) でも可。

## CO₂ 校正 (年1回目安)

SCD41 は NDIR 方式の経年ドリフトがあるため、定期的な校正が必要。

**ASC (Automatic Self-Calibration)**: 出荷時 ON。SCD41 が「週1回 400 ppm の
外気に 4 時間以上連続曝露される」前提で内部補正する仕組み。**温室環境では
この前提が保証されないため OFF 推奨** (本ノードの既定も OFF)。

**FRC (Forced Recalibration)**: 任意のタイミングで「いま外気 (400 ppm) に
居る」とセンサに教えて補正をかける手動操作。年1回程度で十分。

手順:

1. センサーノードを屋外 (または十分換気された外気と同じ場所) に置く
2. ブラウザで `/config` を開く
3. SCD41 セクションの `Perform FRC now` にチェック → Save
4. 3 分後に自動実行 (この間センサは普通に periodic 計測を続ける)
5. Dashboard の SCD41 セクション / Config の `Last FRC` 行に補正値 (±ppm)
   が表示される

`Last FRC` が `failed` の場合は曝露時間不足 (3 分未満で SCD41 が動作開始
した) か測定濃度が安定していない可能性。屋外で 5 分ほど待ってからやり直す。

## バージョン概略

- v0.2: arduino-esp32 3.x + ESP-IDF lwIP へ移行
- v0.3: `agri-node-poe-core` に共通処理を切り出し
- v0.4: UECS-CCM 撤廃 → agriha MQTT 1本 (`<prefix>/state` から 1値1トピックへ)
- v0.5: GitHub Release self-update
- v0.7: ArSprout 向け optional CCM export 復活 (`ccm_en` gate)
- v0.8: InAirHD (飽差) 導出・publish
- v0.9: SCD41 内蔵 T/RH を InAirTempSCD41 / InAirHumidSCD41 として publish
- v0.10: CCM 識別子 (wire type names) を NVS で configurable に
- v0.11: SCD41 圧力補正 (QMP6988 → SCD41) と ASC/FRC 校正 UI

## 関連プロジェクト

- [`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core) — 共通基盤ライブラリ
- [`agri-rain-poe`](https://github.com/yasunorioi/agri-rain-poe) — 雨量 sibling
- [`agri-flow-poe`](https://github.com/yasunorioi/agri-flow-poe) — 流量 sibling
- [`agri-solar-poe`](https://github.com/yasunorioi/agri-solar-poe) — 日射 sibling
- [`ccm_rp2350_relay`](https://github.com/yasunorioi/ccm_rp2350_relay) — 制御リレー
- [`OGMS`](https://github.com/yasunorioi/OGMS) (`~/agri-relay`) — 灌水/CO2/結露制御
