#include "storage_manager.h"

#if __has_include("credentials.h")
#include "credentials.h"
#endif

StorageManager& StorageManager::instance() {
    static StorageManager s;
    return s;
}

StorageManager::StorageManager() : adBlockCounterSession(0) {}

bool StorageManager::begin() {
    bool ok = prefs.begin("aetherpulse", false);
    
#if __has_include("credentials.h")
    // Pre-seed failover networks if defined in private credentials.h
    if (!prefs.isKey("seeded_user_v1")) {
        Serial.println("[STORAGE] Pre-seeding user failover networks from credentials.h...");
        #ifdef DEFAULT_FAILOVER_SSID_1
        addKnownNetwork(DEFAULT_FAILOVER_SSID_1, DEFAULT_FAILOVER_PASS_1, DEFAULT_FAILOVER_PRIO_1);
        #endif
        #ifdef DEFAULT_FAILOVER_SSID_2
        addKnownNetwork(DEFAULT_FAILOVER_SSID_2, DEFAULT_FAILOVER_PASS_2, DEFAULT_FAILOVER_PRIO_2);
        #endif
        #ifdef DEFAULT_FAILOVER_SSID_3
        addKnownNetwork(DEFAULT_FAILOVER_SSID_3, DEFAULT_FAILOVER_PASS_3, DEFAULT_FAILOVER_PRIO_3);
        #endif
        #ifdef DEFAULT_FAILOVER_SSID_4
        addKnownNetwork(DEFAULT_FAILOVER_SSID_4, DEFAULT_FAILOVER_PASS_4, DEFAULT_FAILOVER_PRIO_4);
        #endif
        prefs.putBool("seeded_user_v1", true);
        Serial.println("[STORAGE] User failover networks seeded successfully.");
    }
#endif
    return ok;
}

void StorageManager::loadDefaults(RepeaterSettings& s) {
    s.apSSID = DEFAULT_AP_SSID;
    s.apPassword = DEFAULT_AP_PASS;
    s.apChannel = DEFAULT_AP_CHANNEL;
    s.apHidden = false;
    s.apMaxClients = DEFAULT_AP_MAX_CLIENTS;
    s.txPowerDbm = DEFAULT_TX_POWER_DBM;
    s.autoRoamEnabled = true;
    s.roamThresholdDbm = ROAM_RSSI_TRIGGER_DBM;
    s.roamHysteresisDbm = ROAM_HYSTERESIS_DELTA_DBM;
    s.adBlockEnabled = true;
    s.adminPassword = "admin";
    s.clonedMac = "";
    s.adBlocksTotal = 0;
}

std::vector<KnownNetwork> StorageManager::getKnownNetworks() {
    std::vector<KnownNetwork> list;
    int count = prefs.getInt("net_count", 0);
    for (int i = 0; i < count; i++) {
        String key_s = "n_s_" + String(i);
        String key_p = "n_p_" + String(i);
        String key_r = "n_r_" + String(i);
        if (prefs.isKey(key_s.c_str())) {
            KnownNetwork net;
            net.ssid = prefs.getString(key_s.c_str(), "");
            net.password = prefs.getString(key_p.c_str(), "");
            net.priority = prefs.getInt(key_r.c_str(), 1);
            net.lastRssi = -100.0f;
            if (net.ssid.length() > 0) {
                list.push_back(net);
            }
        }
    }
    return list;
}

bool StorageManager::saveKnownNetworks(const std::vector<KnownNetwork>& networks) {
    clearKnownNetworks();
    int count = min((int)networks.size(), MAX_KNOWN_NETWORKS);
    prefs.putInt("net_count", count);
    for (int i = 0; i < count; i++) {
        String key_s = "n_s_" + String(i);
        String key_p = "n_p_" + String(i);
        String key_r = "n_r_" + String(i);
        prefs.putString(key_s.c_str(), networks[i].ssid);
        prefs.putString(key_p.c_str(), networks[i].password);
        prefs.putInt(key_r.c_str(), networks[i].priority);
    }
    return true;
}

bool StorageManager::addKnownNetwork(const String& ssid, const String& password, int priority) {
    std::vector<KnownNetwork> list = getKnownNetworks();
    for (auto& n : list) {
        if (n.ssid == ssid) {
            n.password = password;
            n.priority = priority;
            return saveKnownNetworks(list);
        }
    }
    KnownNetwork net;
    net.ssid = ssid;
    net.password = password;
    net.priority = priority;
    list.push_back(net);
    return saveKnownNetworks(list);
}

bool StorageManager::removeKnownNetwork(const String& ssid) {
    std::vector<KnownNetwork> list = getKnownNetworks();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->ssid == ssid) {
            list.erase(it);
            return saveKnownNetworks(list);
        }
    }
    return false;
}

bool StorageManager::clearKnownNetworks() {
    int count = prefs.getInt("net_count", 0);
    for (int i = 0; i < count + 5; i++) {
        String key_s = "n_s_" + String(i);
        String key_p = "n_p_" + String(i);
        String key_r = "n_r_" + String(i);
        prefs.remove(key_s.c_str());
        prefs.remove(key_p.c_str());
        prefs.remove(key_r.c_str());
    }
    prefs.putInt("net_count", 0);
    return true;
}

std::vector<PortForwardRule> StorageManager::getPortForwards() {
    std::vector<PortForwardRule> list;
    int count = prefs.getInt("pf_count", 0);
    for (int i = 0; i < count; i++) {
        String kp = "pf_p_" + String(i);
        String ke = "pf_e_" + String(i);
        String ki = "pf_i_" + String(i);
        String kd = "pf_d_" + String(i);
        String kdesc = "pf_c_" + String(i);
        if (prefs.isKey(ke.c_str())) {
            PortForwardRule r;
            r.protocol = (uint8_t)prefs.getInt(kp.c_str(), 6);
            r.externalPort = (uint16_t)prefs.getInt(ke.c_str(), 0);
            r.internalIp = prefs.getString(ki.c_str(), "192.168.4.2");
            r.internalPort = (uint16_t)prefs.getInt(kd.c_str(), 0);
            r.description = prefs.getString(kdesc.c_str(), "");
            if (r.externalPort > 0 && r.internalPort > 0) {
                list.push_back(r);
            }
        }
    }
    return list;
}

bool StorageManager::savePortForwards(const std::vector<PortForwardRule>& rules) {
    int oldCount = prefs.getInt("pf_count", 0);
    for (int i = 0; i < oldCount + 2; i++) {
        prefs.remove(("pf_p_" + String(i)).c_str());
        prefs.remove(("pf_e_" + String(i)).c_str());
        prefs.remove(("pf_i_" + String(i)).c_str());
        prefs.remove(("pf_d_" + String(i)).c_str());
        prefs.remove(("pf_c_" + String(i)).c_str());
    }
    int count = min((int)rules.size(), NAPT_MAX_PORTMAPS);
    prefs.putInt("pf_count", count);
    for (int i = 0; i < count; i++) {
        prefs.putInt(("pf_p_" + String(i)).c_str(), rules[i].protocol);
        prefs.putInt(("pf_e_" + String(i)).c_str(), rules[i].externalPort);
        prefs.putString(("pf_i_" + String(i)).c_str(), rules[i].internalIp);
        prefs.putInt(("pf_d_" + String(i)).c_str(), rules[i].internalPort);
        prefs.putString(("pf_c_" + String(i)).c_str(), rules[i].description);
    }
    return true;
}

bool StorageManager::addPortForward(uint8_t proto, uint16_t extPort, const String& intIp, uint16_t intPort, const String& desc) {
    auto list = getPortForwards();
    for (auto& r : list) {
        if (r.protocol == proto && r.externalPort == extPort) {
            r.internalIp = intIp;
            r.internalPort = intPort;
            r.description = desc;
            return savePortForwards(list);
        }
    }
    PortForwardRule rule;
    rule.protocol = proto;
    rule.externalPort = extPort;
    rule.internalIp = intIp;
    rule.internalPort = intPort;
    rule.description = desc;
    list.push_back(rule);
    return savePortForwards(list);
}

bool StorageManager::removePortForward(uint8_t proto, uint16_t extPort) {
    auto list = getPortForwards();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->protocol == proto && it->externalPort == extPort) {
            list.erase(it);
            return savePortForwards(list);
        }
    }
    return false;
}

RepeaterSettings StorageManager::getSettings() {
    RepeaterSettings s;
    loadDefaults(s);
    if (!prefs.isKey("cfg_init")) {
        saveSettings(s);
        return s;
    }
    s.apSSID = prefs.getString("ap_ssid", s.apSSID);
    s.apPassword = prefs.getString("ap_pass", s.apPassword);
    s.apChannel = prefs.getInt("ap_chan", s.apChannel);
    s.apHidden = prefs.getBool("ap_hid", s.apHidden);
    s.apMaxClients = prefs.getInt("ap_max", s.apMaxClients);
    s.txPowerDbm = prefs.getFloat("tx_pwr", s.txPowerDbm);
    s.autoRoamEnabled = prefs.getBool("roam_en", s.autoRoamEnabled);
    s.roamThresholdDbm = prefs.getFloat("roam_th", s.roamThresholdDbm);
    s.roamHysteresisDbm = prefs.getFloat("roam_hy", s.roamHysteresisDbm);
    s.adBlockEnabled = prefs.getBool("ad_blk", s.adBlockEnabled);
    s.adminPassword = prefs.getString("adm_pwd", s.adminPassword);
    s.clonedMac = prefs.getString("cloned_mac", "");
    s.adBlocksTotal = prefs.getUInt("ad_blocks_tot", 0);
    return s;
}

bool StorageManager::saveSettings(const RepeaterSettings& s) {
    prefs.putBool("cfg_init", true);
    prefs.putString("ap_ssid", s.apSSID);
    prefs.putString("ap_pass", s.apPassword);
    prefs.putInt("ap_chan", s.apChannel);
    prefs.putBool("ap_hid", s.apHidden);
    prefs.putInt("ap_max", s.apMaxClients);
    prefs.putFloat("tx_pwr", s.txPowerDbm);
    prefs.putBool("roam_en", s.autoRoamEnabled);
    prefs.putFloat("roam_th", s.roamThresholdDbm);
    prefs.putFloat("roam_hy", s.roamHysteresisDbm);
    prefs.putBool("ad_blk", s.adBlockEnabled);
    prefs.putString("adm_pwd", s.adminPassword);
    prefs.putString("cloned_mac", s.clonedMac);
    prefs.putUInt("ad_blocks_tot", s.adBlocksTotal);
    return true;
}

void StorageManager::setClonedMac(const String& mac) {
    auto s = getSettings();
    s.clonedMac = mac;
    saveSettings(s);
}

void StorageManager::setAdBlockEnabled(bool enabled) {
    auto s = getSettings();
    s.adBlockEnabled = enabled;
    saveSettings(s);
}

void StorageManager::incrementAdBlockCount() {
    adBlockCounterSession++;
    if (adBlockCounterSession % 10 == 0) {
        auto s = getSettings();
        s.adBlocksTotal += 10;
        prefs.putUInt("ad_blocks_tot", s.adBlocksTotal);
    }
}

uint32_t StorageManager::getAdBlockCount() {
    return prefs.getUInt("ad_blocks_tot", 0) + (adBlockCounterSession % 10);
}
