#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config.h"
#include "logger.h"
#include "buffer.h"
#include "OBDManager.h"

// State Machine Definitions
enum ProxyState {
    STATE_SCANNING,
    STATE_CONNECTED_TO_WICAN,
    STATE_CONNECTED_TO_HOME
};

static ProxyState currentState = STATE_SCANNING;
static const char* stateNames[] = { "SCANNING", "CONNECTED_TO_WICAN", "CONNECTED_TO_HOME" };

// Global Instances
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Cache for telemetry data
static TelemetryData latestCachedData;
static bool obdActive = false;

// Timing variables (non-blocking)
static unsigned long lastStateChange = 0;
static unsigned long lastLEDUpdate = 0;
static unsigned long lastTelemetryFetch = 0;
static unsigned long lastSlowTelemetryFetch = 0;
static unsigned long lastHomeRescanCheck = 0;
static unsigned long scanStartTime = 0;

static unsigned long wicanConnectionTimer = 0;
static unsigned long lastOBDReconnectAttempt = 0;

static bool isScanningActive = false;
static bool webServerRunning = false;
static bool mqttFlushDone = false;
static bool timeSyncDone = false;
static bool stateMachineInitLogged = false;
static bool lastScanFailed = false;
static bool isWebServerStarted = false;


// Function declarations
void transitionTo(ProxyState newState, const String& reason = "");
void handleScanning();
void handleWican();
void handleHome();
void updateLED();
void fetchOBDMetrics(bool forceSlow = false);
void flushQueueToMQTT();
void publishHAAutoDiscovery();
void setupWDT();
void feedWDT();

void transitionTo(ProxyState newState, const String& reason) {
    if (newState == currentState && stateMachineInitLogged) {
        return;
    }
    if (newState == STATE_SCANNING) {
        if (currentState != STATE_SCANNING) {
            String prevSSID = WiFi.SSID();
            if (prevSSID.length() == 0) {
                prevSSID = (currentState == STATE_CONNECTED_TO_WICAN) ? WICAN_SSID : "HomeNet";
            }
            logEvent("SWITCH", "Disconnecting from: \"" + prevSSID + "\"");
            if (reason.length() > 0) {
                logEvent("SWITCH", "Reason: " + reason);
            }
            
            disconnectOBD();
            WiFi.disconnect(true);
        }
        obdActive = false;
        isScanningActive = false;
        webServerRunning = false;
        mqttFlushDone = false;
        lastScanFailed = false;
    } 
    else if (newState == STATE_CONNECTED_TO_WICAN) {
        wicanConnectionTimer = millis();
        lastOBDReconnectAttempt = millis();
        
        // Clear cached data before new session
        memset(&latestCachedData, 0, sizeof(latestCachedData));
        latestCachedData.src = "CAR_BUFFERED";
        
        // Attempt TCP OBD socket connection
        obdActive = connectOBD();
        
        // Immediate pre-flight read cycle
        fetchOBDMetrics(true);
        
        lastTelemetryFetch = millis();
        lastSlowTelemetryFetch = millis();
    } 
    else if (newState == STATE_CONNECTED_TO_HOME) {
        mqttFlushDone = false;
        lastHomeRescanCheck = millis();
        
        // Start NTP configuration with DST
        configTzTime(TZ_INFO, NTP_SERVER);
        
        // Start minimal background WebServer once
        webServerRunning = true;
        if (!isWebServerStarted) {
            server.begin();
            isWebServerStarted = true;
            logEvent("WEBSERVER", "Debug server started at http://" + WiFi.localIP().toString() + "/debug");
        }
        
        // Configure MQTT Broker
        mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    }
    
    currentState = newState;
    lastStateChange = millis();
}


void setupWDT() {
    logEvent("BOOT", "Initializing hardware Watchdog...");
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = WDT_TIMEOUT_S * 1000,
            .idle_core_mask = 0,
            .trigger_panic = true
        };
        esp_task_wdt_init(&wdt_config);
        esp_task_wdt_add(NULL);
    #else
        esp_task_wdt_init(WDT_TIMEOUT_S, true);
        esp_task_wdt_add(NULL);
    #endif
}

void feedWDT() {
    esp_task_wdt_reset();
}

void updateLED() {
    unsigned long currentMillis = millis();
    
    switch (currentState) {
        case STATE_SCANNING: {
            if (currentMillis - lastLEDUpdate >= 100) {
                lastLEDUpdate = currentMillis;
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            }
            break;
        }
        case STATE_CONNECTED_TO_WICAN: {
            unsigned int phase = (currentMillis - lastLEDUpdate) % 2000;
            if (phase < 100) {
                digitalWrite(LED_PIN, HIGH);
            } else if (phase < 300) {
                digitalWrite(LED_PIN, LOW);
            } else if (phase < 400) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
        }
        case STATE_CONNECTED_TO_HOME: {
            digitalWrite(LED_PIN, HIGH);
            break;
        }
    }
}

void handleScanning() {
    unsigned long currentMillis = millis();
    
    if (!isScanningActive) {
        if (!lastScanFailed) {
            logEvent("SCAN", "Starting non-blocking WiFi scan...");
        }
        WiFi.scanNetworks(true, false); 
        scanStartTime = currentMillis;
        isScanningActive = true;
        return;
    }
    
    int scanResult = WiFi.scanComplete();
    
    if (scanResult >= 0) {
        bool wicanFound = false;
        bool home1Found = false;
        bool home2Found = false;
        int32_t wicanRSSI = -100;
        int32_t homeRSSI = -100;
        String homeSSID = "";
        String homePass = "";
        
        int knownCount = 0;
        if (scanResult > 0) {
            for (int i = 0; i < scanResult; i++) {
                String ssid = WiFi.SSID(i);
                if (ssid == WICAN_SSID || ssid == HOME_SSID_1 || ssid == HOME_SSID_2) {
                    knownCount++;
                }
            }
        }
        
        if (knownCount > 0) {
            lastScanFailed = false;
        }
        
        if (!lastScanFailed) {
            logEvent("SCAN", "Found " + String(knownCount) + " networks:");
        }
        
        if (scanResult > 0) {
            int* indices = new int[scanResult];
            for (int i = 0; i < scanResult; i++) {
                indices[i] = i;
            }
            
            // Sort indices by RSSI descending
            for (int i = 0; i < scanResult - 1; i++) {
                for (int j = i + 1; j < scanResult; j++) {
                    if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
                        int temp = indices[i];
                        indices[i] = indices[j];
                        indices[j] = temp;
                    }
                }
            }
            
            for (int k = 0; k < scanResult; k++) {
                int i = indices[k];
                String ssid = WiFi.SSID(i);
                int32_t rssi = WiFi.RSSI(i);
                int32_t channel = WiFi.channel(i);
                
                bool isKnown = (ssid == WICAN_SSID || ssid == HOME_SSID_1 || ssid == HOME_SSID_2);
                if (isKnown) {
                    if (!lastScanFailed) {
                        String quotedSSID = "\"" + ssid + "\"";
                        char scanBuf[128];
                        snprintf(scanBuf, sizeof(scanBuf), "  SSID: %-15s RSSI: %d dBm  CH: %d", quotedSSID.c_str(), (int)rssi, (int)channel);
                        logEvent("SCAN", String(scanBuf));
                    }
                }
                
                if (ssid == WICAN_SSID) {
                    wicanFound = true;
                    wicanRSSI = rssi;
                } else if (ssid == HOME_SSID_1 && !wicanFound) {
                    if (!home1Found && !home2Found) {
                        home1Found = true;
                        homeSSID = HOME_SSID_1;
                        homePass = HOME_PASS_1;
                        homeRSSI = rssi;
                    }
                } else if (ssid == HOME_SSID_2 && !wicanFound) {
                    if (!home1Found && !home2Found) {
                        home2Found = true;
                        homeSSID = HOME_SSID_2;
                        homePass = HOME_PASS_2;
                        homeRSSI = rssi;
                    }
                }
            }
            delete[] indices;
        }
        
        isScanningActive = false;
        
        if (wicanFound) {
            logEvent("SCAN", "Priority target selected: \"" + String(WICAN_SSID) + "\"");
            WiFi.scanDelete();
            
            logEvent("SWITCH", "Connecting to: \"" + String(WICAN_SSID) + "\"  RSSI: " + String(wicanRSSI) + " dBm");
            unsigned long startAttempt = millis();
            WiFi.begin(WICAN_SSID, WICAN_PASS);
            
            while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
                updateLED();
                feedWDT();
                delay(50);
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                unsigned long duration = millis() - startAttempt;
                logEvent("SWITCH", "Connected. Duration: " + String(duration) + " ms. IP: " + WiFi.localIP().toString());
                transitionTo(STATE_CONNECTED_TO_WICAN, "Wican found");
            } else {
                logEvent("ERROR", "Failed to connect to Wican AP!");
                WiFi.disconnect(true);
            }
        } 
        else if (home1Found || home2Found) {
            logEvent("SCAN", "Priority target selected: \"" + homeSSID + "\"");
            WiFi.scanDelete();
            
            logEvent("SWITCH", "Connecting to: \"" + homeSSID + "\"  RSSI: " + String(homeRSSI) + " dBm");
            unsigned long startAttempt = millis();
            WiFi.begin(homeSSID.c_str(), homePass.c_str());
            
            while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
                updateLED();
                feedWDT();
                delay(50);
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                unsigned long duration = millis() - startAttempt;
                logEvent("SWITCH", "Connected. Duration: " + String(duration) + " ms. IP: " + WiFi.localIP().toString());
                transitionTo(STATE_CONNECTED_TO_HOME);
            } else {
                logEvent("ERROR", "Failed to connect to Home WiFi!");
                WiFi.disconnect(true);
            }
        } 
        else {
            WiFi.scanDelete();
            if (!lastScanFailed) {
                logEvent("SCAN", "No known networks found. Retrying in 30s.");
                lastScanFailed = true;
            }
            
            unsigned long pauseStart = millis();
            while (millis() - pauseStart < 30000) {
                updateLED();
                feedWDT();
                delay(50);
            }
        }
    } 
    else if (scanResult == WIFI_SCAN_FAILED) {
        if (!lastScanFailed) {
            logEvent("ERROR", "WiFi scan failed. Retrying...");
        }
        isScanningActive = false;
    } 
    else if (currentMillis - scanStartTime > 15000) {
        if (!lastScanFailed) {
            logEvent("ERROR", "WiFi scan timed out! Restarting...");
        }
        WiFi.scanDelete();
        isScanningActive = false;
    }
}


void fetchOBDMetrics(bool forceSlow) {
    if (obdActive) {
        bool groupAOk = queryGroupA(latestCachedData);
        bool groupBOk = true;
        
        if (forceSlow) {
            groupBOk = queryGroupB(latestCachedData);
        }
        
        if (groupAOk && groupBOk) {
            latestCachedData.ts = time(nullptr);
            latestCachedData.src = "CAR_BUFFERED";
            
            logTelemetry(latestCachedData);
            if (forceSlow) {
                logTelemetrySlow(latestCachedData);
            }
            
            enqueueData(latestCachedData);
            return;
        } else {
            logEvent("ERROR", "UDS OBD queries failed. Disconnecting OBD for active retry.");
            disconnectOBD();
            obdActive = false;
        }
    }
    
    logEvent("WICAN", "Generating simulated telemetry payload...");
    generateSimulatedTelemetry(latestCachedData, false);
    if (forceSlow) {
        generateSimulatedTelemetry(latestCachedData, true);
    }
    
    latestCachedData.ts = time(nullptr);
    latestCachedData.src = "CAR_BUFFERED";
    
    logTelemetry(latestCachedData);
    if (forceSlow) {
        logTelemetrySlow(latestCachedData);
    }
    
    enqueueData(latestCachedData);
}

void handleWican() {
    if (WiFi.status() != WL_CONNECTED) {
        transitionTo(STATE_SCANNING, "Wican signal lost");
        return;
    }
    
    unsigned long currentMillis = millis();
    
    if (obdActive) {
        runOBDKeepAlive();
    } else {
        // Active TCP Reconnection scheduler
        if (currentMillis - lastOBDReconnectAttempt >= 15000) {
            lastOBDReconnectAttempt = currentMillis;
            logEvent("CONN", "OBD is offline. Attempting TCP reconnection...");
            obdActive = connectOBD();
            if (obdActive) {
                logEvent("CONN", "OBD TCP connection re-established successfully.");
                fetchOBDMetrics(true); // immediate pre-flight read
                lastTelemetryFetch = currentMillis;
                lastSlowTelemetryFetch = currentMillis;
            }
        }
        
        // Timeout if no OBD connection established within 60s
        if (currentMillis - wicanConnectionTimer >= 60000) {
            transitionTo(STATE_SCANNING, "Wican timeout");
            return;
        }
    }
    
    if (currentMillis - lastSlowTelemetryFetch >= 600000) {
        lastSlowTelemetryFetch = currentMillis;
        fetchOBDMetrics(true);
        lastTelemetryFetch = currentMillis;
    }
    else if (currentMillis - lastTelemetryFetch >= 60000) {
        lastTelemetryFetch = currentMillis;
        fetchOBDMetrics(false);
    }
}


void publishHAAutoDiscovery() {
    if (!mqttClient.connected()) return;
    
    logEvent("CONN", "Publishing Home Assistant Auto-Discovery sensors...");
    
    struct SensorDef {
        const char* id;
        const char* name;
        const char* unit;
        const char* devClass;
        const char* valTpl;
        const char* icon;
    };
    
    SensorDef sensors[] = {
        {"soc", "Battery SoC", "%", "battery", "{{ value_json.soc }}", nullptr},
        {"volt", "12V Voltage", "V", "voltage", "{{ value_json.volt }}", nullptr},
        {"temp", "Battery Temperature", "C", "temperature", "{{ value_json.temp }}", nullptr},
        {"range", "Estimated Range", "km", nullptr, "{{ value_json.range }}", "mdi:car-range"},
        {"power", "Current Power", "W", "power", "{{ value_json.power }}", nullptr},
        {"odo", "Odometer", "km", nullptr, "{{ value_json.odo }}", "mdi:counter"},
        {"service_days", "Days to Service", "d", nullptr, "{{ value_json.service_days }}", "mdi:calendar-check"},
        {"service_km", "Distance to Service", "km", nullptr, "{{ value_json.service_km }}", "mdi:map-marker-distance"},
        {"bat_cap", "Battery Capacity", "Ah", nullptr, "{{ value_json.bat_cap }}", "mdi:battery-charging-100"},
        {"tp_alarm", "Tire Pressure Alarm", nullptr, nullptr, "{{ value_json.tp_alarm }}", "mdi:car-tire-alert"}
    };
    
    for (const auto& s : sensors) {
        String topic = "homeassistant/sensor/eup_proxy_" + String(s.id) + "/config";
        JsonDocument doc;
        doc["name"] = "e-up! " + String(s.name);
        doc["stat_t"] = MQTT_TOPIC_DATA;
        if (s.unit) doc["unit_of_meas"] = s.unit;
        if (s.devClass) doc["dev_class"] = s.devClass;
        doc["val_tpl"] = s.valTpl;
        doc["uniq_id"] = "eup_proxy_" + String(s.id);
        if (s.icon) doc["ic"] = s.icon;
        
        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"] = "eup_proxy_esp32";
        dev["name"] = "e-up! Proxy";
        dev["mf"] = "VW / evNotify-local";
        dev["sw"] = FW_VERSION;
        
        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
        
        feedWDT();
        delay(20);
    }
    
    {
        String topic = "homeassistant/sensor/eup_proxy_lastsync/config";
        JsonDocument doc;
        doc["name"] = "e-up! Last Sync";
        doc["stat_t"] = MQTT_TOPIC_LASTSYNC;
        doc["dev_class"] = "timestamp";
        doc["uniq_id"] = "eup_proxy_lastsync";
        doc["ic"] = "mdi:sync";
        
        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"] = "eup_proxy_esp32";
        dev["name"] = "e-up! Proxy";
        dev["mf"] = "VW / evNotify-local";
        dev["sw"] = FW_VERSION;
        
        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
        
        feedWDT();
        delay(20);
    }
    
    logEvent("CONN", "Home Assistant Auto-Discovery published.");
}

void flushQueueToMQTT() {
    if (!mqttClient.connected()) {
        logEvent("MQTT", "Connecting to Broker " + String(MQTT_HOST) + "...");
        
        String clientID = "eupProxy_" + String(ESP.getEfuseMac(), HEX);
        
        bool connected = false;
        if (strlen(MQTT_USER) > 0) {
            connected = mqttClient.connect(clientID.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            connected = mqttClient.connect(clientID.c_str());
        }
        
        if (!connected) {
            logEvent("ERROR", "MQTT connection failed, state: " + String(mqttClient.state()));
            return;
        }
        logEvent("MQTT", "Successfully connected to Broker.");
        
        publishHAAutoDiscovery();
    }
    
    size_t queueSize = getQueueSize();
    if (queueSize == 0) {
        logEvent("MQTT", "No buffered data to flush.");
        mqttFlushDone = true;
        return;
    }
    
    logEvent("MQTT", "Found " + String(queueSize) + " records to flush. Starting...");
    
    String filepath;
    TelemetryData data;
    int flushCount = 0;
    
    while (getNextQueuedFile(filepath, data)) {
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
        
        String payload;
        serializeJson(doc, payload);
        
        logEvent("MQTT", "Publishing to topic " + String(MQTT_TOPIC_DATA) + ": " + payload);
        if (mqttClient.publish(MQTT_TOPIC_DATA, payload.c_str(), true)) {
            removeQueuedFile(filepath);
            flushCount++;
        } else {
            logEvent("ERROR", "Failed to publish message to topic " + String(MQTT_TOPIC_DATA) + "!");
            break; 
        }
        
        feedWDT();
    }
    
    logEvent("MQTT", "Flush finished. Dispatched " + String(flushCount) + " messages.");
    
    struct tm timeinfo;
    char timeBuf[64] = "2026-05-21T11:05:04+02:00";
    if (getLocalTime(&timeinfo, 100)) {
        char zBuf[10];
        strftime(zBuf, sizeof(zBuf), "%z", &timeinfo);
        String tzStr = String(zBuf);
        if (tzStr.length() == 5) {
            tzStr = tzStr.substring(0, 3) + ":" + tzStr.substring(3);
        }
        char tBuf[32];
        strftime(tBuf, sizeof(tBuf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        snprintf(timeBuf, sizeof(timeBuf), "%s%s", tBuf, tzStr.c_str());
    }
    
    logEvent("MQTT", "Updating eup/lastSync: " + String(timeBuf));
    mqttClient.publish(MQTT_TOPIC_LASTSYNC, timeBuf, true);
    
    mqttFlushDone = true;
}

void handleHome() {
    if (WiFi.status() != WL_CONNECTED) {
        transitionTo(STATE_SCANNING, "Home signal lost");
        return;
    }
    
    if (webServerRunning) {
        server.handleClient();
    }
    
    if (!timeSyncDone) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            if (timeinfo.tm_year > 120) { // Year > 2020 (meaning NTP sync is active)
                char timeBuf[16];
                strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
                
                char tzBuf[10];
                strftime(tzBuf, sizeof(tzBuf), "%Z", &timeinfo); // e.g. "CET" or "CEST"
                
                char offBuf[10];
                strftime(offBuf, sizeof(offBuf), "%z", &timeinfo); // e.g. "+0100" or "+0200"
                String offStr = String(offBuf);
                if (offStr.length() == 5) {
                    offStr = offStr.substring(0, 3) + ":" + offStr.substring(3);
                }
                
                String localTimeMsg = "NTP synchronised. Local time: " + String(timeBuf) + 
                                      " (Europe/Berlin, " + String(tzBuf) + " " + offStr + ")";
                
                logEvent("BOOT", localTimeMsg);
                timeSyncDone = true;
                g_ntpSynchronized = true;
                
                if (!stateMachineInitLogged) {
                    logEvent("BOOT", "State machine initialised. Entering SCANNING.");
                    stateMachineInitLogged = true;
                }
            }
        }
    }
    
    if (!mqttFlushDone) {
        flushQueueToMQTT();
    }
    
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastHomeRescanCheck >= 300000) {
        logEvent("STATE", "5-minute home period elapsed. Checking if prioritized Wican is nearby...");
        transitionTo(STATE_SCANNING);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Initialize WiFi mode early so MAC address registers correctly
    WiFi.mode(WIFI_STA);
    
    initLogger();
    initBuffer();
    
    logBootSequence(WiFi.macAddress(), 50, getQueueSize());
    
    // Pre-register debug endpoint handler once
    server.on("/debug", HTTP_GET, []() {
        logEvent("WEBSERVER", "GET /debug endpoint requested.");
        streamLog(server);
    });
    
    setupWDT();
    
    transitionTo(STATE_SCANNING);
}

void loop() {
    feedWDT();
    updateLED();
    
    if (!stateMachineInitLogged && millis() > 10000) {
        logEvent("BOOT", "State machine initialised. Entering SCANNING.");
        stateMachineInitLogged = true;
    }
    
    switch (currentState) {
        case STATE_SCANNING:
            handleScanning();
            break;
        case STATE_CONNECTED_TO_WICAN:
            handleWican();
            break;
        case STATE_CONNECTED_TO_HOME:
            handleHome();
            break;
    }
    
    delay(1);
}
