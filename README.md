# e-up! Proxy

An ESP32-based OBD2/MQTT proxy designed for the **Volkswagen e-up!** (and its siblings: SEAT Mii Electric and Škoda CITIGOe iV). 

This firmware communicates with a **WiCAN meatPi OBD2 Dongle** over TCP/WiFi, polls vehicle diagnostic parameters (such as State of Charge, battery temperature, battery capacity, voltage, odometer, and service metrics) using **UDS (Unified Diagnostic Services)**, queues telemetry locally if offline, and publishes the data to a Home Assistant MQTT broker once connected to your home network.

## Hardware Architecture
*   **Microcontroller:** ESP32-WROOM-32
*   **OBD2 Interface:** WiCAN OBD2 Dongle (meatPi) configured in TCP server mode (`192.168.4.1:35000`)
*   **Status Indicator:** Single onboard LED (GPIO 2) showing active device states.

## Firmware State Machine
The firmware runs a robust, non-blocking asynchronous state machine with three main operational states:

1.  **`SCANNING`**
    *   Asynchronously scans for known WiFi networks.
    *   Prioritizes connecting to the vehicle's `WiCAN` dongle over the home network if both are in range.
    *   LED pattern: Rapidly blinking (10Hz).
2.  **`CONNECTED_TO_WICAN`**
    *   Connects to the OBD2 TCP socket, establishes a UDS extended diagnostic session on ECU `7E5`, and maintains it with a periodic `Tester Present` keep-alive.
    *   Polls high-frequency metrics (SoC, temperature, capacity, voltage, tire pressure) every 60 seconds (Group A).
    *   Polls low-frequency metrics (Odometer, service intervals) every 10 minutes (Group B).
    *   Enqueues telematics in a local JSON-based FIFO queue on LittleFS (resists data loss when offline/driving).
    *   LED pattern: Alternating short double-flashes.
3.  **`CONNECTED_TO_HOME`**
    *   Connects to your home WiFi.
    *   Synchronizes system time via NTP with the Europe/Berlin timezone.
    *   Flushes all buffered data packets from the local LittleFS queue to the MQTT broker.
    *   Publishes Home Assistant Auto-Discovery payloads for seamless dashboard integration of all 10 diagnostic sensors.
    *   Starts a minimal debugging WebServer at the `/debug` endpoint to stream live logs.
    *   Enables **ArduinoOTA** (Over-the-Air) firmware updates.
    *   Periodically rescans for Wican after 5 minutes to switch back to logging mode.
    *   LED pattern: Solid ON.

## Project Files
*   [SPEC.md](file:///home/bert/projects/e-up!Proxy/SPEC.md): Technical protocol specifications, LED patterns, and Home Assistant MQTT topics.
*   [MISSIONPROMPT.md](file:///home/bert/projects/e-up!Proxy/MISSIONPROMPT.md): Original functional requirements and initial design constraints.
*   `include/config.h`: Local settings for SSIDs, MQTT host, timer thresholds, and NVS parameters.
*   `include/version.h`: Central firmware version tracking.

## Endpoints & Network Interfaces

When the proxy is in the `CONNECTED_TO_HOME` state, it exposes the following network interfaces and endpoints:

### 1. HTTP Web Server (Port 80)
| Endpoint | Method | Response Type | Description |
| :--- | :--- | :--- | :--- |
| `/debug` | `GET` | `text/plain` | Streams the active logs (`debug.log`) followed by any rotated backup logs (`debug.bak.log`) for real-time remote diagnostics. No authentication required. |

### 2. Over-the-Air (OTA) Updates (Port 3232)
*   **Protocol:** standard ESP32 `ArduinoOTA`
*   **Hostname:** `eup-proxy` (resolves locally as `http://eup-proxy.local/` via mDNS)
*   **Security:** Password-protected (`eup-proxy-ota`) to prevent unauthorized uploads.
*   **Partitioning:** Seamless dual-partition swapping (dual 1.5MB slots via `partitions_ota.csv`).

### 3. MQTT Telemetry Broker Interface (Port 1883)
| Topic | Payload Format | Retained | Description |
| :--- | :--- | :---: | :--- |
| `eup/data` | JSON (Object) | `true` | Consolidated traction battery telemety (SoC, capacity, temperature, odometer, tire pressure, voltage, range, power). |
| `eup/lastSync` | String (ISO-8601) | `true` | Transmission timestamp in local time (`Europe/Berlin` CET/CEST offset, e.g. `2026-05-27T07:50:06+02:00`). |
| `homeassistant/sensor/eup_proxy_<sensor>/config` | JSON (Object) | `true` | Auto-Discovery configurations for Home Assistant dashboard integration (QoS 0). |

## Getting Started
1.  Copy `include/config.example.h` to `include/config.h`.
2.  Configure your WiFi networks (Wican SSID/pass and Home SSID/pass) and MQTT broker credentials in `include/config.h`.
3.  Build and upload the project using PlatformIO.
    *   For standard OTA flashing: `pio run -e esp32dev`
    *   For wired USB flashing fallback: `pio run -e usb`
    *   To run native unit tests on your PC: `pio test -e native`
