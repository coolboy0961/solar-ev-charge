#pragma once

#include "domain/Interfaces.h"

// Use case: poll smart meter data and publish it
class MeterService {
public:
    MeterService(IMeterReader& reader, IPublisher& publisher, IDisplay& display);

    // Called from loop(). Returns true if display was updated.
    bool update(bool meterConnected);

private:
    IMeterReader& _reader;
    IPublisher& _publisher;
    IDisplay& _display;
};
