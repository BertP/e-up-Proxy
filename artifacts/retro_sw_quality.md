# Retro: Software-Qualität, Wartbarkeit, Struktur & Testtiefe

**Datum:** 2026-05-21  
**Scope:** Gesamte Codebasis `e-up!Proxy` — 8 Dateien, ~1.480 LOC

---

## Gesamturteil

| Dimension | Note | Kommentar |
|---|---|---|
| **Funktionalität** | 🟢 B+ | State-Machine, OBD-Abfragen, MQTT-Flush, Logging — alles da, alles kompiliert |
| **Struktur / Architektur** | 🟡 C | Modulschnitt erkennbar, aber `main.cpp` ist ein 713-Zeilen-Monolith |
| **Wartbarkeit** | 🟠 C− | Globaler Mutable State überall, Kopplungen quer durch Module |
| **Robustheit** | 🟠 C− | Kein einziger automatisierter Test, mehrere ungesicherte Fehlerpfade |
| **Testtiefe** | 🔴 F | Null. Keine Unit-Tests, keine Integration-Tests, kein CI |
| **Konfigurationsmanagement** | 🔴 F | Credentials hardcoded, kein `.gitignore`, kein Git |

---

## 1. Architektur & Modulschnitt

### Was funktioniert
Die Aufteilung in vier Module ist erkennbar und nachvollziehbar:

| Modul | Verantwortung | LOC |
|---|---|---|
| [main.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp) | State-Machine, WiFi, MQTT, WebServer, Orchestrierung | 713 |
| [OBDManager.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/OBDManager.cpp) | ELM327/UDS-Protokoll, Telemetrie-Abfragen | 327 |
| [logger.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/logger.cpp) | LittleFS-Logging, Log-Rotation, WebServer-Streaming | 184 |
| [buffer.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/buffer.cpp) | FIFO-Queue auf LittleFS | 172 |

### Was nicht funktioniert

> [!CAUTION]
> **`main.cpp` ist ein God Object.** Es vereinigt 7 verschiedene Verantwortlichkeiten in einer Datei: State-Machine, WiFi-Scanning, WiFi-Verbindungsaufbau, OBD-Orchestrierung, MQTT-Client, HA Auto-Discovery und WebServer-Setup. Ein Fehler in der MQTT-Logik erfordert Orientierung in einer 713-Zeilen-Datei, in der auch LED-Blinkcodes leben.

**Fehlende Abstraktionen:**

- **Kein WiFi-Manager-Modul:** Die gesamte Scan-Logik, Verbindungsaufbau, Timeout-Handling und SSID-Priorisierung steckt direkt in `handleScanning()` — eine 170-Zeilen-Funktion mit verschachtelten Schleifen, dynamischer Speicherallokation und blockierenden `delay(50)` Wartezyklen.
- **Kein MQTT-Modul:** `flushQueueToMQTT()` und `publishHAAutoDiscovery()` (zusammen ~120 Zeilen) gehören in ein eigenständiges Modul. Die HA-Discovery-Payloads sind fest einprogrammiert statt deklarativ beschrieben.
- **Kein State-Machine-Abstrahierung:** `transitionTo()` ist eine 60-Zeilen if/else-Kaskade, die Seiteneffekte für jeden Zustandsübergang inline ausführt (WiFi disconnect, OBD-Socket schließen, Timer zurücksetzen, MQTT-Flags flippen). Das ist weder testbar noch erweiterbar.

---

## 2. Globaler Zustand & Kopplung

> [!WARNING]
> **14 globale/statische Zustandsvariablen** in `main.cpp` steuern das gesamte Systemverhalten. Keine davon ist in einer Struktur gebündelt oder hat definierte Invarianten.

```cpp
currentState, obdActive, isScanningActive, webServerRunning, 
mqttFlushDone, timeSyncDone, stateMachineInitLogged, lastScanFailed, 
isWebServerStarted, wicanConnectionTimer, lastOBDReconnectAttempt,
lastStateChange, lastLEDUpdate, lastTelemetryFetch, lastSlowTelemetryFetch, 
lastHomeRescanCheck, scanStartTime
```

**Probleme:**
- **Unsichtbare Abhängigkeiten:** `transitionTo()` setzt `obdActive`, `isScanningActive`, `webServerRunning`, `mqttFlushDone`, `lastScanFailed` in einem Rutsch. Wer welche Variable wann zurücksetzt, ist nur durch Lesen des gesamten Codes zu verstehen.
- **Flag-Spaghetti:** `stateMachineInitLogged` wird sowohl in `handleHome()` (Zeile 644) als auch in `loop()` (Zeile 694) gesetzt — zwei verschiedene Codepfade für die gleiche Entscheidung. Das ist ein Wartbarkeits-Albtraum.
- **Kein Lifecycle-Konzept:** `timeSyncDone` überlebt bewusst Netzwerkwechsel (Fix für den NTP-Bootlog-Bug), aber `mqttFlushDone` wird bei jedem Home-Eintritt zurückgesetzt. Welche Flags welchen Lifecycle haben, ist nirgends dokumentiert.

---

## 3. Konkrete Code-Smells

### 3.1 Blockierende Wartezyklen trotz „Non-Blocking"-Anspruch

> [!IMPORTANT]
> Das MISSIONPROMPT verbietet explizit `delay()`. Die Codebasis enthält **mindestens 6 Stellen** mit blockierenden Warteschleifen.

| Datei | Zeile(n) | Problem |
|---|---|---|
| [main.cpp:289–293](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L289-L293) | 289–293 | `while + delay(50)` beim Wican-Connect — blockiert bis zu 10 Sekunden |
| [main.cpp:312–316](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L312-L316) | 312–316 | `while + delay(50)` beim Home-Connect — blockiert bis zu 15 Sekunden |
| [main.cpp:335–339](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L335-L339) | 335–339 | `while + delay(50)` 30-Sekunden-Pause bei gescheitertem Scan |
| [main.cpp:494](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L494) | 494 | `delay(20)` in HA-Discovery-Schleife |
| [main.cpp:517](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L517) | 517 | `delay(20)` nach LastSync-Discovery |
| [main.cpp:711](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L711) | 711 | `delay(1)` in `loop()` — harmlos, aber unnötig |

Die blockierenden WiFi-Connects (10s, 15s, 30s) sind besonders kritisch: Während dieser Zeit werden zwar LED und WDT bedient, aber der WebServer ist eingefroren, und jeder andere State-Handler ist blockiert.

### 3.2 Wiederholtes `initBuffer()` / `LittleFS.begin()`

[buffer.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/buffer.cpp) ruft `initBuffer()` (und damit `LittleFS.begin(true)`) bei **jedem** `enqueueData()`, `getNextQueuedFile()`, `getQueueSize()`, und `clearQueue()` Aufruf auf. `LittleFS.begin()` ist idempotent, aber das wiederholte Aufrufen maskiert die Frage: „Ist das Filesystem überhaupt gemountet?" Es ist defensives Programmieren an der falschen Stelle.

### 3.3 Heap-Fragmentierung durch String-Operationen

Die gesamte Codebasis nutzt exzessiv `Arduino String` für:
- Log-Formatierung ([logger.cpp:93–102](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/logger.cpp#L93-L102))
- OBD-Response-Parsing ([OBDManager.cpp:10–18](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/OBDManager.cpp#L10-L18))
- MQTT-Payload-Konstruktion ([main.cpp:559–574](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L559-L574))

Auf einem ESP32 mit 320 KB RAM und 14,6% Auslastung ist das aktuell nicht kritisch, aber `stripWhitespace()` baut z.B. zeichenweise einen neuen String auf — klassischer O(n²)-Heap-Fragmentierer.

### 3.4 Fehlendes Error-Recovery in der Queue

- [getNextQueuedFile()](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/buffer.cpp#L61-L113) löscht korrupte JSON-Dateien automatisch (Zeile 85), loggt den Fehler aber nur einmal und geht weiter. Wenn LittleFS-Corruption systemisch ist (z.B. durch Brownout beim Schreiben), wird die gesamte Queue still gelöscht.
- `enqueueData()` loggt einen Fehler bei Write-Failure, gibt `false` zurück, aber kein Aufrufer prüft den Rückgabewert ([main.cpp:376](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L376), [main.cpp:399](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L399)).

### 3.5 Range-Berechnung dupliziert

Die Reichweiten-Ableitung (`range_coefficient` Logik) existiert **identisch** an zwei Stellen:
- [OBDManager.cpp:254–262](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/OBDManager.cpp#L254-L262) (in `queryGroupA`)
- [OBDManager.cpp:313–319](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/OBDManager.cpp#L313-L319) (in `generateSimulatedTelemetry`)

Klassischer DRY-Verstoß. Wenn die Formel geändert wird, muss man an beiden Stellen daran denken.

### 3.6 Hardcoded Firmware-Version

Die Firmware-Version `"2.1-ota-logfix"` taucht an **4 verschiedenen Stellen** auf:
- [logger.cpp:74](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/logger.cpp#L74) (Boot-Log)
- [main.cpp:487](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L487) (HA Discovery Sensor)
- [main.cpp:510](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/src/main.cpp#L510) (HA Discovery LastSync)

Keine zentrale `FW_VERSION`-Konstante in `config.h`.

---

## 4. Sicherheit & Credentials

> [!CAUTION]
> **Alle Credentials stehen im Klartext in [config.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up%21Proxy/include/config.h):**
> - WiFi-Passwörter (Zeile 7, 11, 14)
> - MQTT-Passwort (Zeile 20)
> 
> Es gibt **kein Git-Repository** (`fatal: not a git repository`), also auch keinen `.gitignore` und keine History. Wenn dieses Projekt jemals in ein Repo wandert, sind die Credentials sofort im Commit-Log.

Für ein Home-Projekt auf einem ESP32 ist Klartext in `config.h` pragmatisch akzeptabel — aber es sollte dokumentiert sein, und die Datei sollte in einem zukünftigen `.gitignore` stehen.

---

## 5. Testtiefe — Ist-Zustand

### Automatisierte Tests: **Keine.**

- Kein `test/` Verzeichnis
- Keine PlatformIO Test-Konfiguration (`test_port`, `test_framework` fehlen in `platformio.ini`)
- Keine Mocks für WiFi, LittleFS, TCP-Socket oder MQTT
- Keine Assertion-Library eingebunden

### Validierung bisher:

| Methode | Was wird geprüft | Tiefe |
|---|---|---|
| `pio run` (Compiler) | Syntaxfehler, Typ-Inkompatibilitäten | Minimal — prüft nur ob es kompiliert |
| Manueller Flash + Serial Monitor | Happy Path bei realer Hardware | Anekdotisch — kein strukturierter Testplan |
| `/debug` Endpunkt | Log-Ausgabe visuell kontrollieren | Manuell, nicht reproduzierbar |

> [!IMPORTANT]
> **Es gibt keine einzige automatisierte Prüfung**, die sicherstellt, dass:
> - Die State-Machine bei einem Transition-Aufruf den richtigen Zustand erreicht
> - Ein UDS-Response korrekt geparst wird (z.B. SoC=82.5% aus Hex `CE`)
> - Die Queue bei korrupten Dateien nicht die gesamte Buffer-Queue löscht
> - Die Log-Rotation bei exakt 25.600 Bytes auslöst
> - Die HA-Discovery-Payloads valides JSON produzieren

---

## 6. Was **gut** ist (Fair bleiben)

- **OBDManager ist sauber isoliert.** Klare API (`connectOBD`, `queryGroupA/B`, `disconnectOBD`), Header-Caching, UDS-Session-Handling — das ist solide embedded Arbeit.
- **Log-Rotation funktioniert.** Zwei-Datei-Rotation mit Backup und automatischem Cleanup ist ein pragmatisch guter Ansatz für einen ESP32 mit begrenztem Flash.
- **Spec-Konformität ist hoch.** Die Log-Formate, MQTT-Payloads und HA-Discovery-Topics entsprechen exakt der Spezifikation. Das zeigt Disziplin.
- **Watchdog ist aktiv.** `feedWDT()` wird an allen kritischen Stellen aufgerufen. ESP-IDF v3/v5-Kompatibilität ist sauber gelöst.
- **Queue-Design ist robust.** Ein File pro Payload mit Timestamp-basiertem Dateinamen ist ein guter Trade-off für LittleFS. Korrupte Dateien werden erkannt und entfernt.

---

## 7. Priorisierte Empfehlungen

### Priorität 1 — Sofort (vor dem nächsten Feature)

| # | Maßnahme | Aufwand | Wirkung |
|---|---|---|---|
| 1.1 | **Git initialisieren** mit `.gitignore` (config.h, .pio/) | 10 min | Versionskontrolle, Credential-Schutz |
| 1.2 | **`FW_VERSION` zentralisieren** in `config.h`, alle 4 Stellen ersetzen | 15 min | DRY, Release-Tracking |
| 1.3 | **Range-Berechnung** in eine eigene Funktion extrahieren | 15 min | DRY-Verstoß beseitigen |

### Priorität 2 — Nächster Refactoring-Sprint

| # | Maßnahme | Aufwand | Wirkung |
|---|---|---|---|
| 2.1 | **`main.cpp` aufbrechen:** WiFiManager, MQTTManager, StateMachine als eigene Module | 3–4h | Wartbarkeit, Testbarkeit |
| 2.2 | **Blocking WiFi-Connects** durch non-blocking State-basierte Verbindung ersetzen | 2h | Echtes non-blocking System |
| 2.3 | **State-Context-Struct** einführen: alle 14+ globale Variablen in `struct ProxyContext` bündeln | 1h | Lesbarkeit, klare Lifecycle-Zuordnung |
| 2.4 | **`enqueueData()`-Rückgabewert prüfen** an allen Aufrufstellen | 15 min | Silent Data Loss verhindern |

### Priorität 3 — Testinfrastruktur aufbauen

| # | Maßnahme | Aufwand | Wirkung |
|---|---|---|---|
| 3.1 | **PlatformIO native Unit Tests** für OBDManager (UDS-Response-Parsing) | 2h | Härtester Parsing-Code abgesichert |
| 3.2 | **Unit Tests für Buffer** (Enqueue/Dequeue/Corrupt-File-Handling) | 1.5h | Datenintegrität validiert |
| 3.3 | **Unit Tests für Logger** (Rotation-Trigger, Prefix-Logik) | 1h | Format-Konformität automatisiert |
| 3.4 | **State-Machine Transition-Tests** (nach Refactoring 2.1) | 2h | Regressionsschutz für Kernlogik |

---

## 8. Fazit

Die Firmware **funktioniert** — sie kompiliert, sie flasht, sie macht das was die Spec sagt. Für ein Home-Projekt auf einem ESP32 ist das ein solider Stand.

Aber es ist **Prototyp-Qualität**, kein wartbares Produkt. Der gesamte Stabilitäts-Sprint der heutigen Session (WebServer-Crash, NTP-Loop, Scan-Spam) waren Symptome der gleichen Grundursache: Eine monolithische `main.cpp` mit 14 globalen Flags, in der niemand mehr vorhersagen kann, welche Seiteneffekte ein Zustandswechsel auslöst.

Die kritischste Lücke ist die **Testtiefe von exakt null**. Jede Änderung am UDS-Parsing, an der State-Machine oder an der Queue-Logik kann nur durch manuelles Flashen und Beobachten validiert werden. Das ist bei einem System, das unbeaufsichtigt im Auto läuft, ein echtes Risiko.

Die gute Nachricht: Das Projekt ist mit ~1.500 LOC klein genug, dass ein gezielter Refactoring-Sprint (Prio 2) und eine Basis-Testsuite (Prio 3) in einem Wochenende realisierbar wären. Der OBDManager zeigt schon, wie ein gut geschnittenes Modul aussieht — das Muster muss nur auf den Rest angewendet werden.
