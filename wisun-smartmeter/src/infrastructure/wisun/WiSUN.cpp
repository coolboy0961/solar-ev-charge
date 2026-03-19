#include "WiSUN.h"
#include "config.h"

WiSUN::WiSUN(BP35A1& modem) : _modem(modem) {}

bool WiSUN::isConnected() const { return _connected; }
const String& WiSUN::getPanaAddress() const { return _panaAddress; }
const String& WiSUN::getChannel() const { return _channel; }
const String& WiSUN::getPanId() const { return _panId; }

// --- NVS Scan Cache ---

void WiSUN::saveCache() {
    _prefs.begin("wisun", false);
    _prefs.putString("channel", _channel);
    _prefs.putString("panId", _panId);
    _prefs.putString("macAddr", _macAddr);
    _prefs.end();
}

bool WiSUN::loadCache() {
    _prefs.begin("wisun", true);
    _channel = _prefs.getString("channel", "");
    _panId = _prefs.getString("panId", "");
    _macAddr = _prefs.getString("macAddr", "");
    _prefs.end();
    return _channel.length() > 0 && _panId.length() > 0 && _macAddr.length() > 0;
}

void WiSUN::clearCache() {
    _prefs.begin("wisun", false);
    _prefs.clear();
    _prefs.end();
    _channel = "";
    _panId = "";
    _macAddr = "";
    _connected = false;
}

// --- Module Initialization ---

bool WiSUN::init() {
    _modem.sendCommand("SKRESET", 3000);
    delay(1000);

    _modem.sendCommand("SKSREG SFE 0", 3000);
    delay(500);

    // Switch ERXUDP output to ASCII mode if needed
    String res = _modem.sendCommand("ROPT", 3000);
    if (res.indexOf("OK 00") >= 0) {
        _modem.sendCommand("WOPT 01", 3000);
        delay(500);
    }

    // Terminate any previous PANA session
    _modem.sendCommand("SKTERM", 3000);
    delay(500);

    // Set B-Route credentials
    res = _modem.sendCommand("SKSETPWD C " + String(BROUTE_PASSWORD), 3000);
    if (res.indexOf("OK") < 0) return false;
    delay(500);

    res = _modem.sendCommand("SKSETRBID " + String(BROUTE_ID), 3000);
    if (res.indexOf("OK") < 0) return false;
    delay(500);

    return true;
}

// --- Active Scan ---

bool WiSUN::scan() {
    for (int duration = 4; duration <= 7; duration++) {
        _modem.sendCommand("SKSCAN 2 FFFFFFFF " + String(duration), 1000);

        unsigned long scanTimeout = (duration <= 4) ? 30000 : 90000;
        String result = _modem.waitFor("EVENT 22", scanTimeout);

        int chIdx = result.indexOf("Channel:");
        int panIdx = result.indexOf("Pan ID:");
        int addrIdx = result.indexOf("Addr:");

        if (chIdx >= 0 && panIdx >= 0 && addrIdx >= 0) {
            _channel = result.substring(chIdx + 8, result.indexOf("\r", chIdx));
            _channel.trim();
            _panId = result.substring(panIdx + 7, result.indexOf("\r", panIdx));
            _panId.trim();
            _macAddr = result.substring(addrIdx + 5, result.indexOf("\r", addrIdx));
            _macAddr.trim();

            saveCache();
            return true;
        }
    }
    return false;
}

// --- PANA Authentication ---

bool WiSUN::panaAuth() {
    _modem.sendCommand("SKSREG S2 " + _channel, 3000);
    delay(500);
    _modem.sendCommand("SKSREG S3 " + _panId, 3000);
    delay(500);

    // MAC -> IPv6 link-local address
    String res = _modem.sendCommand("SKLL64 " + _macAddr, 3000);
    int llIdx = res.indexOf("FE80");
    if (llIdx < 0) return false;
    _panaAddress = res.substring(llIdx, res.indexOf("\r", llIdx));
    _panaAddress.trim();
    delay(500);

    // Start PANA authentication
    _modem.sendCommand("SKJOIN " + _panaAddress, 1000);

    // Wait for EVENT 25 (success) or EVENT 24 (failure)
    unsigned long authStart = millis();
    String line = "";

    while (millis() - authStart < 120000) {
        while (_modem.serial().available()) {
            char c = _modem.serial().read();
            if (c == '\r') continue;
            if (c == '\n') {
                line.trim();
                if (line.indexOf("EVENT 25") >= 0) {
                    _connected = true;
                    return true;
                } else if (line.indexOf("EVENT 24") >= 0) {
                    return false;
                }
                line = "";
            } else {
                line += c;
            }
        }
        delay(10);
    }
    return false;
}

// --- Main Connect (with retry) ---

bool WiSUN::connect() {
    if (!init()) return false;

    for (int attempt = 0; attempt < 3; attempt++) {
        bool hasScan = (attempt == 0) && loadCache();
        if (!hasScan) {
            clearCache();
            if (!scan()) continue;
        }

        if (panaAuth()) return true;

        clearCache();
        delay(5000);
    }
    return false;
}
