#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// TEMPLATE — Copy this file to config.h and fill in your values
// ============================================================

// Firmware Version (single source of truth)
#define FW_VERSION "2.1-ota-logfix"

// WiFi Configuration
// Define the Wican Dongle Access Point credentials
#define WICAN_SSID "your_wican_ssid"
#define WICAN_PASS "your_wican_password"

// Define the two Home WiFi credentials
#define HOME_SSID_1 "your_home_ssid_1"
#define HOME_PASS_1 "your_home_password_1"

#define HOME_SSID_2 "your_home_ssid_2"
#define HOME_PASS_2 "your_home_password_2"

// MQTT Configuration
#define MQTT_HOST "192.168.1.251"
#define MQTT_PORT 1883
#define MQTT_USER "your_mqtt_user"   // Leave empty if no authentication is required
#define MQTT_PASS "your_mqtt_pass"   // Leave empty if no authentication is required
#define MQTT_TOPIC_DATA "eup/data"
#define MQTT_TOPIC_LASTSYNC "eup/lastSync"

// Wican OBD2 Dongle Configuration
#define WICAN_IP "192.168.4.1"
#define WICAN_PORT 35000

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3" // Central European Time (Berlin) with automatic DST

// Hardware Pin Configuration
#define LED_PIN 2

// Watchdog Configuration
#define WDT_TIMEOUT_S 8

#endif // CONFIG_H
