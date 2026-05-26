# Implementation Plan — Prio 2 (Refactoring) + Prio 3 (Tests)

## Ziel

`main.cpp` (713 LOC, 7 Verantwortlichkeiten) in wartbare Module aufbrechen, blockierende WiFi-Connects eliminieren, und eine PlatformIO-native Testinfrastruktur aufbauen — ohne das Laufzeitverhalten zu verändern.

---

## Prio 2 — Refactoring

### 2.3 State-Context-Struct (wird zuerst gemacht, weil 2.1 darauf aufbaut)

#### [NEW] [proxy_context.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/include/proxy_context.h)

Bündelt alle 17 globalen/statischen Variablen aus `main.cpp` in eine Struktur mit klaren Lifecycle-Kommentaren:

```cpp
struct ProxyContext {
    // --- State ---
    ProxyState state = STATE_SCANNING;

    // --- Timing (millis-based, survive reboots: NO) ---
    unsigned long lastStateChange = 0;
    unsigned long lastLEDUpdate = 0;
    unsigned long lastTelemetryFetch = 0;
    unsigned long lastSlowTelemetryFetch = 0;
    unsigned long lastHomeRescanCheck = 0;
    unsigned long scanStartTime = 0;
    unsigned long wicanConnectionTimer = 0;
    unsigned long lastOBDReconnectAttempt = 0;

    // --- Flags (reset on transition unless noted) ---
    bool obdActive = false;
    bool isScanningActive = false;
    bool webServerRunning = false;
    bool mqttFlushDone = false;
    bool lastScanFailed = false;

    // --- Flags (survive transitions — set once per boot) ---
    bool timeSyncDone = false;
    bool stateMachineInitLogged = false;
    bool isWebServerStarted = false;

    // --- WiFi connection state (non-blocking) ---
    enum WiFiPhase { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED };
    WiFiPhase wifiPhase = WIFI_IDLE;
    unsigned long wifiConnectStart = 0;
    String targetSSID;
    String targetPass;
    int32_t targetRSSI = -100;
    ProxyState targetState = STATE_SCANNING; // where to go on success

    // --- Telemetry cache ---
    TelemetryData latestData;
};
```

Die `ProxyState`-Enum-Definition wandert ebenfalls in diese Header-Datei, da sie von mehreren Modulen gebraucht wird.

---

### 2.1 main.cpp aufbrechen

#### [NEW] [wifi_manager.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/include/wifi_manager.h) / [wifi_manager.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/wifi_manager.cpp)

**Verantwortung:** WiFi-Scanning, SSID-Priorisierung, non-blocking Verbindungsaufbau

Übernimmt aus `main.cpp`:
- `handleScanning()` (~170 Zeilen) → wird non-blocking umgebaut (Prio 2.2)
- WiFi-Verbindungslogik (SSID-Sortierung, Known-Network-Filter)

**API:**
```cpp
void initWiFi();                          // WiFi.mode(WIFI_STA)
void handleScanning(ProxyContext& ctx);   // Non-blocking scan + connect
void handleWiFiConnecting(ProxyContext& ctx); // Non-blocking connect poll
```

#### [NEW] [mqtt_manager.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/include/mqtt_manager.h) / [mqtt_manager.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/mqtt_manager.cpp)

**Verantwortung:** MQTT-Verbindung, Queue-Flush, HA Auto-Discovery

Übernimmt aus `main.cpp`:
- `flushQueueToMQTT()` (~85 Zeilen)
- `publishHAAutoDiscovery()` (~75 Zeilen)

**API:**
```cpp
void initMQTT();                         // Broker-Konfiguration
void handleMQTTFlush(ProxyContext& ctx);  // Connect + flush + lastSync
```

#### [MODIFY] [main.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp)

**Ziel: ~200 LOC** (von aktuell 713). Wird zum schlanken Orchestrator:
- `setup()` — Init-Kette aufrufen
- `loop()` — WDT feed, LED update, State-Handler dispatchen
- `transitionTo()` — State-Übergänge, Flags in `ProxyContext` setzen
- `handleWican()` — OBD-Orchestrierung (bleibt in main, da eng mit State-Machine gekoppelt)
- `handleHome()` — NTP-Sync, WebServer, Rescan-Timer
- `updateLED()` — LED-Patterns
- `fetchOBDMetrics()` — OBD-Abfrage-Orchestrierung

---

### 2.2 Non-blocking WiFi-Connects

Die drei blockierenden `while (WiFi.status() != WL_CONNECTED)` Schleifen werden durch einen State-basierten Ansatz ersetzt:

**Statt:**
```cpp
WiFi.begin(ssid, pass);
while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(50);  // ← BLOCKIERT 10 Sekunden
}
```

**Wird:**
```cpp
// In handleScanning() — einmalig beim Fund eines Netzwerks:
ctx.wifiPhase = WIFI_CONNECTING;
ctx.wifiConnectStart = millis();
ctx.targetSSID = ssid;
WiFi.begin(ssid, pass);

// In handleWiFiConnecting() — pro loop()-Iteration:
if (WiFi.status() == WL_CONNECTED) {
    ctx.wifiPhase = WIFI_CONNECTED;
    transitionTo(ctx.targetState, ...);
} else if (millis() - ctx.wifiConnectStart >= timeout) {
    ctx.wifiPhase = WIFI_FAILED;
    WiFi.disconnect(true);
}
```

Ebenso wird die 30-Sekunden-Scan-Pause non-blocking über einen Timer in `ProxyContext` gelöst.

---

### 2.4 enqueueData()-Rückgabewert prüfen

#### [MODIFY] [main.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp)

An beiden Aufrufstellen von `enqueueData()` (Zeile 376 und 399) wird der Rückgabewert geprüft und bei Fehler ein `[ERROR]`-Log geschrieben:

```cpp
if (!enqueueData(latestCachedData)) {
    logEvent("ERROR", "Failed to enqueue telemetry data!");
}
```

---

## Prio 3 — Testinfrastruktur

### Architektur-Entscheidung

**PlatformIO `native` Environment** — Tests laufen auf dem Host-Rechner (Linux/WSL2), nicht auf dem ESP32. Das ermöglicht schnelle Compile-Test-Zyklen ohne Hardware.

**Test-Framework:** Unity (PlatformIO-Standard für C/C++).

**Strategie:** Testbare Logik wird in pure Functions extrahiert, die keine ESP32-Hardware-Abhängigkeiten haben. Hardware-nahe Funktionen (WiFi, LittleFS, TCP) werden *nicht* gemockt — die Tests fokussieren auf Parsing, Berechnung und Serialisierung.

#### [MODIFY] [platformio.ini](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/platformio.ini)

Neues native Test-Environment:
```ini
[env:native]
platform = native
build_flags = -DUNIT_TEST
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.4
test_framework = unity
```

#### [NEW] [test/test_native/test_obd_parsing.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/test/test_native/test_obd_parsing.cpp)

Testet die UDS-Response-Parsing-Logik (extrahiert aus OBDManager):
- `extractUDSPayload("6202 8C CE", "028C", 1)` → `0xCE` → SoC = 82.4%
- `extractUDSPayload("622261 0267", "22E1", 2)` → `0x0267` → Cap = 61.5 Ah
- Edge Cases: leere Response, Timeout-Response, `NO DATA`, falsche DID

Dafür wird eine pure Parsing-Funktion aus `queryUDS*Byte()` extrahiert:

#### [MODIFY] [OBDManager.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/include/OBDManager.h) / [OBDManager.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/OBDManager.cpp)

Neue testbare Funktionen (public API):
```cpp
// Pure parsing — no hardware dependency
String stripWhitespace(const String& str);  // war static, wird public
bool extractUDSPayload(const String& rawResponse, const String& did,
                       int byteCount, long& outRaw);
float deriveRange(float soc, float temp, float bat_cap); // war static void
```

#### [NEW] [test/test_native/test_range.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/test/test_native/test_range.cpp)

Testet `deriveRange()`:
- Warm (18°C) → Coefficient 5.5
- Cold (10°C) → Coefficient 4.5
- Frozen (-5°C) → Coefficient 3.5
- Edge: 0°C genau → 3.5 (Grenzwert ≤)

#### [NEW] [test/test_native/test_log_prefix.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/test/test_native/test_log_prefix.cpp)

Testet die Prefix-Logik des Loggers (extrahiert):
- `g_ntpSynchronized = false` → Prefix beginnt mit `[T+`
- `g_ntpSynchronized = true` → Prefix beginnt mit `[HH:MM:SS]`
- NO-NTP Prefix wird korrekt eingefügt/ausgelassen

#### [NEW] [test/test_native/test_buffer_json.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/test/test_native/test_buffer_json.cpp)

Testet die JSON-Serialisierung/Deserialisierung der `TelemetryData`-Struktur:
- Roundtrip: Struct → JSON → Struct mit identischen Werten
- Edge: `src` = leerer String
- Edge: `ts` = 0

> [!IMPORTANT]
> Die Buffer-Tests prüfen nur die JSON-Serialisierung, nicht das LittleFS-Dateisystem. Filesystem-Tests würden ein vollständiges Mock erfordern — das ist für den ersten Testaufbau zu aufwändig.

---

## Zusammenfassung: Neue Dateistruktur

```
include/
├── proxy_context.h    [NEW]  — ProxyState enum, ProxyContext struct
├── wifi_manager.h     [NEW]  — WiFi scan/connect API
├── mqtt_manager.h     [NEW]  — MQTT flush/discovery API
├── OBDManager.h       [MOD]  — extractUDSPayload(), deriveRange() public
├── buffer.h                  — unverändert
├── config.h                  — unverändert (gitignored)
├── config.example.h          — unverändert
└── logger.h                  — unverändert

src/
├── main.cpp           [MOD]  — ~200 LOC Orchestrator (von 713)
├── wifi_manager.cpp   [NEW]  — Scan-Logik, non-blocking connect
├── mqtt_manager.cpp   [NEW]  — Flush, HA discovery
├── OBDManager.cpp     [MOD]  — Parsing-Funktionen public
├── buffer.cpp                — unverändert
└── logger.cpp                — unverändert

test/test_native/
├── test_obd_parsing.cpp  [NEW]  — UDS hex parsing
├── test_range.cpp        [NEW]  — Range derivation
├── test_log_prefix.cpp   [NEW]  — Log prefix logic
└── test_buffer_json.cpp  [NEW]  — TelemetryData JSON roundtrip
```

---

## Verifikationsplan

1. **Compile ESP32:** `pio run -e esp32dev` — 0 Warnings
2. **Run native Tests:** `pio test -e native` — alle grün
3. **Binärvergleich:** RAM/Flash-Verbrauch darf sich nicht signifikant ändern (±1%)
4. **Funktionsvergleich:** Laufzeitverhalten identisch — gleiche Log-Ausgaben, gleiche MQTT-Payloads, gleiche State-Übergänge
