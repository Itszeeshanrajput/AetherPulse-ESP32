#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ==============================================================================
// AETHERPULSE - HARDWARE & SYSTEM CONFIGURATION
// Target: ESP32-D0WD-V3 (Dual-Core @ 240 MHz)
// ==============================================================================

#define PROJECT_NAME              "AetherPulse"
#define PROJECT_VERSION           "v3.1.0-beta.1"

// Hardware Pin Configuration (CYD-compatible RGB LED; optional TFT backlight)
#define PIN_RGB_RED               4    // Active LOW LED
#define PIN_RGB_GREEN             16   // Active LOW LED (unavailable on PSRAM modules)
#define PIN_RGB_BLUE              17   // Active LOW LED (unavailable on PSRAM modules)
#define PIN_TFT_BL                27   // Optional TFT Backlight control (pulled LOW to save power/heat)

// RF & Physical Layer Power Calibration (0.25 dBm steps; ESP-IDF bins: 44..80)
#define DEFAULT_TX_POWER_DBM      18.0f // Calibrated stable RF output
#define DEFAULT_TX_POWER_RAW      72    // 72 * 0.25 = 18.00 dBm
#define MAX_TX_POWER_RAW          80    // 80 * 0.25 = 20.00 dBm max (ESP32 legal/PA ceiling)
#define MIN_TX_POWER_RAW          44    // 44 * 0.25 = 11.00 dBm min for dynamic TPC backoff

// Dynamic Transmit Power Control (TPC) Closed-Loop Backoff
#define TPC_ENABLED_DEFAULT       true
#define TPC_CLOSE_RSSI_THRESHOLD  -48.0f // Back off when peer RSSI > -48 dBm to avoid saturation
#define TPC_FAR_RSSI_THRESHOLD    -74.0f // Boost power when peer RSSI < -74 dBm

// SoftAP & Routing Parameters
#define SOFTAP_BEACON_INTERVAL_TU 100   // 100 TU (102.4 ms) standard beacon interval
#define SOFTAP_DTIM_PERIOD        1     // Immediate delivery of buffered multicast/ARP/mDNS frames
#define TCP_MSS_CLAMP_VALUE       1412  // Clamps TCP SYN MSS to prevent PMTU black-hole drops

// lwIP Precompiled NAPT Limits (matches framework-arduinoespressif32 liblwip.a)
#define NAPT_MAX_FLOWS            512   // Precompiled IP_NAPT_MAX
#define NAPT_MAX_PORTMAPS         32    // Precompiled IP_PORTMAP_MAX

// Smart Roaming & Multi-AP Fast-Connect Parameters
#define MAX_KNOWN_NETWORKS        10
#define ROAM_RSSI_TRIGGER_DBM     -76.0f // Begin candidate evaluation when RSSI drops below -76 dBm
#define ROAM_HYSTERESIS_DELTA_DBM 8.0f   // Candidate must be at least 8 dB stronger
#define ROAM_DWELL_TIME_MS        3000   // Candidate must sustain strength for 3 seconds
#define ROAM_COOLDOWN_MS          30000  // 30 seconds hold-down after roaming to prevent flapping

// Sub-Millisecond On-Chip DNS Caching Daemon
#define DNS_CACHE_MAX_ENTRIES     64    // Maximum cached domain -> IPv4 mappings in SRAM
#define DNS_CACHE_DEFAULT_TTL     300   // 5 minutes fallback TTL for cached records

// Default SoftAP Broadcast Settings
#define DEFAULT_AP_SSID           "AetherPulse-Extender"
#define DEFAULT_AP_PASS           "aetherpulse123"
#define DEFAULT_AP_CHANNEL        6
#define DEFAULT_AP_MAX_CLIENTS    8
#define DEFAULT_AP_IP             IPAddress(192, 168, 4, 1)
#define DEFAULT_AP_ALT_IP         IPAddress(192, 168, 5, 1) // Alternate subnet on upstream collision
#define DEFAULT_AP_NETMASK        IPAddress(255, 255, 255, 0)

// Hostname & Discovery
#define MDNS_HOSTNAME             "aetherpulse"

// Watchdog Protection (Core 3.x TWDT)
#define WDT_TIMEOUT_MS            15000 // 15 seconds Task Watchdog Timer

// Thermal Protection Threshold
#define THERMAL_WARN_TEMP_C       75.0f // Warn threshold for internal die temperature

// Serial CLI Configuration
#define CLI_BAUD_RATE             115200
#define CLI_BUFFER_SIZE           256

// Autonomous WAN Internet Health Watchdog & Fast Failover
#define WAN_WATCHDOG_ENABLED_DEFAULT    true
#define WAN_PROBE_INTERVAL_HEALTHY_MS   3000  // 3s default steady-state probe interval
#define WAN_PROBE_INTERVAL_FAST_MS      400   // 400ms fast confirmation interval
#define WAN_PROBE_TIMEOUT_MS            600   // 600ms non-blocking Anycast probe timeout
#define WAN_FAIL_THRESHOLD              2     // 2 consecutive strikes = confirmed outage
#define WAN_COOLDOWN_MS                 20000 // 20s hold-down after failover
#define WAN_BACKOFF_BASE_MS             60000 // 60s initial penalty for failed AP
#define WAN_PRIMARY_PROBE_IP            "1.1.1.1"
#define WAN_SECONDARY_PROBE_IP          "8.8.8.8"
#define WAN_PROBE_PORT                  53

// Unified UART Logger (routes safely to Serial before CliShell::init(), and CliShell after)
void apLog(const char* format, ...) __attribute__((format(printf, 1, 2)));
