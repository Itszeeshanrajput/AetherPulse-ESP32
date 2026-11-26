#pragma once

#include "config.h"
#include "storage_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_attr.h>
#include <vector>

// RTC Slow SRAM Fast Reconnection Structure (survives warm reset and WDT reboot)
struct RtcFastConnectCache {
    uint32_t magic;
    uint8_t bssid[6];
    uint8_t channel;
    char ssid[33];
    bool valid;
};

class KalmanFilter1D {
public:
    KalmanFilter1D(float q = 0.008f, float r = 4.500f)
        : q_(q), r_(r), x_(-80.0f), p_(1.0f), k_(0.0f), initialized_(false) {}

    void reset(float initial_val = -80.0f, bool initState = true) {
        x_ = initial_val;
        p_ = 1.0f;
        initialized_ = initState;
    }

    float update(float measurement) {
        if (!initialized_) {
            reset(measurement, true);
            return x_;
        }
        p_ = p_ + q_;
        k_ = p_ / (p_ + r_);
        x_ = x_ + k_ * (measurement - x_);
        p_ = (1.0f - k_) * p_;
        return x_;
    }

    float get() const { return x_; }

private:
    float q_;
    float r_;
    float x_;
    float p_;
    float k_;
    bool initialized_;
};

enum RoamState {
    ROAM_STATE_IDLE,
    ROAM_STATE_EVALUATING,
    ROAM_STATE_HANDOVER,
    ROAM_STATE_COOLDOWN
};

enum WanHealthState : uint8_t {
    WAN_STATE_DISABLED,
    WAN_STATE_IDLE_WAIT,
    WAN_STATE_HEALTHY,
    WAN_STATE_DEGRADED_CONFIRM,
    WAN_STATE_FAILOVER_TRIGGERED,
    WAN_STATE_COOLDOWN
};

struct WanBackoffRecord {
    String ssid;
    String bssid;
    uint8_t failCount;
    unsigned long lastFailTime;
    unsigned long backoffMs;
};

struct ScannedApInfo {
    String ssid;
    String bssid;
    int32_t rssi;
    int32_t channel;
    uint8_t encryptionType;
    bool isKnown;
};

class RoamingEngine {
public:
    static RoamingEngine& instance();

    void init();
    void update();

    // Manual controls
    bool startScan();
    bool connectToNetwork(const String& ssid, const String& password, const String& targetBssid = "", int targetChannel = 0);
    void disconnect();
    void triggerRoamEvaluation();
    bool applyClonedMac(const String& macStr);
    void resetMac();
    int getBestChannel();
    std::vector<int> getChannelLoads();
    bool isSubnetConflict(IPAddress staIp);
    void updateDynamicTpc();
    void setTxPowerDbm(float dbm);
    void syncSoftApConfig();

    // RTC Slow SRAM Fast Reconnection Cache
    void cacheFastConnect(const String& ssid, const uint8_t* bssid, uint8_t channel);
    bool getCachedFastConnect(const String& ssid, uint8_t* outBssid, uint8_t* outChannel);

    // Queries
    bool isConnected() const;
    bool isScanning() const { return scanning; }
    String getCurrentSsid() const { return currentSsid; }
    String getCurrentBssid() const { return currentBssid; }
    float getCurrentRssi() const { return currentRssiFiltered; }
    int getCurrentChannel() const { return currentChannel; }
    float getCurrentTxPowerDbm() const { return currentTxPowerDbm; }
    std::vector<ScannedApInfo> getLastScanResults() const { return lastScanResults; }

    // WAN Internet Health Watchdog & Fast Failover
    bool executeWanProbe(float* outRttMs = nullptr, const char* targetIp = nullptr);
    bool isWanOnline() const { return wanOnline; }
    float getWanRttMs() const { return wanLastRttMs; }
    uint8_t getWanConsecutiveFails() const { return wanConsecutiveFails; }
    uint32_t getWanFailoverCount() const { return wanFailoverCount; }
    void triggerWanFailover();
    bool isWanInBackoff(const String& ssid) const;
    void recordWanFailure(const String& ssid, const String& bssid);
    void clearWanFailure(const String& ssid);
    void clearAllWanBackoff();
    std::vector<WanBackoffRecord> getWanBackoffList() const { return wanBackoffList; }

private:
    RoamingEngine();

    RoamState state;
    bool scanning;
    unsigned long lastScanTime;
    unsigned long lastRoamTime;
    unsigned long candidateFirstSeenTime;

    String currentSsid;
    String currentBssid;
    int currentChannel;
    float currentRssiFiltered;

    String candidateSsid;
    String candidateBssid;
    String candidatePass;
    int candidateChannel;
    float candidateRssi;

    KalmanFilter1D rssiFilter;
    std::vector<ScannedApInfo> lastScanResults;
    float currentTxPowerDbm;
    int lastAppliedTxRaw;
    float apClientRssiEwma;
    bool apClientEwmaInit;
    unsigned long lastTpcTime;
    unsigned long lastRssiSampleTime;

    // WAN Internet Health Watchdog
    WanHealthState wanState;
    bool wanOnline;
    float wanLastRttMs;
    uint8_t wanConsecutiveFails;
    uint32_t wanFailoverCount;
    unsigned long lastWanProbeTime;
    unsigned long lastWanFailoverTime;
    std::vector<WanBackoffRecord> wanBackoffList;
    bool probePrimaryTarget;

    void updateWanWatchdog();
    void processScanResults();
    void executeHandover();
    void updateLedStatus();
};
