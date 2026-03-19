#pragma once

#include <cstdint>

// Value object: smart meter readings
struct MeterData {
    int32_t power = 0;       // Instantaneous power (W)
    float buyEnergy = 0.0;   // Cumulative buy energy (kWh)
    float sellEnergy = 0.0;  // Cumulative sell energy (kWh)
    bool powerValid = false;
    bool energyValid = false;
};
