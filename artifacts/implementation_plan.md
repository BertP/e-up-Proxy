# Implementation Plan — Korrektur der e-up! UDS-DIDs & Formeln (v2.4.1)

Dieses Dokument beschreibt die geplanten Code-Änderungen, um die fälschlicherweise generischen UDS-Parameter (DIDs) und Umrechnungsformeln für den **VW e-up!** (sowie SEAT Mii electric und Skoda Citigoᵉ iV) durch die verifizierten Community-Standards (GoingElectric / OVMS) zu ersetzen.

---

## 1. Problemstellung & Motivation

Im Feldtest antwortete das Steuergerät `7E5` (BMS) bei den DIDs `11 62` (Temperatur) und `22 E1` (Kapazität) mit der UDS-Fehlermeldung `7F 22 31` (Request Out Of Range). Zudem scheiterte die Abfrage von `02 8C` (SoC) an einer inkompatiblen Byte-Skalierungs-Formel.
* **Temperatur:** Die e-up! BMS-Temperatur liegt auf DID **`2A 0B`** (als vorzeichenbehafteter 2-Byte-Wert) und nicht auf `11 62`.
* **SoC:** Die BMS-SoC-Formel lautet `raw / 2.5` (was dem Wertebereich von `0 - 250` entspricht) statt `raw * 0.4` (OBD2-Standard), wodurch das Byte-Parsing-Verhalten korrigiert werden muss.
* **Odometer & Service:** Müssen robuster behandelt werden, da Steuergerät `7E0` (Dashboard/Gateway) bei Zündung-AUS schläft.

---

## 2. Geplante Änderungen

### 2.1. OBDManager DIDs & Formeln anpassen (`src/OBDManager.cpp`)
- **SoC (DID `02 8C`):** 
  - Die Formel in `queryGroupA()` wird auf `scale = 0.4f` (entspricht `1 / 2.5f`) und `offset = 0.0f` angepasst. (Da `02 8C` UDS-seitig ein Byte zurückgibt, bleibt es bei `queryUDS1Byte`).
- **Temperatur (DID `2A 0B`):**
  - Die Abfrage in `queryGroupA()` wird von `queryUDS1Byte("7E5", "11 62")` auf **`queryUDS2Bytes("7E5", "2A 0B")`** umgestellt.
  - Die Skalierung wird auf `1.0f / 64.0f` (entspricht `0.015625f`) und der Offset auf `0.0f` gesetzt. Da die Temperatur vorzeichenbehaftet sein kann (2-Byte signed int), passen wir `queryUDS2Bytes` so an, dass es den Wert als `int16_t` statt `uint16_t` parst, um Minusgrade im Winter korrekt darzustellen.
- **Kapazität (DID `22 E1`):**
  - Der e-up! hat für die absolute Batteriekapazität die DID **`22 E1`** auf `7E5` (2 Bytes). Die Formel ist `raw * 0.1` (in Ah). Dies ist bereits so implementiert und bleibt bestehen.

### 2.2. Signed 2-Byte UDS Parsing in `queryUDS2Bytes` (`src/OBDManager.cpp`)
Wir passen `queryUDS2Bytes` so an, dass das extrahierte Hex-Wort korrekt als vorzeichenbehafteter 16-Bit-Integer (`int16_t`) gecastet wird, bevor wir Skalierung und Offset anwenden. Dies ist kritisch für die korrekte Erfassung von Minustemperaturen im Winter über die neue 2-Byte-Temperatur-DID `2A 0B`.

```cpp
// In queryUDS2Bytes:
long rawVal = strtol(hexBytes.c_str(), NULL, 16);
int16_t signedVal = (int16_t)rawVal;
outVal = (signedVal * scale) + offset;
```

### 2.3. Test-Mocks anpassen (`test/mocks/WiFiClient.h`)
Damit die automatisierten Unit-Tests fehlerfrei laufen, passen wir den OBD-Simulator in den Mocks an die neuen DIDs und Formeln an:
- **SoC (`22 02 8C`):** Reagiert wie bisher (gibt `62 02 8C A6` zurück, was `166` bzw. `66.4%` entspricht).
- **Temperatur (`22 2A 0B`):** 
  - Ersetzt den alten Mock für `22 11 62`.
  - Berechnet `raw = temp * 64`.
  - Gibt die 2-Byte-Hex-Antwort `62 2A 0B <XXXX>` zurück.
- **Tests anpassen (`test/test_obd.cpp`):**
  - Ersetzen aller Vorkommen von `11 62` im Testcode durch `2A 0B`.

### 2.4. Firmware-Version bumps (`include/version.h`)
- Wir heben die Version auf **`2.4.1-uds-did-fix`** an.

---

## 3. Verifikationsplan

### 3.1. Automatisierte Tests
- Kompilieren und Ausführen der nativen Unit-Tests in WSL2:
  ```bash
  wsl bash -c "cd /home/bert/projects/e-up\!Proxy && /home/bert/.local/bin/pio test -e native"
  ```
  Alle 7 Tests müssen erfolgreich grün melden.

### 3.2. Firmware Build
- Kompilieren der Firmware für das USB-Target:
  ```bash
  wsl bash -c "cd /home/bert/projects/e-up\!Proxy && /home/bert/.local/bin/pio run -e usb"
  ```

### 3.3. Manuelle Verifikation im Auto
- Flashen der neuen Firmware via Windows-OTA-Skript oder USB.
- Nach dem Booten im Auto: Aufruf des Web-Endpoints `/obd` or `/status` bei **Zündung AN** (während Ladevorgang oder Fahrt):
  - Prüfen, ob SoC, Temperatur und Kapazität ohne NRC-Fehler `7F 22 31` gelesen werden und plausible Werte liefern.
