# Tasks — Dynamische Gateway-IP-Auflösung

- [x] Unit-Test-Mocks anpassen
  - [x] `IPAddress` und `gatewayIP()` in `test/mocks/WiFi.h` implementieren
  - [x] `connect(IPAddress, uint16_t)` Überladung in `test/mocks/WiFiClient.h` implementieren
- [x] Dynamischen Verbindungsaufbau implementieren
  - [x] `connectOBD()` in `src/OBDManager.cpp` auf `WiFi.gatewayIP()` umstellen
  - [x] `disconnectOBD()` in `src/OBDManager.cpp` anpassen (Logging)
- [x] Verifikation & Testen
  - [x] Native Unit-Tests lokal ausführen und verifizieren (`pio test -e native`)
  - [x] ESP32 Firmware bauen (`pio run -e usb`)
  - [x] ESP32 flashen und serielles Log prüfen
