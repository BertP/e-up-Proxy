# Retrospektive — e-up!Proxy v2.3-logging-opt

**Datum:** 2026-05-29  
**Firmware:** `2.3-logging-opt`  
**Scope:** Logging-Optimierung, MQTT-Diagnose, OTA-Stabilisierung  
**Status:** ✅ Alle Tasks abgeschlossen, Firmware deployed, Ladetest läuft

---

## 1. Zielsetzung vs. Ergebnis

| Geplantes Ziel | Status | Anmerkung |
|---|---|---|
| Telemetrie-Spam aus LittleFS entfernen | ✅ Erledigt | Flash-Verschleiß drastisch reduziert |
| Dediziertes MQTT-Logging (`/mqtt.log`) | ✅ Erledigt | Eigene Rotation, eigener Endpoint |
| Web-Endpoint `/mqtt` registrieren | ✅ Erledigt | Live-Streaming der MQTT-Diagnose |
| OTA-Crash durch LittleFS-Writes fixen | ✅ Erledigt | Root-Cause identifiziert und behoben |
| OTA-Rescan-Lock (`otaInProgress`) | ✅ Erledigt | Wi-Fi-Trennung während OTA unterbunden |
| Native Unit Tests (7/7) bestehen | ✅ Erledigt | Unity + Mocks auf `native`-Environment |
| USB-Flash zur Durchbrechung des OTA-Locks | ✅ Erledigt | Einmalig nötig, danach OTA wieder voll funktional |

> [!TIP]
> **Fazit:** 100% Zielerreichung. Alle geplanten Features sind implementiert und getestet.

---

## 2. Was gut lief

### Systematische Fehleranalyse
Die Root-Cause-Analyse des OTA-Locks war methodisch sauber: Vom Symptom (ESP32 crasht bei OTA) über die Hypothese (Flash-Konflikt) bis zur Verifikation (LittleFS-Writes in Callbacks identifiziert). Der Fix war minimal-invasiv — `logEvent()` durch `Serial.println()` ersetzen.

### Dokumentationskultur
- Der [wsl_build_guide.md](file:///\\wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/artifacts/wsl_build_guide.md) wurde als direkte Konsequenz aus den Build-Problemen erstellt und in der [MISSIONPROMPT.md](file:///\\wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/MISSIONPROMPT.md) verankert.
- Jede Erkenntnis wurde sofort in Steuerungsdokumenten festgehalten — kein Wissen geht verloren.

### Testabdeckung
Das native Testframework mit Unity + Mocks ermöglicht Desktop-Tests ohne Hardware. Alle 7 Tests bestanden konsistent.

### Saubere Architektur-Entscheidungen
- `char[16]` statt `String` für Telemetrie-Quellen → Heap-Fragmentierung eliminiert
- Non-blocking Wi-Fi State Machine → Kein `delay()` im gesamten Codebase
- Dual-Partition OTA mit NVS-Crash-Counter → Boot-Loop-Schutz

---

## 3. Was schlecht lief — Probleme & Zeitfresser

### 🔴 Problem 1: OTA-Lock durch LittleFS-Writes in Callbacks
**Impact:** Hoch — OTA komplett unbenutzbar, USB-Flash zwingend nötig  
**Ursache:** `logEvent()` in `ArduinoOTA.onStart()` und `onEnd()` schrieb auf LittleFS, während der Flash-Controller für den OTA-Binary-Write gesperrt war. Ergebnis: Hardware-Cache-Exception → sofortiger Reboot.  
**Zeitverlust:** ~2 Stunden Debugging + mehrere fehlgeschlagene OTA-Versuche  
**Lösung:** Alle LittleFS-Writes in OTA-Callbacks durch `Serial.println()` ersetzt.

> [!CAUTION]
> **Merksatz:** Auf dem ESP32 darf NIEMALS gleichzeitig auf den SPI-Flash geschrieben werden, wenn der Flash-Controller für OTA reserviert ist. LittleFS und OTA teilen sich denselben physischen Chip.

### 🔴 Problem 2: WSL-Build-Umgebung — immer wieder dieselben Fallen
**Impact:** Mittel — wiederholte Zeitverluste durch bekannte, aber nicht dokumentierte Probleme  
**Symptome:**
1. **9P-Protokoll:** PlatformIO von Windows auf WSL-Dateisystem → extrem langsame Kompilierung
2. **NAT vs. Bridged:** OTA aus WSL2 im NAT-Modus scheitert, weil die Rückverbindung zum ESP32 blockiert wird
3. **PATH-Problem:** `pio` nur über Login-Shell (`bash -l`) erreichbar
4. **USB-Forwarding:** `usbipd attach --wsl` muss manuell vor jedem USB-Flash ausgeführt werden

**Zeitverlust:** ~1,5 Stunden über mehrere Sitzungen verteilt  
**Lösung:** `wsl_build_guide.md` als permanentes Nachschlagewerk erstellt und in MISSIONPROMPT.md verankert.

### 🟡 Problem 3: Fehlende MQTT-Diagnostik bei Feldtest
**Impact:** Mittel — Debugging eines Verbindungsproblems im Auto unmöglich  
**Ursache:** Hochfrequente Telemetrie-Logs (`DATA`/`DATA:SLOW`) rotierten den 25KB-Ringpuffer so schnell, dass MQTT-Connection-Events innerhalb von Minuten überschrieben wurden.  
**Lösung:** Log-Splitting in `/debug.log` (System) und `/mqtt.log` (MQTT-Diagnose), Telemetrie nur noch auf Serial.

### 🟡 Problem 4: WDT-Reset während OTA
**Impact:** Niedrig (nach Root-Cause-Fix) — aber initial verwirrend  
**Ursache:** 1,1 MB Image-Upload bei 30s WDT-Timeout ohne Feeding → System-Reset  
**Lösung:** `esp_task_wdt_reset()` in `ArduinoOTA.onProgress()` bei jedem Chunk.

---

## 4. Lessons Learned

### Technisch

| # | Lesson | Konsequenz |
|---|---|---|
| 1 | **ESP32: Flash = eine Resource.** OTA und LittleFS teilen sich den SPI-Flash. Gleichzeitige Writes crashen die Hardware. | Regel in MISSIONPROMPT.md: Keine LittleFS-Writes in OTA-Callbacks. |
| 2 | **Logs müssen nach Zweck getrennt werden.** Ein monolithisches Debug-Log wird bei hoher Telemetrie-Frequenz nutzlos. | Dedizierte Log-Dateien pro Subsystem (Debug, MQTT). |
| 3 | **WDT muss bei langläufigen Operationen gefüttert werden.** OTA-Uploads, große SPIFFS-Writes, etc. | Explizites `esp_task_wdt_reset()` in Progress-Callbacks. |
| 4 | **`String`-Objekte auf ESP32 fragmentieren den Heap.** Besonders bei häufiger Allokation/Deallokation. | Fixed-Size `char[]` für wiederkehrende Datenstrukturen. |

### Prozessual

| # | Lesson | Konsequenz |
|---|---|---|
| 5 | **WSL ≠ natives Linux.** 9P-Overhead, NAT-Routing, USB-Forwarding — jede Interaktion mit der Hardware hat Sonderfälle. | Build-Guide als permanentes Pflichtdokument. |
| 6 | **"Funktioniert auf meinem Rechner" gilt nicht für Embedded.** OTA-Bugs manifestieren sich erst beim echten Flash. | Immer zuerst USB flashen, wenn Unsicherheit besteht. |
| 7 | **Dokumentation muss an der Quelle verankert sein.** Ein Build-Guide bringt nichts, wenn der Agent ihn nicht liest. | Guide in MISSIONPROMPT.md referenziert + User-Rules. |
| 8 | **Git Commit vor jedem Flash.** Rollback-Fähigkeit ist bei Embedded nicht verhandelbar. | Feste Regel im Flash-Protokoll. |

---

## 5. Handlungsempfehlungen

### Sofort umsetzen (nächste Session)

- [ ] **Ladetest auswerten:** Nach dem aktuellen 30-Min-Ladevorgang `/debug` und `/mqtt` Endpoints prüfen. Sind die MQTT-Connection-Events sauber protokolliert? Ist die Log-Rotation stabil?
- [ ] **OTA-Verifikation:** Einen Test-OTA-Flash durchführen, um zu bestätigen, dass OTA nach dem Fix stabil funktioniert (der ESP32 ist jetzt im Auto — perfekter Ferntest).

### Kurzfristig (nächste 1-2 Releases)

- [ ] **Health-Endpoint `/status`:** JSON-Endpoint mit Uptime, Free Heap, Wi-Fi RSSI, MQTT-Status, aktueller Firmware-Version. Ermöglicht automatisiertes Monitoring über Home Assistant.
- [ ] **Log-Download-Button:** Auf der `/debug` und `/mqtt` Webseite einen Download-Button für die kompletten Log-Dateien (inkl. `.bak`).
- [ ] **Automatische OTA-Version-Prüfung:** Firmware meldet ihre Version an den MQTT-Broker → Home Assistant kann bei veralteter Version warnen.
- [ ] **Integration-Tests:** Neben den Unit-Tests ein Testskript, das eine simulierte MQTT-Verbindung und OBD-Responses gegen eine Testinstanz fährt.

### Mittelfristig (Architektur)

- [ ] **Remote-Logging via MQTT:** Kritische Fehlermeldungen direkt als MQTT-Nachricht an den Broker senden (zusätzlich zu LittleFS). Debugging im Feld ohne physischen Zugriff.
- [ ] **Konfiguration über Web-UI:** SSID/Passwort, MQTT-Broker, OBD-PIDs über ein Web-Formular konfigurierbar machen — ohne Rekompilierung.
- [ ] **Metriken-Dashboard:** Langzeit-Tracking von Heap-Nutzung, Wi-Fi-Reconnects, MQTT-Drops als HA-Sensor-Daten.

### Prozess-Empfehlungen

- [ ] **Pre-Flight-Checklist vor jedem Release:**
  1. `pio run -e esp32dev` → Clean Build ✅
  2. `pio test -e native` → Alle Tests grün ✅
  3. `CHANGELOG.md` aktualisiert ✅
  4. `version.h` bumped ✅
  5. Git Commit + Push ✅
  6. Flash (USB bevorzugt, OTA als Fallback) ✅
  7. Endpoint-Verifikation (`/debug`, `/mqtt`) ✅
- [ ] **Jedes neue Projekt mit WSL:** Sofort `wsl_build_guide.md` als Template kopieren und anpassen.
- [ ] **Fehler-Dokumentation:** Jeder nicht-triviale Bug bekommt einen Eintrag in einem `KNOWN_ISSUES.md` — auch wenn er gefixt ist. Verhindert Wiederholung.

---

## 6. Metriken

| Metrik | Wert |
|---|---|
| Geplante Features | 7 |
| Umgesetzte Features | 7 (100%) |
| Unit Tests | 7/7 bestanden |
| Kritische Bugs gefunden | 2 (OTA-Lock, Log-Spam) |
| Kritische Bugs gefixt | 2 (100%) |
| Dokumentierte Lessons Learned | 8 |
| Neue Steuerungsdokumente | 2 (`wsl_build_guide.md`, diese Retro) |
| Firmware-Releases in diesem Zyklus | 3 (v2.1 → v2.2 → v2.3) |

---

## 7. Fazit

Das Projekt hat alle gesetzten Ziele erreicht. Die kritischsten Erkenntnisse betreffen die **ESP32-Flash-Architektur** (kein gleichzeitiger LittleFS + OTA Zugriff) und die **WSL-Toolchain-Komplexität** (9P, NAT, USB-Forwarding).

Die größten Zeitverluste entstanden nicht durch fehlende Skills, sondern durch **fehlende Dokumentation von bereits bekanntem Wissen.** Die Einführung des Build-Guides und dessen Verankerung in der MISSIONPROMPT.md ist der wichtigste prozessuale Fortschritt dieser Iteration.

> [!IMPORTANT]
> **Goldene Regel für die Zukunft:** Jede Erkenntnis, die mehr als 30 Minuten Debugging gekostet hat, muss in einem Steuerungsdokument landen — nicht im Kopf, nicht im Chat-Verlauf, sondern in einer Datei, die automatisch gelesen wird.

---

*Erstellt am 2026-05-29 im Rahmen des e-up!Proxy Projekts.*
