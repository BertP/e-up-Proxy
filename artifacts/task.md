# Checklist - WICAN TCP Resilience & Timezone-Aware Logging

- `[x]` Define dynamic timezone logging status in `logger.h` and `logger.cpp`
- `[x]` Update `logEvent()` formatting and dynamic prefix switching in `logger.cpp`
- `[x]` Add Wican connection resilience tracking variables in `main.cpp`
- `[x]` Sort Wi-Fi scan results by RSSI and format log output in `main.cpp`
- `[x]` Implement 15-second OBD TCP reconnection scheduler and 60-second Wican AP timeout in `main.cpp`
- `[x]` Ensure timezone-aware NTP syncing updates `g_ntpSynchronized` status in `main.cpp`
- `[x]` Compile and verify the build in WSL2
- `[x]` Detect device and flash firmware on `/dev/ttyACM0`

## Bug Fixes (Transition Crash & NTP Redundant Logs)
- `[x]` Move WebServer initialization and `/debug` endpoint setup to `setup()` (prevent duplicate registration crash)
- `[x]` Retain `timeSyncDone = true` across re-connections to avoid duplicate NTP boot logs
- `[x]` Implement silent scan logic on consecutive scan failures and increase retry delay to 30s in handleScanning() to prevent log spam
- `[x]` Recompile and verify the build in WSL2
- `[x]` Request user approval to flash `/dev/ttyACM0` and flash

## Stability & Boot-Loop Resolution
- `[ ]` Call `WiFi.mode(WIFI_STA)` at the beginning of `setup()` to fix the all-zeros MAC address
- `[ ]` Implement `logBootSequence()` in `logger.cpp` to combine the 4 boot logs into a single LittleFS write, preventing brownout resets
- `[ ]` Guard the `transitionTo(STATE_SCANNING)` transition to skip redundant disconnections and logs if already in `STATE_SCANNING`
- `[ ]` Guard `State machine initialised. Entering SCANNING.` in `handleHome()` to prevent duplicate logging
- `[ ]` Optimize `publishHAAutoDiscovery()` with network stack yields to prevent memory/heap exhaustion crashes
- `[ ]` Compile and verify the build in WSL2
- `[ ]` Flash the firmware to `/dev/ttyACM0` after checking device presence and obtaining user confirmation
