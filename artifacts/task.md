# Tasks — Dediziertes OBD-Logging & Session-Statistiken

- [ ] Implementierung der neuen Log-Logik in `logger.h` und `logger.cpp`
  - [ ] Deklaration/Definition von `logObdEvent`
  - [ ] Eigene Log-Rotation für `/obd.log` und `/obd.bak.log` (25 KB)
  - [ ] Erstellung der Live-Streaming-Logik `streamObdLog`
  - [ ] Integration in `clearLog` zum Purgen
- [ ] Integration der Session-Statistiken in `main.cpp`
  - [ ] Globale Session-Zähler deklarieren
  - [ ] Reset bei erfolgreichem `connectOBD()`
  - [ ] Inkrementieren bei OBD-Abfragen (Gruppe A & B)
  - [ ] Ausgabe der Session-Dauer und Zähler bei `disconnectOBD()`
  - [ ] Registrieren des `/obd` Web-Endpoints zum Streamen
- [ ] Code-Verifikation
  - [ ] Clean Compile via WSL (`pio run -e esp32dev`)
  - [ ] Native Unit-Tests ausführen (`pio test -e native`)
- [ ] Deployment & Live-Test
  - [ ] Flash via OTA (`pio run -t upload -e esp32dev`)
  - [ ] Test des `/obd` Endpoints im Browser/per Curl
