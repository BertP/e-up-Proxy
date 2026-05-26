#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// TEMPLATE - Copy this file to config.h and fill in your values
// ============================================================

#include "version.h"

// WiFi Configuration
#define WICAN_SSID "your_wican_ssid"
#define WICAN_PASS "your_wican_password"

#define HOME_SSID_1 "your_home_ssid_1"
#define HOME_PASS_1 "your_home_password_1"

#define HOME_SSID_2 "your_home_ssid_2"
#define HOME_PASS_2 "your_home_password_2"

// MQTT Configuration
#define MQTT_HOST "192.168.1.251"
#define MQTT_PORT 1883
#define MQTT_USER "your_mqtt_user"
#define MQTT_PASS "your_mqtt_pass"
#define MQTT_TOPIC_DATA "eup/data"
#define MQTT_TOPIC_LASTSYNC "eup/lastSync"

// Wican OBD2 Dongle Configuration
#define WICAN_IP "192.168.4.1"
#define WICAN_PORT 35000

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// Hardware Pin Configuration
#define LED_PIN 2

// Watchdog Configuration
#define WDT_TIMEOUT_S 30

// OTA Configuration
#define OTA_PASSWORD "your_ota_password"

// Timing Constants (all in milliseconds)
#define POLL_INTERVAL_FAST_MS       60000   // Group A OBD poll (60s)
#define POLL_INTERVAL_SLOW_MS       600000  // Group B OBD poll (10min)
#define HOME_RESCAN_INTERVAL_MS     300000  // Rescan for Wican while on Home (5min)
#define SCAN_RETRY_PAUSE_MS         30000   // Pause between scan retries (30s)
#define OBD_RECONNECT_INTERVAL_MS   15000   // OBD TCP reconnect attempt (15s)
#define WICAN_TIMEOUT_MS            60000   // Max time without OBD before giving up (60s)
#define TESTER_PRESENT_INTERVAL_MS  2500    // UDS Tester Present keep-alive (2.5s)
#define OBD_CMD_TIMEOUT_MS          2000    // ELM327 command response timeout (2s)
#define WIFI_CONNECT_TIMEOUT_WICAN_MS 10000
#define WIFI_CONNECT_TIMEOUT_HOME_MS  15000

// Buffer Configuration
#define MAX_QUEUE_FILES 500

// Boot-Loop Protection
#define BOOT_CRASH_THRESHOLD 5
#define BOOT_CRASH_NVS_KEY "bootcrash"

#endif // CONFIG_H
