#include <unity.h>
#include "Arduino.h"
#include "buffer.h"
#include "OBDManager.h"
#include "WiFiClient.h"
#include "LittleFS.h"

// Copy stripWhitespace to test it directly
String test_stripWhitespace(const String& str) {
    String clean;
    clean.reserve(str.length());
    for (unsigned int i = 0; i < str.length(); i++) {
        char c = str[i];
        if (c != ' ' && c != '\r' && c != '\n') {
            clean += c;
        }
    }
    return clean;
}

void setUp(void) {
    WiFiClient::clear();
    g_mock_millis = 1000;
}

void tearDown(void) {
    // Clean up
}

void test_whitespace_stripping(void) {
    String raw = "62 02 8C A6 \r\n >";
    String clean = test_stripWhitespace(raw);
    TEST_ASSERT_EQUAL_STRING("62028CA6>", clean.c_str());

    String raw2 = "  AT  SH  7E5 \r";
    String clean2 = test_stripWhitespace(raw2);
    TEST_ASSERT_EQUAL_STRING("ATSH7E5", clean2.c_str());
}

void test_derive_range_math(void) {
    TelemetryData data;
    
    // generateSimulatedTelemetry sets:
    // soc = 82.5, bat_cap = 61.5, temp = 18.0 (Warm -> coefficient 5.5)
    // 61.5 * (82.5 / 100) * 5.5 = 279.05625
    generateSimulatedTelemetry(data, false); 
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 279.0562f, data.range);
}

void test_uds_warm_range_derivation(void) {
    WiFiClient::mock_connected = true;
    WiFiClient::mock_temp = 20.0f; // Warm temperature (> 15C) -> 5.5

    TelemetryData data;
    bool success = queryGroupA(data);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 66.4f, data.soc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, data.temp);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 61.2f, data.bat_cap);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.4f, data.volt);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, data.tp_alarm);
    
    // Warm Range: 61.2 * (66.4 / 100) * 5.5 = 223.5072
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 223.5f, data.range);
}

void test_uds_cold_range_derivation(void) {
    WiFiClient::mock_connected = true;
    WiFiClient::mock_temp = 5.0f; // Cold temperature (0C < temp < 15C) -> 4.5

    TelemetryData data;
    bool success = queryGroupA(data);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 66.4f, data.soc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, data.temp);
    
    // Cold Range: 61.2 * (66.4 / 100) * 4.5 = 182.8696
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 182.9f, data.range);
}

void test_uds_freezing_range_derivation(void) {
    WiFiClient::mock_connected = true;
    WiFiClient::mock_temp = -5.0f; // Freezing temperature (<= 0C) -> 3.5

    TelemetryData data;
    bool success = queryGroupA(data);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 66.4f, data.soc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, data.temp);
    
    // Freezing Range: 61.2 * (66.4 / 100) * 3.5 = 142.232
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 142.2f, data.range);
}

void test_uds_3_bytes_parsing(void) {
    WiFiClient::mock_connected = true;

    TelemetryData data;
    bool success = queryGroupB(data);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 42107.0f, data.odo);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 180.0f, data.service_days);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 7500.0f, data.service_km);
}

void test_nrc_error_handling(void) {
    WiFiClient::mock_connected = true;
    WiFiClient::mock_nrc_mode = true; // Force negative responses

    TelemetryData data;
    bool success = queryGroupA(data);

    TEST_ASSERT_FALSE(success);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_whitespace_stripping);
    RUN_TEST(test_derive_range_math);
    RUN_TEST(test_uds_warm_range_derivation);
    RUN_TEST(test_uds_cold_range_derivation);
    RUN_TEST(test_uds_freezing_range_derivation);
    RUN_TEST(test_uds_3_bytes_parsing);
    RUN_TEST(test_nrc_error_handling);
    return UNITY_END();
}
