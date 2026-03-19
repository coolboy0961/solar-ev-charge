#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// =============================================================
// BP35A1 Wi-SUN Module Communication
// =============================================================
HardwareSerial BP35A1Serial(2);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Smart meter state
String panaAddress = "";      // PAN Address (IPv6)
String channel = "";          // Wi-SUN channel
String panId = "";            // PAN ID
bool wisunConnected = false;

// Meter values
int32_t instantPower = 0;     // Instantaneous power (W)
float cumulativeBuy = 0.0;    // Cumulative buy energy (kWh)
float cumulativeSell = 0.0;   // Cumulative sell energy (kWh)

// Timing
unsigned long lastPowerRead = 0;
unsigned long lastEnergyRead = 0;

// =============================================================
// Display
// =============================================================
void updateDisplay() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(1);

    // Title
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.println("Wi-SUN Smart Meter");

    // Connection status
    M5.Lcd.setCursor(5, 25);
    if (wisunConnected) {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("Connected");
    } else {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("Connecting...");
    }

    // Power
    M5.Lcd.setCursor(5, 50);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("%d W", instantPower);

    // Energy
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(5, 80);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.printf("Buy:  %.1f kWh", cumulativeBuy);
    M5.Lcd.setCursor(5, 100);
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.printf("Sell: %.1f kWh", cumulativeSell);

    // MQTT status
    M5.Lcd.setCursor(5, 125);
    M5.Lcd.setTextSize(1);
    if (mqtt.connected()) {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("MQTT: OK");
    } else {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("MQTT: Disconnected");
    }
}

// =============================================================
// BP35A1 Serial Communication
// =============================================================
String sendCommand(const String& cmd, unsigned long timeout = 5000) {
    // Clear input buffer
    while (BP35A1Serial.available()) BP35A1Serial.read();

    BP35A1Serial.print(cmd + "\r\n");
    Serial.printf("[TX] %s\n", cmd.c_str());

    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            response += c;
        }
        if (response.indexOf("OK") >= 0 || response.indexOf("FAIL") >= 0 ||
            response.indexOf("EVENT") >= 0) {
            break;
        }
        delay(10);
    }
    Serial.printf("[RX] %s\n", response.c_str());
    return response;
}

String waitResponse(unsigned long timeout = 30000) {
    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            response += c;
        }
        delay(10);
    }
    return response;
}

// =============================================================
// Wi-SUN B-Route Connection
// =============================================================
bool connectWiSUN() {
    Serial.println("=== Starting Wi-SUN B-Route connection ===");

    // Reset module
    sendCommand("SKRESET", 3000);
    delay(1000);

    // Disable echo
    sendCommand("SKSREG SFE 0", 3000);
    delay(500);

    // Set B-Route password
    String pwdCmd = "SKSETPWD C " + String(BROUTE_PASSWORD);
    String res = sendCommand(pwdCmd, 3000);
    if (res.indexOf("OK") < 0) {
        Serial.println("Failed to set password");
        return false;
    }
    delay(500);

    // Set B-Route ID
    String idCmd = "SKSETRBID " + String(BROUTE_ID);
    res = sendCommand(idCmd, 3000);
    if (res.indexOf("OK") < 0) {
        Serial.println("Failed to set B-Route ID");
        return false;
    }
    delay(500);

    // Active scan for smart meter
    Serial.println("Scanning for smart meter...");
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("Scanning...");

    // Scan duration 6 (about 10 seconds per channel)
    sendCommand("SKSCAN 2 FFFFFFFF 6", 1000);

    // Wait for scan results (up to 60 seconds)
    unsigned long scanStart = millis();
    String scanResult = "";
    bool scanDone = false;

    while (!scanDone && millis() - scanStart < 60000) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            scanResult += c;
        }
        if (scanResult.indexOf("EVENT 22") >= 0) {
            scanDone = true;
        }
        delay(10);
    }

    Serial.printf("[SCAN] %s\n", scanResult.c_str());

    // Parse scan results
    int chIdx = scanResult.indexOf("Channel:");
    int panIdx = scanResult.indexOf("Pan ID:");
    int addrIdx = scanResult.indexOf("Addr:");

    if (chIdx < 0 || panIdx < 0 || addrIdx < 0) {
        Serial.println("Smart meter not found in scan");
        return false;
    }

    // Extract values
    channel = scanResult.substring(chIdx + 8, scanResult.indexOf("\r", chIdx));
    channel.trim();
    panId = scanResult.substring(panIdx + 7, scanResult.indexOf("\r", panIdx));
    panId.trim();
    String macAddr = scanResult.substring(addrIdx + 5, scanResult.indexOf("\r", addrIdx));
    macAddr.trim();

    Serial.printf("Channel: %s, PAN ID: %s, Addr: %s\n",
                  channel.c_str(), panId.c_str(), macAddr.c_str());

    // Set channel
    sendCommand("SKSREG S2 " + channel, 3000);
    delay(500);

    // Set PAN ID
    sendCommand("SKSREG S3 " + panId, 3000);
    delay(500);

    // Convert MAC to IPv6 link-local
    res = sendCommand("SKLL64 " + macAddr, 3000);
    int llIdx = res.indexOf("FE80");
    if (llIdx < 0) {
        Serial.println("Failed to get IPv6 address");
        return false;
    }
    panaAddress = res.substring(llIdx, res.indexOf("\r", llIdx));
    panaAddress.trim();
    Serial.printf("IPv6: %s\n", panaAddress.c_str());
    delay(500);

    // PANA authentication
    Serial.println("Starting PANA authentication...");
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("Authenticating...");

    sendCommand("SKJOIN " + panaAddress, 1000);

    // Wait for PANA result (up to 60 seconds)
    unsigned long authStart = millis();
    String authResult = "";
    bool authDone = false;

    while (!authDone && millis() - authStart < 60000) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            authResult += c;
        }
        // EVENT 25 = PANA success, EVENT 24 = PANA fail
        if (authResult.indexOf("EVENT 25") >= 0) {
            Serial.println("PANA authentication successful!");
            authDone = true;
            wisunConnected = true;
        } else if (authResult.indexOf("EVENT 24") >= 0) {
            Serial.println("PANA authentication FAILED");
            return false;
        }
        delay(10);
    }

    if (!authDone) {
        Serial.println("PANA authentication timeout");
        return false;
    }

    return true;
}

// =============================================================
// ECHONET Lite Smart Meter Queries
// =============================================================

// Build ECHONET Lite frame
// EHD1=10, EHD2=81, TID=0001
// SEOJ=05FF01 (controller), DEOJ=028801 (smart meter)
// ESV=62 (Get), OPC=01, EPC=target property
String buildEchonetFrame(uint8_t epc) {
    // Frame: 1081 0001 05FF01 028801 62 01 EPC 00
    char frame[64];
    snprintf(frame, sizeof(frame),
             "1081000105FF01028801620100%02X00", epc);
    return String(frame);
}

void sendEchonetRequest(uint8_t epc) {
    String frame = buildEchonetFrame(epc);
    int dataLen = frame.length() / 2;

    // SKSENDTO <handle> <ipaddr> <port> <sec> <datalen> <data>
    String cmd = "SKSENDTO 1 " + panaAddress + " 0E1A 1 " +
                 String(dataLen, HEX) + " ";

    // Send command
    BP35A1Serial.print(cmd);
    // Send binary data
    for (int i = 0; i < frame.length(); i += 2) {
        uint8_t b = strtoul(frame.substring(i, i + 2).c_str(), NULL, 16);
        BP35A1Serial.write(b);
    }
    BP35A1Serial.print("\r\n");
    Serial.printf("[ECHONET TX] EPC=0x%02X\n", epc);
}

// Parse ECHONET Lite response from ERXUDP
void parseEchonetResponse(const String& data) {
    // Find ERXUDP in data
    int erxIdx = data.indexOf("ERXUDP");
    if (erxIdx < 0) return;

    // ERXUDP format: ERXUDP <sender> <dest> <rport> <lport> <senderlla> <secured> <datalen> <data>
    // Split by space to get the data field (last element)
    String line = data.substring(erxIdx);
    int lastSpace = line.lastIndexOf(' ');
    if (lastSpace < 0) return;

    String hexData = line.substring(lastSpace + 1);
    hexData.trim();

    Serial.printf("[ECHONET RX] %s\n", hexData.c_str());

    // Minimum ECHONET Lite frame: header(4) + TID(4) + SEOJ(6) + DEOJ(6) + ESV(2) + OPC(2) + EPC(2) + PDC(2) = 28 chars
    if (hexData.length() < 28) return;

    // Check ESV = 72 (Get_Res)
    String esv = hexData.substring(20, 22);
    if (esv != "72") return;

    // Get EPC and data
    String epc = hexData.substring(24, 26);
    int pdc = strtoul(hexData.substring(26, 28).c_str(), NULL, 16);
    String propData = hexData.substring(28, 28 + pdc * 2);

    if (epc == "E7") {
        // Instantaneous power (signed 32-bit, W)
        int32_t power = (int32_t)strtol(propData.c_str(), NULL, 16);
        instantPower = power;
        Serial.printf("Instantaneous power: %d W\n", power);

        // Publish to MQTT
        if (mqtt.connected()) {
            mqtt.publish(MQTT_TOPIC_POWER, String(power).c_str(), true);
        }
    } else if (epc == "E0") {
        // Cumulative buy energy (unsigned 32-bit, 0.1kWh unit)
        uint32_t energy = strtoul(propData.c_str(), NULL, 16);
        cumulativeBuy = energy * 0.1;
        Serial.printf("Cumulative buy: %.1f kWh\n", cumulativeBuy);

        if (mqtt.connected()) {
            mqtt.publish(MQTT_TOPIC_ENERGY_BUY, String(cumulativeBuy, 1).c_str(), true);
        }
    } else if (epc == "E3") {
        // Cumulative sell energy (unsigned 32-bit, 0.1kWh unit)
        uint32_t energy = strtoul(propData.c_str(), NULL, 16);
        cumulativeSell = energy * 0.1;
        Serial.printf("Cumulative sell: %.1f kWh\n", cumulativeSell);

        if (mqtt.connected()) {
            mqtt.publish(MQTT_TOPIC_ENERGY_SELL, String(cumulativeSell, 1).c_str(), true);
        }
    }
}

// =============================================================
// WiFi & MQTT
// =============================================================
void connectWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.println("WiFi connecting...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

void connectMQTT() {
    if (!mqtt.connected()) {
        Serial.println("Connecting to MQTT...");
        bool connected;
        if (strlen(MQTT_USER) > 0) {
            connected = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
        } else {
            connected = mqtt.connect(MQTT_CLIENT_ID);
        }
        if (connected) {
            Serial.println("MQTT connected");
            mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
        } else {
            Serial.printf("MQTT failed: %d\n", mqtt.state());
        }
    }
}

// =============================================================
// Setup & Loop
// =============================================================
void setup() {
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(5, 5);
    M5.Lcd.println("Wi-SUN Smart Meter");
    M5.Lcd.println("Initializing...");

    Serial.begin(115200);
    Serial.println("=== Wi-SUN Smart Meter ===");

    // Initialize BP35A1 serial
    BP35A1Serial.begin(BP35A1_BAUD, SERIAL_8N1, BP35A1_RX_PIN, BP35A1_TX_PIN);
    delay(1000);

    // Connect WiFi
    connectWiFi();

    // Setup MQTT
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);
    connectMQTT();

    // Connect to smart meter via Wi-SUN
    if (!connectWiSUN()) {
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(5, 5);
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("Wi-SUN connection");
        M5.Lcd.println("FAILED!");
        M5.Lcd.println("");
        M5.Lcd.println("Check B-Route ID");
        M5.Lcd.println("and Password");
        Serial.println("Wi-SUN connection failed!");
    }

    updateDisplay();
}

void loop() {
    M5.update();
    mqtt.loop();

    // Reconnect WiFi if needed
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    // Reconnect MQTT if needed
    if (!mqtt.connected()) {
        connectMQTT();
    }

    // Read BP35A1 responses
    if (BP35A1Serial.available()) {
        String response = "";
        unsigned long readStart = millis();
        while (millis() - readStart < 2000) {
            while (BP35A1Serial.available()) {
                char c = BP35A1Serial.read();
                response += c;
                readStart = millis(); // Reset timeout on new data
            }
            delay(10);
        }
        if (response.length() > 0) {
            parseEchonetResponse(response);
            updateDisplay();
        }
    }

    // Request instantaneous power (EPC 0xE7)
    if (wisunConnected && millis() - lastPowerRead >= POWER_READ_INTERVAL) {
        sendEchonetRequest(0xE7);
        lastPowerRead = millis();
    }

    // Request cumulative energy (EPC 0xE0 buy, 0xE3 sell)
    if (wisunConnected && millis() - lastEnergyRead >= ENERGY_READ_INTERVAL) {
        sendEchonetRequest(0xE0);
        delay(2000);
        sendEchonetRequest(0xE3);
        lastEnergyRead = millis();
    }

    // Button A: Force refresh display
    if (M5.BtnA.wasPressed()) {
        updateDisplay();
    }

    delay(100);
}
