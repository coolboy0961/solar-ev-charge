#pragma once

#include <Preferences.h>
#include "infrastructure/driver/BP35A1.h"

// Wi-SUN B-Route connection manager
// Handles scan, PANA authentication, and NVS scan cache
class WiSUN {
public:
    WiSUN(BP35A1& modem);

    bool connect();
    bool isConnected() const;
    void clearCache();

    const String& getPanaAddress() const;
    const String& getChannel() const;
    const String& getPanId() const;

private:
    BP35A1& _modem;
    Preferences _prefs;

    String _channel;
    String _panId;
    String _macAddr;
    String _panaAddress;
    bool _connected = false;

    bool init();
    bool scan();
    bool loadCache();
    void saveCache();
    bool panaAuth();
};
