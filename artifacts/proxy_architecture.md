# e-up!Proxy System Architecture & Diagram Guide
> **Firmware Version Alignment:** `2.4.2-dongle-first`  
> **Last updated:** 2026-05-31

This document provides a highly detailed, graphical breakdown of the entire functional loop, states, timings, and dynamic sequences of the `e-up!Proxy`.

---

## 1. System States & Control Flow Diagram

The following flowchart details the non-blocking state machine, network scanning decision tree, watchdog feeding loops, and the new **"Dongle First" exklusiv** architecture.

```mermaid
graph TD
    %% Styling
    classDef stateStyle fill:#1e1e2f,stroke:#7c4dff,stroke-width:2px,color:#fff;
    classDef logicStyle fill:#2e2e3f,stroke:#03a9f4,stroke-width:1px,color:#fff;
    classDef wdtStyle fill:#3e2723,stroke:#ff5722,stroke-width:2px,color:#fff;

    %% Nodes
    Start([System Boot]) --> Init[Hardware Init: NVS, Pins, LittleFS]
    Init --> WDT_Setup["Setup Hardware WDT (30s)"]
    WDT_Setup --> Boot_Check{Boot Crash Count < 5?}
    
    Boot_Check -- No --> Safe_Mode["Safe Mode (Warning logged)"]
    Boot_Check -- Yes --> Init_Scan[Transition to STATE_SCANNING]

    subgraph SCANNING_STATE ["Zustand: SCANNING"]
        Init_Scan --> Scan_Start["Start non-blocking WiFi Scan (3-4s)"]
        Scan_Start --> Scan_Loop{Scan Complete?}
        Scan_Loop -- No --> Scan_Loop
        Scan_Loop -- Yes --> Sort_RSSI[Sort known networks by signal strength]
        Sort_RSSI --> Decision{"WiCAN SSID visible?"}
        
        Decision -- Yes --> Connect_Wican["beginWiFiConnect(Wican, timeout: 10s)"]
        Decision -- No --> Decision_Home{"Home SSID visible?"}
        
        Decision_Home -- Yes --> Connect_Home["beginWiFiConnect(Home, timeout: 15s)"]
        Decision_Home -- No --> Scan_Pause["Pause (30s)"]
        Scan_Pause --> Scan_Start
    end

    Connect_Wican --> Connect_Wican_Check{Connected?}
    Connect_Wican_Check -- Yes --> Entering_Wican[Transition to STATE_CONNECTED_TO_WICAN]
    Connect_Wican_Check -- No --> Scan_Start

    Connect_Home --> Connect_Home_Check{Connected?}
    Connect_Home_Check -- Yes --> Entering_Home[Transition to STATE_CONNECTED_TO_HOME]
    Connect_Home_Check -- No --> Scan_Start

    subgraph WICAN_STATE ["Zustand: CONNECTED_TO_WICAN (Dongle First)"]
        Entering_Wican --> Wican_Init["Init TCP Client, Open UDS Session (10 03)"]
        Wican_Init --> Wican_Loop["Loop: Run OBD Keep-Alive (3E 80) every 2.5s"]
        
        Wican_Loop --> Link_Check{"WiFi connected?"}
        Link_Check -- No --> Lost_Wican[Transition to STATE_SCANNING: 'Wican lost']
        
        Link_Check -- Yes --> OBD_Active{"OBD Active?"}
        OBD_Active -- No --> OBD_Reconnect_Timer{"15s elapsed?"}
        OBD_Reconnect_Timer -- Yes --> Attempt_Reconnect["Attempt TCP Reconnect"]
        Attempt_Reconnect --> Wican_Loop
        OBD_Reconnect_Timer -- No --> Wican_Loop
        
        OBD_Active -- Yes --> Poll_Timer{"Fast Poll (150s) or Slow Poll (10min)?"}
        Poll_Timer -- Yes --> Fetch_OBD["Query UDS DIDs (Group A/B)"]
        Fetch_OBD --> Buffer_Data["Buffer Telemetry in LittleFS queue"]
        Buffer_Data --> Wican_Loop
        Poll_Timer -- No --> Wican_Loop
        
        OBD_Active -- No --> OBD_Timeout{"60s elapsed without OBD connection?"}
        OBD_Timeout -- Yes --> Timeout_Wican[Transition to STATE_SCANNING: 'Wican timeout']
        OBD_Timeout -- No --> Wican_Loop
    end

    subgraph HOME_STATE ["Zustand: CONNECTED_TO_HOME"]
        Entering_Home --> Home_Init["NTP Time Sync, WebServer /debug, OTA Init"]
        Home_Init --> Home_Loop["Loop: Handle WebServer /debug & OTA"]
        
        Home_Loop --> Home_Link_Check{"WiFi connected?"}
        Home_Link_Check -- No --> Lost_Home[Transition to STATE_SCANNING: 'Home lost']
        
        Home_Link_Check -- Yes --> Sync_Done{"NTP synced?"}
        Sync_Done -- Yes --> Reset_Crash_Counter["Reset NVS boot crash count to 0"]
        Sync_Done -- No --> Home_Loop
        
        Reset_Crash_Counter --> Flush_Trigger{"Queue not empty?"}
        Flush_Trigger -- Yes --> Flush_Mqtt["flushQueueToMQTT()"]
        
        subgraph OPTIMIZED_FLUSH ["Optimized Watchdog-Safe Flush"]
            Flush_Mqtt --> Get_Queue["getQueueSize()"]
            Get_Queue --> Loop_Queue["while (getNextQueuedFile)"]
            Loop_Queue --> Publish_Mqtt["mqttClient.publish(Topic, Data, retain: true)"]
            Publish_Mqtt --> Delete_File["removeQueuedFile()"]
            Delete_File --> WDT_Feed1["feedWDT()"]:::wdtStyle
            WDT_Feed1 --> Yield_CPU1["yield()"]
            Yield_CPU1 --> Delay_Stack["delay(5)"]
            Delay_Stack --> Loop_Queue
            Loop_Queue -- Empty --> Flush_Finish["Update eup/lastSync timestamp"]
        end
        
        Flush_Finish --> Home_Loop
        Flush_Trigger -- No --> Home_Loop
        
        Home_Loop --> Home_Period{"5-minute period elapsed?"}
        Home_Period -- Yes --> Home_Rescan[Transition to STATE_SCANNING]
    end

    %% Apply Classes
    class Entering_Wican,Entering_Home,Init_Scan stateStyle;
    class WDT_Feed1 wdtStyle;
```

---

## 2. Dynamic Sequence Diagram

This diagram maps out the live, physical interactions between the ESP32 Proxy, the WiCAN Dongle/Car ECU, and the Home WiFi AP / MQTT Broker / Home Assistant during a full cycle of operation.

```mermaid
sequenceDiagram
    autonumber
    actor Car as Car ECU (7E5/7E0)
    participant Dongle as WiCAN Dongle
    participant Proxy as e-up!Proxy (ESP32)
    participant HomeAP as Home WiFi AP
    participant HA as MQTT Broker & HA

    Note over Proxy: 1. SYSTEM BOOT
    Proxy->>Proxy: Init Hardware, Mount LittleFS
    Proxy->>Proxy: Read persistent queue (e.g. 195 files)

    Note over Proxy: 2. STATE_SCANNING (Home AP Range)
    Proxy->>Proxy: Run WiFi Scan (3-4s, LED blinks fast)
    Proxy-->>HomeAP: Detect Home SSID (RSSI: -65dBm)
    Proxy->>HomeAP: beginWiFiConnect(HomeAP)
    HomeAP-->>Proxy: Connected (IP: 192.168.1.55, LED Solid ON)

    Note over Proxy: 3. STATE_CONNECTED_TO_HOME
    Proxy->>HomeAP: Query NTP Server (pool.ntp.org)
    HomeAP-->>Proxy: NTP Synced (Berlin local time applied)
    Proxy->>Proxy: Reset NVS boot crash counter to 0
    Proxy->>HA: Connect to MQTT Broker (192.168.1.251)
    HA-->>Proxy: Successfully Connected
    Proxy->>HA: Publish HA Auto-Discovery Sensors
    HA-->>Proxy: Sensors registered in Home Assistant

    Note over Proxy: 4. OPTIMIZED WATCHDOG-SAFE QUEUE FLUSH
    loop For each of the 195 queued files
        Proxy->>Proxy: Open next JSON file in LittleFS
        Proxy->>HA: publish("eup/data", JSON, retain: true)
        Proxy->>Proxy: Delete sent JSON file
        Proxy->>Proxy: feedWDT() + yield() + delay(5) (WDT remains happy!)
    end
    Proxy->>HA: publish("eup/lastSync", current ISO-Timestamp)
    
    Note over Proxy: 5. 5-MINUTE PERIOD EXPIRES
    Proxy->>Proxy: 5 minutes elapsed on Home WiFi
    Proxy->>HomeAP: Disconnect
    Proxy->>Proxy: Transition to STATE_SCANNING

    Note over Proxy: 6. STATE_SCANNING (Car started, Wican active)
    Proxy->>Proxy: Run WiFi Scan
    Proxy-->>Dongle: Detect "WICAN_eUp" AP
    Proxy->>Dongle: beginWiFiConnect(Wican AP)
    Dongle-->>Proxy: Connected (IP: 192.168.0.x, LED double-blink)

    Note over Proxy: 7. STATE_CONNECTED_TO_WICAN
    Proxy->>Dongle: Open TCP socket on Port 35000
    Dongle-->>Proxy: TCP Socket Open
    Proxy->>Dongle: send("10 03") (Open UDS Extended Session)
    Dongle-->>Proxy: resp("50 03") (Session Active)
    
    Note over Proxy: 8. CYCLIC KEEP-ALIVE & OBD QUERIES
    loop Every 2.5s (Keep-Alive)
        Proxy->>Dongle: send("3E 80") (Tester Present)
    end
    
    loop Every 150s (Fast Poll Group A)
        Proxy->>Dongle: send("22 2A0B") (Query Temp BMS)
        Dongle->>Car: CanRequest("7E5 22 2A0B")
        Car-->>Dongle: CanResponse(Raw Temp data)
        Dongle-->>Proxy: resp("62 2A0B <bytes>")
        Proxy->>Proxy: Parse signed int16 (temp * 0.015625)
        
        Proxy->>Dongle: send("22 028C") (Query SoC BMS)
        Dongle-->>Proxy: resp("62 028C <byte>")
        Proxy->>Proxy: Parse uint8 SoC (raw * 0.4)
        
        Proxy->>Proxy: Write compiled Telemetry to LittleFS /queue
    end

    Note over Proxy: 9. DISCONNECT (Car turned off)
    Car-->>Dongle: Power loss (Zündung aus)
    Dongle-->>Proxy: Wi-Fi Access Point shuts down
    Proxy->>Proxy: Detect WiFi status lost
    Proxy->>Proxy: Close TCP Socket
    Proxy->>Proxy: Transition to STATE_SCANNING (Ready to connect to Home WiFi again)
```
