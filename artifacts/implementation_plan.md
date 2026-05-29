# Implementation Plan - Log Optimization & Dedicated MQTT Diagnostic Endpoint

We will optimize the system logger and WebServer of the `e-up!Proxy` to address the issue where no MQTT data reached the broker during the last drive, but no diagnostic records were available due to log spam/rotation.

## User Review Required

> [!IMPORTANT]
> - We will **remove high-frequency telemetry data (`DATA` and `DATA:SLOW` events) from the persistent LittleFS `/debug.log`**.
>   - *Rationale:* Telemetry is already buffered as JSON files in the queue before being successfully flushed to MQTT, and is printed to `Serial`. Logging it again in `/debug.log` causes rapid log rotation (25KB limit) and wipes out critical system state history.
>   - We will continue to print telemetry to `Serial` for real-time USB debugging.
> - We will **introduce a new persistent `/mqtt.log` (and `/mqtt.bak.log` rotation)**.
> - We will **register a new `/mqtt` HTTP endpoint** that streams `/mqtt.log` and `/mqtt.bak.log`.

## Proposed Changes

### Logger Module

#### [MODIFY] [logger.h](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/include/logger.h)
- Declare `streamMqttLog(WebServer& server)` to stream the MQTT logs.
- Declare `logMqttEvent(const String& level, const String& message)` or expand `logEvent` logic. Let's declare `logMqttEvent(const String& level, const String& message)` to explicitly log MQTT events.

#### [MODIFY] [logger.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/src/logger.cpp)
- Remove file-writing logic from `logTelemetry` and `logTelemetrySlow`, making them only log to `Serial`.
- Implement `logMqttEvent(const String& level, const String& message)`:
  - Writes to `/mqtt.log` with timestamp prefix and `checkMqttRotation()`.
  - Also outputs to `Serial`.
- Implement `checkMqttRotation()` and `streamMqttLog(WebServer& server)` matching the `/debug` log rotation patterns (max 25KB, `.bak` file rename).
- Ensure `clearLog()` also clears `/mqtt.log` and `/mqtt.bak.log`.

### Web Server & State Machine

#### [MODIFY] [main.cpp](file:///wsl.localhost/Ubuntu-22.04/home/bert/projects/e-up!Proxy/src/main.cpp)
- Replace MQTT-related `logEvent` calls inside `flushQueueToMQTT`, `publishHAAutoDiscovery`, and related error-handling with `logMqttEvent`.
- Register the new `/mqtt` HTTP endpoint at setup:
  ```cpp
  server.on("/mqtt", HTTP_GET, []() {
      logMqttEvent("WEBSERVER", "GET /mqtt endpoint requested.");
      streamMqttLog(server);
  });
  ```

---

## Verification Plan

### Automated / Manual Verification
1. Build the firmware and flash it to the ESP32.
2. Verify that `/debug` no longer contains high-frequency telemetry spams.
3. Access `/mqtt` and verify it contains connection attempts, auto-discovery publications, and flush reports.
4. Verify file rotation and persistent logs behavior under simulated disconnects.
