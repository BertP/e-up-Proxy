#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

struct TelemetryData;

// Initialize the LittleFS logging system
void initLogger();

// Log a system event string
void logEvent(const String& message);
void logEvent(const String& level, const String& message);

// Log car telematics specifically
void logTelemetry(const TelemetryData& data);
void logTelemetrySlow(const TelemetryData& data);

// Stream the contents of debug.bak.log and debug.log to the WebServer client
void streamLog(WebServer& server);

// Log an MQTT-specific event string
void logMqttEvent(const String& message);
void logMqttEvent(const String& level, const String& message);

// Stream the contents of mqtt.bak.log and mqtt.log to the WebServer client
void streamMqttLog(WebServer& server);

// Combine and write the 4 initial boot log lines in a single file open operation
void logBootSequence(const String& mac, size_t freeKB, size_t queueSize);

// Clear all logs manually
void clearLog();

// Global NTP synchronization status to control time formatting dynamic prefix
extern bool g_ntpSynchronized;

#endif // LOGGER_H

