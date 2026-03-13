# Solar EV Charge - 太陽光余剰電力Tesla自動充電システム

太陽光パネルの余剰電力でTesla Model 3を自動充電するHome Assistantベースのシステム。

## アーキテクチャ

```
Synology NAS (Docker)                  ESP32 (BLE範囲内)
┌───────────────────────┐        ┌────────────────────┐
│  Home Assistant       │ WiFi   │ ESPHome Tesla BLE  │
│  ・制御ロジック        │───────→│ ・車両センサー      │
│  ・EP Cube統合        │ ESPHome│ ・充電制御コマンド   │
│  ・Dashboard          │  API   └────────┬───────────┘
└───────────┬───────────┘                 │ BLE
            │ Cloud API          ┌────────▼───────────┐
   ┌────────▼──────────┐        │ Tesla Model 3      │
   │ EP Cube 13.3kWh   │        │ + Wall Connector   │
   └───────────────────┘        └────────────────────┘
```

## ファイル構成

```
solar-ev-charge/
├── docker-compose.yaml                    # HA Docker設定 (Synology用)
├── esphome/
│   ├── tesla-ble.yaml                     # ESPHome Tesla BLE設定
│   └── secrets.yaml                       # WiFi/VIN/BLE MAC (要編集)
├── homeassistant/
│   ├── configuration.yaml                 # HA設定 (テンプレートセンサー含む)
│   ├── automations.yaml                   # 余剰充電自動化ロジック
│   └── dashboards/
│       └── solar-ev-charging.yaml         # Lovelaceダッシュボード
├── docs/
│   └── dashboard-guide.md                 # ダッシュボード操作ガイド
└── README.md
```

## セットアップ手順

### Phase 1: Home Assistant (Synology Docker)

1. `docker-compose.yaml` をSynologyにコピー
2. デプロイ:
   ```bash
   docker-compose up -d
   ```
3. `http://<NAS_IP>:8123` にアクセスして初期設定

### Phase 2: EP Cube統合 (HACS)

1. HACSをインストール: Settings > Add-ons > HACS
2. HACS > Integrations > 右上メニュー > **Custom repositories**
3. URLに `https://github.com/Bobsilvio/epcube`、カテゴリに `Integration` を指定して追加
4. HACS > Integrations > `EP Cube` を検索してインストール
5. Home Assistantを**再起動**
6. Settings > Devices & Services > Add Integration > EP Cube
7. EP Cubeアプリの認証情報を入力:
   - Email/Password
   - Endpoint: `https://monitoring-jp.epcube.com/api`
8. `sensor.epcube_solarpower`, `sensor.epcube_gridpower` 等が表示されるか確認

### Phase 3: ESPHome Tesla BLE (ESP32)

ESPHome Tesla BLEは [PedroKTFC/esphome-tesla-ble](https://github.com/PedroKTFC/esphome-tesla-ble) をベースにしている。
ボード定義・パッケージは全てこのリポジトリから取得される。

1. **M5Stack NanoC6を購入** (~¥2,000)

2. **ESPHome Add-onをインストール**:
   - Settings > Add-ons > ESPHome を検索してインストール
   - ESPHome Dashboard: `http://<NAS_IP>:6052`  (またはHAサイドバーから)

3. **secrets.yamlを編集**:
   ```yaml
   wifi_ssid: "your_wifi"
   wifi_password: "your_password"
   tesla_vin: "5YJ3E..."
   tesla_ble_mac: "AA:BB:CC:DD:EE:FF"  # Step 6で取得
   api_encryption_key: "generate_me"     # 下記コマンドで生成
   ota_password: "your_ota_password"
   ```

4. **API暗号化キーの生成**:
   ```bash
   python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
   ```

5. **初回フラッシュ** (USB接続):
   - M5Stack NanoC6をPCにUSB-C接続
   - Web ESPHome: https://web.esphome.io でブラウザからフラッシュ可能
   - または ESPHome Dashboard > New Device > `tesla-ble.yaml` の内容を貼り付け
   - 「Install」→ 「Plug into this computer」を選択
   - ブラウザのシリアルポート選択でNanoC6を選ぶ

6. **BLE MACアドレスの検出**:
   - `tesla-ble.yaml` の `tesla_ble_listener:` セクションのコメントを外す
   - ESPHomeでファームウェアをビルド・フラッシュ (OTA)
   - ESPHomeのログでTeslaのBLE MACアドレスを確認
   - `secrets.yaml` に記入し、`tesla_ble_listener:` を再度コメントアウト
   - 再度ビルド・フラッシュ

7. **ESP32を設置**: 充電器/車の近く (BLE範囲10m以内) にUSB-C電源で設置

8. **HAに統合を追加**:
   - Settings > Devices & Services > Add Integration > ESPHome
   - ESP32のIPアドレスを入力 (サブネットが異なる場合は下記「クロスサブネット接続」参照)
   - API暗号化キーを入力

9. **BLEペアリング**:
   - HAのデバイス画面で「Pair BLE Key」ボタンを押す
   - **重要**: Tesla車のドアを開けた状態で行う
   - Tesla車内のタッチスクリーンでBLEキーを承認

10. **動作確認** (Developer Tools > States):
    - `sensor.tesla_ble_charge_level` → バッテリー残量% (例: 74.0)
    - `sensor.tesla_ble_charging_state` → 充電状態 (例: Stopped, Charging, Complete)
    - `binary_sensor.tesla_ble_charge_flap` → 充電フラップ開閉 (on/off)
    - `switch.tesla_ble_charger` → 充電ON/OFF
    - `sensor.tesla_ble_charge_current` → 充電電流 (注: 充電停止後も最後の値を保持する)

### クロスサブネット接続 (NASとESP32が別サブネットの場合)

NASとESP32が同じサブネットにあれば、mDNS (`tesla-ble.local`) やIPアドレス直指定で接続できる。
異なるサブネットの場合、mDNS/DNS解決が機能しないため、WiFiルーターのポートマッピングで中継する。

**ネットワーク構成例:**

```
Synology NAS (192.168.0.3)          WiFi Router (192.168.0.2)          ESP32 (192.168.1.156)
  subnet: 192.168.0.x/24     ──→     ポートマッピング            ──→    subnet: 192.168.1.x/24
                                    6053/TCP → 192.168.1.156:6053
```

**設定手順:**

1. **WiFiルーターでポートマッピングを設定**:
   - 外部ポート: `6053` (TCP)
   - 転送先IP: ESP32のIPアドレス (例: `192.168.1.156`)
   - 転送先ポート: `6053` (TCP)
   - ESPHome APIはポート6053を使用する

2. **`tesla-ble.yaml` の `use_address` を設定**:
   ```yaml
   wifi:
     use_address: 192.168.0.2  # WiFiルーターのIP (ポートマッピング経由)
   ```
   `use_address` はHAがESP32に接続する際のアドレスを指定する。
   ESP32の実IPではなく、ポートマッピングの入口 (ルーターIP) を指定する。

3. **HAの統合追加時**: ESP32のアドレスとしてルーターIP `192.168.0.2` を入力

**注意事項:**
- OTA更新も同じポートマッピング経由で動作する
- ルーターのDHCPでESP32のIPを固定割り当てにすることを推奨
- ファクトリーフラッシュするとESP32のMACアドレスが変わり、IPも変わる可能性がある。その場合はルーターでESP32の新しいIPを確認し、ポートマッピングを更新する

### Phase 4: 余剰充電自動化

1. **設定ファイルをHA configディレクトリにコピー**:
   ```bash
   # Synology Docker: /volume1/docker/homeassistant/config/
   # 以下はDocker execで実行する例
   docker cp homeassistant/configuration.yaml homeassistant:/config/configuration.yaml
   docker cp homeassistant/automations.yaml homeassistant:/config/automations.yaml
   docker exec homeassistant mkdir -p /config/dashboards
   docker cp homeassistant/dashboards/solar-ev-charging.yaml homeassistant:/config/dashboards/
   ```
   または、Synologyのファイルマネージャーで直接 `/volume1/docker/homeassistant/config/` にコピー

2. **HAを再起動**: Settings > System > Restart

3. **テンプレートセンサーの確認** (Developer Tools > States):
   - `sensor.solar_surplus_power` → 余剰電力W (正=余剰, 負=買電)
   - `sensor.solar_total_available_power` → 余剰+充電中電力W (充電停止時の余剰予測)
   - `sensor.available_charging_amps` → 利用可能充電電流A (5-24A)
   - `sensor.tesla_actual_charging_current` → 実充電電流A (充電停止時は0)
   - `binary_sensor.solar_surplus_charging_available` → 充電条件成立 (on/off)

   **タイマーの確認** (Developer Tools > States):
   - `timer.solar_enable_delay` → 充電開始遅延 (1分)
   - `timer.solar_disable_delay` → 充電停止遅延 (3分)
   - `timer.solar_guard` → ON/OFF間ガード (5分)
   - `timer.solar_stabilize` → 電流変更後安定化待ち (90秒)

   **「未知」と表示される場合**: HA再起動直後は起動完了まで数分待つ。
   それでも未知の場合は `configuration.yaml` のパス・構文を確認。

4. **Solar Chargingを有効化**:
   - ダッシュボードまたはDeveloper Toolsで `input_boolean.solar_charging_enabled` をON

5. **パラメータ調整** (必要に応じて configuration.yaml / automations.yaml を編集):

   | パラメータ | デフォルト値 | 設定場所 |
   |-----------|------------|---------|
   | 充電開始閾値 | 1200W | automations.yaml |
   | 充電停止判定 (total_available) | 800W | automations.yaml |
   | 開始電流 | 常に5A | automations.yaml |
   | Target計算マージン | 400W | automations.yaml |
   | 上方向ランプ制限 | +2A/サイクル | automations.yaml |
   | 下方向デッドバンド | 3A | automations.yaml |
   | 段階的REDUCE閾値 | surplus < -100W | automations.yaml |
   | Enable delay | 1分 | configuration.yaml (timer) |
   | Disable delay | 3分 | configuration.yaml (timer) |
   | Guard duration | 5分 | configuration.yaml (timer) |
   | Stabilize | 90秒 (電流変更後) / 2分 (開始後) | automations.yaml |
   | 緊急停止 (買電超過) | 500W x 2分 | automations.yaml |
   | 最小電流 | 5A | automations.yaml |
   | 最大電流 | 24A | automations.yaml |
   | 電圧 (計算用) | 200V | configuration.yaml / automations.yaml |
   | 制御ループ間隔 | 30秒 | automations.yaml |

### Phase 5: ダッシュボード

ダッシュボードは `configuration.yaml` の `lovelace:` セクションで自動登録される。
HAを再起動すると、サイドバーに「EV」(アイコン: `mdi:car-electric`) が表示される。

手動で追加する場合:
1. Settings > Dashboards > Add Dashboard
2. 「Use YAML」を選択
3. ファイル名: `dashboards/solar-ev-charging.yaml`

ダッシュボードの各項目の詳細は [docs/dashboard-guide.md](docs/dashboard-guide.md) を参照。

## 制御ロジック

設計方針: **「電流調整は頻繁に、ON/OFFは慎重に」**

### コンセプト: `total_available`

```
total_available = 現在の余剰電力 + 現在の充電消費電力
```

充電中は余剰が充電分だけ下がるため、「もし充電を止めたら余剰はいくらか」を示す。
これを電流計算の基準にすることで、充電中も正確な目標電流を算出できる。

例: 余剰400W、充電中1000W (5A) → total_available = 1400W → target = 5A (マージン400W控除)

### 制御フロー

```
30秒ごと:
  前提条件: Solar Charging ON & プラグ接続 & バッテリー < 上限
  IF guard timer active → スキップ (ON/OFF直後)
  IF stabilize timer active → スキップ (電流変更直後)

  total_available = surplus + charging_power_w
  target = clamp((total_available - 400W) / 200V, 5, 24)

  [充電中]
    IF surplus < -100W AND amps > 5A:
      REDUCE by (|surplus|/200 + 1)A    ← 段階的に下げる
    IF total_available < 800W:
      disable delay timer開始 (3分)     ← 即停止しない
    ELSE:
      disable delay timerキャンセル       ← 余剰回復
      IF target > current → set min(target, current+2A)  ← ランプ +2A/cycle
      IF target < current AND diff >= 3A → set target    ← デッドバンド 3A

  [非充電]
    IF surplus >= 1200W:
      enable delay timer開始 (1分)      ← 即開始しない
    ELSE:
      enable delay timerキャンセル

  [enable delay満了]  → 5Aで充電開始 + guard timer (5分)
  [disable delay満了] → 充電停止 + guard timer (5分)
  [緊急停止] surplus < -500W 2分 → 即停止 (guard/delay無視)
  [手動OFF] Solar Charging OFF → 充電停止 + 全timer cancel
```

### チューニングガイド

- 充電機会を逃しすぎる場合: enable delay 1分→30秒、disable delay 3分→2分
- まだON/OFF繰り返す場合: disable delay 3分→5分、guard 5分→10分
- 開始閾値: EP Cube蓄電池の充放電パターンに応じて1200W→1500Wに調整

## エンティティ一覧

### ESPHome Tesla BLE (ESP32経由)

| Entity ID | 説明 | 値の例 |
|-----------|------|--------|
| `sensor.tesla_ble_charge_level` | バッテリー残量 % | 74.0 |
| `sensor.tesla_ble_charging_state` | 充電状態 | Stopped / Charging / Complete |
| `sensor.tesla_ble_charge_current` | 充電電流 A (※) | 4.0 |
| `sensor.tesla_ble_charge_power` | 充電電力 kW | 0.0 |
| `sensor.tesla_ble_charge_voltage` | 充電電圧 V | 207.0 |
| `sensor.tesla_ble_charge_energy_added` | 充電追加エネルギー kWh | 0.0 |
| `sensor.tesla_ble_charge_distance_added` | 充電追加距離 km | 0.0 |
| `sensor.tesla_ble_range` | 推定航続距離 km | 396.5 |
| `binary_sensor.tesla_ble_charge_flap` | 充電フラップ開閉 | on / off |
| `binary_sensor.tesla_ble_asleep` | スリープ状態 | on / off |
| `binary_sensor.tesla_ble_climate` | エアコン動作 | on / off |
| `binary_sensor.tesla_ble_doors` | ドアロック状態 | on(unlocked) / off(locked) |
| `binary_sensor.tesla_ble_user_presence` | ユーザー在席 | on / off |
| `switch.tesla_ble_charger` | 充電 ON/OFF | on / off |
| `switch.tesla_ble_climate` | エアコン ON/OFF | on / off |
| `number.tesla_ble_charging_amps` | 充電電流設定 | 0-24 A |
| `number.tesla_ble_charging_limit` | 充電上限設定 | 50-100 % |
| `lock.tesla_ble_lock_car` | 車両ロック | locked / unlocked |
| `button.tesla_ble_wake_up` | 車両ウェイクアップ | - |
| `button.tesla_ble_flash_light` | ライトフラッシュ | - |
| `button.tesla_ble_sound_horn` | ホーン鳴動 | - |
| `sensor.tesla_ble_exterior` | 外気温 °C | 3.5 |
| `sensor.tesla_ble_interior` | 車内温度 °C | 20.0 |

※ `sensor.tesla_ble_charge_current` は充電停止後も最後の値を保持する。
ダッシュボードでは `sensor.tesla_actual_charging_current` (テンプレートセンサー) を使用し、
充電停止時は 0A と表示する。

### EP Cube (HACS統合)

| Entity ID | 説明 | 値の例 |
|-----------|------|--------|
| `sensor.epcube_solarpower` | 太陽光発電 W | 0 (夜間) / 3500 (晴天) |
| `sensor.epcube_gridpower` | 系統電力 W (正=買電, 負=売電) | 40 |
| `sensor.epcube_batterysoc` | 蓄電池 SoC % | 41 |
| `sensor.epcube_battery_power` | 蓄電池充放電 kW (正=充電, 負=放電) | -1.02 |
| `sensor.epcube_backuppower` | 家庭消費 W | 970 |

### カスタムテンプレートセンサー (configuration.yaml)

| Entity ID | 説明 | 計算ロジック |
|-----------|------|-------------|
| `sensor.solar_surplus_power` | 余剰電力 W | `epcube_gridpower × -1` (正=余剰) |
| `sensor.solar_total_available_power` | 総利用可能電力 W | `surplus + charging_power_w` |
| `sensor.available_charging_amps` | 利用可能充電電流 A | `(total_available - 400) ÷ 200V` (5-24A) |
| `sensor.tesla_actual_charging_current` | 実充電電流 A | 充電中のみ電流値、停止時は0 |
| `sensor.tesla_charging_power_w` | 充電電力 W | `charge_power × 1000` (state_class: measurement) |
| `binary_sensor.solar_surplus_charging_available` | 充電条件成立 | 余剰>=1200W & プラグ接続 & バッテリー<上限 |

### Riemann積分センサー (configuration.yaml)

| Entity ID | 説明 | ソース |
|-----------|------|--------|
| `sensor.tesla_ev_charging_energy` | Tesla充電エネルギー kWh | `sensor.tesla_charging_power_w` を積分 (Energy Dashboard用) |

### タイマー (configuration.yaml)

| Entity ID | 説明 | デフォルト |
|-----------|------|-----------|
| `timer.solar_enable_delay` | 充電開始遅延 | 1分 |
| `timer.solar_disable_delay` | 充電停止遅延 | 3分 |
| `timer.solar_guard` | ON/OFF切替ガード | 5分 |
| `timer.solar_stabilize` | 電流変更後安定化待ち | 90秒 |

## 必要なハードウェア

| 項目 | 価格 |
|------|------|
| M5Stack NanoC6 (ESP32-C6) | ~¥2,000 |
| USB-C電源 (5V) | ~¥500 |

既存: Synology NAS, Tesla Model 3, Tesla Wall Connector Gen 3, EP Cube 13.3kWh

## トラブルシューティング

### テンプレートセンサーが「未知」と表示される
- HA再起動直後は起動完了まで数分待つ (下部バナーが消えるまで)
- EP Cube統合が正常に動作しているか確認 (Developer Tools > States で `sensor.epcube_gridpower`)
- `configuration.yaml` が正しいパスにコピーされているか確認

### Tesla BLE接続が切れる
- ESP32と車の距離が10m以内か確認
- `sensor.tesla_ble_ble_signal` で信号強度を確認 (-70dBm以上が望ましい)
- `button.tesla_ble_wake_up` で車両をウェイクアップ

### 充電電流が停止後も0にならない
- `sensor.tesla_ble_charge_current` はTesla BLEの仕様で最後の値を保持する
- ダッシュボードでは `sensor.tesla_actual_charging_current` を使用しているため0と表示される

### 自動充電が開始されない
- `input_boolean.solar_charging_enabled` がONか確認
- `binary_sensor.tesla_ble_charge_flap` がON (充電フラップが開いている) か確認
- `sensor.solar_surplus_power` が1200W以上か確認 (1分間継続が必要)
- `sensor.tesla_ble_charge_level` が `number.tesla_ble_charging_limit` 未満か確認
- `timer.solar_guard` が `idle` か確認 (前回のON/OFF後5分間はブロック)
- `timer.solar_enable_delay` が動作中か確認 (1分間のカウントダウン中)

### 充電が停止しない (余剰が低下しているのに)
- `timer.solar_disable_delay` が動作中か確認 (3分間のカウントダウン中)
- `timer.solar_guard` が動作中か確認 (前回のON/OFF後5分間はブロック)
- `sensor.solar_total_available_power` が800W未満か確認 (停止判定基準)
