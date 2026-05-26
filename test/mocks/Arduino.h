#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <time.h>

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#define INPUT 0
#define OUTPUT 1

inline void pinMode(int pin, int mode) {}
inline void digitalWrite(int pin, int val) {}
inline int digitalRead(int pin) { return LOW; }
inline void yield() {}

class String {
private:
    std::string val;
public:
    String() : val("") {}
    String(const char* s) : val(s ? s : "") {}
    String(const std::string& s) : val(s) {}
    String(char c) { val = std::string(1, c); }
    String(int num) { val = std::to_string(num); }
    String(unsigned int num) { val = std::to_string(num); }
    String(long num) { val = std::to_string(num); }
    String(unsigned long num) { val = std::to_string(num); }
    String(float num) { val = std::to_string(num); }
    String(double num) { val = std::to_string(num); }

    size_t length() const { return val.length(); }
    const char* c_str() const { return val.c_str(); }

    void reserve(size_t size) {}

    bool endsWith(const String& suffix) const {
        if (suffix.length() > length()) return false;
        return val.compare(length() - suffix.length(), suffix.length(), suffix.val) == 0;
    }

    bool startsWith(const String& prefix) const {
        if (prefix.length() > length()) return false;
        return val.compare(0, prefix.length(), prefix.val) == 0;
    }

    int indexOf(char ch) const {
        size_t idx = val.find(ch);
        return (idx == std::string::npos) ? -1 : (int)idx;
    }

    int indexOf(const String& str) const {
        size_t idx = val.find(str.val);
        return (idx == std::string::npos) ? -1 : (int)idx;
    }

    String substring(int from) const {
        if (from >= (int)val.length()) return String("");
        return String(val.substr(from));
    }

    String substring(int from, int to) const {
        if (from >= (int)val.length() || from >= to) return String("");
        return String(val.substr(from, to - from));
    }

    void trim() {
        val.erase(val.begin(), std::find_if(val.begin(), val.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        val.erase(std::find_if(val.rbegin(), val.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), val.end());
    }

    int toInt() const {
        try {
            return std::stoi(val);
        } catch (...) {
            return 0;
        }
    }

    float toFloat() const {
        try {
            return std::stof(val);
        } catch (...) {
            return 0.0f;
        }
    }

    bool equalsIgnoreCase(const String& other) const {
        if (val.length() != other.val.length()) return false;
        for (size_t i = 0; i < val.length(); i++) {
            if (std::tolower(val[i]) != std::tolower(other.val[i])) return false;
        }
        return true;
    }

    String& operator+=(const String& other) {
        val += other.val;
        return *this;
    }

    String& operator+=(char c) {
        val += c;
        return *this;
    }

    friend String operator+(String lhs, const String& rhs) {
        lhs.val += rhs.val;
        return lhs;
    }

    friend String operator+(String lhs, char rhs) {
        lhs.val += rhs;
        return lhs;
    }

    bool operator==(const String& other) const { return val == other.val; }
    bool operator!=(const String& other) const { return val != other.val; }
    bool operator<(const String& other) const { return val < other.val; }

    char operator[](size_t index) const { return val[index]; }
    char& operator[](size_t index) { return val[index]; }
};

namespace std {
    inline string to_string(const String& s) {
        return string(s.c_str());
    }
}

// Global virtual time
extern unsigned long g_mock_millis;
inline unsigned long millis() { return g_mock_millis; }
inline void delay(unsigned long ms) { g_mock_millis += ms; }

class MockSerial {
public:
    void print(const String& s) { std::cout << s.c_str(); }
    void print(int n) { std::cout << n; }
    void print(const char* s) { std::cout << s; }
    
    void println(const String& s) { std::cout << s.c_str() << std::endl; }
    void println(int n) { std::cout << n << std::endl; }
    void println(const char* s) { std::cout << s << std::endl; }

    void printf(const char* format, ...) {
        char loc_buf[512];
        va_list arg;
        va_start(arg, format);
        vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
        va_end(arg);
        std::cout << loc_buf;
    }
};

extern MockSerial Serial;

inline unsigned long long strtoull(const char* str, char** endptr, int base) {
    return std::strtoull(str, endptr, base);
}

inline bool getLocalTime(struct tm * info, uint32_t ms = 5000) {
    time_t now = time(nullptr);
    struct tm* local = localtime(&now);
    if (local && info) {
        *info = *local;
        return true;
    }
    return false;
}

inline size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t i;
    for (i = 0; i < siz - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    if (siz > 0) {
        dst[i] = '\0';
    }
    while (src[i] != '\0') {
        i++;
    }
    return i;
}

#endif // MOCK_ARDUINO_H
