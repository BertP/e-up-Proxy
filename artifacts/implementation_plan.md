# Implementation Plan — Dediziertes OBD-Logging & Statistik-Tracker

Dieses Dokument beschreibt die geplante Implementierung zur detaillierten Protokollierung der Verbindung zum OBD-Dongle (MeatPi WiCan) und der Auswertung der gelesenen Datensätze bei Disconnection.

---

## 1. Problemstellung & Zielsetzung

Beim Feldtest (z.B. dem 30-minütigen Ladetest) war unklar, ob:
- Der ESP32 überhaupt jemals eine TCP-Verbindung zum WiCan-Dongle aufbauen konnte.
- Ob während einer bestehenden Verbindung Abfragen erfolgreich waren.
- Wie viele Datensätze tatsächlich bis zur Trennung geholt wurden.

**Ziel:** Ein getrenntes Log-System (`/obd.log` und Web-Endpoint `/obd`) implementieren, das genauestens Verbindungsausfälle, erfolgreiche TCP-Connects, Abfrage-Performance und gelesene Datensätze pro Session aufzeichnet.

---

## 2. Geplante Änderungen

### 2.1. Logger-Erweiterung (`logger.h` & `logger.cpp`)
- Einführung von `logObdEvent(const String& level, const String& message)` und `logObdEvent(const String& message)`.
- Speicherung in `/obd.log` (mit Rotation zu `/obd.bak.log` bei 25 KB Limit, analog zu `/mqtt.log`).
- Erstellung einer Streaming-Funktion `streamObdLog(WebServer& server)`.
- Integration von `/obd.log` and `/obd.bak.log` in `clearLog()`.

### 2.2. WebServer-Erweiterung (`main.cpp`)
- Registrierung des HTTP GET Endpoints `/obd`.
- Bei Aufruf von `http://192.168.1.55/obd` wird das komplette OBD-Diagnose-Protokoll live gestreamt.

### 2.3. OBD Session-Statistik (`main.cpp`)
- Einführung globaler Zähler in `main.cpp`:
  - `uint32_t obdSessionSuccessCount = 0;` (Erfolgreich gelesene Gruppen-Datensätze in der aktuellen Session).
  - `uint32_t obdSessionFailCount = 0;` (Fehlgeschlagene UDS-Abfragen in der aktuellen Session).
  - `unsigned long obdSessionStartTime = 0;` (Uhrzeit/Millis des Connects).
- Bei **Verbindungsaufbau (`connectOBD() == true`)**:
  - Logge Connect im OBD-Log: `[CONN] Connected to OBD Gateway. Resetting session counters.`
  - Setze `obdSessionSuccessCount = 0`, `obdSessionFailCount = 0`, `obdSessionStartTime = millis()`.
- Bei **erfolgreicher Abfrage (`fetchOBDMetrics()`)**:
  - Inkrementiere `obdSessionSuccessCount` pro erfolgreichem Durchlauf (bzw. für jede erfolgreiche Abfrage).
- Bei **UDS-Abfragefehler**:
  - Inkrementiere `obdSessionFailCount`.
- Bei **Verbindungstrennung (`disconnectOBD()`)**:
  - Ermittle die Dauer der Session: `duration = (millis() - obdSessionStartTime) / 1000`.
  - Logge die Trennung im OBD-Log inklusive Statistik:
    ```text
    [DISCONN] OBD session closed. Duration: X seconds. Successfully read datasets: Y. Failed queries: Z.
    ```

---

## 3. Betroffene Dateien

### [MODIFY] [logger.h](file:///home/bert/projects/e-up!Proxy/include/logger.h)
- Deklaration von `logObdEvent(...)` und `streamObdLog(...)`.

### [MODIFY] [logger.cpp](file:///home/bert/projects/e-up!Proxy/src/logger.cpp)
- Definition von `logObdEvent(...)` und `streamObdLog(...)` mit eigener log rotation (`/obd.log`).
- Einbindung in `clearLog()`.

### [MODIFY] [main.cpp](file:///home/bert/projects/e-up!Proxy/src/main.cpp)
- Hinzufügen der Session-Variablen (`obdSessionSuccessCount`, etc.).
- Verknüpfung im State-Transition-Bereich und beim Aufruf von `connectOBD()` / `disconnectOBD()`.
- Registrierung des `/obd` Web-Endpoints.
- Logging der Session-Statistik bei Trennung.

---

## 4. Verifikationsplan

### 4.1. Automatisierte Tests
- Kompilierung des Codes nativ in WSL: `pio run -e esp32dev`
- Lokale Unit-Tests ausführen: `pio test -e native` (um sicherzustellen, dass keine Regressionen auftreten).

### 4.2. Manuelle Verifikation
- Flashen des ESP32 (bevorzugt über USB oder OTA, falls WLAN steht).
- Aufrufen des Endpoints `http://192.168.1.55/obd` im Browser oder per Curl.
- Simulation einer OBD-Verbindung/Trennung und Prüfung, ob das Log die exakten Zeitpunkte und Zähler sauber auflistet.
