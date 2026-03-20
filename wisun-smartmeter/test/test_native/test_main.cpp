#include <unity.h>
#include "domain/MeterData.h"
#include "domain/Interfaces.h"
#include "domain/EchonetLiteParser.h"
#include "domain/SessionMonitor.h"
#include "domain/BatteryUtil.h"
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
// Test helpers
// =============================================================

// ECHONET Lite frame header constants
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
static const char* FRAME_PREFIX      = "10810001028801" "05FF01" "72" "01";
static const char* FRAME_BAD_SEOJ    = "10810001029901" "05FF01" "72" "01";
static const char* FRAME_BAD_ESV     = "10810001028801" "05FF01" "52" "01";

static bool parseHex(const char* hex, MeterData& data) {
    return EchonetLiteParser::parseFrame(hex, strlen(hex), data);
}

// Simulates one poll cycle and records result to monitor.
// Returns true if any request succeeded (matching poll() logic).
static bool simulatePollCycle(SessionMonitor& monitor,
                              const bool* requestResults, int count) {
    bool anySuccess = false;
    for (int i = 0; i < count; i++) {
        if (requestResults[i]) anySuccess = true;
    }
    if (anySuccess) {
        monitor.recordSuccess();
    } else {
        monitor.recordFailure();
    }
    return anySuccess;
}

// =============================================================
// Domain: MeterData tests
// =============================================================

void test_meter_data_default_values() {
    // Arrange
    MeterData data;

    // Assert
    TEST_ASSERT_EQUAL_INT32(0, data.power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, data.buyEnergy);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, data.sellEnergy);
    TEST_ASSERT_FALSE(data.powerValid);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
    TEST_ASSERT_FALSE(data.sellEnergyValid);
}

void test_meter_data_assignment() {
    // Arrange
    MeterData data;

    // Act
    data.power = -500;
    data.buyEnergy = 1200.8;
    data.sellEnergy = 456.3;

    // Assert
    TEST_ASSERT_EQUAL_INT32(-500, data.power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1200.8, data.buyEnergy);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 456.3, data.sellEnergy);
}

// =============================================================
// Domain: EchonetLiteParser — parseFrame tests
// =============================================================

void test_parse_power_response() {
    // Arrange: E7 response with power = 42W (0x0000002A)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "0000002A";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(42, data.power);
    TEST_ASSERT_TRUE(data.powerValid);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
    TEST_ASSERT_FALSE(data.sellEnergyValid);
}

void test_parse_negative_power() {
    // Arrange: power = -300W (0xFFFFFED4)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "FFFFFED4";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(-300, data.power);
    TEST_ASSERT_TRUE(data.powerValid);
}

void test_parse_buy_energy() {
    // Arrange: E0 response, 12008 * 0.1 = 1200.8 kWh (0x00002EE8)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E0" "04" "00002EE8";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1200.8, data.buyEnergy);
    TEST_ASSERT_TRUE(data.buyEnergyValid);
    TEST_ASSERT_FALSE(data.sellEnergyValid);
    TEST_ASSERT_FALSE(data.powerValid);
}

void test_parse_sell_energy() {
    // Arrange: E3 response, 4563 * 0.1 = 456.3 kWh (0x000011D3)
    const char* hex = "10810001028801" "05FF01" "72" "01" "E3" "04" "000011D3";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 456.3, data.sellEnergy);
    TEST_ASSERT_TRUE(data.sellEnergyValid);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
}

void test_parse_power_out_of_range() {
    // Arrange: power = 50000W (0x0000C350), exceeds +/-30000W filter
    const char* hex = "10810001028801" "05FF01" "72" "01" "E7" "04" "0000C350";
    MeterData data;
    data.power = 999;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT32(999, data.power);
    TEST_ASSERT_FALSE(data.powerValid);
}

void test_parse_failed_frame_keeps_valid_flags_false() {
    // Arrange: invalid SEOJ (not smart meter 028801)
    const char* hex = "10810001029901" "05FF01" "72" "01" "E7" "04" "0000002A";
    MeterData data;

    // Act
    parseHex(hex, data);

    // Assert
    TEST_ASSERT_FALSE(data.powerValid);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
    TEST_ASSERT_FALSE(data.sellEnergyValid);
}

void test_parse_invalid_seoj() {
    // Arrange
    const char* hex = "10810001029901" "05FF01" "72" "01" "E7" "04" "0000002A";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_FALSE(ok);
}

void test_parse_invalid_esv() {
    // Arrange: ESV = 52 (not 72 = Get_Res)
    const char* hex = "10810001028801" "05FF01" "52" "01" "E7" "04" "0000002A";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_FALSE(ok);
}

void test_parse_too_short() {
    // Arrange
    const char* hex = "108100010288";
    MeterData data;

    // Act
    bool ok = parseHex(hex, data);

    // Assert
    TEST_ASSERT_FALSE(ok);
}

// =============================================================
// Domain: EchonetLiteParser — buildFrame tests
// =============================================================

void test_build_frame_power() {
    // Arrange
    char buf[64];

    // Act
    EchonetLiteParser::buildFrame(0xE7, buf, sizeof(buf));

    // Assert
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E700", buf);
}

void test_build_frame_buy() {
    // Arrange
    char buf[64];

    // Act
    EchonetLiteParser::buildFrame(0xE0, buf, sizeof(buf));

    // Assert
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E000", buf);
}

void test_build_frame_sell() {
    // Arrange
    char buf[64];

    // Act
    EchonetLiteParser::buildFrame(0xE3, buf, sizeof(buf));

    // Assert
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E300", buf);
}

void test_build_multi_frame_all_three() {
    // Arrange
    char buf[128];
    const uint8_t epcs[] = { 0xE7, 0xE0, 0xE3 };

    // Act
    EchonetLiteParser::buildMultiFrame(epcs, 3, buf, sizeof(buf));

    // Assert — OPC=03, then E7/00, E0/00, E3/00
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016203E700E000E300", buf);
}

void test_build_multi_frame_single() {
    // Arrange
    char buf[128];
    const uint8_t epcs[] = { 0xE7 };

    // Act
    EchonetLiteParser::buildMultiFrame(epcs, 1, buf, sizeof(buf));

    // Assert — OPC=01, same as buildFrame
    TEST_ASSERT_EQUAL_STRING("1081000105FF010288016201E700", buf);
}

void test_parse_multi_property_response() {
    // Arrange — response with OPC=03: E7(52W) + E0(12017=1201.7kWh) + E3(22966=2296.6kWh)
    MeterData data;
    const char* hex = "1081000102880105FF017203E70400000034E00400002EF1E304000059B6";
    int len = strlen(hex);

    // Act
    bool ok = EchonetLiteParser::parseFrame(hex, len, data);

    // Assert
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(data.powerValid);
    TEST_ASSERT_EQUAL_INT32(52, data.power);
    TEST_ASSERT_TRUE(data.buyEnergyValid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1201.7f, data.buyEnergy);
    TEST_ASSERT_TRUE(data.sellEnergyValid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2296.6f, data.sellEnergy);
}

// =============================================================
// Domain: EchonetLiteParser — regression / scenario tests
// =============================================================

void test_startup_e0_timeout_e3_ok_does_not_publish_buy() {
    // BUG REGRESSION: On startup, E7 succeeds but E0 times out.
    // Publisher must skip buyEnergy (still 0.0) because only
    // sellEnergyValid is set.
    MeterData data;

    // Arrange & Act: E7 response arrives
    const char* powerHex = "10810001028801" "05FF01" "72" "01" "E7" "04" "0000002A";
    parseHex(powerHex, data);

    // Assert: power updated, energy flags both false
    TEST_ASSERT_TRUE(data.powerValid);
    TEST_ASSERT_EQUAL_INT32(42, data.power);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
    TEST_ASSERT_FALSE(data.sellEnergyValid);

    // Act: E0 times out (no parse), then E3 arrives
    const char* sellHex = "10810001028801" "05FF01" "72" "01" "E3" "04" "000011D3";
    parseHex(sellHex, data);

    // Assert: only sell is valid, buy remains invalid
    TEST_ASSERT_TRUE(data.sellEnergyValid);
    TEST_ASSERT_FALSE(data.buyEnergyValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, data.buyEnergy);
}

// =============================================================
// Application: MeterService tests
// =============================================================

void test_service_always_calls_publisher_loop() {
    // Arrange
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    // Act
    service.update(false);
    service.update(true);

    // Assert
    TEST_ASSERT_EQUAL_INT(2, publisher.loopCount);
}

void test_service_skips_poll_when_disconnected() {
    // Arrange
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);

    // Act
    bool result = service.update(false);

    // Assert
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(0, reader.pollCount);
    TEST_ASSERT_EQUAL_INT(0, publisher.publishCount);
    TEST_ASSERT_EQUAL_INT(0, display.showStatusCount);
}

void test_service_no_update_when_poll_returns_false() {
    // Arrange
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);
    reader.pollReturn = false;

    // Act
    bool result = service.update(true);

    // Assert
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(1, reader.pollCount);
    TEST_ASSERT_EQUAL_INT(0, publisher.publishCount);
}

void test_service_publishes_and_displays_on_new_data() {
    // Arrange
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);
    reader.pollReturn = true;
    reader.data.power = 150;
    reader.data.buyEnergy = 100.5;
    reader.data.sellEnergy = 50.2;
    publisher.connected = true;

    // Act
    bool result = service.update(true);

    // Assert
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
    // Arrange
    MockMeterReader reader;
    MockPublisher publisher;
    MockDisplay display;
    MeterService service(reader, publisher, display);
    reader.pollReturn = true;
    publisher.connected = false;

    // Act
    service.update(true);

    // Assert
    TEST_ASSERT_FALSE(display.lastPublisherOk);
}

// =============================================================
// Domain: SessionMonitor tests
// =============================================================

void test_session_monitor_initial_state() {
    // Arrange
    SessionMonitor monitor(3);

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
    TEST_ASSERT_EQUAL_INT(0, monitor.failureCount());
}

void test_session_monitor_not_lost_below_threshold() {
    // Arrange
    SessionMonitor monitor(3);

    // Act
    monitor.recordFailure();
    monitor.recordFailure();

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
    TEST_ASSERT_EQUAL_INT(2, monitor.failureCount());
}

void test_session_monitor_lost_at_threshold() {
    // Arrange
    SessionMonitor monitor(3);

    // Act
    monitor.recordFailure();
    monitor.recordFailure();
    monitor.recordFailure();

    // Assert
    TEST_ASSERT_TRUE(monitor.isSessionLost());
}

void test_session_monitor_success_resets_count() {
    // Arrange
    SessionMonitor monitor(3);
    monitor.recordFailure();
    monitor.recordFailure();

    // Act
    monitor.recordSuccess();

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
    TEST_ASSERT_EQUAL_INT(0, monitor.failureCount());
}

void test_session_monitor_success_after_threshold_resets() {
    // Arrange
    SessionMonitor monitor(3);
    monitor.recordFailure();
    monitor.recordFailure();
    monitor.recordFailure();
    TEST_ASSERT_TRUE(monitor.isSessionLost());

    // Act
    monitor.recordSuccess();

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

void test_session_monitor_reset() {
    // Arrange
    SessionMonitor monitor(3);
    monitor.recordFailure();
    monitor.recordFailure();
    monitor.recordFailure();
    TEST_ASSERT_TRUE(monitor.isSessionLost());

    // Act
    monitor.reset();

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
    TEST_ASSERT_EQUAL_INT(0, monitor.failureCount());
}

void test_session_monitor_intermittent_failures_no_loss() {
    // Arrange & Act: fail, fail, success, fail, fail — never 3 consecutive
    SessionMonitor monitor(3);
    monitor.recordFailure();
    monitor.recordFailure();
    monitor.recordSuccess();
    monitor.recordFailure();
    monitor.recordFailure();

    // Assert
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

// =============================================================
// Scenario: Poll-level session monitoring
// Verifies that session loss is counted per poll cycle, not per
// individual request. A single poll with 3 failed requests
// (E7+E0+E3) must count as ONE failure, not three.
// =============================================================

void test_poll_all_requests_fail_counts_as_one_failure() {
    // BUG REGRESSION: previously each requestSync timeout incremented
    // the counter, so a single 60s poll (E7+E0+E3 all fail) hit
    // threshold=3 and triggered immediate reconnection.

    // Arrange
    SessionMonitor monitor(3);
    bool results[] = {false, false, false};

    // Act
    simulatePollCycle(monitor, results, 3);

    // Assert
    TEST_ASSERT_EQUAL_INT(1, monitor.failureCount());
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

void test_poll_partial_success_resets_count() {
    // Arrange: 2 previous poll failures, then E7 succeeds but E0+E3 timeout
    SessionMonitor monitor(3);
    monitor.recordFailure();
    monitor.recordFailure();
    bool results[] = {true, false, false};

    // Act
    simulatePollCycle(monitor, results, 3);

    // Assert
    TEST_ASSERT_EQUAL_INT(0, monitor.failureCount());
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

void test_poll_three_full_failures_triggers_session_lost() {
    // Arrange
    SessionMonitor monitor(3);
    bool allFail[] = {false, false, false};

    // Act
    simulatePollCycle(monitor, allFail, 3);
    simulatePollCycle(monitor, allFail, 3);
    simulatePollCycle(monitor, allFail, 3);

    // Assert
    TEST_ASSERT_EQUAL_INT(3, monitor.failureCount());
    TEST_ASSERT_TRUE(monitor.isSessionLost());
}

void test_poll_power_only_cycle_counts_as_one() {
    // Arrange: 10s cycle, only E7 requested and fails
    SessionMonitor monitor(3);
    bool results[] = {false};

    // Act
    simulatePollCycle(monitor, results, 1);

    // Assert
    TEST_ASSERT_EQUAL_INT(1, monitor.failureCount());
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

void test_poll_recovery_mid_sequence_prevents_session_lost() {
    // Arrange: 2 full-failure polls
    SessionMonitor monitor(3);
    bool allFail[] = {false, false, false};
    bool partialOk[] = {true, false, false};
    simulatePollCycle(monitor, allFail, 3);
    simulatePollCycle(monitor, allFail, 3);

    // Act: 3rd poll has partial success
    simulatePollCycle(monitor, partialOk, 3);

    // Assert
    TEST_ASSERT_EQUAL_INT(0, monitor.failureCount());
    TEST_ASSERT_FALSE(monitor.isSessionLost());
}

// =============================================================
// Domain: BatteryUtil tests
// =============================================================

void test_battery_full_charge() {
    // Arrange & Act & Assert
    TEST_ASSERT_EQUAL_INT(100, BatteryUtil::voltageToPercent(4.2f));
}

void test_battery_empty() {
    // Arrange & Act & Assert
    TEST_ASSERT_EQUAL_INT(0, BatteryUtil::voltageToPercent(3.0f));
}

void test_battery_half() {
    // Arrange & Act & Assert
    TEST_ASSERT_EQUAL_INT(50, BatteryUtil::voltageToPercent(3.6f));
}

void test_battery_clamps_above_max() {
    // Arrange: voltage above 4.2V (USB charging)
    // Act & Assert
    TEST_ASSERT_EQUAL_INT(100, BatteryUtil::voltageToPercent(4.5f));
}

void test_battery_clamps_below_min() {
    // Arrange: voltage below 3.0V (deep discharge)
    // Act & Assert
    TEST_ASSERT_EQUAL_INT(0, BatteryUtil::voltageToPercent(2.5f));
}

void test_battery_typical_values() {
    // Arrange & Act & Assert
    TEST_ASSERT_EQUAL_INT(83, BatteryUtil::voltageToPercent(4.0f));
    TEST_ASSERT_EQUAL_INT(16, BatteryUtil::voltageToPercent(3.2f));
}

// =============================================================
// Test Runner
// =============================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Domain: MeterData
    RUN_TEST(test_meter_data_default_values);
    RUN_TEST(test_meter_data_assignment);

    // Domain: EchonetLiteParser — parseFrame
    RUN_TEST(test_parse_power_response);
    RUN_TEST(test_parse_negative_power);
    RUN_TEST(test_parse_buy_energy);
    RUN_TEST(test_parse_sell_energy);
    RUN_TEST(test_parse_power_out_of_range);
    RUN_TEST(test_parse_failed_frame_keeps_valid_flags_false);
    RUN_TEST(test_parse_invalid_seoj);
    RUN_TEST(test_parse_invalid_esv);
    RUN_TEST(test_parse_too_short);

    // Domain: EchonetLiteParser — buildFrame
    RUN_TEST(test_build_frame_power);
    RUN_TEST(test_build_frame_buy);
    RUN_TEST(test_build_frame_sell);

    // Domain: EchonetLiteParser — buildMultiFrame
    RUN_TEST(test_build_multi_frame_all_three);
    RUN_TEST(test_build_multi_frame_single);
    RUN_TEST(test_parse_multi_property_response);

    // Domain: EchonetLiteParser — regression
    RUN_TEST(test_startup_e0_timeout_e3_ok_does_not_publish_buy);

    // Application: MeterService
    RUN_TEST(test_service_always_calls_publisher_loop);
    RUN_TEST(test_service_skips_poll_when_disconnected);
    RUN_TEST(test_service_no_update_when_poll_returns_false);
    RUN_TEST(test_service_publishes_and_displays_on_new_data);
    RUN_TEST(test_service_reports_publisher_disconnected);

    // Domain: SessionMonitor
    RUN_TEST(test_session_monitor_initial_state);
    RUN_TEST(test_session_monitor_not_lost_below_threshold);
    RUN_TEST(test_session_monitor_lost_at_threshold);
    RUN_TEST(test_session_monitor_success_resets_count);
    RUN_TEST(test_session_monitor_success_after_threshold_resets);
    RUN_TEST(test_session_monitor_reset);
    RUN_TEST(test_session_monitor_intermittent_failures_no_loss);

    // Scenario: Poll-level session monitoring
    RUN_TEST(test_poll_all_requests_fail_counts_as_one_failure);
    RUN_TEST(test_poll_partial_success_resets_count);
    RUN_TEST(test_poll_three_full_failures_triggers_session_lost);
    RUN_TEST(test_poll_power_only_cycle_counts_as_one);
    RUN_TEST(test_poll_recovery_mid_sequence_prevents_session_lost);

    // Domain: BatteryUtil
    RUN_TEST(test_battery_full_charge);
    RUN_TEST(test_battery_empty);
    RUN_TEST(test_battery_half);
    RUN_TEST(test_battery_clamps_above_max);
    RUN_TEST(test_battery_clamps_below_min);
    RUN_TEST(test_battery_typical_values);

    return UNITY_END();
}
