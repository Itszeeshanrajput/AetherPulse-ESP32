#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ==============================================================================
// AETHERPULSE EXTREME - HARDWARE & SYSTEM CONFIGURATION
// Target: ESP32-D0WD-V3 (Silicon ECO 3.1) @ 240 MHz Dual-Core
// ==============================================================================

#define PROJECT_NAME              "AetherPulse Extreme"
#define PROJECT_VERSION           "v2.0.0-Titanium"
#define PROJECT_AUTHOR            "AetherPulse Core Team"

// Hardware Pin Configuration
#define PIN_RGB_RED               4    // Active LOW LED
#define PIN_RGB_GREEN             16   // Active LOW LED
#define PIN_RGB_BLUE              17   // Active LOW LED
#define PIN_TFT_BL                27   // Backlight (Disabled to save 100% power/RAM)

// RF & Physical Layer Anti-Jitter Calibration
#define DEFAULT_TX_POWER_DBM      18.5f // Calibrated high-gain RF output
#define DEFAULT_TX_POWER_RAW      74    // 74 * 0.25 = 18.50 dBm
#define MAX_TX_POWER_RAW          82    // +20.5 dBm max
#define MIN_TX_POWER_RAW          40    // +10.0 dBm min

// Anti-Jitter Timing Parameters
#define SOFTAP_BEACON_INTERVAL_TU 103   // 103 TU (105.47ms) - Prime offset prevents phase collisions
#define SOFTAP_DTIM_PERIOD        1     // Instant transmission of buffered multicast/unicast frames
#define TCP_MSS_CLAMP_VALUE       1412  // Clamps TCP MSS to eliminate PMTU black-hole packet drops

// lwIP Hardware NAPT Configuration & Timers
#define NAPT_MAX_FLOWS            1024  // Concurrent stateful connection capacity
#define NAPT_MAX_PORTMAPS         16    // Maximum active port forwarding rules
#define NAPT_UDP_TIMEOUT_SEC      25    // Aggressive UDP flow recycling (prevents DNS/NAT table staling)
#define NAPT_TCP_TIMEOUT_SEC      300   // 5 minutes TCP timeout for half-open/abruptly dropped sockets

// Smart Roaming & Multi-AP Fast-Connect Parameters
#define MAX_KNOWN_NETWORKS        10
#define ROAM_RSSI_TRIGGER_DBM     -76.0f // Begin candidate evaluation when RSSI drops below -76 dBm
#define ROAM_HYSTERESIS_DELTA_DBM 8.0f   // Candidate must be at least 8 dB stronger
#define ROAM_DWELL_TIME_MS        3000   // Candidate must sustain strength for 3 seconds
#define ROAM_COOLDOWN_MS          30000  // 30 seconds hold-down after roaming to prevent flapping

// Default SoftAP Broadcast Settings
#define DEFAULT_AP_SSID           "AetherPulse-Extender"
#define DEFAULT_AP_PASS           "aetherpulse123"
#define DEFAULT_AP_CHANNEL        6
#define DEFAULT_AP_MAX_CLIENTS    8
#define DEFAULT_AP_IP             IPAddress(192, 168, 4, 1)
#define DEFAULT_AP_NETMASK        IPAddress(255, 255, 255, 0)

// Hostname & Discovery
#define MDNS_HOSTNAME             "aetherpulse"

// Watchdog Protection (Core 3.x TWDT)
#define WDT_TIMEOUT_MS            15000 // 15 seconds Task Watchdog Timer

// Serial CLI Configuration
#define CLI_BAUD_RATE             115200
#define CLI_BUFFER_SIZE           256

// Telemetry & Bandwidth Monitoring
#define TELEMETRY_INTERVAL_MS     1000
