#pragma once

#include <Preferences.h>
#include "domain/Interfaces.h"
#include "infrastructure/driver/BP35A1.h"

// Wi-SUN B-Route connection manager
// Handles scan, PANA authentication, and NVS scan cache
class WiSUN {
public:
    WiSUN(BP35A1& modem);

    void setLogger(ILogger* logger) { _logger = logger; }
    bool connect();
    bool reconnect();
    bool isConnected() const;
    void setDisconnected() { _connected = false; }
    void clearCache();

    const String& getPanaAddress() const;
    const String& getChannel() const;
    const String& getPanId() const;

private:
    BP35A1& _modem;
    ILogger* _logger = nullptr;
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
