# Solar EV Charge - 太陽光余剰電力Tesla自動充電システム

太陽光パネルの余剰電力でTesla Model 3を自動充電するHome Assistantベースのシステム。

## アーキテクチャ

```
                          Synology NAS (Docker)
                          ┌───────────────────┐
                          │ Home Assistant    │
                          │  - Control        │
                          │  - ECHONET Lite   │
                          │  - Dashboard      │
M5Stack NanoC6            │                   │          M5StickC Plus
┌─────────────────┐  WiFi │ ┌───────────────┐ │         ┌─────────────────┐
│ Tesla BLE       │<------│-│ ESPHome       │ │         │ Wi-SUN Smart    │
│  - Vehicle      │  API  │ └───────────────┘ │  MQTT   │  Meter Reader   │
│    sensors      │       │ ┌───────────────┐ │  WiFi   │  - BP35A1       │
└────────┬────────┘       │ │ Mosquitto     │-│<--------│  - ECHONET Lite │
         │                │ └───────────────┘ │         └────────┬────────┘
     Bluetooth            └────────┬──────────┘                  │
         │                         │                   920MHz Wi-SUN
┌────────▼────────┐         ECHONET Lite                         │
│ Tesla Model 3   │            (LAN)                    ┌────────▼────────┐
│ + Wall Connector│                │                    │ Smart Meter     │
└─────────────────┘     ┌──────────▼──────────┐         └─────────────────┘
                        │ EP Cube             │
                        │  - Solar 5.75kW     │
                        │  - Battery 13.3kWh  │
                        └─────────────────────┘
```

### データソース

| データ | ソース | プロトコル | 更新頻度 |
|--------|--------|-----------|----------|
| 系統電力 (W) | スマートメーター | Wi-SUN B Route → MQTT | 10秒 |
| 累積買電/売電 (kWh) | スマートメーター | Wi-SUN B Route → MQTT | 5分 |
| ソーラー発電 (W) | EP Cube | ECHONET Lite (LAN) | 自動 |
| バッテリー充放電 (W) | EP Cube | ECHONET Lite (LAN) | 自動 |
| バッテリーSOC (%) | EP Cube | ECHONET Lite (LAN) | 自動 |
| Tesla車両状態 | Tesla Model 3 | Bluetooth via ESPHome | リアルタイム |

## ファイル構成

```
solar-ev-charge/
├── docker/                  # Docker setup
│   ├── docker-compose.yaml
│   └── mosquitto/config/
├── esphome/                 # Tesla BLE (M5Stack NanoC6)
│   ├── tesla-ble.yaml
│   └── secrets.yaml
├── homeassistant/           # HA config, automations, dashboard
│   ├── configuration.yaml
│   ├── automations.yaml
│   └── dashboards/
├── wisun-smartmeter/        # Wi-SUN firmware (M5StickC Plus)
│   ├── src/
│   ├── test/
│   ├── Makefile
│   └── platformio.ini
└── docs/
```

各フォルダの詳細は個別の README を参照。

## セットアップ手順

| Phase | 内容 | 詳細 |
|-------|------|------|
| 1 | Docker (NAS) | [docker/README.md](docker/README.md) |
| 2 | EP Cube (ECHONET Lite) | [homeassistant/README.md](homeassistant/README.md) |
| 3 | Wi-SUN Smart Meter | [wisun-smartmeter/README.md](wisun-smartmeter/README.md) |
| 4 | ESPHome Tesla BLE | [esphome/README.md](esphome/README.md) |
| 5 | 余剰充電自動化 | [homeassistant/README.md](homeassistant/README.md) |
| 6 | ダッシュボード | [docs/dashboard-guide.md](docs/dashboard-guide.md) |

## 必要なハードウェア

| 項目 | 用途 | 価格 |
|------|------|------|
| M5StickC Plus | Wi-SUN スマートメーター読み取り | ¥4,180 |
| BP35A1 + Wi-SUN HAT | Wi-SUN B ルート通信 | ¥8,270 |
| M5Stack NanoC6 | Tesla BLE 制御 | ~¥2,000 |
| USB-C 電源 x 2 | 各デバイス用 | ~¥1,000 |

既存: Synology NAS, Tesla Model 3, Tesla Wall Connector Gen 3, EP Cube 13.3kWh
