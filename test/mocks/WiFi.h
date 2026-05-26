#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#include "Arduino.h"

// Basic stub
class MockWiFi {
public:
    String macAddress() { return "00:11:22:33:44:55"; }
};

extern MockWiFi WiFi;

#endif // MOCK_WIFI_H
