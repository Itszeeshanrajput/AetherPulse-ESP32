#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ==============================================================================
// AETHERPULSE EXTREME - HARDWARE & SYSTEM CONFIGURATION
// Target: ESP32-D0WD-V3 (Silicon ECO 3.1)
// ==============================================================================

#define PROJECT_NAME              "AetherPulse Extreme"
#define PROJECT_VERSION           "v2.4.0-Enterprise"
#define PROJECT_AUTHOR            "Advanced Systems"

// Hardware Pin Configuration
#define PIN_RGB_RED               4    // Active LOW LED
#define PIN_RGB_GREEN             16   // Active LOW LED
#define PIN_RGB_BLUE              17   // Active LOW LED
#define PIN_TFT_BL                27   // Backlight (Disabled / Kept LOW)

// RF & Signal Calibration Settings
#define DEFAULT_TX_POWER_DBM      16.0f // +16.0 dBm (64 * 0.25 dBm) - Optimal EVM linearity
#define DEFAULT_TX_POWER_RAW      64    // 64 * 0.25 = 16.00 dBm
#define MAX_TX_POWER_RAW          82    // +20.5 dBm max
#define MIN_TX_POWER_RAW          40    // +10.0 dBm min

// Anti-Jitter & Store-and-Forward Timing
#define SOFTAP_BEACON_INTERVAL_TU 103   // 103 TU (105.47ms) - Prime offset prevents Root AP collisions
#define SOFTAP_DTIM_PERIOD        1     // Instant transmission of buffered multicast/unicast frames
#define TCP_MSS_CLAMP_VALUE       1412  // Clamps TCP MSS to eliminate PMTU black-hole drops

// NAPT Routing Engine
#define NAPT_MAX_FLOWS            1024  // Concurrent stateful connection capacity (24B each)
#define NAPT_MAX_PORTMAPS         32    // Port-forwarding mapping entries

// Smart Roaming & Multi-AP Parameters
#define MAX_KNOWN_NETWORKS        10
#define ROAM_RSSI_TRIGGER_DBM     -76.0f // Begin candidate evaluation when RSSI drops below this
#define ROAM_HYSTERESIS_DELTA_DBM 8.0f   // Candidate must be at least 8 dB stronger
#define ROAM_DWELL_TIME_MS        3000   // Candidate must sustain strength for 3 seconds
#define ROAM_COOLDOWN_MS          30000  // 30 seconds hold-down after roaming to prevent flapping

// Default SoftAP Configuration
#define DEFAULT_AP_SSID           "AetherPulse-Extender"
#define DEFAULT_AP_PASS           "aetherpulse123"
#define DEFAULT_AP_CHANNEL        6
#define DEFAULT_AP_MAX_CLIENTS    8
#define DEFAULT_AP_IP             IPAddress(192, 168, 4, 1)
#define DEFAULT_AP_NETMASK        IPAddress(255, 255, 255, 0)

// Serial CLI Configuration
#define CLI_BAUD_RATE             115200
#define CLI_BUFFER_SIZE           256

// Telemetry & Bandwidth Monitoring
#define TELEMETRY_INTERVAL_MS     1000
