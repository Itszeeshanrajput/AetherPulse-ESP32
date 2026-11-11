#pragma once

#include "config.h"
#include <Preferences.h>
#include <vector>

struct KnownNetwork {
    String ssid;
    String password;
    int priority; // Higher = preferred
    float lastRssi;
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
    bool adBlockEnabled;
    String adminPassword;
};

class StorageManager {
public:
    static StorageManager& instance();

    bool begin();
    
    // Known Networks
    std::vector<KnownNetwork> getKnownNetworks();
    bool saveKnownNetworks(const std::vector<KnownNetwork>& networks);
    bool addKnownNetwork(const String& ssid, const String& password, int priority = 1);
    bool removeKnownNetwork(const String& ssid);
    bool clearKnownNetworks();

    // Repeater Settings
    RepeaterSettings getSettings();
    bool saveSettings(const RepeaterSettings& settings);

private:
    StorageManager();
    Preferences prefs;
    void loadDefaults(RepeaterSettings& s);
};
