#include "MqttPublisher.h"
#include "config.h"

void MqttPublisher::begin(ILogger* logger) {
    _logger = logger;
    _mqtt.setClient(_wifiClient);
    connectWiFi();
    _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    _mqtt.setBufferSize(512);
    connectMQTT();
}

void MqttPublisher::loop() {
    _mqtt.loop();

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    if (!_mqtt.connected() && millis() - _lastAttempt >= MQTT_RECONNECT_INTERVAL) {
        _lastAttempt = millis();
        connectMQTT();
    }
}

bool MqttPublisher::isConnected() {
    return _mqtt.connected();
}

void MqttPublisher::publish(const MeterData& data) {
    if (!_mqtt.connected()) return;
    if (data.powerValid) {
        _mqtt.publish(MQTT_TOPIC_POWER, String(data.power).c_str(), true);
    }
    if (data.buyEnergyValid) {
        _mqtt.publish(MQTT_TOPIC_ENERGY_BUY, String(data.buyEnergy, 1).c_str(), true);
    }
    if (data.sellEnergyValid) {
        _mqtt.publish(MQTT_TOPIC_ENERGY_SELL, String(data.sellEnergy, 1).c_str(), true);
    }
}

void MqttPublisher::connectWiFi() {
    if (_logger) _logger->log("WiFi connecting...", ILogger::WARNING);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (_logger) _logger->log(("WiFi OK: " + WiFi.localIP().toString()).c_str(), ILogger::SUCCESS);
    } else {
        if (_logger) _logger->log("WiFi FAILED!", ILogger::ERROR);
    }
}

void MqttPublisher::connectMQTT() {
    if (_mqtt.connected()) return;
    bool connected;
    if (strlen(MQTT_USER) > 0) {
        connected = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    } else {
        connected = _mqtt.connect(MQTT_CLIENT_ID);
    }
    if (connected) {
        _mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
    }
}
