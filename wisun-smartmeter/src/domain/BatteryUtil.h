#pragma once

namespace BatteryUtil {

// Convert battery voltage (V) to percentage (0-100)
// M5StickC Plus: 3.0V = empty, 4.2V = full
inline int voltageToPercent(float voltage) {
    int pct = (int)((voltage - 3.0f) / (4.2f - 3.0f) * 100.0f);
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
}

} // namespace BatteryUtil
