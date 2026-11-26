#pragma once

#include "config.h"
#include <Preferences.h>
#include <vector>
#include <atomic>
#include <esp_wifi.h>
#include <esp_err.h>
#include <esp_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define CRASH_LOG_MAGIC 0x43525348 // "CRSH"
#define CRASH_LOG_MAX_ENTRIES 8

struct KnownNetwork {
    String ssid;
    String password;
    int priority;
};

struct PortForwardRule {
    uint8_t protocol; // 6 = TCP, 17 = UDP
    uint16_t externalPort;
    String internalIp;
    uint16_t internalPort;
    String description;
};

struct RepeaterSettings {
    String apSSID;
    String apPassword;
    int apChannel;
    bool apHidden;
    int apMaxClients;
    float txPowerDbm;
    bool autoRoamEnabled;
    float roamThresholdDbm;
    float roamHysteresisDbm;
    String adminPassword;
    String clonedMac;
    bool clientIsolation;
    bool dynamicTpcEnabled;
    bool wanWatchdogEnabled;
    uint32_t wanProbeIntervalSec;
};

struct CrashLogEntry {
    uint32_t timestamp_sec;
    uint32_t reset_reason;
    uint32_t pc;
    uint32_t excvaddr;
    uint32_t free_heap;
    uint32_t min_free_heap;
    char reason_str[32];
};

struct RtcCrashBuffer {
    uint32_t magic;
    uint32_t count;
    uint32_t head;
    CrashLogEntry entries[CRASH_LOG_MAX_ENTRIES];
    uint32_t crc32;
};

#pragma pack(push, 1)
struct SettingsJournalBlob {
    uint32_t magic;           // 0x41455448 ("AETH")
    uint16_t version;         // 0x0311 (v3.1.0 cleaned)
    char apSSID[33];
    char apPassword[65];
    int32_t apChannel;
    uint8_t apHidden;
    int32_t apMaxClients;
    float txPowerDbm;
    uint8_t autoRoamEnabled;
    float roamThresholdDbm;
    float roamHysteresisDbm;
    char adminPassword[33];
    char clonedMac[18];
    uint8_t clientIsolation;
    uint8_t dynamicTpcEnabled;
    uint8_t wanWatchdogEnabled;
    uint32_t wanProbeIntervalSec;
    uint32_t reserved[2];
    uint32_t crc32;
};
#pragma pack(pop)

class StorageManager {
public:
    static StorageManager& instance();

    bool begin();
    void update();

    // Known Networks
    std::vector<KnownNetwork> getKnownNetworks();
    bool saveKnownNetworks(const std::vector<KnownNetwork>& networks);
    bool addKnownNetwork(const String& ssid, const String& password, int priority = 1);
    bool removeKnownNetwork(const String& ssid);
    bool clearKnownNetworks();

    // Port Forwarding (DNAPT)
    std::vector<PortForwardRule> getPortForwards();
    bool savePortForwards(const std::vector<PortForwardRule>& rules);
    bool addPortForward(uint8_t proto, uint16_t extPort, const String& intIp, uint16_t intPort, const String& desc = "");
    bool removePortForward(uint8_t proto, uint16_t extPort);

    // Repeater Settings (Thread-Safe In-Memory Shadow Journal)
    RepeaterSettings getSettings();
    bool saveSettings(const RepeaterSettings& settings);
    bool commitJournal(bool force = false);
    void setClonedMac(const String& mac);
    void setClientIsolation(bool enabled);
    void setDynamicTpc(bool enabled);
    void setWanWatchdog(bool enabled, uint32_t intervalSec = 3);

    // Client Device Aliases & 24-bit IEEE OUI Vendor Detection
    String getClientAlias(const String& mac);
    bool setClientAlias(const String& mac, const String& alias);
    String getVendorName(const String& mac);

    // Layer-2 Station Isolation & Anti-ARP Spoofing
    bool isClientIsolationActive() const { return clientIsolationAtomic.load(); }
    void installIsolationHook();

    // RTC Slow SRAM Circular Crash Dump
    static void recordCrash(const char* reason, uint32_t pc = 0, uint32_t excvaddr = 0);
    static const RtcCrashBuffer& getCrashLog();
    static void clearCrashLog();
    static const char* getResetReasonName(esp_reset_reason_t reason);

private:
    StorageManager();
    Preferences prefs;
    SemaphoreHandle_t storageMutex;
    void loadDefaults(RepeaterSettings& s);

    RepeaterSettings shadowSettings;
    std::atomic<bool> clientIsolationAtomic;
    uint32_t lastPersistedCrc;
    bool journalDirty;
    unsigned long dirtyTimestamp;
    esp_timer_handle_t debounceTimer;
    static void debounceTimerCallback(void* arg);
    static void shutdownHandler();

    bool isolationHookInstalled;
};
