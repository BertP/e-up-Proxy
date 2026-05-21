# Implementation Plan - WICAN TCP Connection Resilience & Timezone-Aware Logging

This plan addresses the WICAN dongle connection issues (specifically the design flaw where a connection failure locks the proxy in simulation fallback forever) and brings the system into full compliance with the newly updated `SPEC.md` log prefix and scan layout specifications.

---

## 1. Problem Analysis & Goals

### 1.1 WICAN Connection Resilience (Critical Connection Design Flaw)
*   **The Issue:** When entering `STATE_CONNECTED_TO_WICAN`, `connectOBD()` is called once. If the WICAN dongle's TCP port `35000` is not yet open, or there is a transient failure, `obdActive` is set to `false`. The proxy then drops into simulation fallback forever without *ever* retrying `connectOBD()` while remaining connected to the Wican AP.
*   **The Solution:** 
    1.  Introduce an active reconnection scheduler in `handleWican()` that periodically (every 15 seconds) attempts to reconnect via `connectOBD()` if `obdActive` is false.
    2.  Implement a strict **Wican Timeout** of 60 seconds. If the proxy remains connected to the Wican Wi-Fi but cannot establish a successful TCP connection and get valid OBD responses within 60 seconds, it must disconnect from Wican, log the transition reason `Wican timeout`, and fall back to `STATE_SCANNING` so it can reconnect to Home Wi-Fi or scan again.

### 1.2 Timezone-Aware Dynamic Log Prefix
*   **The Issue:** Currently, the prefix is statically set to `[T+<ms>]`. The updated `SPEC.md` requires:
    *   **Before NTP Sync:** Prefix must be `[T+<ms>]` (milliseconds since boot) and messages must be prefixed with `[NO-NTP]`, e.g., `[T+312] [BOOT] [NO-NTP] LittleFS mounted...`
    *   **After NTP Sync:** Prefix must dynamically change to local wall-clock time in the `Europe/Berlin` timezone: `[hh:mm:ss] [<LEVEL>] <message>`, e.g., `[17:35:02] [BOOT] State machine initialised.`
*   **The Solution:**
    1.  Expose the NTP synchronization status as a global variable `extern bool g_ntpSynchronized` declared in `logger.h` and defined in `logger.cpp`.
    2.  Update `logEvent` inside `logger.cpp` to dynamically format the time prefix. If `g_ntpSynchronized` is true, use `getLocalTime()` to extract `[hh:mm:ss]`. Otherwise, use `[T+<ms>]` and prefix the message with `[NO-NTP] ` (except for the startup message `e-up!Proxy starting`).
    3.  Set `g_ntpSynchronized = true` in `main.cpp` as soon as NTP synchronization completes.

### 1.3 Wi-Fi Scan Logging Format
*   **The Issue:** Wi-Fi scan results are logged in their discovery order. The new spec requires scan results to be sorted by RSSI descending and formatted precisely as:
    `[hh:mm:ss] [SCAN]   SSID: "WicanAP"      RSSI: -58 dBm  CH: 6` (using the correct timezone/boot prefix).
*   **The Solution:** Sort the discovered network indices by their RSSI in descending order using a robust bubble sort, then format the SSID, RSSI, and channel with the exact padding specified.

---

## 2. Proposed Changes

### Component 1: Core Logger (`logger.h` & `logger.cpp`)

#### [MODIFY] [logger.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/include/logger.h)
*   Add `extern bool g_ntpSynchronized;` to allow `main.cpp` to communicate the NTP status to the logging subsystem.

#### [MODIFY] [logger.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/src/logger.cpp)
*   Implement `bool g_ntpSynchronized = false;`.
*   Create a helper function `String getLogTimePrefix()` to return the timezone-aware string `[hh:mm:ss]` (via `getLocalTime`) if `g_ntpSynchronized` is true, or `[T+<ms>]` if false.
*   Update `logEvent(const String& level, const String& message)` to format the log entries exactly according to the spec, adding the `[NO-NTP]` prefix to the message when `g_ntpSynchronized` is false (excluding the startup line).
*   Update `logTelemetry` to use degree symbol `°` in `Temp` to match the exact spec output: `SoC=82.5% Temp=18°C Cap=61.5Ah Volt=12.4V TpAlarm=0`.

---

### Component 2: Main Application (`main.cpp`)

#### [MODIFY] [main.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/src/main.cpp)
*   Add active scheduler tracking variables:
    *   `static unsigned long wicanConnectionTimer = 0;` (tracks when Wican Wi-Fi was established).
    *   `static unsigned long lastOBDReconnectAttempt = 0;` (tracks periodic 15s TCP connection retries).
*   Update `transitionTo()`:
    *   Initialize `wicanConnectionTimer = millis();` and `lastOBDReconnectAttempt = millis();` when transitioning to `STATE_CONNECTED_TO_WICAN`.
    *   Log `[SWITCH] Disconnecting from: "<SSID>"` and `[SWITCH] Reason: <reason>` when transitioning to `STATE_SCANNING` from Wican or Home.
*   Update `handleScanning()`:
    *   Sort the Wi-Fi networks in descending order of RSSI before logging.
    *   Print scan logs exactly matching the padding layout: `[SCAN]   SSID: "WicanAP"      RSSI: -58 dBm  CH: 6` (using the correct dynamically formatted time prefix).
    *   Log `[SCAN] No known networks found. Retrying in 5s.` when scanning returns no priority networks.
    *   Transition to `STATE_CONNECTED_TO_WICAN` with reason `"Wican found"` when Wican is selected.
    *   Transition to `STATE_CONNECTED_TO_HOME` with the correct connecting transition logs.
*   Update `handleWican()`:
    *   If `obdActive` is false:
        *   If `millis() - lastOBDReconnectAttempt >= 15000` (15 seconds):
            *   Log an event and call `connectOBD()`.
            *   If connection succeeds, set `obdActive = true` and trigger an immediate pre-flight OBD read (`fetchOBDMetrics(true)`).
        *   If `millis() - wicanConnectionTimer >= 60000` (60 seconds connection timeout):
            *   Disconnect from Wican AP and transition to `STATE_SCANNING` with reason `"Wican timeout"`.
*   Update NTP Synchronization in `handleHome()`:
    *   When the local time year > 120 (after 2020), set `g_ntpSynchronized = true`.
    *   Format and log `NTP synchronised. Local time: <time> (Europe/Berlin, <TZ> <Offset>)` exactly as shown in the spec.

---

## 3. Verification Plan

### 3.1 Automated / Compiler Verification
*   Compile inside the WSL2 environment using PlatformIO:
    ```bash
    wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c "pio run"
    ```
*   Verify the compilation is clean with 0 warnings.
*   Copy the compiled binary to the `artifacts/` folder:
    ```bash
    wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c "cp .pio/build/esp32dev/firmware.bin artifacts/firmware.bin"
    ```

### 3.2 Manual & Runtime Verification
1.  **Check ESP32 serial presence:**
    ```bash
    wsl -d Ubuntu-22.04 bash -l -c "ls /dev/ttyACM0"
    ```
2.  **Flash the firmware (after explicit user approval):**
    ```bash
    wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c "pio run --target upload --upload-port /dev/ttyACM0"
    ```
3.  **Boot & Scanning Verification:**
    *   Monitor the boot messages to ensure `[T+<ms>] [BOOT] [NO-NTP]` prefix is printed.
    *   Monitor scanning logs to verify networks are sorted by RSSI descending and match the spacing:
        `  SSID: "WicanAP"      RSSI: -58 dBm  CH: 6`.
4.  **Wican Reconnection & Timeout Verification:**
    *   Force a connection to WICAN while its TCP server is disabled/closed (or simulate it).
    *   Verify that the proxy prints a connection retry log every 15 seconds.
    *   Verify that after 60 seconds of failing to establish an OBD connection, the proxy disconnects with log `[SWITCH] Reason: Wican timeout` and transitions back to `STATE_SCANNING`.
5.  **NTP & Timezone Prefix Verification:**
    *   Connect the proxy to the Home Wi-Fi (`partlycloudy`).
    *   Verify the `NTP synchronised. Local time: ...` log appears, and that subsequent logs seamlessly transition to using the `[hh:mm:ss]` prefix (e.g. `[17:35:04]`).
6.  **WebServer Verification:**
    *   Access `http://192.168.1.55/debug` in the browser to view the dynamic log files stream smoothly without any formatting glitches or chunking failures.
