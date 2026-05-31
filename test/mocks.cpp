#include "Arduino.h"
#include "LittleFS.h"
#include "WiFi.h"
#include "WiFiClient.h"

unsigned long g_mock_millis = 0;
MockSerial Serial;
LittleFSImpl LittleFS;
MockWiFi WiFi;

std::vector<uint8_t> WiFiClient::mock_rx_buffer;
std::vector<uint8_t> WiFiClient::mock_tx_buffer;
bool WiFiClient::mock_connected = false;
std::string WiFiClient::current_cmd = "";
bool WiFiClient::mock_nrc_mode = false;
float WiFiClient::mock_temp = 20.0f;

void feedWDT() {
    // Mock WDT feed - do nothing on host native tests
}
