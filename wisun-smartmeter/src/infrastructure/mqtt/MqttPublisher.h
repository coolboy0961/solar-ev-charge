#pragma once

#include <WiFi.h>
#include <PubSubClient.h>
#include "domain/Interfaces.h"

// MQTT publisher: connects WiFi + MQTT, publishes meter data
class MqttPublisher : public IPublisher {
public:
    void begin(ILogger* logger);

    // IPublisher
    void loop() override;
    bool isConnected() override;
    void publish(const MeterData& data) override;

private:
    WiFiClient _wifiClient;
    PubSubClient _mqtt;
    unsigned long _lastAttempt = 0;
    ILogger* _logger = nullptr;

    void connectWiFi();
    void connectMQTT();
};
