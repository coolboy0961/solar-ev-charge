# Home Assistant Configuration

太陽光余剰電力による Tesla 自動充電の制御ロジック・センサー・ダッシュボード設定。

## セットアップ

### EP Cube 統合 (ECHONET Lite)

EP Cube は ECHONET Lite プロトコルでローカル接続する（Cloud API は不安定なため非推奨）。

1. Settings > Devices & Services > Add Integration > **ECHONET Lite**
2. EP Cube の IP アドレスを入力
3. 以下のセンサーが表示されるか確認:
   - `sensor.ep_cube_measured_instantaneous_amount_of_electricity_generated` (ソーラー発電 W)
   - `sensor.ep_cube_measured_instantaneous_charging_discharging_electric_energy` (バッテリー W)
   - `sensor.ep_cube_remaining_stored_electricity_3` (バッテリー SOC%)
   - `sensor.ep_cube_measured_cumulative_amount_of_electric_energy_generated` (累積発電 Wh)

### MQTT 統合 (Wi-SUN Smart Meter)

1. Settings > Devices & Services > Add Integration > MQTT
   - ブローカー: `localhost` (同じ Docker ネットワーク)
   - ポート: `1883`
2. MQTT センサーは `configuration.yaml` で定義済み:
   - `sensor.smart_meter_power` (瞬時電力 W)
   - `sensor.smart_meter_cumulative_buy` (累積買電 kWh)
   - `sensor.smart_meter_cumulative_sell` (累積売電 kWh)
   - `binary_sensor.smart_meter_online` (接続状態)

### 設定ファイルのデプロイ

```bash
# Synology: /volume1/docker/homeassistant/config/
docker cp configuration.yaml homeassistant:/config/configuration.yaml
docker cp automations.yaml homeassistant:/config/automations.yaml
docker exec homeassistant mkdir -p /config/dashboards
docker cp dashboards/solar-ev-charging.yaml homeassistant:/config/dashboards/
```

または、Synology のファイルマネージャーで直接 `/volume1/docker/homeassistant/config/` にコピー。

デプロイ後、Settings > System > Restart で HA を再起動。

### Energy Dashboard 設定 (Settings > Energy)

| 項目 | センサー |
|------|---------|
| Grid consumption | `sensor.smart_meter_cumulative_buy` |
| Return to grid | `sensor.smart_meter_cumulative_sell` |
| Grid power | `sensor.smart_meter_power` |
| Solar production | `sensor.ep_cube_measured_cumulative_amount_of_electric_energy_generated` |
| Battery charge | `sensor.ep_cube_ac_measured_cumulative_charging_electric_energy` |
| Battery discharge | `sensor.ep_cube_ac_measured_cumulative_discharging_electric_energy` |
| EV charging | `sensor.tesla_ev_charging_energy` |

### ダッシュボード

`configuration.yaml` の `lovelace:` セクションで自動登録される。
HA を再起動すると、サイドバーに「EV」(アイコン: `mdi:car-electric`) が表示される。

詳細は [docs/dashboard-guide.md](../docs/dashboard-guide.md) を参照。

## 制御ロジック

設計方針: **「電流調整は頻繁に、ON/OFF は慎重に」**

### コンセプト: ハイブリッド方式

- **充電開始判定**: surplus（グリッドベース） → 正確な余剰測定
- **充電停止/電流調整**: solarpower（ソーラー発電量） → EP Cube バッテリー補償による見かけ上の余剰を回避

```
surplus    = smart_meter_power × -1                              (正=売電=余剰)
solarpower = ep_cube_measured_instantaneous_amount_of_electricity_generated
target     = clamp((solarpower - 400W) / 200V, 5A, 24A)
```

### 制御フロー

```
30秒ごと (Main Control Loop):
  前提条件:
    - Solar Charging ON
    - プラグ接続 (charge_flap = on)
    - バッテリー < 充電上限
    - センサー鮮度 3分以内 (smart_meter, battery, SOC)
    - guard timer idle (ON/OFF 直後ではない)
    - stabilize timer idle (電流変更直後ではない)

  [充電中] choose 分岐 (上から順に評価、最初に一致した分岐のみ実行):
    1. REDUCE: solarpower < (current_amps × 200V + 400W) AND amps > 5A
       → target A に変更 + stabilize 45秒
    2. LOW SOLAR: solarpower < 800W
       → disable delay timer 開始 (idle の場合のみ)
    3. DEFAULT (十分な発電):
       → disable delay timer キャンセル (動作中の場合)
       → IF target > current → set target + stabilize 45秒
       → IF current - target >= 3A → set target + stabilize 45秒 (デッドバンド)

  [非充電]
    IF surplus >= 1200W → enable delay timer 開始 (idle の場合のみ)
    ELSE → enable delay timer キャンセル

別オートメーション:
  [enable delay 満了]  → surplus > 1000W 再チェック → 5A で充電開始
                         + guard timer (5分) + stabilize 1分
  [disable delay 満了] → 充電停止 + guard timer (5分)
  [低ソーラー停止] solarpower < 800W が 3分継続 → 即停止 + guard
  [緊急停止] surplus < -500W が 2分継続 → 即停止 + guard + 通知
  [充電完了] charging_state = Complete → timer 全キャンセル + 通知
  [手動OFF] Solar Charging OFF → 充電停止 + 全 timer cancel
  [ケーブル接続ガード] charger ON 検出 + Solar Charging ON + guard idle
                         → charger OFF + guard 開始 (余剰待機)
```

### パラメータ一覧

| パラメータ | デフォルト値 | 設定場所 |
|-----------|------------|---------|
| 充電開始閾値 (surplus) | 1200W | automations.yaml (main loop) |
| 充電開始再チェック (surplus) | 1000W | automations.yaml (enable delay expired) |
| 充電停止判定 (solarpower) | 800W | automations.yaml (main loop + low solar) |
| 開始電流 | 5A | automations.yaml |
| Target 計算マージン (base_load) | 400W | automations.yaml |
| 下方向デッドバンド | 3A | automations.yaml |
| 緊急停止 (買電超過) | 500W × 2分 | automations.yaml |
| 低ソーラー停止 | 800W × 3分 | automations.yaml |
| センサー鮮度チェック | 3分 | automations.yaml |
| Enable delay | 1分 | configuration.yaml (timer) |
| Disable delay | 3分 | configuration.yaml (timer) |
| Guard duration | 5分 | configuration.yaml (timer) |
| Stabilize | 45秒 (電流変更後) / 1分 (開始後) | automations.yaml |
| 最小/最大電流 | 5A / 24A | automations.yaml |
| 電圧 (計算用) | 200V | automations.yaml |
| 制御ループ間隔 | 30秒 | automations.yaml |

### チューニングガイド

- 充電機会を逃しすぎる場合: enable delay 1分→30秒、disable delay 3分→2分
- まだ ON/OFF 繰り返す場合: disable delay 3分→5分、guard 5分→10分
- 開始閾値: EP Cube 蓄電池の充放電パターンに応じて 1200W→1500W に調整

## エンティティ一覧

### Wi-SUN Smart Meter (MQTT)

| Entity ID | 説明 | 値の例 |
|-----------|------|--------|
| `sensor.smart_meter_power` | 系統電力 W (正=買電, 負=売電) | 44 |
| `sensor.smart_meter_cumulative_buy` | 累積買電 kWh | 1201.0 |
| `sensor.smart_meter_cumulative_sell` | 累積売電 kWh | 2296.6 |
| `binary_sensor.smart_meter_online` | 接続状態 | on / off |

### EP Cube (ECHONET Lite)

| Entity ID | 説明 | 値の例 |
|-----------|------|--------|
| `sensor.ep_cube_measured_instantaneous_amount_of_electricity_generated` | ソーラー発電 W | 3500 |
| `sensor.ep_cube_measured_instantaneous_charging_discharging_electric_energy` | バッテリー充放電 W (正=充電, 負=放電) | -481 |
| `sensor.ep_cube_remaining_stored_electricity_3` | バッテリー SOC % | 87 |
| `sensor.ep_cube_measured_cumulative_amount_of_electric_energy_generated` | 累積発電量 Wh | 5470000 |

### カスタムテンプレートセンサー (configuration.yaml)

| Entity ID | 説明 | 計算ロジック |
|-----------|------|-------------|
| `sensor.solar_surplus_power` | 余剰電力 W | `smart_meter_power × -1` (正=余剰) |
| `sensor.solar_total_available_power` | 総利用可能電力 W | `surplus + charging_power_w` |
| `sensor.available_charging_amps` | 利用可能充電電流 A | `(total_available - 400) ÷ 200V` (5-24A) |
| `sensor.tesla_actual_charging_current` | 実充電電流 A | 充電中のみ電流値、停止時は 0 |
| `sensor.tesla_charging_power_w` | 充電電力 W | `V × A` (state_class: measurement) |
| `sensor.home_consumption_power` | 家庭消費 W | `solar - battery + grid` |
| `sensor.ep_cube_battery_power_display` | バッテリー表示用 W | ECHONET Lite 値 × -1 (正=放電) |
| `sensor.tesla_battery_energy` | Tesla バッテリー kWh | `charge_level% × 0.75` (75kWh 容量) |
| `sensor.ep_cube_battery_energy` | EP Cube バッテリー kWh | `SOC% × 0.133` (13.3kWh 容量) |
| `binary_sensor.solar_surplus_charging_available` | 充電条件成立 | 余剰>=1200W & プラグ接続 & バッテリー<上限 |

### Riemann 積分センサー

| Entity ID | 説明 | ソース |
|-----------|------|--------|
| `sensor.tesla_ev_charging_energy` | Tesla 充電エネルギー kWh | `sensor.tesla_charging_power_w` を積分 (Energy Dashboard 用) |

### タイマー

| Entity ID | 説明 | デフォルト |
|-----------|------|-----------|
| `timer.solar_enable_delay` | 充電開始遅延 | 1分 |
| `timer.solar_disable_delay` | 充電停止遅延 | 3分 |
| `timer.solar_guard` | ON/OFF 切替ガード | 5分 |
| `timer.solar_stabilize` | 電流変更後安定化待ち | 90秒 |

## トラブルシューティング

### テンプレートセンサーが「未知」と表示される
- HA 再起動直後は起動完了まで数分待つ (下部バナーが消えるまで)
- ECHONET Lite インテグレーションが正常に動作しているか確認
- MQTT インテグレーションが接続されているか確認
- `configuration.yaml` が正しいパスにコピーされているか確認

### Smart Meter の値が表示されない
- M5StickC Plus の LCD で「MQTT: OK」と表示されているか確認
- Mosquitto ブローカーが起動しているか確認: `docker logs mosquitto`
- HA の MQTT インテグレーションが接続されているか確認

### Tesla BLE 接続が切れる
- NanoC6 と車の距離が 10m 以内か確認
- `sensor.tesla_ble_ble_signal` で信号強度を確認 (-70dBm 以上が望ましい)
- `button.tesla_ble_wake_up` で車両をウェイクアップ

### 自動充電が開始されない
- `input_boolean.solar_charging_enabled` が ON か確認
- `binary_sensor.tesla_ble_charge_flap` が ON (充電フラップが開いている) か確認
- `sensor.solar_surplus_power` が 1200W 以上か確認 (1分間継続が必要)
- `sensor.tesla_ble_charge_level` が `number.tesla_ble_charging_limit` 未満か確認
- `timer.solar_guard` が `idle` か確認 (前回の ON/OFF 後 5分間はブロック)

### 充電が停止しない (余剰が低下しているのに)
- `timer.solar_disable_delay` が動作中か確認 (3分間のカウントダウン中)
- `timer.solar_guard` が動作中か確認
- `sensor.ep_cube_measured_instantaneous_amount_of_electricity_generated` が 800W 未満か確認
