#ifndef MOCK_WIFICLIENT_H
#define MOCK_WIFICLIENT_H

#include "Arduino.h"
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

class WiFiClient {
private:
    static std::string current_cmd;
public:
    static std::vector<uint8_t> mock_rx_buffer;
    static std::vector<uint8_t> mock_tx_buffer;
    static bool mock_connected;
    static bool mock_nrc_mode; // Force NRC response for error tests
    static float mock_temp;    // Custom temperature to test range math

    WiFiClient() {}

    bool connect(const char* host, uint16_t port) {
        mock_connected = true;
        return true;
    }

    bool connect(const String& host, uint16_t port) {
        mock_connected = true;
        return true;
    }

    bool connected() {
        return mock_connected;
    }

    size_t write(const uint8_t* buf, size_t size) {
        for (size_t i = 0; i < size; i++) {
            char c = (char)buf[i];
            mock_tx_buffer.push_back(buf[i]);
            if (c == '\r') {
                processCommand();
            } else {
                current_cmd += c;
            }
        }
        return size;
    }

    size_t write(const char* str) {
        return write((const uint8_t*)str, strlen(str));
    }

    void print(const String& s) {
        write(s.c_str());
    }

    void print(const char* s) {
        write(s);
    }

    int available() {
        if (mock_rx_buffer.empty()) {
            g_mock_millis += 10; // Auto-tick clock to prevent infinite timeout loops!
        }
        return mock_rx_buffer.size();
    }

    int read() {
        if (mock_rx_buffer.empty()) return -1;
        int val = mock_rx_buffer.front();
        mock_rx_buffer.erase(mock_rx_buffer.begin());
        return val;
    }

    void stop() {
        mock_connected = false;
    }

    void setTimeout(int seconds) {}

    static void clear() {
        mock_rx_buffer.clear();
        mock_tx_buffer.clear();
        current_cmd.clear();
        mock_connected = false;
        mock_nrc_mode = false;
        mock_temp = 20.0f; // Default warm
    }

private:
    void processCommand() {
        // Trim whitespace and leading/trailing stuff
        std::string cmd = current_cmd;
        cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), cmd.end());

        current_cmd.clear();

        if (cmd.empty()) return;

        std::string response = "";

        if (mock_nrc_mode) {
            // In NRC error simulation mode, respond with 7F NRC for UDS queries
            if (cmd.rfind("22 ", 0) == 0) {
                response = "7F " + cmd.substr(3) + " 11\r>";
            } else if (cmd.rfind("10 ", 0) == 0) {
                response = "7F 10 11\r>";
            } else {
                response = "OK\r>";
            }
        } 
        else {
            // General simulator logic
            if (cmd.rfind("AT", 0) == 0) {
                if (cmd == "AT RV") {
                    response = "12.4V\r>";
                } else {
                    response = "OK\r>";
                }
            } 
            else if (cmd == "10 03") {
                response = "50 03\r>";
            } 
            else if (cmd == "3E 80") {
                // Tester present, suppress response
                response = ""; 
            } 
            else if (cmd == "22 02 8C") {
                // SoC (7E5): 66.4% -> raw 166 (0xA6) -> 62 02 8C A6
                response = "62 02 8C A6\r>";
            } 
            else if (cmd == "22 11 62") {
                // Battery Temp (7E5): raw = temp + 40
                int raw_temp = (int)mock_temp + 40;
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X", raw_temp & 0xFF);
                response = "62 11 62 " + std::string(hex) + "\r>";
            } 
            else if (cmd == "22 22 E1") {
                // Battery Capacity (7E5): 61.2 Ah -> raw 612 (0x0264)
                response = "62 22 E1 02 64\r>";
            } 
            else if (cmd == "22 02 1A") {
                // Tire Pressure Alarm (7E0): 0
                response = "62 02 1A 00\r>";
            } 
            else if (cmd == "22 22 03") {
                // Odometer (7E0): 42107 km -> raw 42107 (0x00A47B)
                response = "62 22 03 00 A4 7B\r>";
            } 
            else if (cmd == "22 02 48") {
                // Days to service (7E0): 180 d -> raw 180 (0x00B4)
                response = "62 02 48 00 B4\r>";
            } 
            else if (cmd == "22 02 47") {
                // Km to service (7E0): 7500 km -> raw 7500 (0x1D4C)
                response = "62 02 47 1D 4C\r>";
            } 
            else {
                response = "7F " + cmd + " 11\r>";
            }
        }

        // Push response to rx buffer
        for (char c : response) {
            mock_rx_buffer.push_back((uint8_t)c);
        }
    }
};

#endif // MOCK_WIFICLIENT_H
