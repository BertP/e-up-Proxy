# Changelog

## 2.2-retro-sweep (2026-05-26)
- Added OTA (Over-the-Air) update support with dual partition layout
- Added boot-loop protection via NVS crash counter
- Added `sendCommand()` timeout distinction in OBD logging
- Moved `FW_VERSION` to dedicated `version.h` (no longer gitignored)
- Replaced all magic number timing literals with named constants in `config.h`
- Changed `TelemetryData.src` from `String` to `char[16]` to reduce heap fragmentation
- Fixed `initBuffer()` being called redundantly on every queue operation
- Increased WDT timeout from 8s to 30s for safety margin
- Made WiFi connection attempts non-blocking (async state machine)
- Added native test framework (`[env:native]` with Unity + mocks)
- Added unit tests for OBD parsing, range derivation, and WiFi priority
- Removed plaintext credentials from `SPEC.md`
- Added `README.md` and `CHANGELOG.md`

## 2.1-ota-logfix (2026-05-21)
- Initial release with full state machine, OBD2 telemetry, MQTT/HA integration
- File-based FIFO queue on LittleFS for offline buffering
- NTP time synchronization with Europe/Berlin timezone
- Home Assistant Auto-Discovery for 10 sensors
- Debug WebServer at `/debug` endpoint
