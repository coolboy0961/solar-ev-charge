# Wi-SUN Smart Meter Reader

M5StickC Plus + BP35A1 (Wi-SUN HAT) を使って、TEPCO スマートメーターから電力データをローカルで取得し、MQTT で Home Assistant に送信するファームウェア。

## ハードウェア

| 部品 | 型番 |
|---|---|
| マイコン | M5StickC Plus (ESP32-PICO-D4) |
| Wi-SUN モジュール | ROHM BP35A1 |
| 接続 | Wi-SUN HAT (Rev 0.2+) |

### ピン接続 (Wi-SUN HAT)

| ESP32 GPIO | 方向 | BP35A1 |
|---|---|---|
| GPIO26 (RX) | ← | BP35A1 TX |
| GPIO0 (TX) | → | BP35A1 RX |

- UART: HardwareSerial(1), 115200 baud, 8N1

## 取得データ

| データ | ECHONET Lite EPC | 単位 | 更新頻度 | MQTT トピック |
|---|---|---|---|---|
| 瞬時電力 | 0xE7 | W | 10秒 | `smartmeter/power` |
| 積算買電量 | 0xE0 | kWh | 60秒 | `smartmeter/energy_buy` |
| 積算売電量 | 0xE3 | kWh | 60秒 | `smartmeter/energy_sell` |
| 接続状態 | — | — | 接続時 | `smartmeter/status` |

- 起動後の初回リクエストでは E7 + E0 + E3 を全て取得
- 以降は E7 を10秒毎、E0 + E3 を60秒毎にリクエスト
- 瞬時電力は ±30,000W の範囲フィルタリングあり（異常値除外）
- 積算電力量の係数は 0.1 kWh/unit

## 通信方式

### ECHONET Lite リクエスト

- **同期方式**: リクエスト送信 → 応答 (ERXUDP) 待ち → 次のリクエスト
- 各リクエスト前に1秒の待機時間
- タイムアウト: 10秒/リクエスト
- ERXUDP 出力: ASCII モード (WOPT 01)

### ECHONET Lite フレーム構造

```
1081 0001 05FF01 028801 62 01 XX 00
│    │    │      │      │  │  │  │
│    │    │      │      │  │  │  └─ PDC (0: Get要求)
│    │    │      │      │  │  └──── EPC (E7/E0/E3)
│    │    │      │      │  └─────── OPC (1: プロパティ数)
│    │    │      │      └────────── ESV (62: Get)
│    │    │      └───────────────── DEOJ (028801: 低圧スマートメーター)
│    │    └──────────────────────── SEOJ (05FF01: コントローラ)
│    └───────────────────────────── TID (0001)
└────────────────────────────────── EHD (1081: ECHONET Lite)
```

## Wi-SUN 接続シーケンス

1. **SKRESET** — モジュールリセット
2. **SKSREG SFE 0** — エコーバック無効化
3. **ROPT / WOPT 01** — ERXUDP 出力を ASCII モードに設定
4. **SKTERM** — 前回セッションの終了
5. **SKSETPWD / SKSETRBID** — B ルート認証情報設定
6. **SKSCAN** — スマートメーター探索 (duration 4→7 で段階的に)
7. **SKSREG S2/S3** — チャンネル・PAN ID 設定
8. **SKLL64** — MAC → IPv6 リンクローカルアドレス変換
9. **SKJOIN** — PANA 認証開始
10. **EVENT 25** — 認証成功 → データ取得開始

### リトライ・キャッシュ

- スキャン結果は NVS (ESP32 フラッシュ) にキャッシュ
- 初回は NVS キャッシュを使用、失敗時は再スキャン
- 接続リトライ: 最大3回（キャッシュクリア → 再スキャン → PANA 再認証）
- スキャン duration は 4→5→6→7 と段階的に延長

## ボタン操作

| ボタン | 位置 | 機能 |
|---|---|---|
| A | 正面 (M5) | デバッグ情報表示 (5秒間) |
| B | 側面 | スキャンキャッシュクリア & 再起動 |

## LCD 表示

通常画面:
```
Wi-SUN Smart Meter
Connected              (緑) / Connecting... (赤)

123 W                  瞬時電力

Buy:  1200.8 kWh       積算買電量
Sell:  456.3 kWh       積算売電量

MQTT: OK (緑) / Disconnected (赤)
```

## 設定ファイル

| ファイル | 内容 |
|---|---|
| `src/config.h` | MQTT サーバー、トピック、ピン設定、更新間隔 |
| `src/secrets.h` | WiFi / MQTT / B ルート認証情報 (gitignore 対象) |
| `src/secrets.example.h` | secrets.h のテンプレート |

### 主要パラメータ (config.h)

| パラメータ | デフォルト | 説明 |
|---|---|---|
| `MQTT_SERVER` | 192.168.0.3 | MQTT ブローカー IP |
| `MQTT_PORT` | 1883 | MQTT ポート |
| `POWER_READ_INTERVAL` | 10000 (10秒) | 電力取得間隔 |
| `ENERGY_READ_INTERVAL` | 60000 (60秒) | 累積電力量取得間隔 |
| `BP35A1_BAUD` | 115200 | BP35A1 ボーレート |

## ビルド & フラッシュ

```bash
# PlatformIO CLI
pio run              # ビルド
pio run -t upload    # フラッシュ
```

## 依存ライブラリ

| ライブラリ | バージョン | 用途 |
|---|---|---|
| M5StickCPlus | ^0.1.0 | LCD・ボタン・IMU |
| PubSubClient | ^2.8 | MQTT クライアント |
| ArduinoJson | ^7.0.0 | (将来の拡張用) |
