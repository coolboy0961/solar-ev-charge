#pragma once

#include <M5StickCPlus.h>
#include "domain/Interfaces.h"

// M5StickC Plus LCD implementation of IDisplay
class LcdDisplay : public IDisplay {
public:
    void begin();

    // ILogger
    void log(const char* msg, Level level = INFO) override;

    // IDisplay
    void showStatus(bool meterOk, const MeterData& data, bool publisherOk) override;

    void showDebug(bool meterOk, const String& ipv6, const MeterData& data,
                   bool publisherOk, const String& channel, const String& panId);
    bool btnAPressed();
    bool btnBPressed();

private:
    int _logY = 0;

    uint16_t levelToColor(Level level);
};
