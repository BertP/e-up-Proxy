#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#include "Arduino.h"

class IPAddress {
private:
    std::string _ip;
public:
    IPAddress() : _ip("0.0.0.0") {}
    IPAddress(const char* ipStr) : _ip(ipStr ? ipStr : "0.0.0.0") {}
    IPAddress(const std::string& ipStr) : _ip(ipStr) {}
    
    String toString() const { return String(_ip); }
};

// Basic stub
class MockWiFi {
public:
    String macAddress() { return "00:11:22:33:44:55"; }
    IPAddress gatewayIP() { return IPAddress("192.168.0.10"); }
};

extern MockWiFi WiFi;

#endif // MOCK_WIFI_H
