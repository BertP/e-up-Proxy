#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <nvs_flash.h>

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

// WiFi connection sub-states (non-blocking)
enum WiFiConnectPhase {
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED
};

static ProxyState currentState = STATE_SCANNING;
static const char* stateNames[] = { "SCANNING", "CONNECTED_TO_WICAN", "CONNECTED_TO_HOME" };

// Global Instances
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences prefs;

// Cache for telemetry data
static TelemetryData latestCachedData;
static bool obdActive = false;

// OBD Session Stats Tracker
static uint32_t obdSessionSuccessCount = 0;
static uint32_t obdSessionFailCount = 0;
static unsigned long obdSessionStartTime = 0;

// Timing variables (non-blocking)
static unsigned long lastStateChange = 0;
static unsigned long lastLEDUpdate = 0;
static unsigned long lastTelemetryFetch = 0;
static unsigned long lastSlowTelemetryFetch = 0;
static unsigned long lastHomeRescanCheck = 0;
static unsigned long scanStartTime = 0;

static unsigned long wicanConnectionTimer = 0;
static unsigned long lastOBDReconnectAttempt = 0;

// WiFi non-blocking connect state
static WiFiConnectPhase wifiConnectPhase = WIFI_IDLE;
static unsigned long wifiConnectStartTime = 0;
static String pendingSSID = "";
static String pendingPass = "";
static ProxyState pendingTargetState = STATE_SCANNING;
static unsigned long wifiConnectTimeout = 0;

static bool isScanningActive = false;
static bool webServerRunning = false;
static bool mqttFlushDone = false;
static bool timeSyncDone = false;
static bool stateMachineInitLogged = false;
static bool lastScanFailed = false;
static bool isWebServerStarted = false;
static bool otaInitialized = false;
static bool otaInProgress = false;

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
void beginWiFiConnect(const String& ssid, const String& pass, ProxyState target, unsigned long timeout);
void checkWiFiConnect();
void checkBootLoopProtection();
void checkFirmwareUpdate();

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

            // Log session stats upon disconnect
            if (obdSessionStartTime > 0) {
                unsigned long duration = (millis() - obdSessionStartTime) / 1000;
                logObdEvent("DISCONN", "OBD session closed. Duration: " + String(duration) + "s. Successfully read datasets: " + String(obdSessionSuccessCount) + ". Failed queries: " + String(obdSessionFailCount) + ".");
                obdSessionStartTime = 0;
            } else {
                logObdEvent("DISCONN", "OBD session closed (was not successfully established).");
            }

            disconnectOBD();
            WiFi.disconnect(true);
        }
        obdActive = false;
        isScanningActive = false;
        webServerRunning = false;
        mqttFlushDone = false;
        lastScanFailed = false;
        wifiConnectPhase = WIFI_IDLE;
    }
    else if (newState == STATE_CONNECTED_TO_WICAN) {
        wicanConnectionTimer = millis();
        lastOBDReconnectAttempt = millis();

        // Clear cached data before new session
        memset(&latestCachedData, 0, sizeof(latestCachedData));
        strlcpy(latestCachedData.src, "CAR_BUFFERED", sizeof(latestCachedData.src));

        // Reset session statistics trackers
        obdSessionSuccessCount = 0;
        obdSessionFailCount = 0;
        obdSessionStartTime = millis();

        logObdEvent("CONN", "Connecting to OBD Gateway. Resetting session counters.");

        // Attempt TCP OBD socket connection
        obdActive = connectOBD();
        if (obdActive) {
            logObdEvent("CONN", "Connected to OBD Gateway successfully.");
        } else {
            logObdEvent("ERROR", "Initial OBD connection failed!");
            obdSessionFailCount++;
        }

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

        // Initialize OTA once
        if (!otaInitialized) {
            ArduinoOTA.setHostname("eup-proxy");
            ArduinoOTA.setPassword(OTA_PASSWORD);

            ArduinoOTA.onStart([]() {
                otaInProgress = true;
                Serial.println("[OTA] Update starting...");
            });
            ArduinoOTA.onEnd([]() {
                Serial.println("[OTA] Update complete. Rebooting...");
            });
            ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                feedWDT();
            });
            ArduinoOTA.onError([](ota_error_t error) {
                otaInProgress = false;
                Serial.printf("[ERROR] OTA failed, error: %d\n", error);
            });

            ArduinoOTA.begin();
            otaInitialized = true;
            logEvent("OTA", "OTA service initialized on port 3232.");
        }

        // Configure MQTT Broker
        mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    }

    currentState = newState;
    lastStateChange = millis();
}


void setupWDT() {
    logEvent("BOOT", "Initializing hardware Watchdog (" + String(WDT_TIMEOUT_S) + "s)...");
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

void checkBootLoopProtection() {
    prefs.begin("proxy", false);
    int crashCount = prefs.getInt(BOOT_CRASH_NVS_KEY, 0);

    if (crashCount >= BOOT_CRASH_THRESHOLD) {
        logEvent("BOOT", "WARNING: Boot-loop detected (" + String(crashCount) + " consecutive crashes). Resetting counter.");
        prefs.putInt(BOOT_CRASH_NVS_KEY, 0);
        // Could add safe-mode logic here in the future
    } else {
        // Increment crash counter — will be reset to 0 after successful NTP sync
        prefs.putInt(BOOT_CRASH_NVS_KEY, crashCount + 1);
        logEvent("BOOT", "Boot counter: " + String(crashCount + 1) + "/" + String(BOOT_CRASH_THRESHOLD));
    }
    prefs.end();
}

void resetBootCrashCounter() {
    prefs.begin("proxy", false);
    prefs.putInt(BOOT_CRASH_NVS_KEY, 0);
    prefs.end();
}

void checkFirmwareUpdate() {
    prefs.begin("proxy", false);
    String lastFw = prefs.getString("fw_version", "");
    if (lastFw != FW_VERSION) {
        clearLog();
        logEvent("BOOT", "Firmware version changed from '" + lastFw + "' to '" + String(FW_VERSION) + "'. Log files cleared.");
        prefs.putString("fw_version", FW_VERSION);
    }
    prefs.end();
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

// Non-blocking WiFi connect: initiate
void beginWiFiConnect(const String& ssid, const String& pass, ProxyState target, unsigned long timeout) {
    pendingSSID = ssid;
    pendingPass = pass;
    pendingTargetState = target;
    wifiConnectTimeout = timeout;
    wifiConnectStartTime = millis();
    wifiConnectPhase = WIFI_CONNECTING;

    logEvent("SWITCH", "Connecting to: \"" + ssid + "\" (non-blocking, timeout: " + String(timeout / 1000) + "s)");
    WiFi.begin(ssid.c_str(), pass.c_str());
}

// Non-blocking WiFi connect: poll
void checkWiFiConnect() {
    if (wifiConnectPhase != WIFI_CONNECTING) return;

    if (WiFi.status() == WL_CONNECTED) {
        unsigned long duration = millis() - wifiConnectStartTime;
        logEvent("SWITCH", "Connected. Duration: " + String(duration) + " ms. IP: " + WiFi.localIP().toString());
        wifiConnectPhase = WIFI_IDLE;
        transitionTo(pendingTargetState, pendingSSID + " connected");
        return;
    }

    if (millis() - wifiConnectStartTime >= wifiConnectTimeout) {
        logEvent("ERROR", "Failed to connect to " + pendingSSID + " (timeout)");
        WiFi.disconnect(true);
        wifiConnectPhase = WIFI_IDLE;
        return;
    }
}

void handleScanning() {
    // If we're waiting for a non-blocking WiFi connect, just poll it
    if (wifiConnectPhase == WIFI_CONNECTING) {
        checkWiFiConnect();
        return;
    }

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
            logEvent("SCAN", "Found " + String(knownCount) + " known networks:");
        }

        if (scanResult > 0) {
            // Sort by RSSI descending using simple selection
            int* indices = new int[scanResult];
            for (int i = 0; i < scanResult; i++) indices[i] = i;

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
                        logEvent("SCAN", "  " + quotedSSID + "  RSSI: " + String(rssi) + " dBm  CH: " + String(channel) + "  [KNOWN]");
                    }

                    if (ssid == WICAN_SSID && !wicanFound) {
                        wicanFound = true;
                        wicanRSSI = rssi;
                    }
                    if (ssid == HOME_SSID_1 && !home1Found) {
                        home1Found = true;
                        if (rssi > homeRSSI) {
                            homeRSSI = rssi;
                            homeSSID = HOME_SSID_1;
                            homePass = HOME_PASS_1;
                        }
                    }
                    if (ssid == HOME_SSID_2 && !home2Found) {
                        home2Found = true;
                        if (rssi > homeRSSI) {
                            homeRSSI = rssi;
                            homeSSID = HOME_SSID_2;
                            homePass = HOME_PASS_2;
                        }
                    }
                }
            }
            delete[] indices;
        }

        isScanningActive = false;

        if (wicanFound) {
            logEvent("SCAN", "Priority target selected: \"" + String(WICAN_SSID) + "\"");
            WiFi.scanDelete();
            beginWiFiConnect(WICAN_SSID, WICAN_PASS, STATE_CONNECTED_TO_WICAN, WIFI_CONNECT_TIMEOUT_WICAN_MS);
        }
        else if (home1Found || home2Found) {
            logEvent("SCAN", "Priority target selected: \"" + homeSSID + "\"");
            WiFi.scanDelete();
            beginWiFiConnect(homeSSID, homePass, STATE_CONNECTED_TO_HOME, WIFI_CONNECT_TIMEOUT_HOME_MS);
        }
        else {
            WiFi.scanDelete();
            if (!lastScanFailed) {
                logEvent("SCAN", "No known networks found. Retrying in " + String(SCAN_RETRY_PAUSE_MS / 1000) + "s.");
                lastScanFailed = true;
            }

            // Non-blocking pause: just set scanStartTime and don't start new scan until pause elapsed
            scanStartTime = millis();
            // isScanningActive stays false; next loop iteration will check timing
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

    // Non-blocking scan retry pause
    if (!isScanningActive && lastScanFailed && wifiConnectPhase == WIFI_IDLE) {
        if (millis() - scanStartTime < SCAN_RETRY_PAUSE_MS) {
            return; // Still in pause period, do nothing
        }
        // Pause elapsed, allow new scan on next loop
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
            strlcpy(latestCachedData.src, "CAR_BUFFERED", sizeof(latestCachedData.src));

            logTelemetry(latestCachedData);
            if (forceSlow) {
                logTelemetrySlow(latestCachedData);
            }

            enqueueData(latestCachedData);
            obdSessionSuccessCount++;
            return;
        } else {
            logObdEvent("ERROR", "UDS OBD queries failed. Disconnecting OBD for active retry.");
            obdSessionFailCount++;
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
    strlcpy(latestCachedData.src, "CAR_BUFFERED", sizeof(latestCachedData.src));

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
        if (currentMillis - lastOBDReconnectAttempt >= OBD_RECONNECT_INTERVAL_MS) {
            lastOBDReconnectAttempt = currentMillis;
            logEvent("CONN", "OBD is offline. Attempting TCP reconnection...");
            obdActive = connectOBD();
            if (obdActive) {
                logEvent("CONN", "OBD TCP connection re-established successfully.");
                fetchOBDMetrics(true);
                lastTelemetryFetch = currentMillis;
                lastSlowTelemetryFetch = currentMillis;
            }
        }

        // Timeout if no OBD connection established
        if (currentMillis - wicanConnectionTimer >= WICAN_TIMEOUT_MS) {
            transitionTo(STATE_SCANNING, "Wican timeout");
            return;
        }
    }

    if (currentMillis - lastSlowTelemetryFetch >= POLL_INTERVAL_SLOW_MS) {
        lastSlowTelemetryFetch = currentMillis;
        fetchOBDMetrics(true);
        lastTelemetryFetch = currentMillis;
    }
    else if (currentMillis - lastTelemetryFetch >= POLL_INTERVAL_FAST_MS) {
        lastTelemetryFetch = currentMillis;
        fetchOBDMetrics(false);
    }
}


void publishHAAutoDiscovery() {
    if (!mqttClient.connected()) return;

    logMqttEvent("CONN", "Publishing Home Assistant Auto-Discovery sensors...");

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
        doc["state_class"] = "measurement";
        doc["val_tpl"] = s.valTpl;
        doc["uniq_id"] = "eup_proxy_" + String(s.id);
        if (s.icon) doc["ic"] = s.icon;

        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"] = "eup_proxy_esp32";
        dev["name"] = "e-up! Proxy";
        dev["mf"] = "VW / local";
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
        dev["mf"] = "VW / local";
        dev["sw"] = FW_VERSION;

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);

        feedWDT();
        delay(20);
    }

    logMqttEvent("CONN", "Home Assistant Auto-Discovery published.");
}

void flushQueueToMQTT() {
    if (!mqttClient.connected()) {
        logMqttEvent("MQTT", "Connecting to Broker " + String(MQTT_HOST) + "...");

        String clientID = "eupProxy_" + String(ESP.getEfuseMac(), HEX);

        bool connected = false;
        if (strlen(MQTT_USER) > 0) {
            connected = mqttClient.connect(clientID.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            connected = mqttClient.connect(clientID.c_str());
        }

        if (!connected) {
            logMqttEvent("ERROR", "MQTT connection failed, state: " + String(mqttClient.state()));
            return;
        }
        logMqttEvent("MQTT", "Successfully connected to Broker.");

        publishHAAutoDiscovery();
    }

    size_t queueSize = getQueueSize();
    if (queueSize == 0) {
        logMqttEvent("MQTT", "No buffered data to flush.");
        mqttFlushDone = true;
        return;
    }

    logMqttEvent("MQTT", "Found " + String(queueSize) + " records to flush. Starting...");

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

        logMqttEvent("MQTT", "Publishing to topic " + String(MQTT_TOPIC_DATA) + ": " + payload);
        if (mqttClient.publish(MQTT_TOPIC_DATA, payload.c_str(), true)) {
            removeQueuedFile(filepath);
            flushCount++;
        } else {
            logMqttEvent("ERROR", "Failed to publish message to topic " + String(MQTT_TOPIC_DATA) + "!");
            break;
        }

        feedWDT();
    }

    logMqttEvent("MQTT", "Flush finished. Dispatched " + String(flushCount) + " messages.");

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

    logMqttEvent("MQTT", "Updating eup/lastSync: " + String(timeBuf));
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

    // Handle OTA updates
    if (otaInitialized) {
        ArduinoOTA.handle();
    }

    if (!timeSyncDone) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            if (timeinfo.tm_year > 120) {
                char timeBuf[16];
                strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);

                char tzBuf[10];
                strftime(tzBuf, sizeof(tzBuf), "%Z", &timeinfo);

                char offBuf[10];
                strftime(offBuf, sizeof(offBuf), "%z", &timeinfo);
                String offStr = String(offBuf);
                if (offStr.length() == 5) {
                    offStr = offStr.substring(0, 3) + ":" + offStr.substring(3);
                }

                String localTimeMsg = "NTP synchronised. Local time: " + String(timeBuf) +
                                      " (Europe/Berlin, " + String(tzBuf) + " " + offStr + ")";

                logEvent("BOOT", localTimeMsg);
                timeSyncDone = true;
                g_ntpSynchronized = true;

                // Successful boot: reset crash counter
                resetBootCrashCounter();

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

    if (!otaInProgress && currentMillis - lastHomeRescanCheck >= HOME_RESCAN_INTERVAL_MS) {
        logEvent("STATE", String(HOME_RESCAN_INTERVAL_MS / 60000) + "-minute home period elapsed. Checking if prioritized Wican is nearby...");
        transitionTo(STATE_SCANNING);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // Initialize low-level NVS and recover if partition is corrupted or layout shifted (e.g. after changing partition tables)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize WiFi mode early so MAC address registers correctly
    WiFi.mode(WIFI_STA);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    initLogger();

    // Check for firmware update and clear logs if updated
    checkFirmwareUpdate();

    initBuffer();

    logBootSequence(WiFi.macAddress(), 50, getQueueSize());

    // Boot-loop protection check
    checkBootLoopProtection();

    // Pre-register debug endpoint handler once
    server.on("/debug", HTTP_GET, []() {
        logEvent("WEBSERVER", "GET /debug endpoint requested.");
        streamLog(server);
    });

    // Register dedicated /mqtt endpoint handler once
    server.on("/mqtt", HTTP_GET, []() {
        logMqttEvent("WEBSERVER", "GET /mqtt endpoint requested.");
        streamMqttLog(server);
    });

    // Register dedicated /obd endpoint handler once
    server.on("/obd", HTTP_GET, []() {
        logObdEvent("WEBSERVER", "GET /obd endpoint requested.");
        streamObdLog(server);
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
