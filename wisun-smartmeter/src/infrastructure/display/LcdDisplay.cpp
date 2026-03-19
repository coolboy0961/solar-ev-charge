#include "LcdDisplay.h"
#include "domain/BatteryUtil.h"

uint16_t LcdDisplay::levelToColor(Level level) {
    switch (level) {
        case SUCCESS: return GREEN;
        case WARNING: return YELLOW;
        case ERROR:   return RED;
        default:      return WHITE;
    }
}

void LcdDisplay::begin() {
    M5.begin();
    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(BLACK);
    _logY = 0;
}

void LcdDisplay::log(const char* msg, Level level) {
    if (_logY > 120) {
        M5.Lcd.fillScreen(BLACK);
        _logY = 0;
    }
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(levelToColor(level), BLACK);
    M5.Lcd.setCursor(2, _logY);
    M5.Lcd.println(msg);
    _logY += 10;
    Serial.println(msg);
}

int LcdDisplay::getBatteryPercent() {
    return BatteryUtil::voltageToPercent(M5.Axp.GetBatVoltage());
}

void LcdDisplay::drawBattery() {
    int pct = getBatteryPercent();
    uint16_t color = (pct > 50) ? GREEN : (pct > 20) ? YELLOW : RED;

    // Battery icon (16x8)
    int bx = 178, by = 5;
    M5.Lcd.drawRect(bx, by, 14, 8, color);       // body
    M5.Lcd.fillRect(bx + 14, by + 2, 2, 4, color); // tip
    int fillW = (int)(10.0f * pct / 100.0f);
    if (fillW > 0) M5.Lcd.fillRect(bx + 2, by + 2, fillW, 4, color);

    // Percentage text
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(color, BLACK);
    M5.Lcd.setCursor(198, 5);
    M5.Lcd.printf("%3d%%", pct);
}

void LcdDisplay::showStatus(bool meterOk, const MeterData& data, bool publisherOk) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(5, 5);
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.println("Wi-SUN Smart Meter");
    drawBattery();

    M5.Lcd.setCursor(5, 25);
    if (meterOk) {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("Connected");
    } else {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("Connecting...");
    }

    M5.Lcd.setCursor(5, 50);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.printf("%d W", data.power);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(5, 80);
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.printf("Buy:  %.1f kWh", data.buyEnergy);
    M5.Lcd.setCursor(5, 100);
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.printf("Sell: %.1f kWh", data.sellEnergy);

    M5.Lcd.setCursor(5, 125);
    M5.Lcd.setTextSize(1);
    if (publisherOk) {
        M5.Lcd.setTextColor(GREEN);
        M5.Lcd.println("MQTT: OK");
    } else {
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("MQTT: Disconnected");
    }
}

void LcdDisplay::showDebug(bool meterOk, const String& ipv6, const MeterData& data,
                           bool publisherOk, const String& channel, const String& panId) {
    M5.Lcd.fillScreen(BLACK);
    _logY = 0;

    log("=== DEBUG ===", INFO);
    log(meterOk ? "wisunOK: YES" : "wisunOK: NO", meterOk ? SUCCESS : ERROR);
    log(("IPv6: " + ipv6.substring(0, 25)).c_str());
    log(("Power: " + String(data.power) + " W").c_str());
    log(("Buy: " + String(data.buyEnergy, 1) + " kWh").c_str());
    log(("Sell: " + String(data.sellEnergy, 1) + " kWh").c_str());
    log(publisherOk ? "MQTT: OK" : "MQTT: NO");
    log(("Ch:" + channel + " PAN:" + panId).c_str());
}

bool LcdDisplay::btnAPressed() {
    return M5.BtnA.wasPressed();
}

bool LcdDisplay::btnBPressed() {
    return M5.BtnB.wasPressed();
}
