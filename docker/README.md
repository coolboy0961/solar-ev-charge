# Docker Setup (Synology NAS)

Portainer で以下のサービスをデプロイする。

## サービス一覧

| サービス | ポート | 用途 |
|---------|--------|------|
| Home Assistant | 8123 | スマートホーム制御 |
| ESPHome | 6052 | ESP32 ファームウェア管理 |
| Matter Server | - | Matter プロトコル |
| Mosquitto MQTT | 1883 | MQTT ブローカー |

## デプロイ手順

1. Portainer > Stacks > Add Stack
2. `docker-compose.yaml` の内容を貼り付け
3. Deploy

```bash
# または CLI でデプロイ
docker-compose up -d
```

## Mosquitto 設定

`mosquitto/config/mosquitto.conf`:
- ポート 1883 (MQTT)
- ポート 9001 (WebSocket)
- 匿名接続許可 (`allow_anonymous true`)
- 永続化: 有効 (`persistence_location /mosquitto/data/`)
- ログ: `/mosquitto/log/mosquitto.log` + stdout

## ボリュームマッピング

| コンテナ | ホスト |
|---------|--------|
| Home Assistant `/config` | `/volume1/docker/homeassistant` |
| ESPHome `/config` | `/volume1/docker/esphome/config` |
| Matter Server `/data` | `/volume1/docker/matter-server` |
| Mosquitto | `/volume1/docker/mosquitto/{config,data,log}` |

## 確認

- Home Assistant: `http://<NAS_IP>:8123`
- ESPHome Dashboard: `http://<NAS_IP>:6052`
