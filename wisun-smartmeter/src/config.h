#pragma once

#include "secrets.h"

// =============================================================
// MQTT Configuration
// =============================================================
#define MQTT_SERVER "192.168.0.3"  // Synology NAS IP
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "wisun-smartmeter"

// MQTT Topics
#define MQTT_TOPIC_POWER "smartmeter/power"              // Instantaneous power (W)
#define MQTT_TOPIC_ENERGY_BUY "smartmeter/energy_buy"    // Cumulative buy (kWh)
#define MQTT_TOPIC_ENERGY_SELL "smartmeter/energy_sell"   // Cumulative sell (kWh)
#define MQTT_TOPIC_STATUS "smartmeter/status"             // Connection status

// =============================================================
// BP35A1 Serial Configuration (Wi-SUN HAT)
// =============================================================
#define BP35A1_RX_PIN 26  // HAT: ESP32 RX (GPIO26) ← BP35A1 TX
#define BP35A1_TX_PIN 0   // HAT: ESP32 TX (GPIO0) → BP35A1 RX
#define BP35A1_BAUD 115200

// =============================================================
// Timing
// =============================================================
#define POLL_INTERVAL 30000         // Read all meters every 30 seconds
#define MQTT_RECONNECT_INTERVAL 5000
