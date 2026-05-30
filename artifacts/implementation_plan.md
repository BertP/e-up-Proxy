# Implementation Plan — Dynamische Gateway-IP-Auflösung für OBD-Verbindung

Dieses Dokument beschreibt die geplante Änderung, um die IP-Adresse des MeatPi WiCAN-Dongles (Gateway) zur Laufzeit dynamisch über den DHCP-Client des ESP32 aufzulösen (`WiFi.gatewayIP()`), statt die IP-Adresse im Quellcode hartzucodieren.

---

## 1. Problemstellung & Motivation

Aktuell ist in `include/config.h` die Konstante `WICAN_IP` auf `"192.168.0.10"` (oder früher `"192.168.4.1"`) hartcodiert.
* **Problem**: Wenn der Dongle ein anderes Subnetz für seinen Access Point verwendet (oder der Benutzer das Subnetz ändert), kann sich der Proxy nicht mehr verbinden, obwohl er erfolgreich mit dem WLAN verbunden ist und vom Dongle eine IP-Adresse zugewiesen bekommen hat.
* **Lösung**: Da der WiCAN-Dongle im AP-Modus als DHCP-Server agiert, ist seine eigene IP-Adresse für alle verbundenen Clients (wie den ESP32) automatisch die Gateway-IP. Wir können diese dynamisch mit `WiFi.gatewayIP()` auslesen und für die TCP-Verbindung nutzen.

---

## 2. Geplante Änderungen

### 2.1. OBD-Verbindung dynamisieren (`OBDManager.cpp`)
- Wir ersetzen in `connectOBD()` und `disconnectOBD()` die Verwendung der hartcodierten Konstante `WICAN_IP` durch `WiFi.gatewayIP()`.
- Wir protokollieren die aufgelöste Gateway-IP beim Verbindungsaufbau im OBD-Log, um Klarheit bei der Diagnose zu haben.
- Code-Entwurf für `connectOBD()`:
  ```cpp
  IPAddress gateway = WiFi.gatewayIP();
  logObdEvent("CONN", "Connecting to OBD TCP Gateway " + gateway.toString() + ":" + String(WICAN_PORT) + "...");
  obdClient.setTimeout(3);

  if (!obdClient.connect(gateway, WICAN_PORT)) {
      logObdEvent("ERROR", "OBD TCP connection failed!");
      return false;
  }
  ```

### 2.3. Unit-Tests & Mocks anpassen (`test/mocks/`)
Damit die nativen Unit-Tests weiterhin erfolgreich bauen und durchlaufen, müssen wir die Mocks erweitern:
- **`test/mocks/WiFi.h`**:
  - Hinzufügen einer Mock-Klasse `IPAddress` mit der Methode `toString()`.
  - Hinzufügen der Methode `IPAddress gatewayIP()` in `MockWiFi`, die standardmäßig eine Test-IP (z.B. `"192.168.0.10"`) zurückgibt.
- **`test/mocks/WiFiClient.h`**:
  - Hinzufügen einer Überladung `bool connect(IPAddress ip, uint16_t port)` zu `WiFiClient`, die intern die Verbindung als erfolgreich markiert.

---

## 3. Betroffene Dateien

### [MODIFY] [OBDManager.cpp](file:///home/bert/projects/e-up!Proxy/src/OBDManager.cpp)
- Dynamische Gateway-IP auflösen und für die TCP-Verbindung nutzen.
- Logging der IP-Adresse anpassen.

### [MODIFY] [WiFi.h (Mock)](file:///home/bert/projects/e-up!Proxy/test/mocks/WiFi.h)
- Definition von `IPAddress` und `MockWiFi::gatewayIP()`.

### [MODIFY] [WiFiClient.h (Mock)](file:///home/bert/projects/e-up!Proxy/test/mocks/WiFiClient.h)
- Überladung von `connect` mit `IPAddress` hinzufügen.

---

## 4. Verifikationsplan

### 4.1. Automatisierte Tests
- Kompilieren und Ausführen der nativen Unit-Tests in WSL2:
  ```bash
  wsl bash -l -c "cd /home/bert/projects/e-up!Proxy && pio test -e native"
  ```

### 4.2. Manuelle Verifikation
- Kompilieren für die Ziel-Hardware:
  ```bash
  wsl bash -l -c "cd /home/bert/projects/e-up!Proxy && pio run -e usb"
  ```
- Flash des ESP32 via USB (oder OTA, falls vorhanden).
- Überwachung der seriellen Konsole oder des Web-Logs `/obd` nach dem Boot:
  - Überprüfen, ob der Proxy erfolgreich `192.168.0.10` auflöst und sich verbindet.
