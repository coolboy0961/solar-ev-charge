#pragma once

#include "domain/Interfaces.h"
#include "domain/SessionMonitor.h"
#include "infrastructure/driver/BP35A1.h"
#include "config.h"

// ECHONET Lite smart meter reader
// Implements IMeterReader: sends synchronous requests via BP35A1
class EchonetLiteMeter : public IMeterReader {
public:
    EchonetLiteMeter(BP35A1& modem);

    void setPanaAddress(const String& addr);

    // IMeterReader
    bool poll() override;
    const MeterData& getData() const override;

    SessionMonitor& session() { return _session; }

private:
    BP35A1& _modem;
    String _panaAddress;
    MeterData _data;
    SessionMonitor _session{3};

    unsigned long _lastPowerRead = 0;
    unsigned long _lastEnergyRead = 0;

    bool requestSync(uint8_t epc, unsigned long timeout = 10000);
    void parseResponse(const String& data);
};
