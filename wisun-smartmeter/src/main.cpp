// Composition Root
// Wires all layers together: domain <- application <- infrastructure

#include "config.h"
#include "infrastructure/display/LcdDisplay.h"
#include "infrastructure/driver/BP35A1.h"
#include "infrastructure/wisun/WiSUN.h"
#include "infrastructure/meter/EchonetLiteMeter.h"
#include "infrastructure/mqtt/MqttPublisher.h"
#include "application/MeterService.h"

// --- Infrastructure ---
LcdDisplay display;
BP35A1 modem(BP35A1_RX_PIN, BP35A1_TX_PIN, BP35A1_BAUD);
WiSUN wisun(modem);
EchonetLiteMeter meter(modem);
MqttPublisher publisher;

// --- Application ---
MeterService service(meter, publisher, display);

void setup() {
    // Display & serial
    display.begin();
    display.log("Wi-SUN Smart Meter", ILogger::INFO);
    display.log("Initializing...", ILogger::INFO);
    Serial.begin(115200);

    // Inject logger into infrastructure
    modem.setLogger(&display);
    modem.begin();

    // Network
    publisher.begin(&display);

    // Wi-SUN connection
    if (wisun.connect()) {
        display.log("Wi-SUN READY!", ILogger::SUCCESS);
        meter.setPanaAddress(wisun.getPanaAddress());
    } else {
        display.log("Wi-SUN FAILED after retries", ILogger::ERROR);
    }

    delay(3000);
    MeterData empty;
    display.showStatus(wisun.isConnected(), empty, publisher.isConnected());
}

void loop() {
    M5.update();

    service.update(wisun.isConnected());

    // Button A: debug info
    if (display.btnAPressed()) {
        display.showDebug(wisun.isConnected(), wisun.getPanaAddress(),
                          meter.getData(), publisher.isConnected(),
                          wisun.getChannel(), wisun.getPanId());
        delay(5000);
        display.showStatus(wisun.isConnected(), meter.getData(), publisher.isConnected());
    }

    // Button B: clear cache & reboot
    if (display.btnBPressed()) {
        display.log("Clearing scan cache...", ILogger::ERROR);
        wisun.clearCache();
        delay(1000);
        display.log("Rebooting...", ILogger::ERROR);
        delay(1000);
        ESP.restart();
    }

    delay(100);
}
