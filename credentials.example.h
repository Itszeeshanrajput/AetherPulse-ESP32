#pragma once

// ====================================================================
// AETHERPULSE EXTREME - OPTIONAL COMPILE-TIME CREDENTIALS TEMPLATE
// ====================================================================
// To optionally pre-seed your known Wi-Fi networks at compile-time:
// 1. Copy this file to "credentials.h" (credentials.h is git-ignored)
// 2. Fill in your preferred SSIDs and passwords below
// 3. Compile and flash your board
//
// NOTE: You can also configure, add, and remove networks dynamically at
// runtime anytime via the Web Portal (http://192.168.4.1) or Serial CLI!

#define DEFAULT_FAILOVER_SSID_1 "MyHomeWiFi"
#define DEFAULT_FAILOVER_PASS_1 "MySecretPassword123"
#define DEFAULT_FAILOVER_PRIO_1 10

#define DEFAULT_FAILOVER_SSID_2 "OfficeBackup"
#define DEFAULT_FAILOVER_PASS_2 "OfficeSecurePass!"
#define DEFAULT_FAILOVER_PRIO_2 8
