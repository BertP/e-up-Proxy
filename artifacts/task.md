# Tasks — Korrektur der e-up! UDS-DIDs & Formeln (v2.4.1)

- [ ] Version in `include/version.h` auf `2.4.1-uds-did-fix` anheben
- [ ] OBDManager DIDs und equations korrigieren
  - [ ] SoC (`02 8C`) in `queryGroupA` (Skalierung verifizieren/anpassen)
  - [ ] Temperatur von `11 62` auf `2A 0B` in `queryGroupA` umstellen
  - [ ] `queryUDS2Bytes` so anpassen, dass es den Wert als vorzeichenbehafteten `int16_t` parst (für Minustemperaturen im Winter)
- [ ] Test-Mocks anpassen
  - [ ] `test/mocks/WiFiClient.h` auf DID `2A 0B` mit `temp * 64` Formel anpassen
  - [ ] `test/test_obd.cpp` Vorkommen von `11 62` durch `2A 0B` ersetzen
- [ ] Verifikation & Testen
  - [ ] Native Unit-Tests ausführen (`pio test -e native`) und korrigieren
  - [ ] ESP32 Firmware compilieren (`pio run -e usb` und `esp32dev`)
  - [ ] Git commit & push
  - [ ] OTA Flash auf ESP32 im Auto via Windows PowerShell espota.py
