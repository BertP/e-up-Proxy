#include "OBDManager.h"
#include "config.h"
#include "logger.h"
#include <WiFiClient.h>

static WiFiClient obdClient;
static String currentHeader = "";
static unsigned long lastTesterPresent = 0;

static String stripWhitespace(const String& str) {
    String clean;
    clean.reserve(str.length()); // Pre-allocate to avoid O(n^2) reallocation
    for (unsigned int i = 0; i < str.length(); i++) {
        char c = str[i];
        if (c != ' ' && c != '\r' && c != '\n') {
            clean += c;
        }
    }
    return clean;
}

static String sendCommand(const String& cmd, unsigned int timeout = OBD_CMD_TIMEOUT_MS) {
    if (!obdClient.connected()) {
        return "";
    }

    // Clear read buffer
    while (obdClient.available()) {
        obdClient.read();
    }

    // Send command with carriage return
    obdClient.print(cmd + "\r");

    // Read response until '>' prompt or timeout
    String response = "";
    unsigned long start = millis();
    bool timedOut = true;
    while (millis() - start < timeout) {
        if (obdClient.available()) {
            char c = obdClient.read();
            if (c == '>') {
                timedOut = false;
                break;
            }
            response += c;
        }
        yield();
    }
    response.trim();

    if (timedOut && response.length() == 0) {
        logObdEvent("OBD-TIMEOUT", "No response for command: " + cmd + " (timeout: " + String(timeout) + "ms)");
    } else if (timedOut) {
        logObdEvent("OBD-TIMEOUT", "Partial response for command: " + cmd + " -> \"" + response + "\" (timeout: " + String(timeout) + "ms)");
    }

    return response;
}


static bool setHeader(const String& header) {
    if (currentHeader == header) {
        return true;
    }

    logObdEvent("CONN", "Switching CAN Header to " + header);
    String resp = sendCommand("AT SH " + header);
    String clean = stripWhitespace(resp);

    if (clean.equalsIgnoreCase("OK") || clean.indexOf("OK") >= 0 || clean.length() == 0) {
        currentHeader = header;
        return true;
    }

    logObdEvent("ERROR", "Failed to switch header to " + header + " - Response: " + resp);
    return false;
}

bool connectOBD() {
    logObdEvent("CONN", "Connecting to OBD TCP Gateway " + String(WICAN_IP) + ":" + String(WICAN_PORT) + "...");
    obdClient.setTimeout(3); // 3 seconds timeout

    if (!obdClient.connect(WICAN_IP, WICAN_PORT)) {
        logObdEvent("ERROR", "OBD TCP connection failed!");
        return false;
    }

    logObdEvent("CONN", "OBD TCP connected. Running ELM327 init sequence...");
    currentHeader = ""; // Reset cached header

    // ELM327 Init Sequence
    const char* initCmds[] = { "AT E0", "AT L0", "AT H0", "AT SP 6", "AT AT 1", "AT ST FF" };
    for (const char* cmd : initCmds) {
        String resp = sendCommand(cmd);
        logObdEvent("CONN", "ELM327 Init: " + String(cmd) + " -> " + resp);
    }

    // Open UDS Extended Diagnostic Session on ECU 7E5
    if (setHeader("7E5")) {
        logObdEvent("CONN", "Opening UDS extended diagnostic session (10 03) on 7E5...");
        String resp = sendCommand("10 03");
        String clean = stripWhitespace(resp);
        if (clean.indexOf("5003") >= 0) {
            logObdEvent("CONN", "UDS extended session opened on 7E5.");
            lastTesterPresent = millis();
            return true;
        } else {
            logObdEvent("ERROR", "Failed to open UDS extended session! Response: " + resp);
        }
    }

    disconnectOBD();
    return false;
}

void disconnectOBD() {
    if (obdClient.connected()) {
        logObdEvent("CONN", "OBD session closed. TCP disconnected from " + String(WICAN_IP) + ":" + String(WICAN_PORT) + ".");
        obdClient.stop();
    }
}

void runOBDKeepAlive() {
    if (!obdClient.connected()) {
        return;
    }

    unsigned long now = millis();
    // Keep active with 3E 80 (Tester Present, suppress response)
    if (now - lastTesterPresent >= TESTER_PRESENT_INTERVAL_MS) {
        lastTesterPresent = now;
        // Temporarily ensure 7E5 header is selected for session keep-alive
        String savedHeader = currentHeader;
        if (setHeader("7E5")) {
            sendCommand("3E 80", 500); // 500ms short timeout
        }
        if (savedHeader.length() > 0 && savedHeader != "7E5") {
            setHeader(savedHeader);
        }
    }
}

static bool queryUDS1Byte(const String& header, const String& did, float& outVal, float scale, float offset) {
    if (!setHeader(header)) return false;

    String cmd = "22 " + did;
    String resp = sendCommand(cmd);
    String clean = stripWhitespace(resp);

    // Expected positive response format starts with UDS response code 62 + DID
    String expectedPrefix = "62" + stripWhitespace(did);
    int prefixIndex = clean.indexOf(expectedPrefix);

    if (prefixIndex >= 0 && clean.length() >= prefixIndex + expectedPrefix.length() + 2) {
        String hexByte = clean.substring(prefixIndex + expectedPrefix.length(), prefixIndex + expectedPrefix.length() + 2);
        long rawVal = strtol(hexByte.c_str(), NULL, 16);
        outVal = (rawVal * scale) + offset;
        return true;
    }

    // Distinguish between NRC and empty response
    if (clean.length() == 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — no response (timeout)");
    } else if (clean.indexOf("7F") >= 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — NRC response: \"" + resp + "\"");
    } else {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — unexpected response: \"" + resp + "\"");
    }
    return false;
}

static bool queryUDS2Bytes(const String& header, const String& did, float& outVal, float scale, float offset) {
    if (!setHeader(header)) return false;

    String cmd = "22 " + did;
    String resp = sendCommand(cmd);
    String clean = stripWhitespace(resp);

    String expectedPrefix = "62" + stripWhitespace(did);
    int prefixIndex = clean.indexOf(expectedPrefix);

    if (prefixIndex >= 0 && clean.length() >= prefixIndex + expectedPrefix.length() + 4) {
        String hexBytes = clean.substring(prefixIndex + expectedPrefix.length(), prefixIndex + expectedPrefix.length() + 4);
        long rawVal = strtol(hexBytes.c_str(), NULL, 16);
        outVal = (rawVal * scale) + offset;
        return true;
    }

    if (clean.length() == 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — no response (timeout)");
    } else if (clean.indexOf("7F") >= 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — NRC response: \"" + resp + "\"");
    } else {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — unexpected response: \"" + resp + "\"");
    }
    return false;
}

static bool queryUDS3Bytes(const String& header, const String& did, float& outVal, float scale, float offset) {
    if (!setHeader(header)) return false;

    String cmd = "22 " + did;
    String resp = sendCommand(cmd);
    String clean = stripWhitespace(resp);

    String expectedPrefix = "62" + stripWhitespace(did);
    int prefixIndex = clean.indexOf(expectedPrefix);

    if (prefixIndex >= 0 && clean.length() >= prefixIndex + expectedPrefix.length() + 6) {
        String hexBytes = clean.substring(prefixIndex + expectedPrefix.length(), prefixIndex + expectedPrefix.length() + 6);
        long rawVal = strtol(hexBytes.c_str(), NULL, 16);
        outVal = (rawVal * scale) + offset;
        return true;
    }

    if (clean.length() == 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — no response (timeout)");
    } else if (clean.indexOf("7F") >= 0) {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — NRC response: \"" + resp + "\"");
    } else {
        logObdEvent("ERROR", "OBD query failed: DID " + did + " — unexpected response: \"" + resp + "\"");
    }
    return false;
}

static void deriveRange(TelemetryData& data) {
    float range_coefficient = 5.5f; // Standard warm temperature coefficient
    if (data.temp <= 0.0f) {
        range_coefficient = 3.5f; // Frozen battery coefficient
    } else if (data.temp < 15.0f) {
        range_coefficient = 4.5f; // Cold temperature coefficient
    }
    data.range = data.bat_cap * (data.soc / 100.0f) * range_coefficient;
}

bool queryGroupA(TelemetryData& data) {
    if (!obdClient.connected()) {
        return false;
    }

    bool ok = true;

    // 1. SoC (7E5, DID 02 8C, raw * 0.4)
    float socVal = 0.0f;
    if (queryUDS1Byte("7E5", "02 8C", socVal, 0.4f, 0.0f)) {
        data.soc = socVal;
    } else {
        ok = false;
    }

    // 2. Temp (7E5, DID 11 62, raw - 40)
    float tempVal = 0.0f;
    if (queryUDS1Byte("7E5", "11 62", tempVal, 1.0f, -40.0f)) {
        data.temp = tempVal;
    } else {
        ok = false;
    }

    // 3. Battery Capacity (7E5, DID 22 E1, raw * 0.1)
    float capVal = 0.0f;
    if (queryUDS2Bytes("7E5", "22 E1", capVal, 0.1f, 0.0f)) {
        data.bat_cap = capVal;
    } else {
        ok = false;
    }

    // 4. 12V Voltage (AT RV)
    String rvResp = sendCommand("AT RV");
    String cleanRv = stripWhitespace(rvResp);
    float voltVal = 0.0f;
    if (cleanRv.length() > 0) {
        voltVal = cleanRv.toFloat();
        if (voltVal > 0.0f) {
            data.volt = voltVal;
        } else {
            ok = false;
            logEvent("ERROR", "OBD query failed: voltage (AT RV) — raw response: \"" + rvResp + "\"");
        }
    } else {
        ok = false;
    }

    // 5. Tire Pressure Alarm (7E0, DID 02 1A, raw)
    float tpVal = 0.0f;
    if (queryUDS1Byte("7E0", "02 1A", tpVal, 1.0f, 0.0f)) {
        data.tp_alarm = tpVal;
    } else {
        ok = false;
    }

    // Derive Range
    if (ok) {
        deriveRange(data);
        data.power = 0.0f; // Power derivation placeholder
    }

    return ok;
}

bool queryGroupB(TelemetryData& data) {
    if (!obdClient.connected()) {
        return false;
    }

    bool ok = true;

    // 1. Odometer (7E0, DID 22 03, raw 3 bytes)
    float odoVal = 0.0f;
    if (queryUDS3Bytes("7E0", "22 03", odoVal, 1.0f, 0.0f)) {
        data.odo = odoVal;
    } else {
        ok = false;
    }

    // 2. Days to Service (7E0, DID 02 48, raw 2 bytes)
    float daysVal = 0.0f;
    if (queryUDS2Bytes("7E0", "02 48", daysVal, 1.0f, 0.0f)) {
        data.service_days = daysVal;
    } else {
        ok = false;
    }

    // 3. Km to Service (7E0, DID 02 47, raw 2 bytes)
    float kmVal = 0.0f;
    if (queryUDS2Bytes("7E0", "02 47", kmVal, 1.0f, 0.0f)) {
        data.service_km = kmVal;
    } else {
        ok = false;
    }

    return ok;
}

void generateSimulatedTelemetry(TelemetryData& data, bool slowGroup) {
    if (!slowGroup) {
        data.soc = 82.5f;
        data.volt = 12.4f;
        data.temp = 18.0f;
        data.bat_cap = 61.5f;
        data.tp_alarm = 0.0f;

        deriveRange(data);
        data.power = -2100.0f;
    } else {
        data.odo = 42150.0f;
        data.service_days = 180.0f;
        data.service_km = 7500.0f;
    }
}
