#pragma once

#include "MeterData.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Pure parsing logic for ECHONET Lite frames
// No Arduino/hardware dependency — fully testable on host
namespace EchonetLiteParser {

static const int32_t POWER_MAX = 30000;

// Parse ECHONET Lite hex payload into MeterData
// Returns true if any property was successfully parsed
inline bool parseFrame(const char* hexData, int hexLen, MeterData& data) {
    if (hexLen < 28) return false;

    // SEOJ must be 028801 (smart meter)
    if (strncmp(hexData + 8, "028801", 6) != 0) return false;

    // ESV must be 72 (Get_Res)
    if (strncmp(hexData + 20, "72", 2) != 0) return false;

    // OPC (property count)
    char opcStr[3] = { hexData[22], hexData[23], '\0' };
    int opc = (int)strtoul(opcStr, NULL, 16);

    int pos = 24;
    bool parsed = false;

    for (int i = 0; i < opc && pos + 4 <= hexLen; i++) {
        // EPC (2 hex chars)
        char epcStr[3] = { hexData[pos], hexData[pos + 1], '\0' };

        // PDC (2 hex chars)
        char pdcStr[3] = { hexData[pos + 2], hexData[pos + 3], '\0' };
        int pdc = (int)strtoul(pdcStr, NULL, 16);

        // Property data
        int propDataLen = pdc * 2;
        if (pos + 4 + propDataLen > hexLen) break;

        char propData[32] = {};
        if (propDataLen < (int)sizeof(propData)) {
            strncpy(propData, hexData + pos + 4, propDataLen);
            propData[propDataLen] = '\0';
        }

        pos += 4 + propDataLen;

        if (strcmp(epcStr, "E7") == 0 && pdc == 4) {
            int32_t power = (int32_t)strtol(propData, NULL, 16);
            if (power >= -POWER_MAX && power <= POWER_MAX) {
                data.power = power;
                data.powerValid = true;
                parsed = true;
            }
        } else if (strcmp(epcStr, "E0") == 0 && pdc == 4) {
            data.buyEnergy = strtoul(propData, NULL, 16) * 0.1f;
            data.buyEnergyValid = true;
            parsed = true;
        } else if (strcmp(epcStr, "E3") == 0 && pdc == 4) {
            data.sellEnergy = strtoul(propData, NULL, 16) * 0.1f;
            data.sellEnergyValid = true;
            parsed = true;
        }
    }
    return parsed;
}

// Build ECHONET Lite Get request frame for a single EPC
// Returns hex string length written to buf
inline int buildFrame(uint8_t epc, char* buf, int bufSize) {
    return snprintf(buf, bufSize, "1081000105FF010288016201%02X00", epc);
}

} // namespace EchonetLiteParser
