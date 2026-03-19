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
#define BP35A1_RX_PIN 33  // M5StickC Plus HAT RX (GPIO33)
#define BP35A1_TX_PIN 32  // M5StickC Plus HAT TX (GPIO32)
#define BP35A1_BAUD 115200

// =============================================================
// Timing
// =============================================================
#define POWER_READ_INTERVAL 10000   // Read power every 10 seconds
#define ENERGY_READ_INTERVAL 60000  // Read cumulative energy every 60 seconds
#define MQTT_RECONNECT_INTERVAL 5000
