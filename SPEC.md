# SPEC.md — e-up!Proxy Product Specification

This file describes **what** the system does. For agent behaviour, build tooling, and guardrails, see `MISSIONPROMPT.md`.

### How to reference sections

Each section has a stable ID (e.g. `SPEC-01`). Use these IDs when communicating changes to the agent:
> "SPEC-04 has changed — re-read only that section."

The agent reads only the referenced section, not the entire file.

---

## [SPEC-01] System States, Wi-Fi Logic & LED Signaling
> Last changed: 2026-05-21 · Initial version

The proxy must implement a strict State Machine to handle network connectivity. Status must be visually communicated via the onboard Blue LED (GPIO 2) using non-blocking blink patterns.

### Known Networks

| Role | Count |
|---|---|
| Home Wi-Fi | 2 configured SSIDs / Passwords |
| Dongle Wi-Fi | 1 configured SSID / Password (Wican AP mode) |

####  All other networks can be ignored and should not be listed in the log.  

### Connection States & LED Blink Codes

**State: SCANNING / DISCONNECTED**
- Logic: Continuously scan for SSIDs. If Wican is found, it always takes priority over Home Wi-Fi.
- LED Pattern: Fast blinking (100ms ON / 100ms OFF).

**State: CONNECTED_TO_WICAN**
- Logic: Fetch OBD2 metrics from the Dongle every 60 seconds. Store/Buffer the fetched data locally in a FIFO queue.
- LED Pattern: Distinct double-blink (2 quick blinks every 2 seconds).

**State: CONNECTED_TO_HOME**
- Logic: Connect to Home Wi-Fi, run a minimal background WebServer, and instantly flush all buffered payloads to the MQTT Broker.
- LED Pattern: Solid ON (or very slow breathing pattern).

### State Transition Rules

- Wican SSID always takes priority over Home Wi-Fi when both are visible.
- On abrupt loss of Wican Wi-Fi: transition immediately to SCANNING without crashing.
- On abrupt loss of Home Wi-Fi: transition immediately to SCANNING; buffered data is retained.

### Device Resilience & Connection Safeguards

- **Hardware Watchdog (WDT):** A hardware watchdog is configured with a safety timeout margin of 30 seconds (`WDT_TIMEOUT_S 30`) to absorb network negotiation and diagnostic query delays without triggering unwanted resets.
- **Asynchronous WiFi Connect:** To ensure the background WebServer remains responsive, WiFi connections are established using a non-blocking asynchronous state machine (`WIFI_IDLE`, `WIFI_CONNECTING`, `WIFI_CONNECTED`) with strict connection timeouts (10s for WiCAN, 15s for Home).
- **Boot-Loop Protection:** An NVS crash counter is tracked in preference namespace `proxy` (key `bootcrash`). If the device fails to boot stably 5 consecutive times, a boot loop warning is logged. The counter is automatically zeroed once NTP time sync is confirmed on the Home network.

---

## [SPEC-02] Diagnostics, WebServer & Log Management
> Last changed: 2026-05-21 · Added NTP-based hh:mm:ss timestamps, NO-NTP fallback, boot/scan/switch log formats

### Log Storage

- **Mechanism:** LittleFS, dedicated file `debug.log`.
- **Size cap:** 50 KB maximum. When the limit is reached, oldest entries are purged automatically (circular/ring buffer behaviour).

### Log Entry Format

All entries use a consistent prefix for easy filtering:

```
[hh:mm:ss] [<LEVEL>] <message>
```

- `hh:mm:ss` — local wall-clock time (`Europe/Berlin`), used **after** NTP synchronisation is confirmed. Before NTP sync, fall back to `[T+<ms>]` (milliseconds since boot) and prefix the entry with `[NO-NTP]`.
- `<LEVEL>` — one of `BOOT`, `SCAN`, `CONN`, `SWITCH`, `DATA`, `ERROR`.

#### Example entries before and after NTP sync

```
[T+0]      [BOOT] e-up!Proxy starting. Firmware: <version>
[T+312]    [BOOT] [NO-NTP] LittleFS mounted. Free: 38 KB / 50 KB cap
[T+4821]   [BOOT] NTP synchronised. Local time: 17:35:02 (Europe/Berlin, CEST +02:00)
[17:35:02] [BOOT] State machine initialised. Entering SCANNING.
[17:35:04] [SCAN] Found 3 networks:
[17:35:04] [SCAN]   SSID: "WicanAP"     RSSI: -58 dBm  CH: 6
```

### Boot Log (on every startup)

Logged once during `setup()`. Before NTP sync the `[T+<ms>]` prefix is used; after sync all subsequent entries use `hh:mm:ss`:

```
[T+0]      [BOOT] e-up!Proxy starting. Firmware: <version>
[T+12]     [BOOT] [NO-NTP] Chip: ESP32-WROOM-32, MAC: AA:BB:CC:DD:EE:FF
[T+24]     [BOOT] [NO-NTP] LittleFS mounted. Free: <n> KB / 50 KB cap
[T+30]     [BOOT] [NO-NTP] Buffered payloads in queue: <n>
[T+4821]   [BOOT] NTP synchronised. Local time: 17:35:02 (Europe/Berlin, CEST +02:00)
[17:35:02] [BOOT] State machine initialised. Entering SCANNING.
```

### Wi-Fi Scan Log (on every scan cycle)

Logged each time a scan completes. One line per discovered network, sorted by RSSI descending:

```
[17:35:04] [SCAN] Found <n> networks:
[17:35:04] [SCAN]   SSID: "WicanAP"      RSSI: -58 dBm  CH: 6
[17:35:04] [SCAN]   SSID: "HomeNet_5G"   RSSI: -71 dBm  CH: 36
[17:35:04] [SCAN]   SSID: "HomeNet_2G"   RSSI: -74 dBm  CH: 11
[17:35:04] [SCAN] Priority target selected: "WicanAP"
```

If no known networks are found:
```
[17:35:04] [SCAN] No known networks found. Retrying in <n>s.
```

### Wi-Fi Transition Log (on every state change)

Logged when the proxy switches from one network to another:

```
[17:42:11] [SWITCH] Disconnecting from: "WicanAP"
[17:42:11] [SWITCH] Reason: Wican signal lost (RSSI below threshold / timeout)
[17:42:11] [SWITCH] Connecting to: "HomeNet_5G"  RSSI: -71 dBm
[17:42:12] [SWITCH] Connected. Duration: 1240 ms. IP: 192.168.x.x
```

Transition reasons to be logged:

| Reason token | When used |
|---|---|
| `Wican signal lost` | Wican SSID disappeared from scan results |
| `Wican timeout` | Wican connected but no OBD2 response within timeout |
| `Home signal lost` | Home SSID disappeared during flush |
| `Wican found` | Wican SSID reappears during SCANNING — triggers switch back |

### Minimal WebServer

- Active **only** when `CONNECTED_TO_HOME`.
- Single endpoint: `GET /debug`
  - Streams the full contents of `debug.log` as `text/plain`.
  - No authentication required (local network only).

### Over-the-Air (OTA) Updates

- Active **only** when `CONNECTED_TO_HOME`.
- **Protocol:** ArduinoOTA on standard port `3232`.
- **Hostname:** `eup-proxy`
- **Security:** Access is protected by the upload password `eup-proxy-ota`.
- **Storage Layout:** Supports seamless switching between two 1.5MB application slots using the `partitions_ota.csv` partition layout.

---

## [SPEC-03] Data Buffering Contract
> Last changed: 2026-05-21 · Payload schema aligned to full eup/data field set

Because the ESP32 cannot communicate with both the Wican Dongle and the Home MQTT broker simultaneously, all metrics must be queued locally while connected to Wican and flushed when connected to Home.

### Buffer Mechanism

- **Type:** FIFO (First-In, First-Out) queue.
- **Storage:** LittleFS.
- **Flush trigger:** Immediately upon `CONNECTED_TO_HOME` state entry.

### Payload Structure (JSON)

The buffered payload mirrors the `eup/data` MQTT schema (see [SPEC-05]):

```json
{
  "soc": 82.5,
  "volt": 12.4,
  "temp": 18.0,
  "range": 202.5,
  "power": -2100.0,
  "odo": 42150.0,
  "service_days": 180.0,
  "service_km": 7500.0,
  "bat_cap": 61.5,
  "tp_alarm": 0.0,
  "ts": 1779294900,
  "src": "CAR_BUFFERED"
}
```

> **Memory Optimization Note:** In memory, `TelemetryData.src` is represented as a fixed-size `char[16]` character array to prevent heap fragmentation during recurring queue insertions.

---

## [SPEC-04] OBD2 Metrics — DID Mapping (Wican / STN2120)
> Last changed: 2026-05-21 · Extracted from OBDManager.h; slow-poll group (10 min) added; power/range TBD

Source: `OBDManager.h`. The Wican Dongle is accessed via TCP on `192.168.4.1:35000`.
A UDS Extended Diagnostic Session (`10 03`) is opened once per connection on ECU `7E5` and kept alive with `3E 80` (Tester Present, suppress response).

### ECU Overview

| ECU | CAN Header | Scope |
|---|---|---|
| BMS (Battery Management) | `7E5` | SoC, temperature, battery capacity |
| Dashboard / Gateway | `7E0` | Odometer, service intervals, tyre pressure |

### Read Group A — Cyclic (every 60 s, during driving and charging)

| MQTT field | Method | ECU | DID | Raw formula | Unit |
|---|---|---|---|---|---|
| `soc` | `querySoC()` | `7E5` | `02 8C` | `raw × 0.4` | `%` |
| `temp` | `queryTemp()` | `7E5` | `11 62` | `raw − 40` | `°C` |
| `bat_cap` | `queryBatteryCapacity()` | `7E5` | `22 E1` | `raw × 0.1` | `Ah` |
| `volt` | `queryVoltage()` | — | `AT RV` | direct float | `V` |
| `tp_alarm` | `queryTirePressureAlarm()` | `7E0` | `02 1A` | `raw` (0=OK) | — |

> `power` and `range` are not directly queried via OBD2 — derivation method TBD.

### Read Group B — Pre-flight / slow poll (every 10 min)

| MQTT field | Method | ECU | DID | Raw formula | Unit |
|---|---|---|---|---|---|
| `odo` | `queryOdometer()` | `7E0` | `22 03` | `raw` (3 bytes) | `km` |
| `service_days` | `queryDaysToService()` | `7E0` | `02 48` | `raw` (2 bytes) | `d` |
| `service_km` | `queryKmToService()` | `7E0` | `02 47` | `raw` (2 bytes) | `km` |

Group B is also read **once immediately** after Wican connection is established (pre-flight read), then on the 10-minute timer.

### Session & Protocol Notes

- ELM327 init sequence (once per TCP connect): `AT E0` · `AT L0` · `AT H0` · `AT SP 6` · `AT AT 1` · `AT ST FF`
- CAN protocol: 11-bit, 500 kbps (`AT SP 6`) — required for VW e-up!
- Header switching: `AT SH 7E5` / `AT SH 7E0` — cached in `_currentHeader`, only sent when header changes
- UDS response prefix stripped by `extractPayload()` before raw value parsing

### OBD Timing & Diagnostics

- **Command Timeout:** Commands use an increased timeout of 2.0 seconds (`OBD_CMD_TIMEOUT_MS`) to handle slow ECU responses reliably.
- **Tester Present Keep-Alive:** A `3E 80` keep-alive command is sent every 2.5 seconds (`TESTER_PRESENT_INTERVAL_MS`) to maintain the active UDS diagnostic session.
- **Diagnostic Logging:** Logs explicitly differentiate silent connection/response timeouts from explicit Negative Response Codes (NRCs starting with `7F`) returned by the ECU.

### Log Format for OBD2 Data

Each completed read cycle produces a single `[DATA]` log line — concise, not topic-style:

**Group A (cyclic, every 60 s):**
```
[17:43:00] [DATA] SoC=82.5% Temp=18°C Cap=61.5Ah Volt=12.4V TpAlarm=0
```

**Group B (pre-flight / slow poll):**
```
[17:43:00] [DATA:SLOW] Odo=42150km SvcDays=180d SvcKm=7500km
```

**On query error** (method returns −1 / −99):
```
[17:43:01] [ERROR] OBD query failed: soc (DID 028C) — raw response: "<resp>"
[17:43:01] [ERROR] OBD query failed: temp (DID 1162) — no response (timeout)
```

**On session open / close:**
```
[17:42:15] [CONN] UDS extended session opened on 7E5.
[17:58:22] [CONN] OBD session closed. TCP disconnected from 192.168.4.1:35000.
```

---

## [SPEC-05] MQTT Topics, Broker & Home Assistant Integration
> Last changed: 2026-05-21 · Full topic schema added; Europe/Berlin timezone; ACL requirement documented

### Connection Parameters

| Parameter | Value |
|---|---|
| Broker | Home Assistant Mosquitto Broker Add-on |
| Port | 1883 |
| Username | `e-up!Proxy` |
| QoS | 0 (default) |

> ⚠️ **ACL requirement:** The `e-up!Proxy` user must be created in Home Assistant with **administrator rights**. Standard (non-admin) users are restricted to read-only access by the Mosquitto add-on and cannot publish. Without admin rights, the proxy connects successfully but no data reaches Home Assistant.

### Topic: `eup/data`

- **Description:** All cyclically measured vehicle telemetry as a consolidated JSON object.
- **Retained:** `true`
- **Publish trigger:** Immediately upon `CONNECTED_TO_HOME`, draining the FIFO buffer.

#### JSON Payload Schema

```json
{
  "soc": 82.5,
  "volt": 12.4,
  "temp": 18.0,
  "range": 202.5,
  "power": -2100.0,
  "odo": 42150.0,
  "service_days": 180.0,
  "service_km": 7500.0,
  "bat_cap": 61.5,
  "tp_alarm": 0.0,
  "ts": 1779294900,
  "src": "CAR_BUFFERED"
}
```

#### Field Reference

| Key | Type | Unit | Description |
|---|---|---|---|
| `soc` | Float | `%` | Traction battery State of Charge |
| `volt` | Float | `V` | 12V on-board battery voltage |
| `temp` | Float | `°C` | Mean traction battery temperature |
| `range` | Float | `km` | Temperature-corrected range estimate |
| `power` | Float | `W` | Current power (negative = discharge, positive = charge) |
| `odo` | Float | `km` | Total odometer reading (captured at pre-flight) |
| `service_days` | Float | `d` | Days remaining until next service |
| `service_km` | Float | `km` | Kilometres remaining until next service |
| `bat_cap` | Float | `Ah` | Current traction battery capacity (SOH indicator) |
| `tp_alarm` | Float | — | Tyre pressure alarm (0 = OK, >0 = warning) |
| `ts` | Uint32 | — | Unix timestamp of measurement (NTP-synchronised) |
| `src` | String | — | Data source tag (e.g. `INIT`, `CAR_BUFFERED`) |

### Topic: `eup/lastSync`

- **Description:** Timestamp of the last successful data transmission. Used to monitor data freshness in Home Assistant.
- **Retained:** `true`
- **Format:** ISO-8601 string with timezone offset.
- **Timezone:** `Europe/Berlin` — UTC+1 in winter (CET), UTC+2 in summer (CEST). The ESP32 must apply DST automatically via NTP + timezone library. The offset in the payload reflects the correct local offset at the time of transmission (`+01:00` or `+02:00`).

#### Payload Examples

```
2026-01-15T09:12:44+01:00   ← winter (CET)
2026-05-20T17:35:04+02:00   ← summer (CEST)
```

### Home Assistant Auto-Discovery Topics

The proxy registers all sensors automatically under a single HA device named **"e-up! Proxy"** using the MQTT Discovery protocol. Each sensor has its own config topic:

`homeassistant/sensor/eup_proxy_<SENSOR_ID>/config`

All payloads use HA MQTT abbreviations (`stat_t`, `val_tpl`, `uniq_id`, `unit_of_meas`, `dev_class`, `dev`) to minimise RAM usage on the ESP32.

#### Sensor: Battery SoC

- **Topic:** `homeassistant/sensor/eup_proxy_soc/config`

```json
{
  "name": "e-up! Battery SoC",
  "stat_t": "eup/data",
  "unit_of_meas": "%",
  "state_class": "measurement",
  "val_tpl": "{{ value_json.soc }}",
  "uniq_id": "eup_proxy_soc",
  "dev_class": "battery",
  "dev": {
    "ids": "eup_proxy_esp32",
    "name": "e-up! Proxy",
    "mf": "VW / local",
    "sw": "2.2-retro-sweep"
  }
}
```

#### Sensor: Last Sync

- **Topic:** `homeassistant/sensor/eup_proxy_lastsync/config`

```json
{
  "name": "e-up! Last Sync",
  "stat_t": "eup/lastSync",
  "dev_class": "timestamp",
  "uniq_id": "eup_proxy_lastsync",
  "ic": "mdi:sync",
  "dev": {
    "ids": "eup_proxy_esp32",
    "name": "e-up! Proxy",
    "mf": "VW / local",
    "sw": "2.2-retro-sweep"
  }
}
```

> **Note:** Additional sensors for `volt`, `temp`, `range`, `power`, `odo`, `bat_cap`, `tp_alarm`, `service_days`, and `service_km` follow the same pattern — same `dev` block, matching `val_tpl` field name, and appropriate `dev_class` / `unit_of_meas`.

### HA User Setup Reference (for user, not agent)

To create the `e-up!Proxy` user with the required write permissions in Home Assistant:

1. Enable **Advanced Mode** in your HA profile (required to access user management).
2. Go to **Settings → People → Users tab → Add User**.
3. Set Name: `e-up! Proxy`, Username: `<see config.h>`, Password: `<see config.h>`.
4. Enable **"Allow this user to log in as administrator"**.
5. Restart the **Mosquitto Broker** add-on to reload permissions.
6. Flash and restart the ESP32 proxy.

---

## [SPEC-06] Changelog

| Date | Section | Change |
|---|---|---|
| 2026-05-21 | SPEC-01 | Initial version — system states, LED patterns, transition rules |
| 2026-05-21 | SPEC-02 | Initial version — log format, NTP timestamps, boot/scan/switch log |
| 2026-05-21 | SPEC-03 | Initial version — FIFO buffer, payload aligned to eup/data schema |
| 2026-05-21 | SPEC-04 | Initial version — DID mapping from OBDManager.h, slow-poll group |
| 2026-05-21 | SPEC-05 | Initial version — full MQTT topic schema, HA discovery, timezone |
