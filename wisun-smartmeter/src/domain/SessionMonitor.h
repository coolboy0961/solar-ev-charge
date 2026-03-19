#pragma once

// Tracks consecutive communication failures to detect session loss.
// Used by EchonetLiteMeter to trigger Wi-SUN reconnection.
class SessionMonitor {
public:
    explicit SessionMonitor(int threshold = 3) : _threshold(threshold) {}

    void recordSuccess() { _consecutiveFailures = 0; }
    void recordFailure() { _consecutiveFailures++; }
    bool isSessionLost() const { return _consecutiveFailures >= _threshold; }
    void reset() { _consecutiveFailures = 0; }
    int failureCount() const { return _consecutiveFailures; }

private:
    int _threshold;
    int _consecutiveFailures = 0;
};
