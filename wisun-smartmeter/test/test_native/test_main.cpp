#include <unity.h>
#include "domain/MeterData.h"
#include "domain/Interfaces.h"
#include "domain/EchonetLiteParser.h"
#include "application/MeterService.h"

// =============================================================
// Mock implementations
// =============================================================

class MockMeterReader : public IMeterReader {
public:
    MeterData data;
    bool pollReturn = false;
    int pollCount = 0;

    bool poll() override {
        pollCount++;
        return pollReturn;
    }
    const MeterData& getData() const override { return data; }
};

class MockPublisher : public IPublisher {
public:
    int loopCount = 0;
    int publishCount = 0;
    bool connected = true;
    MeterData lastPublished;

    void loop() override { loopCount++; }
    bool isConnected() override { return connected; }
    void publish(const MeterData& d) override {
        publishCount++;
        lastPublished = d;
    }
};

class MockDisplay : public IDisplay {
public:
    int showStatusCount = 0;
    bool lastMeterOk = false;
    bool lastPublisherOk = false;
    MeterData lastData;

    void log(const char* msg, Level level = INFO) override { (void)msg; (void)level; }
    void showStatus(bool meterOk, const MeterData& d, bool publisherOk) override {
        showStatusCount++;
        lastMeterOk = meterOk;
        lastPublisherOk = publisherOk;
        lastData = d;
    }
};

// =============================================================
// Domain: MeterData tests
// =============================================================

void test_meter_data_default_values() {
    MeterData data;
    TEST_ASSERT_EQUAL_INT32(0, data.power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, data.buyEnergy);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, data.sellEnergy);
}

void test_meter_data_assignment() {
    MeterData data;
    data.power = -500;
    data.buyEnergy = 1200.8;
    data.sellEnergy = 456.3;
    TEST_ASSERT_EQUAL_INT32(-500, data.power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1200.8, data.buyEnergy);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 456.3, data.sellEnergy);
}

// =============================================================
// Domain: EchonetLiteParser tests
// =============================================================

// Response frame layout (hex char positions):
//   0-3:   EHD (1081)
//   4-7:   TID (0001)
//   8-13:  SEOJ (028801 = smart meter)
//   14-19: DEOJ (05FF01 = controller)
//   20-21: ESV (72 = Get_Res)
//   22-23: OPC (01 = 1 property)
//   24-25: EPC
//   26-27: PDC
//   28+:   EDT (property data)

void test_parse_power_response() {
    // E7 response with power = 42W (0x0000002A)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "0000002A";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(42, data.power);
}

void test_parse_negative_power() {
    // Power = -300W (0xFFFFFED4)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "FFFFFED4";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(-300, data.power);
}

void test_parse_buy_energy() {
    // E0 response: 12008 * 0.1 = 1200.8 kWh (0x00002EE8)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E0" "04" "00002EE8";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1200.8, data.buyEnergy);
}

void test_parse_sell_energy() {
    // E3 response: 4563 * 0.1 = 456.3 kWh (0x000011D3)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E3" "04" "000011D3";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 456.3, data.sellEnergy);
}

void test_parse_power_out_of_range() {
    // Power = 50000W (0x0000C350) — exceeds ±30000W filter
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "0000C350";
    MeterData data;
    data.power = 999;  // Pre-set to verify it's NOT overwritten
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT32(999, data.power);  // Unchanged
}

void test_parse_invalid_seoj() {
    // Wrong SEOJ (not smart meter 028801)
    const char* hex = "10810001029901" "05FF01" "72" "01" "E7" "04" "0000002A";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_invalid_esv() {
    // ESV = 52 (not 72 = Get_Res)
    const char* hex = "10810001028801" "05FF01" "52" "01" "E7" "04" "0000002A";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_too_short() {
    const char* hex = "108100010288";
    MeterData data;
    bool ok = EchonetLiteParser::parseFrame(hex, strlen(hex), data);
    TEST_ASSERT_FALSE(ok);
}

void test_build_frame_power() {
    char buf[64];
    EchonetLiteParser::buildFrame(0xE7, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E700", buf);
}

void test_build_frame_buy() {
    char buf[64];
    EchonetLiteParser::buildFrame(0xE0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E000", buf);
}

void test_build_frame_sell() {
    char buf[64];
    EchonetLiteParser::buildFrame(0xE3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E300", buf);
}

// =============================================================
// Application: MeterService tests
// =============================================================

void test_service_always_calls_publisher_loop() {
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    service.update(false);
    TEST_ASSERT_EQUAL_INT(1, publisher.loopCount);

    service.update(true);
    TEST_ASSERT_EQUAL_INT(2, publisher.loopCount);
}

void test_service_skips_poll_when_disconnected() {
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    bool result = service.update(false);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(0, reader.pollCount);
    TEST_ASSERT_EQUAL_INT(0, publisher.publishCount);
    TEST_ASSERT_EQUAL_INT(0, display.showStatusCount);
}

void test_service_no_update_when_poll_returns_false() {
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    reader.pollReturn = false;
    bool result = service.update(true);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(1, reader.pollCount);
    TEST_ASSERT_EQUAL_INT(0, publisher.publishCount);
}

void test_service_publishes_and_displays_on_new_data() {
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    reader.pollReturn = true;
    reader.data.power = 150;
    reader.data.buyEnergy = 100.5;
    reader.data.sellEnergy = 50.2;
    publisher.connected = true;

    bool result = service.update(true);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(1, publisher.publishCount);
    TEST_ASSERT_EQUAL_INT32(150, publisher.lastPublished.power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 100.5, publisher.lastPublished.buyEnergy);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 50.2, publisher.lastPublished.sellEnergy);
    TEST_ASSERT_EQUAL_INT(1, display.showStatusCount);
    TEST_ASSERT_TRUE(display.lastMeterOk);
    TEST_ASSERT_TRUE(display.lastPublisherOk);
}

void test_service_reports_publisher_disconnected() {
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    reader.pollReturn = true;
    publisher.connected = false;

    service.update(true);
    TEST_ASSERT_FALSE(display.lastPublisherOk);
}

// =============================================================
// Test Runner
// =============================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Domain: MeterData
    RUN_TEST(test_meter_data_default_values);
    RUN_TEST(test_meter_data_assignment);

    // Domain: EchonetLiteParser
    RUN_TEST(test_parse_power_response);
    RUN_TEST(test_parse_negative_power);
    RUN_TEST(test_parse_buy_energy);
    RUN_TEST(test_parse_sell_energy);
    RUN_TEST(test_parse_power_out_of_range);
    RUN_TEST(test_parse_invalid_seoj);
    RUN_TEST(test_parse_invalid_esv);
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_build_frame_power);
    RUN_TEST(test_build_frame_buy);
    RUN_TEST(test_build_frame_sell);

    // Application: MeterService
    RUN_TEST(test_service_always_calls_publisher_loop);
    RUN_TEST(test_service_skips_poll_when_disconnected);
    RUN_TEST(test_service_no_update_when_poll_returns_false);
    RUN_TEST(test_service_publishes_and_displays_on_new_data);
    RUN_TEST(test_service_reports_publisher_disconnected);

    return UNITY_END();
}
