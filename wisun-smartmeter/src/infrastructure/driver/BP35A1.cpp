#include "BP35A1.h"

BP35A1::BP35A1(int rxPin, int txPin, long baud)
    : _serial(1), _rxPin(rxPin), _txPin(txPin), _baud(baud) {}

void BP35A1::begin() {
    _serial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
    delay(2000);
    flush();
    _serial.print("\r\n");
    delay(1000);
}

void BP35A1::setLogger(ILogger* logger) {
    _logger = logger;
}

void BP35A1::flush() {
    while (_serial.available()) _serial.read();
}

String BP35A1::sendCommand(const String& cmd, unsigned long timeout) {
    flush();

    _serial.print(cmd + "\r\n");
    Serial.printf("[TX] %s\n", cmd.c_str());
    if (_logger) _logger->log(("> " + cmd.substring(0, 30)).c_str(), ILogger::INFO);

    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (_serial.available()) {
            response += (char)_serial.read();
        }
        if (response.indexOf("OK") >= 0 || response.indexOf("FAIL") >= 0 ||
            response.indexOf("EVENT") >= 0) {
            break;
        }
        delay(10);
    }

    Serial.printf("[RX] %s\n", response.c_str());

    // Log abbreviated response
    String shortResp = response.substring(0, 35);
    shortResp.replace("\r\n", " ");
    shortResp.replace("\r", "");
    shortResp.replace("\n", " ");
    shortResp.trim();

    if (_logger) {
        if (shortResp.length() > 0) {
            ILogger::Level level = (response.indexOf("OK") >= 0) ? ILogger::SUCCESS :
                                   (response.indexOf("FAIL") >= 0) ? ILogger::ERROR :
                                   ILogger::WARNING;
            _logger->log(("< " + shortResp).c_str(), level);
        } else {
            _logger->log("< (no response)", ILogger::ERROR);
        }
    }
    return response;
}

void BP35A1::sendRaw(const char* header, const uint8_t* data, int len) {
    _serial.print(header);
    _serial.write(data, len);
}

String BP35A1::waitFor(const String& marker, unsigned long timeout) {
    String response = "";
    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (_serial.available()) {
            response += (char)_serial.read();
        }
        if (response.indexOf(marker) >= 0) {
            return response;
        }
        delay(10);
    }
    return response;
}

HardwareSerial& BP35A1::serial() {
    return _serial;
}
