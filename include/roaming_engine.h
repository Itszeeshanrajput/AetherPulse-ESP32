#pragma once

#include "config.h"
#include "storage_manager.h"
#include <WiFi.h>

class KalmanFilter1D {
public:
    KalmanFilter1D(float q = 0.008f, float r = 4.500f) 
        : q_(q), r_(r), x_(-80.0f), p_(1.0f), k_(0.0f), initialized_(false) {}

    void reset(float initial_val) {
        x_ = initial_val;
        p_ = 1.0f;
        initialized_ = true;
    }

    float update(float measurement) {
        if (!initialized_) {
            reset(measurement);
            return x_;
        }
        // Prediction
        p_ = p_ + q_;
        // Update
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
    void update(); // Called in loop / periodic task

    // Manual controls
    void startScan();
    bool connectToNetwork(const String& ssid, const String& password);
    void disconnect();
    void triggerRoamEvaluation();

    // Queries
    bool isConnected() const;
    String getCurrentSsid() const { return currentSsid; }
    String getCurrentBssid() const { return currentBssid; }
    float getCurrentRssi() const { return currentRssiFiltered; }
    int getCurrentChannel() const { return currentChannel; }
    RoamState getState() const { return state; }
    std::vector<ScannedApInfo> getLastScanResults() const { return lastScanResults; }
    bool isScanning() const { return scanning; }

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
    float candidateRssi;

    KalmanFilter1D rssiFilter;
    std::vector<ScannedApInfo> lastScanResults;

    void processScanResults();
    void executeHandover();
    void updateLedStatus();
};
