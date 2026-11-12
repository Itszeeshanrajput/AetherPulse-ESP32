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
    bool adBlockEnabled;
    String adminPassword;
    String clonedMac;        // Station MAC address spoofing
    uint32_t adBlocksTotal;  // Lifetime sinkholed ad/tracker queries
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

    // Port Forwarding (DNAPT)
    std::vector<PortForwardRule> getPortForwards();
    bool savePortForwards(const std::vector<PortForwardRule>& rules);
    bool addPortForward(uint8_t proto, uint16_t extPort, const String& intIp, uint16_t intPort, const String& desc = "");
    bool removePortForward(uint8_t proto, uint16_t extPort);

    // Repeater Settings
    RepeaterSettings getSettings();
    bool saveSettings(const RepeaterSettings& settings);
    void setClonedMac(const String& mac);
    void setAdBlockEnabled(bool enabled);

    // AdBlock Stats
    void incrementAdBlockCount();
    uint32_t getAdBlockCount();

private:
    StorageManager();
    Preferences prefs;
    void loadDefaults(RepeaterSettings& s);
    uint32_t adBlockCounterSession;
};
