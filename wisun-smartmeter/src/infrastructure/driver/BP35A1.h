#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "domain/Interfaces.h"

// Low-level serial driver for ROHM BP35A1 Wi-SUN module
class BP35A1 {
public:
    BP35A1(int rxPin, int txPin, long baud);

    void begin();
    void setLogger(ILogger* logger);

    String sendCommand(const String& cmd, unsigned long timeout = 5000);
    void sendRaw(const char* header, const uint8_t* data, int len);
    String waitFor(const String& marker, unsigned long timeout);
    void flush();

    HardwareSerial& serial();

private:
    HardwareSerial _serial;
    int _rxPin;
    int _txPin;
    long _baud;
    ILogger* _logger = nullptr;
};
