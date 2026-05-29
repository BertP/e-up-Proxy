#include "logger.h"
#include <LittleFS.h>
#include "config.h"
#include "buffer.h"
#include <time.h>

#define MAX_LOG_SIZE 25600 // 25 KB for each log file, making 50 KB total limit

bool g_ntpSynchronized = false;

static String getLogTimePrefix() {
    if (g_ntpSynchronized) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 10)) {
            char timeBuf[16];
            snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d:%02d]", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            return String(timeBuf);
        }
    }
    return "[T+" + String(millis()) + "]";
}

static void checkRotation() {
    if (!LittleFS.exists("/debug.log")) {
        return;
    }
    
    File f = LittleFS.open("/debug.log", "r");
    if (!f) {
        return;
    }
    size_t size = f.size();
    f.close();
    
    if (size >= MAX_LOG_SIZE) {
        // Rotate files: remove old backup and rename current to backup
        if (LittleFS.exists("/debug.bak.log")) {
            LittleFS.remove("/debug.bak.log");
        }
        LittleFS.rename("/debug.log", "/debug.bak.log");
        
        // Create new debug.log and log rotation event
        File newFile = LittleFS.open("/debug.log", "w");
        if (newFile) {
            String prefix = getLogTimePrefix();
            newFile.printf("%s [INFO] Log rotated due to size limits.\n", prefix.c_str());
            newFile.close();
        }
    }
}

void initLogger() {
    if (!LittleFS.begin(true)) {
        Serial.println("Failed to mount LittleFS in Logger!");
        return;
    }
    
    // Check if the debug.log exists, if not create it
    if (!LittleFS.exists("/debug.log")) {
        File f = LittleFS.open("/debug.log", "w");
        if (f) {
            f.println("[T+0] [BOOT] Logger initialized successfully.");
            f.close();
        }
    }
}

void logBootSequence(const String& mac, size_t freeKB, size_t queueSize) {
    checkRotation();
    
    File f = LittleFS.open("/debug.log", "a");
    if (f) {
        unsigned long ms = millis();
        f.printf("[T+%lu] [BOOT] e-up!Proxy starting. Firmware: %s\n", ms, FW_VERSION);
        f.printf("[T+%lu] [BOOT] [NO-NTP] Chip: ESP32-WROOM-32, MAC: %s\n", ms, mac.c_str());
        f.printf("[T+%lu] [BOOT] [NO-NTP] LittleFS mounted. Free: %u KB / 50 KB cap\n", ms, (unsigned int)freeKB);
        f.printf("[T+%lu] [BOOT] [NO-NTP] Buffered payloads in queue: %u\n", ms, (unsigned int)queueSize);
        f.close();
        
        // Also print to Serial
        Serial.printf("[T+%lu] [BOOT] e-up!Proxy starting. Firmware: %s\n", ms, FW_VERSION);
        Serial.printf("[T+%lu] [BOOT] [NO-NTP] Chip: ESP32-WROOM-32, MAC: %s\n", ms, mac.c_str());
        Serial.printf("[T+%lu] [BOOT] [NO-NTP] LittleFS mounted. Free: %u KB / 50 KB cap\n", ms, (unsigned int)freeKB);
        Serial.printf("[T+%lu] [BOOT] [NO-NTP] Buffered payloads in queue: %u\n", ms, (unsigned int)queueSize);
    }
}

void logEvent(const String& level, const String& message) {
    checkRotation();
    
    File f = LittleFS.open("/debug.log", "a");
    if (f) {
        String prefix = getLogTimePrefix();
        String formattedMsg = message;
        
        if (!g_ntpSynchronized) {
            if (message.indexOf("starting") < 0 && message.indexOf("[NO-NTP]") < 0 && message.indexOf("NTP synchronised") < 0) {
                formattedMsg = "[NO-NTP] " + formattedMsg;
            }
        }
        
        f.printf("%s [%s] %s\n", prefix.c_str(), level.c_str(), formattedMsg.c_str());
        f.close();
        
        // Also print to Serial for development diagnostics
        Serial.printf("%s [%s] %s\n", prefix.c_str(), level.c_str(), formattedMsg.c_str());
    }
}

void logEvent(const String& message) {
    // If the message starts with "[" e.g. "[BOOT] message", extract level
    if (message.startsWith("[")) {
        int endBracket = message.indexOf(']');
        if (endBracket > 0) {
            String level = message.substring(1, endBracket);
            String content = message.substring(endBracket + 1);
            content.trim();
            logEvent(level, content);
            return;
        }
    }
    logEvent("INFO", message);
}

void logTelemetry(const TelemetryData& data) {
    char buf[128];
    snprintf(buf, sizeof(buf), "SoC=%.1f%% Temp=%.0f°C Cap=%.1fAh Volt=%.1fV TpAlarm=%.0f",
             data.soc, data.temp, data.bat_cap, data.volt, data.tp_alarm);
    // Only print to Serial for development diagnostics to avoid flash wear and log rotation
    Serial.printf("%s [DATA] %s\n", getLogTimePrefix().c_str(), buf);
}

void logTelemetrySlow(const TelemetryData& data) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Odo=%.0fkm SvcDays=%.0fd SvcKm=%.0fkm",
             data.odo, data.service_days, data.service_km);
    // Only print to Serial for development diagnostics to avoid flash wear and log rotation
    Serial.printf("%s [DATA:SLOW] %s\n", getLogTimePrefix().c_str(), buf);
}

static void checkMqttRotation() {
    if (!LittleFS.exists("/mqtt.log")) {
        return;
    }
    
    File f = LittleFS.open("/mqtt.log", "r");
    if (!f) {
        return;
    }
    size_t size = f.size();
    f.close();
    
    if (size >= MAX_LOG_SIZE) {
        // Rotate files: remove old backup and rename current to backup
        if (LittleFS.exists("/mqtt.bak.log")) {
            LittleFS.remove("/mqtt.bak.log");
        }
        LittleFS.rename("/mqtt.log", "/mqtt.bak.log");
        
        // Create new mqtt.log and log rotation event
        File newFile = LittleFS.open("/mqtt.log", "w");
        if (newFile) {
            String prefix = getLogTimePrefix();
            newFile.printf("%s [INFO] MQTT log rotated due to size limits.\n", prefix.c_str());
            newFile.close();
        }
    }
}

void logMqttEvent(const String& level, const String& message) {
    checkMqttRotation();
    
    File f = LittleFS.open("/mqtt.log", "a");
    if (f) {
        String prefix = getLogTimePrefix();
        String formattedMsg = message;
        
        if (!g_ntpSynchronized) {
            if (message.indexOf("starting") < 0 && message.indexOf("[NO-NTP]") < 0 && message.indexOf("NTP synchronised") < 0) {
                formattedMsg = "[NO-NTP] " + formattedMsg;
            }
        }
        
        f.printf("%s [%s] %s\n", prefix.c_str(), level.c_str(), formattedMsg.c_str());
        f.close();
        
        // Also print to Serial for development diagnostics
        Serial.printf("%s [%s] %s\n", prefix.c_str(), level.c_str(), formattedMsg.c_str());
    }
}

void logMqttEvent(const String& message) {
    if (message.startsWith("[")) {
        int endBracket = message.indexOf(']');
        if (endBracket > 0) {
            String level = message.substring(1, endBracket);
            String content = message.substring(endBracket + 1);
            content.trim();
            logMqttEvent(level, content);
            return;
        }
    }
    logMqttEvent("MQTT", message);
}

void streamMqttLog(WebServer& server) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/plain", "");
    
    // 1. Stream the backup log first if it exists
    if (LittleFS.exists("/mqtt.bak.log")) {
        File f = LittleFS.open("/mqtt.bak.log", "r");
        if (f) {
            char buffer[256];
            while (f.available()) {
                int bytesRead = f.read((uint8_t*)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    server.sendContent(buffer);
                }
            }
            f.close();
        }
    }
    
    // 2. Stream the current log
    if (LittleFS.exists("/mqtt.log")) {
        File f = LittleFS.open("/mqtt.log", "r");
        if (f) {
            char buffer[256];
            while (f.available()) {
                int bytesRead = f.read((uint8_t*)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    server.sendContent(buffer);
                }
            }
            f.close();
        }
    }

    // Cleanly terminate the chunked response
    server.sendContent("");
}


void streamLog(WebServer& server) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/plain", "");
    
    // 1. Stream the backup log first if it exists
    if (LittleFS.exists("/debug.bak.log")) {
        File f = LittleFS.open("/debug.bak.log", "r");
        if (f) {
            char buffer[256];
            while (f.available()) {
                int bytesRead = f.read((uint8_t*)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    server.sendContent(buffer);
                }
            }
            f.close();
        }
    }
    
    // 2. Stream the current log
    if (LittleFS.exists("/debug.log")) {
        File f = LittleFS.open("/debug.log", "r");
        if (f) {
            char buffer[256];
            while (f.available()) {
                int bytesRead = f.read((uint8_t*)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    server.sendContent(buffer);
                }
            }
            f.close();
        }
    }

    // Cleanly terminate the chunked response
    server.sendContent("");
}

void clearLog() {
    if (LittleFS.exists("/debug.log")) {
        LittleFS.remove("/debug.log");
    }
    if (LittleFS.exists("/debug.bak.log")) {
        LittleFS.remove("/debug.bak.log");
    }
    if (LittleFS.exists("/mqtt.log")) {
        LittleFS.remove("/mqtt.log");
    }
    if (LittleFS.exists("/mqtt.bak.log")) {
        LittleFS.remove("/mqtt.bak.log");
    }
    initLogger();
}
