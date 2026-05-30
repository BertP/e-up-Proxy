# Implementation Plan — Uptime-Timestamps & Echte OBD-Boardspannung (Zündung-Offline-Modus)

Dieses Dokument beschreibt die geplante Änderung zur Einführung von lesbaren Uptime-Timestamps (`[Up HH:MM:SS]`) vor der NTP-Synchronisation sowie die Abschaffung von gemockten Telemetriedaten zugunsten echter Boardspannungsdaten (`AT RV`) und leeren Werten (`null`) für inaktive CAN-Sensoren.

---

## 1. Motivation

*   **Timestamps**: Rohe Millisekunden seit Boot (`[T+479432]`) sind schwer lesbar. Ein Uptime-Format `[Up 00:07:59]` erhöht die Diagnosequalität drastisch.
*   **Reale Daten**: Der Proxy soll im Standby-AP-Modus keine gefälschten Daten (wie SoC 82.5 %) publizieren. Er soll stattdessen die echte Versorgungsspannung des Dongles (`AT RV`, z.B. 15.6V) auslesen und alle fahrzeugspezifischen CAN-Werte als ungültig (`null`/`NAN`) an Home Assistant senden.

---

## 2. Geplante Änderungen

### 2.1. Uptime-Timestamps (`logger.cpp`)
- Einführung von `formatUptime()`: Rechnet `millis()` in `[Up HH:MM:SS]` um.
- Update von `getLogTimePrefix()` und `logBootSequence()`, um das lesbare Format zu verwenden.

### 2.2. Echte Spannungsabfrage & `NAN`-Fallbacks (`OBDManager.cpp` & `main.cpp`)
- **`OBDManager.cpp`**:
  - Globale Variable `float lastRealVoltage = 0.0f;` definieren.
  - In `connectOBD()` nach dem ELM327-Handshake direkt `AT RV` senden und `lastRealVoltage` aktualisieren.
  - Funktion `float getLatestRealVoltage()` exportieren.
- **`main.cpp`**:
  - Neue Funktion `generateEmptyRealTelemetry(TelemetryData& data)` implementieren.
  - Setzt `data.volt` auf `getLatestRealVoltage()` und alle anderen numerischen Werte auf `NAN`.
  - Ersatz von `generateSimulatedTelemetry` in `fetchOBDMetrics` bei inaktivem OBD.

---

## 3. Verifikationsplan

### 3.1. Automatisierte Tests
- Nativer Build und Unit-Tests in WSL2: `pio test -e native`

### 3.2. Manuelle Verifikation
- Flashen der Version `2.3.3-uptime-real-volt` via USB.
- Prüfung der Uptime-Meldungen im Log.
- Prüfung der MQTT-Datenübertragung (Spannung vorhanden, CAN-Sensoren leer).
