#include "EchonetLiteMeter.h"
#include "domain/EchonetLiteParser.h"

EchonetLiteMeter::EchonetLiteMeter(BP35A1& modem) : _modem(modem) {}

void EchonetLiteMeter::setPanaAddress(const String& addr) { _panaAddress = addr; }
const MeterData& EchonetLiteMeter::getData() const { return _data; }

// --- Polling ---

bool EchonetLiteMeter::poll() {
    if (millis() - _lastPowerRead < POWER_READ_INTERVAL) return false;

    bool needEnergy = (_lastEnergyRead == 0) ||
                      (millis() - _lastEnergyRead >= ENERGY_READ_INTERVAL);

    bool anySuccess = false;

    // Instantaneous power (EPC 0xE7)
    delay(1000);
    if (requestSync(0xE7)) anySuccess = true;

    // Cumulative energy: buy (0xE0) + sell (0xE3)
    if (needEnergy) {
        delay(1000);
        if (requestSync(0xE0)) anySuccess = true;
        delay(1000);
        if (requestSync(0xE3)) anySuccess = true;
        _lastEnergyRead = millis();
    }

    if (anySuccess) {
        _session.recordSuccess();
    } else {
        _session.recordFailure();
        Serial.printf("[ECHONET] Poll failed (consecutive=%d/%d)\n",
                      _session.failureCount(), 3);
    }

    _lastPowerRead = millis();
    return true;
}

// --- Synchronous Request ---

bool EchonetLiteMeter::requestSync(uint8_t epc, unsigned long timeout) {
    _modem.flush();

    char frame[64];
    EchonetLiteParser::buildFrame(epc, frame, sizeof(frame));
    int frameLen = strlen(frame);
    int dataLen = frameLen / 2;

    uint8_t binData[32];
    for (int i = 0; i < dataLen; i++) {
        char byteStr[3] = { frame[i * 2], frame[i * 2 + 1], '\0' };
        binData[i] = (uint8_t)strtoul(byteStr, NULL, 16);
    }

    char cmdBuf[128];
    snprintf(cmdBuf, sizeof(cmdBuf), "SKSENDTO 1 %s 0E1A 1 %04X ",
             _panaAddress.c_str(), dataLen);

    _modem.sendRaw(cmdBuf, binData, dataLen);
    Serial.printf("[ECHONET TX] EPC=0x%02X\n", epc);

    // Wait for ERXUDP response
    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (_modem.serial().available()) {
            response += (char)_modem.serial().read();
        }
        if (response.indexOf("ERXUDP") >= 0) {
            delay(200);
            while (_modem.serial().available()) {
                response += (char)_modem.serial().read();
            }
            Serial.printf("[ECHONET RX] len=%d\n", response.length());
            parseResponse(response);
            return true;
        }
        if (response.indexOf("FAIL") >= 0) {
            Serial.printf("[ECHONET] FAIL for EPC=0x%02X\n", epc);
            return false;
        }
        delay(10);
    }
    Serial.printf("[ECHONET] Timeout EPC=0x%02X\n", epc);
    return false;
}

// --- Response Parser ---
// Extracts hex payload from ERXUDP line, delegates to domain parser

void EchonetLiteMeter::parseResponse(const String& data) {
    int erxIdx = data.indexOf("ERXUDP");
    if (erxIdx < 0) return;

    String line = data.substring(erxIdx);

    // Find 9th space-separated field (hex payload)
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

    String hexData = line.substring(dataStart);
    int endIdx = hexData.indexOf('\r');
    if (endIdx < 0) endIdx = hexData.indexOf('\n');
    if (endIdx < 0) endIdx = hexData.length();
    hexData = hexData.substring(0, endIdx);
    hexData.trim();

    Serial.printf("[ECHONET RX] hex=%s len=%d\n", hexData.c_str(), hexData.length());

    EchonetLiteParser::parseFrame(hexData.c_str(), hexData.length(), _data);
}
