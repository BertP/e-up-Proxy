#include "buffer.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "logger.h"
#include "config.h"

extern void feedWDT(); // Access global hardware Watchdog feeder

static bool bufferInitialized = false;

void initBuffer() {
    if (bufferInitialized) return; // Only initialize once

    if (!LittleFS.begin(true)) {
        Serial.println("Failed to mount LittleFS in Buffer!");
        return;
    }

    // Create the queue directory if it doesn't exist
    if (!LittleFS.exists("/queue")) {
        if (LittleFS.mkdir("/queue")) {
            logEvent("BUFFER", "Created /queue directory in LittleFS.");
        } else {
            Serial.println("Failed to create /queue directory!");
        }
    }

    bufferInitialized = true;
}

bool enqueueData(const TelemetryData& data) {
    if (!bufferInitialized) return false;

    // Construct a unique filename based on timestamp and millis to avoid collisions
    String filepath = "/queue/" + String(data.ts) + "_" + String(millis()) + ".json";

    File f = LittleFS.open(filepath, "w");
    if (!f) {
        logEvent("ERROR", "Failed to create queue file: " + filepath);
        return false;
    }

    // Use ArduinoJson to serialize the struct
    JsonDocument doc;
    doc["soc"] = data.soc;
    doc["volt"] = data.volt;
    doc["temp"] = data.temp;
    doc["range"] = data.range;
    doc["power"] = data.power;
    doc["odo"] = data.odo;
    doc["service_days"] = data.service_days;
    doc["service_km"] = data.service_km;
    doc["bat_cap"] = data.bat_cap;
    doc["tp_alarm"] = data.tp_alarm;
    doc["ts"] = data.ts;
    doc["src"] = data.src;

    if (serializeJson(doc, f) == 0) {
        logEvent("ERROR", "Failed to serialize JSON to " + filepath);
        f.close();
        LittleFS.remove(filepath);
        return false;
    }

    f.close();
    logEvent("BUFFER", "Enqueued data to " + filepath);
    return true;
}

bool getNextQueuedFile(String& filepath, TelemetryData& data) {
    if (!bufferInitialized) return false;

    File dir = LittleFS.open("/queue", "r");
    if (!dir || !dir.isDirectory()) {
        return false;
    }

    File file = dir.openNextFile();
    while (file) {
        feedWDT(); // Feed watchdog during file search
        yield();   // Yield CPU to let system background tasks run
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".json")) {
                // Ensure name has complete path
                filepath = "/queue/" + name;

                // Read and deserialize the file
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, file);
                file.close();

                if (err) {
                    logEvent("ERROR", "Failed to parse JSON in file: " + filepath + " Error: " + err.c_str());
                    // Delete corrupted file to avoid infinite loops
                    LittleFS.remove(filepath);
                    file = dir.openNextFile();
                    continue;
                }

                // Populate data struct
                data.soc = doc["soc"] | 0.0f;
                data.volt = doc["volt"] | 0.0f;
                data.temp = doc["temp"] | 0.0f;
                data.range = doc["range"] | 0.0f;
                data.power = doc["power"] | 0.0f;
                data.odo = doc["odo"] | 0.0f;
                data.service_days = doc["service_days"] | 0.0f;
                data.service_km = doc["service_km"] | 0.0f;
                data.bat_cap = doc["bat_cap"] | 0.0f;
                data.tp_alarm = doc["tp_alarm"] | 0.0f;
                data.ts = doc["ts"] | 0U;
                strlcpy(data.src, doc["src"] | "", sizeof(data.src));

                dir.close();
                return true;
            }
        }
        file = dir.openNextFile();
    }

    dir.close();
    return false;
}

void removeQueuedFile(const String& filepath) {
    if (LittleFS.exists(filepath)) {
        if (LittleFS.remove(filepath)) {
            logEvent("BUFFER", "Successfully removed " + filepath);
        } else {
            logEvent("ERROR", "Failed to remove " + filepath);
        }
    }
}

size_t getQueueSize() {
    if (!bufferInitialized) return 0;

    size_t count = 0;
    File dir = LittleFS.open("/queue", "r");
    if (!dir || !dir.isDirectory()) {
        return 0;
    }

    File file = dir.openNextFile();
    while (file) {
        feedWDT(); // Feed watchdog during queue size counting
        yield();   // Yield CPU
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".json")) {
                count++;
            }
        }
        file = dir.openNextFile();
    }

    dir.close();
    return count;
}

void clearQueue() {
    if (!bufferInitialized) return;

    File dir = LittleFS.open("/queue", "r");
    if (!dir || !dir.isDirectory()) {
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        feedWDT(); // Feed watchdog during queue clearing
        yield();   // Yield CPU
        if (!file.isDirectory()) {
            String filepath = "/queue/" + String(file.name());
            file.close();
            LittleFS.remove(filepath);
        } else {
            file.close();
        }
        file = dir.openNextFile();
    }

    dir.close();
    logEvent("BUFFER", "Queue cleared.");
}
