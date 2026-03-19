#pragma once

#include "MeterData.h"

// --- Meter Reader ---
// Reads data from a smart meter device
class IMeterReader {
public:
    virtual ~IMeterReader() = default;
    virtual bool poll() = 0;  // Returns true if new data was fetched
    virtual const MeterData& getData() const = 0;
};

// --- Publisher ---
// Publishes meter data to an external system (e.g. MQTT)
class IPublisher {
public:
    virtual ~IPublisher() = default;
    virtual void loop() = 0;
    virtual bool isConnected() = 0;
    virtual void publish(const MeterData& data) = 0;
};

// --- Logger ---
// Outputs log messages (LCD, serial, etc.)
class ILogger {
public:
    virtual ~ILogger() = default;
    enum Level { INFO, SUCCESS, WARNING, ERROR };
    virtual void log(const char* msg, Level level = INFO) = 0;
};

// --- Display ---
// Shows meter status on a screen
class IDisplay : public ILogger {
public:
    virtual void showStatus(bool meterOk, const MeterData& data, bool publisherOk) = 0;
};
