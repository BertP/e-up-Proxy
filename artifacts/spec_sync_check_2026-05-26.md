# SPEC.md Sync-Check: Abweichungen zum Projektstand
**Datum:** 2026-05-26  
**Verglichen:** `SPEC.md` (Stand 2026-05-21) ↔ Code (`src/`, `include/`)

---

## Ergebnis-Übersicht

| SPEC-Abschnitt | Status | Abweichungen |
|---|:---:|:---:|
| SPEC-01 (States, WiFi, LED) | ⚠️ | 3 |
| SPEC-02 (Diagnostics, WebServer, Logs) | ⚠️ | 4 |
| SPEC-03 (Data Buffering) | ✅ | 0 |
| SPEC-04 (OBD2 DID Mapping) | ⚠️ | 4 |
| SPEC-05 (MQTT, HA Discovery) | ⚠️ | 3 |

**Gesamt: 14 Abweichungen** (0 kritisch, 8 moderat, 6 kosmetisch)

---

## SPEC-01: System States, Wi-Fi Logic & LED Signaling

### ✅ Synchron
- 3 States korrekt implementiert (`STATE_SCANNING`, `STATE_CONNECTED_TO_WICAN`, `STATE_CONNECTED_TO_HOME`)
- WiCAN-Priorität korrekt: Wird immer vor Home gewählt
- LED-Patterns: Fast-Blink (SCANNING), Double-Blink (WICAN), Solid ON (HOME) — alle korrekt
- Transition-Gründe (`"Wican signal lost"`, `"Wican timeout"`, `"Home signal lost"`, `"Wican found"`) — alle implementiert

### ⚠️ Abweichung 1: Fehlender State `CONNECTED_TO_HOME` Rescan-Intervall

> **SPEC sagt:** *"Wican SSID always takes priority over Home Wi-Fi when both are visible."*

> **Code tut:** In `handleHome()` gibt es einen 5-Minuten-Timer (`300000ms`), der zurück zu `STATE_SCANNING` wechselt, um nach WiCAN zu suchen. Dieses **5-Minuten-Intervall** ist in der SPEC nicht dokumentiert.

```cpp
// main.cpp, handleHome()
if (currentMillis - lastHomeRescanCheck >= 300000) {
    logEvent("STATE", "5-minute home period elapsed. Checking if prioritized Wican is nearby...");
    transitionTo(STATE_SCANNING);
}
```

**Impact:** Moderat — Verhalten ist sinnvoll, aber undokumentiert.

---

### ⚠️ Abweichung 2: Blockierender WiFi-Connect nicht dokumentiert

> **SPEC sagt:** *"If Wican is found, it always takes priority"* (keine Angabe zur Connect-Methodik)

> **Code tut:** Der WiFi-Connect ist **blockierend** mit 10s (WiCAN) bzw. 15s (Home) Timeout:

```cpp
while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    updateLED(); feedWDT(); delay(50);
}
```

**Impact:** Kosmetisch — SPEC müsste nicht-funktionale Anforderungen (Blocking vs. Non-Blocking) dokumentieren.

---

### ⚠️ Abweichung 3: 30s Retry-Pause bei "No known networks" nicht dokumentiert

> **SPEC sagt:** *"No known networks found. Retrying in \<n\>s."*

> **Code tut:** Die Pause ist **30 Sekunden**, blockierend:

```cpp
unsigned long pauseStart = millis();
while (millis() - pauseStart < 30000) {
    updateLED(); feedWDT(); delay(50);
}
```

**Impact:** Kosmetisch — Der `<n>`-Platzhalter in der SPEC sollte durch `30` ersetzt werden.

---

## SPEC-02: Diagnostics, WebServer & Log Management

### ✅ Synchron
- Log-Dateiname: `debug.log` ✅
- Log-Rotation: Backup als `debug.bak.log` ✅
- NTP-Zeitformat nach Sync: `[HH:MM:SS]` ✅
- Fallback vor NTP: `[T+<ms>]` mit `[NO-NTP]` Prefix ✅
- Boot-Log-Sequenz: 4 Zeilen in einer File-Operation ✅
- WebServer-Endpoint: `GET /debug` streamt `debug.log` ✅

### ⚠️ Abweichung 4: Log-Größenlimit weicht ab

> **SPEC sagt:** *"Size cap: 50 KB maximum."*

> **Code tut:** `MAX_LOG_SIZE = 25600` (25 KB) pro Datei. Da es `debug.log` + `debug.bak.log` gibt, ist die **Gesamtkapazität 50 KB** — aber pro Datei sind es nur 25 KB.

```cpp
// logger.cpp
#define MAX_LOG_SIZE 25600 // 25 KB for each log file, making 50 KB total limit
```

**Impact:** Kosmetisch — Die Implementierung ist korrekt (50 KB total), aber die SPEC beschreibt das Rotationsverhalten nicht (sie sagt "oldest entries are purged", der Code macht ein Backup + Neuanlage).

---

### ⚠️ Abweichung 5: Log-Level `WEBSERVER` und `MQTT` fehlen in SPEC

> **SPEC sagt:** *"\<LEVEL\> — one of `BOOT`, `SCAN`, `CONN`, `SWITCH`, `DATA`, `ERROR`."*

> **Code nutzt zusätzlich:**
- `WEBSERVER` (in `main.cpp` beim Starten des Debug-Servers)
- `MQTT` (in `main.cpp` beim MQTT-Connect/Flush)
- `STATE` (in `main.cpp` beim Home-Rescan-Trigger)
- `DATA:SLOW` (in `logger.cpp` für Group-B-Daten)
- `WICAN` (in `main.cpp` für simulierte Telemetrie)
- `INFO` (Default-Level in `logEvent(message)` Overload)
- `BUFFER` (in `buffer.cpp` für Queue-Operationen)

**Impact:** Moderat — 7 undokumentierte Log-Level. Die SPEC-Liste ist unvollständig.

---

### ⚠️ Abweichung 6: Scan-Log-Format weicht ab

> **SPEC sagt:**
> ```
> [17:35:04] [SCAN]   SSID: "WicanAP"      RSSI: -58 dBm  CH: 6
> ```

> **Code produziert** (aus `main.cpp`, `handleScanning()`):
> ```
> [17:35:04] [SCAN]   "WicanAP"  RSSI: -58 dBm  CH: 6  [KNOWN]
> ```

Das Format im Code hat:
- Kein `SSID:`-Prefix
- `[KNOWN]`-Tag statt nichts
- Andere Padding/Alignment

**Impact:** Kosmetisch — Format-Diskrepanz, keine funktionale Auswirkung.

---

### ⚠️ Abweichung 7: WebServer-Aktivierung weicht ab

> **SPEC sagt:** *"Active only when `CONNECTED_TO_HOME`."*

> **Code tut:** Der WebServer wird beim ersten `STATE_CONNECTED_TO_HOME` gestartet und **nie gestoppt**:

```cpp
if (!isWebServerStarted) {
    server.begin();
    isWebServerStarted = true;  // Wird nie auf false gesetzt
}
```

Der `server.handleClient()` wird zwar nur in `handleHome()` aufgerufen, aber der Socket bleibt offen. Wenn der ESP32 zu WiCAN wechselt (anderes Netzwerk), ist der Server technisch gestartet, aber unerreichbar.

**Impact:** Kosmetisch — Kein funktionales Problem, da der Server auf der Home-IP lauscht, die im WiCAN-Netz nicht existiert. Aber es verbraucht minimal RAM.

---

## SPEC-03: Data Buffering Contract — ✅ Vollständig synchron

- FIFO-Queue auf LittleFS: ✅
- Payload-Schema stimmt mit `eup/data` überein: ✅
- Flush bei `CONNECTED_TO_HOME`: ✅
- JSON-Serialisierung der TelemetryData: ✅

---

## SPEC-04: OBD2 DID Mapping

### ✅ Synchron
- ELM327 Init-Sequenz (`AT E0`, `AT L0`, `AT H0`, `AT SP 6`, `AT AT 1`, `AT ST FF`): ✅
- UDS Extended Session (`10 03`) auf `7E5`: ✅
- Tester Present (`3E 80`): ✅
- Group A (60s): SoC, Temp, Cap, Volt, TpAlarm: ✅
- Group B (10min): Odo, ServiceDays, ServiceKm: ✅
- Scale/Offset-Formeln stimmen überein: ✅

### ⚠️ Abweichung 8: Methoden-Namen in SPEC stimmen nicht mit Code überein

> **SPEC sagt:**
> | Method |
> |---|
> | `querySoC()` |
> | `queryTemp()` |
> | `queryBatteryCapacity()` |
> | `queryVoltage()` |
> | `queryTirePressureAlarm()` |
> | `queryOdometer()` |
> | `queryDaysToService()` |
> | `queryKmToService()` |

> **Code hat:** Keine dieser benannten Funktionen. Stattdessen wird das **generische System** verwendet:

```cpp
queryUDS1Byte("7E5", "02 8C", socVal, 0.4f, 0.0f);   // statt querySoC()
queryUDS1Byte("7E5", "11 62", tempVal, 1.0f, -40.0f); // statt queryTemp()
queryUDS2Bytes("7E5", "22 E1", capVal, 0.1f, 0.0f);   // statt queryBatteryCapacity()
```

**Impact:** Moderat — Die SPEC referenziert eine API, die nicht existiert. Die SPEC müsste `queryGroupA()` / `queryGroupB()` dokumentieren.

---

### ⚠️ Abweichung 9: SPEC sagt "Source: `OBDManager.h`" — Code ist in `.cpp`

> **SPEC sagt:** *"Source: `OBDManager.h`."*

> **Code:** Die gesamte Implementierung ist in `OBDManager.cpp`. Der Header `OBDManager.h` enthält nur Funktionsdeklarationen (25 LOC).

**Impact:** Kosmetisch — Falscher Dateiname-Verweis.

---

### ⚠️ Abweichung 10: `range` Ableitung ist implementiert, aber SPEC sagt "TBD"

> **SPEC sagt:** *"`power` and `range` are not directly queried via OBD2 — derivation method TBD."*

> **Code implementiert** `deriveRange()` mit temperaturabhängigen Koeffizienten:

```cpp
static void deriveRange(TelemetryData& data) {
    float range_coefficient = 5.5f;
    if (data.temp <= 0.0f) range_coefficient = 3.5f;
    else if (data.temp < 15.0f) range_coefficient = 4.5f;
    data.range = data.bat_cap * (data.soc / 100.0f) * range_coefficient;
}
```

**Impact:** Moderat — Die SPEC ist veraltet. `range` ist nicht mehr TBD, sondern hat eine konkrete Implementierung. `power` ist weiterhin Platzhalter (`0.0f`).

---

### ⚠️ Abweichung 11: Tester-Present-Intervall weicht ab

> **SPEC sagt:** *"kept alive with `3E 80` (Tester Present, suppress response)"* — kein Intervall angegeben.

> **Code:** Intervall ist **2.5 Sekunden** (`2500ms`).

```cpp
if (now - lastTesterPresent >= 2500) { ... }
```

**Impact:** Kosmetisch — Das Intervall sollte dokumentiert werden, da es Einfluss auf die CAN-Bus-Belastung hat.

---

## SPEC-05: MQTT Topics, Broker & HA Discovery

### ✅ Synchron
- Topic `eup/data` mit Retained: ✅
- Topic `eup/lastSync` mit ISO-8601 + Timezone-Offset: ✅
- HA Auto-Discovery mit `homeassistant/sensor/eup_proxy_<ID>/config`: ✅
- Device-Block `"eup_proxy_esp32"`: ✅
- Flush-Trigger bei `CONNECTED_TO_HOME`: ✅

### ⚠️ Abweichung 12: HA-Discovery enthält `state_class` nicht (im Code)

> **SPEC sagt** (im Beispiel-Payload):
> ```json
> "state_class": "measurement"
> ```

> **Code in `publishHAAutoDiscovery()`:** Das Feld `state_class` wird **nicht gesetzt**. Die `SensorDef`-Struct hat kein `state_class`-Member:

```cpp
struct SensorDef {
    const char* id;
    const char* name;
    const char* unit;
    const char* devClass;
    const char* valTpl;
    const char* icon;
    // KEIN state_class!
};
```

**Impact:** Moderat — Ohne `state_class: "measurement"` zeigt Home Assistant keine Langzeit-Statistiken (Graphen) für diese Sensoren an.

---

### ⚠️ Abweichung 13: SPEC zeigt `"sw": "2.1-ota-logfix"` im Discovery-Payload

> **SPEC sagt:**
> ```json
> "sw": "2.1-ota-logfix"
> ```

> **Code:** Der Code nutzt korrekt `FW_VERSION` als Variable — aber die SPEC hat den Wert hardcodiert. Bei jeder Firmware-Änderung muss die SPEC-Beispiel-Payload manuell aktualisiert werden.

**Impact:** Kosmetisch — Erwartbar, aber fehleranfällig.

---

### ⚠️ Abweichung 14: MQTT-Client-ID weicht ab

> **SPEC sagt:** (implizit, kein explizites Feld)

> **Code:** Die Client-ID ist MAC-basiert:
> ```cpp
> String clientID = "eupProxy_" + String(ESP.getEfuseMac(), HEX);
> ```

Nicht in der SPEC dokumentiert. Relevant, weil bei mehreren Proxys im selben Broker Kollisionen auftreten könnten.

**Impact:** Kosmetisch — Sollte der Vollständigkeit halber dokumentiert sein.

---

## Zusammenfassung: Maßnahmen

### Moderat (SPEC aktualisieren)

| # | Stelle | Aktion |
|---|---|---|
| 1 | SPEC-01 | 5-Minuten Home-Rescan-Intervall dokumentieren |
| 5 | SPEC-02 | Log-Level-Liste erweitern um `WEBSERVER`, `MQTT`, `STATE`, `DATA:SLOW`, `WICAN`, `INFO`, `BUFFER` |
| 8 | SPEC-04 | Methoden-Referenzen durch `queryGroupA()` / `queryGroupB()` ersetzen |
| 10 | SPEC-04 | `range`-Ableitung ist implementiert — TBD-Vermerk entfernen, Formel dokumentieren |
| 12 | SPEC-05 | `state_class` Feld zur HA-Discovery hinzufügen (entweder in SPEC streichen oder im Code implementieren) |

### Kosmetisch (optional)

| # | Stelle | Aktion |
|---|---|---|
| 3 | SPEC-01 | Retry-Pause `<n>` durch `30` ersetzen |
| 4 | SPEC-02 | Rotationsverhalten präzisieren (25 KB pro Datei, Backup-Mechanismus) |
| 6 | SPEC-02 | Scan-Log-Format an tatsächliches Output-Format anpassen |
| 9 | SPEC-04 | "Source: `OBDManager.h`" → "Source: `OBDManager.cpp`" |
| 11 | SPEC-04 | Tester-Present-Intervall (2.5s) dokumentieren |
| 13 | SPEC-05 | Hinweis dass `sw`-Wert aus `FW_VERSION` stammt |
| 14 | SPEC-05 | MQTT Client-ID Format dokumentieren |
