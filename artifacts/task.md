# Tasks — Uptime-Timestamps & Echte OBD-Boardspannung

- [/] Code-Implementierung
  - [ ] `include/version.h` auf `2.3.3-uptime-real-volt` anheben
  - [ ] Uptime-Formatierung `[Up HH:MM:SS]` in `src/logger.cpp` einbauen
  - [ ] Spannungsabfrage und `getLatestRealVoltage()` in `src/OBDManager.cpp` integrieren
  - [ ] `generateEmptyRealTelemetry()` mit `NAN`-Werten in `src/main.cpp` umsetzen
- [ ] Verifikation & Testen
  - [ ] Native Unit-Tests lokal ausführen (`pio test -e native`)
  - [ ] ESP32 Firmware bauen & flashen (`pio run -e usb -t upload`)
  - [ ] Logs & MQTT-Verbindung prüfen
