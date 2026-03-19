#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "config.h"

// =============================================================
// BP35A1 Wi-SUN Module Communication
// =============================================================
HardwareSerial BP35A1Serial(1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

// Smart meter state
String panaAddress = "";
String channel = "";
String panId = "";
String macAddr = "";
bool wisunConnected = false;

// Meter values
int32_t instantPower = 0;
float cumulativeBuy = 0.0;
float cumulativeSell = 0.0;

// Timing
unsigned long lastPowerRead = 0;
unsigned long lastEnergyBuyRead = 0;
unsigned long lastEnergySellRead = 0;
unsigned long lastMqttAttempt = 0;

// =============================================================
// Debug LCD Log
// =============================================================
int lcdLogY = 0;
void lcdLog(const String& msg, uint16_t color = WHITE) {
    if (lcdLogY > 120) {
        M5.Lcd.fillScreen(BLACK);
        lcdLogY = 0;
    }
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(color, BLACK);
    M5.Lcd.setCursor(2, lcdLogY);
    M5.Lcd.println(msg);
    lcdLogY += 10;
    Serial.println(msg);
}

// =============================================================
// Display
// =============================================================
void updateDisplay() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.println("Wi-SUN Smart Meter");

    M5.Lcd.setCursor(5, 25);
    if (wisunConnected) {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("Connected");
    } else {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("Connecting...");
    }

    M5.Lcd.setCursor(5, 50);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("%d W", instantPower);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(5, 80);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.printf("Buy:  %.1f kWh", cumulativeBuy);
    M5.Lcd.setCursor(5, 100);
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.printf("Sell: %.1f kWh", cumulativeSell);

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
    while (BP35A1Serial.available()) BP35A1Serial.read();

    BP35A1Serial.print(cmd + "\r\n");
    Serial.printf("[TX] %s\n", cmd.c_str());
    lcdLog("> " + cmd.substring(0, 30), CYAN);

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
    String shortResp = response.substring(0, 35);
    shortResp.replace("\r\n", " ");
    shortResp.replace("\r", "");
    shortResp.replace("\n", " ");
    shortResp.trim();
    if (shortResp.length() > 0) {
        uint16_t color = (response.indexOf("OK") >= 0) ? GREEN :
                         (response.indexOf("FAIL") >= 0) ? RED : YELLOW;
        lcdLog("< " + shortResp, color);
    } else {
        lcdLog("< (no response)", RED);
    }
    return response;
}

// =============================================================
// Scan result cache (NVS flash)
// =============================================================
void saveScanResult() {
    prefs.begin("wisun", false);
    prefs.putString("channel", channel);
    prefs.putString("panId", panId);
    prefs.putString("macAddr", macAddr);
    prefs.end();
    lcdLog("Scan saved to NVS", GREEN);
}

bool loadScanResult() {
    prefs.begin("wisun", true);
    channel = prefs.getString("channel", "");
    panId = prefs.getString("panId", "");
    macAddr = prefs.getString("macAddr", "");
    prefs.end();
    if (channel.length() > 0 && panId.length() > 0 && macAddr.length() > 0) {
        lcdLog("Loaded scan from NVS", GREEN);
        lcdLog("Ch:" + channel + " PAN:" + panId, GREEN);
        return true;
    }
    return false;
}

void clearScanResult() {
    prefs.begin("wisun", false);
    prefs.clear();
    prefs.end();
    channel = "";
    panId = "";
    macAddr = "";
}

// =============================================================
// Wi-SUN B-Route Connection
// =============================================================
bool initBP35A1() {
    M5.Lcd.fillScreen(BLACK);
    lcdLogY = 0;
    lcdLog("=== Wi-SUN Init ===", CYAN);

    // Reset module
    sendCommand("SKRESET", 3000);
    delay(1000);

    // Disable echo
    sendCommand("SKSREG SFE 0", 3000);
    delay(500);

    // Check ERXUDP output mode and switch to ASCII if binary
    lcdLog("Checking WOPT...", YELLOW);
    String res = sendCommand("ROPT", 3000);
    if (res.indexOf("OK 00") >= 0) {
        // Binary mode -> switch to ASCII
        lcdLog("Binary mode, switching...", YELLOW);
        sendCommand("WOPT 01", 3000);
        delay(500);
    } else {
        lcdLog("ASCII mode OK", GREEN);
    }

    // Terminate any previous PANA session
    sendCommand("SKTERM", 3000);
    delay(500);

    // Set B-Route password
    lcdLog("Setting B-Route auth...", YELLOW);
    String pwdCmd = "SKSETPWD C " + String(BROUTE_PASSWORD);
    res = sendCommand(pwdCmd, 3000);
    if (res.indexOf("OK") < 0) {
        lcdLog("FAIL: set password", RED);
        return false;
    }
    delay(500);

    // Set B-Route ID
    String idCmd = "SKSETRBID " + String(BROUTE_ID);
    res = sendCommand(idCmd, 3000);
    if (res.indexOf("OK") < 0) {
        lcdLog("FAIL: set B-Route ID", RED);
        return false;
    }
    delay(500);
    return true;
}

bool scanSmartMeter() {
    M5.Lcd.fillScreen(BLACK);
    lcdLogY = 0;

    // Try scan with increasing duration (4->5->6->7)
    for (int duration = 4; duration <= 7; duration++) {
        lcdLog("Scanning dur=" + String(duration) + "...", YELLOW);

        sendCommand("SKSCAN 2 FFFFFFFF " + String(duration), 1000);

        // Wait for scan results
        unsigned long scanStart = millis();
        String scanResult = "";
        bool scanDone = false;
        unsigned long scanTimeout = (duration <= 4) ? 30000 : 90000;

        while (!scanDone && millis() - scanStart < scanTimeout) {
            while (BP35A1Serial.available()) {
                char c = BP35A1Serial.read();
                scanResult += c;
            }
            if (scanResult.indexOf("EVENT 22") >= 0) {
                scanDone = true;
            }
            delay(10);
        }

        // Parse scan results
        int chIdx = scanResult.indexOf("Channel:");
        int panIdx = scanResult.indexOf("Pan ID:");
        int addrIdx = scanResult.indexOf("Addr:");

        if (chIdx >= 0 && panIdx >= 0 && addrIdx >= 0) {
            channel = scanResult.substring(chIdx + 8, scanResult.indexOf("\r", chIdx));
            channel.trim();
            panId = scanResult.substring(panIdx + 7, scanResult.indexOf("\r", panIdx));
            panId.trim();
            macAddr = scanResult.substring(addrIdx + 5, scanResult.indexOf("\r", addrIdx));
            macAddr.trim();

            lcdLog("Found! Ch:" + channel, GREEN);
            lcdLog("PAN:" + panId, GREEN);
            lcdLog("Addr:" + macAddr.substring(0, 20), GREEN);

            saveScanResult();
            return true;
        }

        lcdLog("Not found, retrying...", RED);
    }
    return false;
}

bool connectPANA() {
    M5.Lcd.fillScreen(BLACK);
    lcdLogY = 0;

    // Set channel
    sendCommand("SKSREG S2 " + channel, 3000);
    delay(500);

    // Set PAN ID
    sendCommand("SKSREG S3 " + panId, 3000);
    delay(500);

    // Convert MAC to IPv6 link-local
    String res = sendCommand("SKLL64 " + macAddr, 3000);
    int llIdx = res.indexOf("FE80");
    if (llIdx < 0) {
        lcdLog("FAIL: get IPv6", RED);
        return false;
    }
    panaAddress = res.substring(llIdx, res.indexOf("\r", llIdx));
    panaAddress.trim();
    lcdLog("IPv6: " + panaAddress.substring(0, 25), GREEN);
    delay(500);

    // PANA authentication
    lcdLog("PANA auth...", YELLOW);
    sendCommand("SKJOIN " + panaAddress, 1000);

    unsigned long authStart = millis();
    bool authDone = false;
    String line = "";

    while (!authDone && millis() - authStart < 120000) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                line.trim();
                if (line.length() > 0 && line.indexOf("EVENT") >= 0) {
                    lcdLog(line.substring(0, 35), YELLOW);
                }
                if (line.indexOf("EVENT 25") >= 0) {
                    lcdLog("PANA SUCCESS!", GREEN);
                    authDone = true;
                    wisunConnected = true;
                } else if (line.indexOf("EVENT 24") >= 0) {
                    lcdLog("PANA FAILED!", RED);
                    return false;
                }
                line = "";
            } else {
                line += c;
            }
        }
        delay(10);
    }

    if (!authDone) {
        lcdLog("PANA TIMEOUT!", RED);
        return false;
    }
    return true;
}

// Main connection with retry logic
bool connectWiSUN() {
    if (!initBP35A1()) return false;

    for (int attempt = 0; attempt < 3; attempt++) {
        lcdLog("Attempt " + String(attempt + 1) + "/3", WHITE);

        // Try cached scan first, then active scan
        bool hasScan = false;
        if (attempt == 0) {
            hasScan = loadScanResult();
        }
        if (!hasScan) {
            clearScanResult();
            if (!scanSmartMeter()) continue;
        }

        // Try PANA auth
        if (connectPANA()) return true;

        // PANA failed — clear cache and rescan next attempt
        lcdLog("Clearing cache, retry...", RED);
        clearScanResult();
        delay(5000);  // Cooldown before retry
    }
    return false;
}

// =============================================================
// ECHONET Lite Smart Meter Queries
// =============================================================
// Forward declaration
void parseEchonetResponse(const String& data);

// Build ECHONET Lite frame for single EPC
String buildEchonetFrame(uint8_t epc) {
    char frame[64];
    snprintf(frame, sizeof(frame),
             "1081000105FF010288016201%02X00", epc);
    return String(frame);
}

// Send ECHONET request and wait for response (synchronous)
bool sendEchonetRequestSync(uint8_t epc, unsigned long timeout = 10000) {
    // Flush stale data from serial buffer
    while (BP35A1Serial.available()) BP35A1Serial.read();

    String frame = buildEchonetFrame(epc);
    int dataLen = frame.length() / 2;

    uint8_t binData[32];
    for (int i = 0; i < dataLen; i++) {
        binData[i] = strtoul(frame.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
    }

    char cmdBuf[128];
    snprintf(cmdBuf, sizeof(cmdBuf), "SKSENDTO 1 %s 0E1A 1 %04X ",
             panaAddress.c_str(), dataLen);

    BP35A1Serial.print(cmdBuf);
    BP35A1Serial.write(binData, dataLen);
    Serial.printf("[ECHONET TX] EPC=0x%02X\n", epc);

    // Wait for ERXUDP response
    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (BP35A1Serial.available()) {
            char c = BP35A1Serial.read();
            response += c;
        }
        if (response.indexOf("ERXUDP") >= 0) {
            // Wait a bit more for complete data
            delay(200);
            while (BP35A1Serial.available()) {
                response += (char)BP35A1Serial.read();
            }
            Serial.printf("[ECHONET RX] len=%d\n", response.length());
            parseEchonetResponse(response);
            return true;
        }
        if (response.indexOf("FAIL") >= 0) {
            Serial.printf("[ECHONET] FAIL for EPC=0x%02X\n", epc);
            return false;
        }
        delay(10);
    }
    Serial.printf("[ECHONET] Timeout EPC=0x%02X resp=%s\n", epc, response.substring(0, 80).c_str());
    return false;
}

// Parse ECHONET Lite response from ERXUDP
// With WOPT 01 (ASCII mode), data field is hex text, not binary
void parseEchonetResponse(const String& data) {
    int erxIdx = data.indexOf("ERXUDP");
    if (erxIdx < 0) return;

    // ERXUDP has 9 space-separated fields; last is the hex data
    String line = data.substring(erxIdx);

    // Count spaces to find 9th field (data)
    int spaceCount = 0;
    int dataStart = -1;
    for (int i = 0; i < (int)line.length(); i++) {
        if (line[i] == ' ') {
            spaceCount++;
            if (spaceCount == 8) {
                dataStart = i + 1;
                break;
            }
        }
    }

    if (dataStart < 0) return;

    // In ASCII mode, data is hex text until end of line
    String hexData = line.substring(dataStart);
    // Trim at first CR/LF or space
    int endIdx = hexData.indexOf('\r');
    if (endIdx < 0) endIdx = hexData.indexOf('\n');
    if (endIdx < 0) endIdx = hexData.length();
    hexData = hexData.substring(0, endIdx);
    hexData.trim();

    Serial.printf("[ECHONET RX] hex=%s len=%d\n", hexData.c_str(), hexData.length());

    // Minimum ECHONET Lite frame = 28 hex chars
    if (hexData.length() < 28) return;

    // Check SEOJ = 028801 (smart meter)
    String seoj = hexData.substring(8, 14);
    if (seoj != "028801") return;

    // Check ESV = 72 (Get_Res)
    String esv = hexData.substring(20, 22);
    if (esv != "72") return;

    // Parse multiple properties (OPC may be > 1)
    int opc = strtoul(hexData.substring(22, 24).c_str(), NULL, 16);
    int pos = 24;  // start of first EPC

    for (int i = 0; i < opc && pos + 4 <= (int)hexData.length(); i++) {
        String epc = hexData.substring(pos, pos + 2);
        int pdc = strtoul(hexData.substring(pos + 2, pos + 4).c_str(), NULL, 16);
        String propData = hexData.substring(pos + 4, pos + 4 + pdc * 2);
        pos += 4 + pdc * 2;

        Serial.printf("[ECHONET] EPC=%s PDC=%d data=%s\n",
                      epc.c_str(), pdc, propData.c_str());

        if (epc == "E7" && pdc == 4) {
            int32_t power = (int32_t)strtol(propData.c_str(), NULL, 16);
            if (power < -30000 || power > 30000) {
                Serial.printf("Power out of range: %d W\n", power);
                continue;
            }
            instantPower = power;
            if (mqtt.connected()) {
                mqtt.publish(MQTT_TOPIC_POWER, String(power).c_str(), true);
            }
        } else if (epc == "E0" && pdc == 4) {
            uint32_t energy = strtoul(propData.c_str(), NULL, 16);
            cumulativeBuy = energy * 0.1;
            if (mqtt.connected()) {
                mqtt.publish(MQTT_TOPIC_ENERGY_BUY, String(cumulativeBuy, 1).c_str(), true);
            }
        } else if (epc == "E3" && pdc == 4) {
            uint32_t energy = strtoul(propData.c_str(), NULL, 16);
            cumulativeSell = energy * 0.1;
            if (mqtt.connected()) {
                mqtt.publish(MQTT_TOPIC_ENERGY_SELL, String(cumulativeSell, 1).c_str(), true);
            }
        }
    }
}

// =============================================================
// WiFi & MQTT
// =============================================================
void connectWiFi() {
    lcdLog("WiFi: " + String(WIFI_SSID), YELLOW);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        lcdLog("WiFi OK: " + WiFi.localIP().toString(), GREEN);
    } else {
        lcdLog("WiFi FAILED!", RED);
    }
}

void connectMQTT() {
    if (!mqtt.connected()) {
        bool connected;
        if (strlen(MQTT_USER) > 0) {
            connected = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
        } else {
            connected = mqtt.connect(MQTT_CLIENT_ID);
        }
        if (connected) {
            mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
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
    lcdLogY = 0;
    lcdLog("Wi-SUN Smart Meter", CYAN);
    lcdLog("Initializing...", WHITE);

    Serial.begin(115200);

    // Initialize BP35A1 serial
    BP35A1Serial.begin(BP35A1_BAUD, SERIAL_8N1, BP35A1_RX_PIN, BP35A1_TX_PIN);
    delay(2000);
    while (BP35A1Serial.available()) BP35A1Serial.read();
    BP35A1Serial.print("\r\n");
    delay(1000);

    // Connect WiFi
    connectWiFi();

    // Setup MQTT
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);
    connectMQTT();

    // Connect to smart meter via Wi-SUN (with retry)
    if (connectWiSUN()) {
        lcdLog("Wi-SUN READY!", GREEN);
    } else {
        lcdLog("Wi-SUN FAILED after retries", RED);
    }
    delay(3000);
    updateDisplay();
}

void loop() {
    M5.update();
    mqtt.loop();

    // Reconnect WiFi if needed
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    // Reconnect MQTT (with interval)
    if (!mqtt.connected() && millis() - lastMqttAttempt >= MQTT_RECONNECT_INTERVAL) {
        lastMqttAttempt = millis();
        connectMQTT();
    }

    // Synchronous ECHONET Lite requests
    // Request power every 10s, energy (buy+sell) every 60s (and on first run)
    if (wisunConnected && millis() - lastPowerRead >= POWER_READ_INTERVAL) {
        bool needEnergy = (lastEnergyBuyRead == 0) || (millis() - lastEnergyBuyRead >= ENERGY_READ_INTERVAL);

        // Always request instantaneous power (E7)
        delay(1000);  // 1s pause before request (per reference impl)
        sendEchonetRequestSync(0xE7);

        if (needEnergy) {
            // Request cumulative buy (E0)
            delay(1000);
            sendEchonetRequestSync(0xE0);

            // Request cumulative sell (E3)
            delay(1000);
            sendEchonetRequestSync(0xE3);

            lastEnergyBuyRead = millis();
            lastEnergySellRead = millis();
        }

        lastPowerRead = millis();
        updateDisplay();
    }

    // Button A: Show debug info
    if (M5.BtnA.wasPressed()) {
        M5.Lcd.fillScreen(BLACK);
        lcdLogY = 0;
        lcdLog("=== DEBUG ===", CYAN);
        lcdLog("wisunOK: " + String(wisunConnected ? "YES" : "NO"), wisunConnected ? GREEN : RED);
        lcdLog("IPv6: " + panaAddress.substring(0, 25), WHITE);
        lcdLog("Power: " + String(instantPower) + " W", WHITE);
        lcdLog("Buy: " + String(cumulativeBuy, 1) + " kWh", WHITE);
        lcdLog("Sell: " + String(cumulativeSell, 1) + " kWh", WHITE);
        lcdLog("MQTT: " + String(mqtt.connected() ? "OK" : "NO"), WHITE);
        lcdLog("Ch:" + channel + " PAN:" + panId, WHITE);
        delay(5000);
        updateDisplay();
    }

    // Button B (side): Clear scan cache & reboot
    if (M5.BtnB.wasPressed()) {
        M5.Lcd.fillScreen(BLACK);
        lcdLogY = 0;
        lcdLog("Clearing scan cache...", RED);
        clearScanResult();
        delay(1000);
        lcdLog("Rebooting...", RED);
        delay(1000);
        ESP.restart();
    }

    delay(100);
}
