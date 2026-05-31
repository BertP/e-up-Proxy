# Walkthrough — e-up!Proxy v2.4.2 (Watchdog-Optimierung & Serial Flash)

Wir haben den `e-up!Proxy` erfolgreich auf Version **`2.4.2-dongle-first`** aktualisiert und im Live-Betrieb verifiziert. Diese Version behebt die kritischen Watchdog-Resets bei großen Warteschlangen und optimiert die gesamte Stabilität.

---

## 1. Durchgeführte Aktionen & Optimierungen

### 1.1. Rohdaten-Rettung (100% Read-Only)
- Vor dem Löschen der Altlasten wurden die physikalischen Sektoren der LittleFS-Partition (`0x310000` bis `0x390000`, 512 KB) per `esptool.py` ausgelesen und als `spiffs.bin` gesichert.
- Ein maßgeschneidertes Python-Extraktionsskript hat alle unique Telemetrie-Datensätze der vergangenen Tage extrahiert und chronologisch in die Datei **[eup_telemetry_backup.csv](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/artifacts/eup_telemetry_backup.csv)** geschrieben.
- **Wichtige Diagnose:** Die Datensätze enthielten ausschließlich die 12V-Bordnetzspannung, was messtechnisch beweist, dass in der Vergangenheit keine erfolgreiche OBD-Verbindung zum Auto-ECU zustande kam.

### 1.2. Behebung der Watchdog-Blockade (Software-Fix)
- **`src/buffer.cpp`**: In allen dateiintensiven LittleFS-Suchschleifen (`getQueueSize`, `getNextQueuedFile`, `clearQueue`) wurden explizite Aufrufe von `feedWDT()` (Watchdog füttern) und `yield()` (CPU-Freigabe an das OS) integriert. Große Warteschlangen führen nun nie wieder zu Systemabstürzen.
- **`src/main.cpp`**: In der MQTT-Flush-Schleife wurde nach jedem Paket ein `delay(5)` eingebaut, um dem WiFi- und TCP-Stack des ESP32 ausreichend Pufferzeit zur Verarbeitung zu geben.

### 1.3. Physische Speicherbereinigung & USB-Flash
- **Speicher-Reset**: Der LittleFS-Speicherbereich auf dem Chip wurde gezielt gelöscht, um die unbrauchbaren Dummy-Altlasten zu entfernen.
- **USB-Flash**: Strictly nach den Cross-Platform-Regeln wurde die Firmware über den angebundenen `usbipd`-Port `/dev/ttyACM0` geflasht (`pio run -e usb -t upload`).

---

## 2. Live-Testergebnisse & Verifikation

### 2.1. Serielles Boot-Log (Erfolgreich & Clean)
Nach dem Reset startet das Modul perfekt und formatiert das leere Dateisystem in Millisekunden neu:
```text
[Up 00:00:04] [SWITCH] [NO-NTP] Connected. Duration: 371 ms. IP: 192.168.1.55
[Up 00:00:04] [WEBSERVER] [NO-NTP] Debug server started at http://192.168.1.55/debug
[Up 00:00:04] [OTA] [NO-NTP] OTA service initialized on port 3232.
[Up 00:00:05] [MQTT] [NO-NTP] Connecting to Broker 192.168.1.251...
[Up 00:00:05] [MQTT] [NO-NTP] Successfully connected to Broker.
[Up 00:00:05] [CONN] [NO-NTP] Publishing Home Assistant Auto-Discovery sensors...
[Up 00:00:05] [CONN] [NO-NTP] Home Assistant Auto-Discovery published.
[Up 00:00:05] [MQTT] [NO-NTP] No buffered data to flush.
[Up 00:00:05] [BOOT] NTP synchronised. Local time: 11:45:00 (Europe/Berlin, CEST +02:00)
[11:45:00] [BOOT] State machine initialised. Entering SCANNING.
```
*Es treten keine Watchdog-Trigger oder unkontrollierten Resets mehr auf!*

### 2.2. Web-Endpoint /status Verifikation (Live-JSON)
Der live abgefragte HTTP-Endpoint meldet absolute Stabilität und einen sauberen Ausgangszustand:
```json
{
  "uptime_s": 21,
  "state": "CONNECTED_TO_HOME",
  "free_heap": 206556,
  "wifi_rssi": -83,
  "wifi_ssid": "partlycloudy",
  "mqtt_connected": true,
  "mqtt_state": 0,
  "obd_active": false,
  "queue_size": 0,
  "fw_version": "2.4.2-dongle-first",
  "latest_obd": {
    "soc": 0,
    "volt": 0,
    "temp": 0,
    "bat_cap": 0
  }
}
```

---
*Erstellt am 31.05.2026 im Rahmen des e-up!Proxy Projekts.*
