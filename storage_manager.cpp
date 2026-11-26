#include "storage_manager.h"
#include <esp_wifi.h>
#include <esp_private/wifi.h>
#include <esp_netif.h>
#include <esp_crc.h>
#include <esp_timer.h>
#include <esp_system.h>

#if __has_include("credentials.h")
#include "credentials.h"
#endif

// ==============================================================================
// RTC SLOW SRAM HARDWARE CRASH LOG
// ==============================================================================
RTC_DATA_ATTR static RtcCrashBuffer rtc_crash_log;

static esp_err_t isolated_ap_rx_callback(void *buffer, uint16_t len, void *eb);

static uint32_t calcCrashLogCrc(const RtcCrashBuffer& buf) {
    return esp_crc32_le(0, (const uint8_t*)&buf, offsetof(RtcCrashBuffer, crc32));
}

StorageManager& StorageManager::instance() {
    static StorageManager s;
    return s;
}

StorageManager::StorageManager() :
    storageMutex(nullptr),
    clientIsolationAtomic(false),
    lastPersistedCrc(0),
    journalDirty(false),
    dirtyTimestamp(0),
    debounceTimer(nullptr),
    isolationHookInstalled(false)
{
    storageMutex = xSemaphoreCreateRecursiveMutex();
    loadDefaults(shadowSettings);
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
    s.adminPassword = "admin";
    s.clonedMac = "";
    s.clientIsolation = false;
    s.dynamicTpcEnabled = TPC_ENABLED_DEFAULT;
    s.wanWatchdogEnabled = WAN_WATCHDOG_ENABLED_DEFAULT;
    s.wanProbeIntervalSec = WAN_PROBE_INTERVAL_HEALTHY_MS / 1000;
}

void StorageManager::debounceTimerCallback(void* arg) {
    StorageManager* self = static_cast<StorageManager*>(arg);
    if (self) {
        self->commitJournal(false);
    }
}

void StorageManager::shutdownHandler() {
    StorageManager::instance().commitJournal(true);
}

bool StorageManager::begin() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);

    bool ok = prefs.begin("aetherpulse", false);

    // Validate RTC crash log magic and CRC
    if (rtc_crash_log.magic != CRASH_LOG_MAGIC ||
        rtc_crash_log.crc32 != calcCrashLogCrc(rtc_crash_log)) {
        rtc_crash_log.magic = CRASH_LOG_MAGIC;
        rtc_crash_log.count = 0;
        rtc_crash_log.head = 0;
        memset(rtc_crash_log.entries, 0, sizeof(rtc_crash_log.entries));
        rtc_crash_log.crc32 = calcCrashLogCrc(rtc_crash_log);
    }

    esp_reset_reason_t rstReason = esp_reset_reason();
    if (rstReason == ESP_RST_PANIC || rstReason == ESP_RST_INT_WDT ||
        rstReason == ESP_RST_TASK_WDT || rstReason == ESP_RST_WDT ||
        rstReason == ESP_RST_BROWNOUT) {
        recordCrash(getResetReasonName(rstReason), 0, 0);
    }

    esp_register_shutdown_handler(&StorageManager::shutdownHandler);

    if (!debounceTimer) {
        esp_timer_create_args_t timer_args = {
            .callback = &StorageManager::debounceTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "sm_debounce",
            .skip_unhandled_events = true
        };
        esp_timer_create(&timer_args, &debounceTimer);
    }

    SettingsJournalBlob blob;
    size_t blobSize = prefs.getBytes("cfg_blob", &blob, sizeof(blob));
    bool blobValid = false;

    if (blobSize == sizeof(blob) && blob.magic == 0x41455448 && blob.version == 0x0311) {
        uint32_t calcCrc = esp_crc32_le(0, (const uint8_t*)&blob, offsetof(SettingsJournalBlob, crc32));
        if (calcCrc == blob.crc32) {
            blobValid = true;
            shadowSettings.apSSID = String(blob.apSSID);
            shadowSettings.apPassword = String(blob.apPassword);
            shadowSettings.apChannel = blob.apChannel;
            shadowSettings.apHidden = blob.apHidden != 0;
            shadowSettings.apMaxClients = blob.apMaxClients;
            shadowSettings.txPowerDbm = blob.txPowerDbm;
            shadowSettings.autoRoamEnabled = blob.autoRoamEnabled != 0;
            shadowSettings.roamThresholdDbm = blob.roamThresholdDbm;
            shadowSettings.roamHysteresisDbm = blob.roamHysteresisDbm;
            shadowSettings.adminPassword = String(blob.adminPassword);
            shadowSettings.clonedMac = String(blob.clonedMac);
            shadowSettings.clientIsolation = blob.clientIsolation != 0;
            shadowSettings.dynamicTpcEnabled = blob.dynamicTpcEnabled != 0;
            shadowSettings.wanWatchdogEnabled = blob.wanWatchdogEnabled != 0;
            shadowSettings.wanProbeIntervalSec = (blob.wanProbeIntervalSec >= 2 && blob.wanProbeIntervalSec <= 300)
                ? blob.wanProbeIntervalSec : (WAN_PROBE_INTERVAL_HEALTHY_MS / 1000);
            lastPersistedCrc = blob.crc32;
        }
    }

    if (!blobValid) {
        loadDefaults(shadowSettings);
        if (prefs.isKey("cfg_init")) {
            shadowSettings.apSSID = prefs.getString("ap_ssid", shadowSettings.apSSID);
            shadowSettings.apPassword = prefs.getString("ap_pass", shadowSettings.apPassword);
            shadowSettings.apChannel = prefs.getInt("ap_chan", shadowSettings.apChannel);
            shadowSettings.apHidden = prefs.getBool("ap_hid", shadowSettings.apHidden);
            shadowSettings.apMaxClients = prefs.getInt("ap_max", shadowSettings.apMaxClients);
            shadowSettings.txPowerDbm = prefs.getFloat("tx_pwr", shadowSettings.txPowerDbm);
            shadowSettings.autoRoamEnabled = prefs.getBool("roam_en", shadowSettings.autoRoamEnabled);
            shadowSettings.roamThresholdDbm = prefs.getFloat("roam_th", shadowSettings.roamThresholdDbm);
            shadowSettings.roamHysteresisDbm = prefs.getFloat("roam_hy", shadowSettings.roamHysteresisDbm);
            shadowSettings.adminPassword = prefs.getString("adm_pwd", shadowSettings.adminPassword);
            shadowSettings.clonedMac = prefs.getString("cloned_mac", "");
            shadowSettings.clientIsolation = prefs.getBool("cl_iso", false);
            shadowSettings.dynamicTpcEnabled = prefs.getBool("dyn_tpc", true);
            shadowSettings.wanWatchdogEnabled = prefs.getBool("wan_wd_en", true);
            uint32_t migratedInt = prefs.getUInt("wan_p_int", WAN_PROBE_INTERVAL_HEALTHY_MS / 1000);
            shadowSettings.wanProbeIntervalSec = (migratedInt >= 2 && migratedInt <= 300) ? migratedInt : 3;
        }
        commitJournal(true);
    }

    clientIsolationAtomic.store(shadowSettings.clientIsolation);

#if __has_include("credentials.h")
    if (!prefs.isKey("seeded_user_v1")) {
        apLog("[STORAGE] Pre-seeding failover networks from credentials.h...\n");
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
    }
#endif

    installIsolationHook();

    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return ok;
}

void StorageManager::update() {
    if (journalDirty && (millis() - dirtyTimestamp >= 5000)) {
        commitJournal(false);
    }
    if (!isolationHookInstalled && (WiFi.getMode() & WIFI_MODE_AP)) {
        installIsolationHook();
    }
}

// ==============================================================================
// LAYER-2 HARDWARE STATION ISOLATION & ANTI-ARP SPOOFING RXCB
// ==============================================================================
static esp_err_t isolated_ap_rx_callback(void *buffer, uint16_t len, void *eb) {
    if (!buffer || len < 14) {
        if (eb) esp_wifi_internal_free_rx_buffer(eb);
        return ESP_OK;
    }

    const uint8_t *eth = (const uint8_t *)buffer;
    const uint8_t *dst_mac = eth;
    uint16_t ether_type = ((uint16_t)eth[12] << 8) | eth[13];

    static esp_netif_t *s_ap_netif = nullptr;
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }

    // Anti-Gratuitous ARP Gateway Spoofing Protection
    if (ether_type == 0x0806 && len >= 42) {
        const uint8_t *sha = eth + 22;
        const uint8_t *spa = eth + 28;
        if (s_ap_netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
                if (memcmp(spa, &ip_info.ip.addr, 4) == 0) {
                    uint8_t ap_mac[6];
                    esp_wifi_get_mac(WIFI_IF_AP, ap_mac);
                    if (memcmp(sha, ap_mac, 6) != 0) {
                        if (eb) esp_wifi_internal_free_rx_buffer(eb);
                        return ESP_OK;
                    }
                }
            }
        }
    }

    // Intra-BSS Peer-to-Peer Unicast Isolation (lock-free atomic read)
    if (StorageManager::instance().isClientIsolationActive()) {
        if ((dst_mac[0] & 0x01) == 0) {
            wifi_sta_list_t sta_list;
            memset(&sta_list, 0, sizeof(sta_list));
            if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
                for (int i = 0; i < sta_list.num; i++) {
                    if (memcmp(dst_mac, sta_list.sta[i].mac, 6) == 0) {
                        if (eb) esp_wifi_internal_free_rx_buffer(eb);
                        return ESP_OK;
                    }
                }
            }
        }
    }

    if (s_ap_netif) {
        return esp_netif_receive(s_ap_netif, buffer, len, eb);
    }
    if (eb) esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

void StorageManager::installIsolationHook() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    if (!isolationHookInstalled) {
        esp_err_t err = esp_wifi_internal_reg_rxcb(WIFI_IF_AP, isolated_ap_rx_callback);
        if (err == ESP_OK) {
            isolationHookInstalled = true;
            apLog("[SECURITY] L2 Station Isolation & Anti-ARP RXCB installed.\n");
        }
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
}

// ==============================================================================
// REPEATER SETTINGS & ATOMIC SHADOW JOURNAL
// ==============================================================================
RepeaterSettings StorageManager::getSettings() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    RepeaterSettings s = shadowSettings;
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return s;
}

static void packSettingsBlob(const RepeaterSettings& s, SettingsJournalBlob& blob) {
    memset(&blob, 0, sizeof(blob));
    blob.magic = 0x41455448;
    blob.version = 0x0311;

    strncpy(blob.apSSID, s.apSSID.c_str(), sizeof(blob.apSSID) - 1);
    strncpy(blob.apPassword, s.apPassword.c_str(), sizeof(blob.apPassword) - 1);
    blob.apChannel = s.apChannel;
    blob.apHidden = s.apHidden ? 1 : 0;
    blob.apMaxClients = s.apMaxClients;
    blob.txPowerDbm = s.txPowerDbm;
    blob.autoRoamEnabled = s.autoRoamEnabled ? 1 : 0;
    blob.roamThresholdDbm = s.roamThresholdDbm;
    blob.roamHysteresisDbm = s.roamHysteresisDbm;
    strncpy(blob.adminPassword, s.adminPassword.c_str(), sizeof(blob.adminPassword) - 1);
    strncpy(blob.clonedMac, s.clonedMac.c_str(), sizeof(blob.clonedMac) - 1);
    blob.clientIsolation = s.clientIsolation ? 1 : 0;
    blob.dynamicTpcEnabled = s.dynamicTpcEnabled ? 1 : 0;
    blob.wanWatchdogEnabled = s.wanWatchdogEnabled ? 1 : 0;
    blob.wanProbeIntervalSec = s.wanProbeIntervalSec;

    blob.crc32 = esp_crc32_le(0, (const uint8_t*)&blob, offsetof(SettingsJournalBlob, crc32));
}

bool StorageManager::saveSettings(const RepeaterSettings& s) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    shadowSettings = s;
    clientIsolationAtomic.store(s.clientIsolation);

    SettingsJournalBlob blob;
    packSettingsBlob(shadowSettings, blob);

    if (blob.crc32 == lastPersistedCrc) {
        if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
        return true;
    }

    journalDirty = true;
    dirtyTimestamp = millis();

    if (debounceTimer) {
        esp_timer_stop(debounceTimer);
        esp_timer_start_once(debounceTimer, 5000000ULL);
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return true;
}

bool StorageManager::commitJournal(bool force) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    if (!journalDirty && !force) {
        if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
        return true;
    }

    SettingsJournalBlob blob;
    packSettingsBlob(shadowSettings, blob);
    size_t written = prefs.putBytes("cfg_blob", &blob, sizeof(blob));
    if (written == sizeof(blob)) {
        lastPersistedCrc = blob.crc32;
        journalDirty = false;
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return (written == sizeof(blob));
}

void StorageManager::setClonedMac(const String& mac) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    shadowSettings.clonedMac = mac;
    saveSettings(shadowSettings);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
}

void StorageManager::setClientIsolation(bool enabled) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    shadowSettings.clientIsolation = enabled;
    clientIsolationAtomic.store(enabled);
    saveSettings(shadowSettings);
    installIsolationHook();
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
}

void StorageManager::setDynamicTpc(bool enabled) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    shadowSettings.dynamicTpcEnabled = enabled;
    saveSettings(shadowSettings);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
}

void StorageManager::setWanWatchdog(bool enabled, uint32_t intervalSec) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    shadowSettings.wanWatchdogEnabled = enabled;
    if (intervalSec >= 2 && intervalSec <= 300) {
        shadowSettings.wanProbeIntervalSec = intervalSec;
    }
    saveSettings(shadowSettings);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
}

// ==============================================================================
// RTC SLOW SRAM HARDWARE CRASH LOG
// ==============================================================================
void StorageManager::recordCrash(const char* reason, uint32_t pc, uint32_t excvaddr) {
    if (rtc_crash_log.magic != CRASH_LOG_MAGIC) {
        rtc_crash_log.magic = CRASH_LOG_MAGIC;
        rtc_crash_log.count = 0;
        rtc_crash_log.head = 0;
        memset(rtc_crash_log.entries, 0, sizeof(rtc_crash_log.entries));
    }

    uint32_t idx = rtc_crash_log.head % CRASH_LOG_MAX_ENTRIES;
    CrashLogEntry& e = rtc_crash_log.entries[idx];

    e.timestamp_sec = millis() / 1000;
    e.reset_reason = (uint32_t)esp_reset_reason();
    e.pc = pc;
    e.excvaddr = excvaddr;
    e.free_heap = ESP.getFreeHeap();
    e.min_free_heap = ESP.getMinFreeHeap();

    strncpy(e.reason_str, reason ? reason : "Unknown", sizeof(e.reason_str) - 1);
    e.reason_str[sizeof(e.reason_str) - 1] = '\0';

    rtc_crash_log.head = (idx + 1) % CRASH_LOG_MAX_ENTRIES;
    if (rtc_crash_log.count < CRASH_LOG_MAX_ENTRIES) {
        rtc_crash_log.count++;
    }
    rtc_crash_log.crc32 = calcCrashLogCrc(rtc_crash_log);
}

const RtcCrashBuffer& StorageManager::getCrashLog() {
    return rtc_crash_log;
}

void StorageManager::clearCrashLog() {
    rtc_crash_log.magic = CRASH_LOG_MAGIC;
    rtc_crash_log.count = 0;
    rtc_crash_log.head = 0;
    memset(rtc_crash_log.entries, 0, sizeof(rtc_crash_log.entries));
    rtc_crash_log.crc32 = calcCrashLogCrc(rtc_crash_log);
}

const char* StorageManager::getResetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "POWER_ON";
        case ESP_RST_EXT:       return "EXTERNAL_PIN";
        case ESP_RST_SW:        return "SOFTWARE_RESET";
        case ESP_RST_PANIC:     return "HARDWARE_PANIC";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
        case ESP_RST_WDT:       return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT:  return "BROWNOUT_VOLTAGE_SAG";
        case ESP_RST_SDIO:      return "SDIO_RESET";
        default:                return "UNKNOWN";
    }
}

// ==============================================================================
// KNOWN NETWORKS (FAILOVER POOL)
// ==============================================================================
std::vector<KnownNetwork> StorageManager::getKnownNetworks() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    std::vector<KnownNetwork> list;
    int count = prefs.getInt("net_count", 0);
    if (count > MAX_KNOWN_NETWORKS) count = MAX_KNOWN_NETWORKS;
    for (int i = 0; i < count; i++) {
        char ks[12], kp[12], kr[12];
        snprintf(ks, sizeof(ks), "n_s_%d", i);
        snprintf(kp, sizeof(kp), "n_p_%d", i);
        snprintf(kr, sizeof(kr), "n_r_%d", i);
        if (prefs.isKey(ks)) {
            KnownNetwork net;
            net.ssid = prefs.getString(ks, "");
            net.password = prefs.getString(kp, "");
            net.priority = prefs.getInt(kr, 1);
            if (net.ssid.length() > 0) {
                list.push_back(net);
            }
        }
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return list;
}

bool StorageManager::saveKnownNetworks(const std::vector<KnownNetwork>& networks) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    clearKnownNetworks();
    int count = min((int)networks.size(), MAX_KNOWN_NETWORKS);
    prefs.putInt("net_count", count);
    for (int i = 0; i < count; i++) {
        char ks[12], kp[12], kr[12];
        snprintf(ks, sizeof(ks), "n_s_%d", i);
        snprintf(kp, sizeof(kp), "n_p_%d", i);
        snprintf(kr, sizeof(kr), "n_r_%d", i);
        prefs.putString(ks, networks[i].ssid);
        prefs.putString(kp, networks[i].password);
        prefs.putInt(kr, networks[i].priority);
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return true;
}

bool StorageManager::addKnownNetwork(const String& ssid, const String& password, int priority) {
    if (ssid.length() == 0 || ssid.length() > 32) return false;
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    std::vector<KnownNetwork> list = getKnownNetworks();
    for (auto& n : list) {
        if (n.ssid == ssid) {
            n.password = password;
            if (priority > 1 || n.priority <= 1) {
                n.priority = priority;
            }
            bool ok = saveKnownNetworks(list);
            if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
            return ok;
        }
    }
    if ((int)list.size() >= MAX_KNOWN_NETWORKS) {
        if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
        return false;
    }
    KnownNetwork net;
    net.ssid = ssid;
    net.password = password;
    net.priority = priority;
    list.push_back(net);
    bool ok = saveKnownNetworks(list);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return ok;
}

bool StorageManager::removeKnownNetwork(const String& ssid) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    std::vector<KnownNetwork> list = getKnownNetworks();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->ssid == ssid) {
            list.erase(it);
            bool ok = saveKnownNetworks(list);
            if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
            return ok;
        }
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return false;
}

bool StorageManager::clearKnownNetworks() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    int count = prefs.getInt("net_count", 0);
    for (int i = 0; i < count + 2 && i < MAX_KNOWN_NETWORKS + 2; i++) {
        char ks[12], kp[12], kr[12];
        snprintf(ks, sizeof(ks), "n_s_%d", i);
        snprintf(kp, sizeof(kp), "n_p_%d", i);
        snprintf(kr, sizeof(kr), "n_r_%d", i);
        if (prefs.isKey(ks)) prefs.remove(ks);
        if (prefs.isKey(kp)) prefs.remove(kp);
        if (prefs.isKey(kr)) prefs.remove(kr);
    }
    prefs.putInt("net_count", 0);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return true;
}

// ==============================================================================
// PORT FORWARDING (DNAPT)
// ==============================================================================
std::vector<PortForwardRule> StorageManager::getPortForwards() {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    std::vector<PortForwardRule> list;
    int count = prefs.getInt("pf_count", 0);
    if (count > NAPT_MAX_PORTMAPS) count = NAPT_MAX_PORTMAPS;
    for (int i = 0; i < count; i++) {
        char kp[12], ke[12], ki[12], kd[12], kc[12];
        snprintf(kp, sizeof(kp), "pf_p_%d", i);
        snprintf(ke, sizeof(ke), "pf_e_%d", i);
        snprintf(ki, sizeof(ki), "pf_i_%d", i);
        snprintf(kd, sizeof(kd), "pf_d_%d", i);
        snprintf(kc, sizeof(kc), "pf_c_%d", i);
        if (prefs.isKey(ke)) {
            PortForwardRule r;
            r.protocol = (uint8_t)prefs.getInt(kp, 6);
            r.externalPort = (uint16_t)prefs.getInt(ke, 0);
            r.internalIp = prefs.getString(ki, "192.168.4.2");
            r.internalPort = (uint16_t)prefs.getInt(kd, 0);
            r.description = prefs.getString(kc, "");
            if (r.externalPort > 0 && r.internalPort > 0) {
                list.push_back(r);
            }
        }
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return list;
}

bool StorageManager::savePortForwards(const std::vector<PortForwardRule>& rules) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    int oldCount = prefs.getInt("pf_count", 0);
    for (int i = 0; i < oldCount + 2 && i < NAPT_MAX_PORTMAPS + 2; i++) {
        char kp[12], ke[12], ki[12], kd[12], kc[12];
        snprintf(kp, sizeof(kp), "pf_p_%d", i);
        snprintf(ke, sizeof(ke), "pf_e_%d", i);
        snprintf(ki, sizeof(ki), "pf_i_%d", i);
        snprintf(kd, sizeof(kd), "pf_d_%d", i);
        snprintf(kc, sizeof(kc), "pf_c_%d", i);
        if (prefs.isKey(kp)) prefs.remove(kp);
        if (prefs.isKey(ke)) prefs.remove(ke);
        if (prefs.isKey(ki)) prefs.remove(ki);
        if (prefs.isKey(kd)) prefs.remove(kd);
        if (prefs.isKey(kc)) prefs.remove(kc);
    }
    int count = min((int)rules.size(), NAPT_MAX_PORTMAPS);
    prefs.putInt("pf_count", count);
    for (int i = 0; i < count; i++) {
        char kp[12], ke[12], ki[12], kd[12], kc[12];
        snprintf(kp, sizeof(kp), "pf_p_%d", i);
        snprintf(ke, sizeof(ke), "pf_e_%d", i);
        snprintf(ki, sizeof(ki), "pf_i_%d", i);
        snprintf(kd, sizeof(kd), "pf_d_%d", i);
        snprintf(kc, sizeof(kc), "pf_c_%d", i);
        prefs.putInt(kp, rules[i].protocol);
        prefs.putInt(ke, rules[i].externalPort);
        prefs.putString(ki, rules[i].internalIp);
        prefs.putInt(kd, rules[i].internalPort);
        prefs.putString(kc, rules[i].description);
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return true;
}

bool StorageManager::addPortForward(uint8_t proto, uint16_t extPort, const String& intIp, uint16_t intPort, const String& desc) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    auto list = getPortForwards();
    for (auto& r : list) {
        if (r.protocol == proto && r.externalPort == extPort) {
            r.internalIp = intIp;
            r.internalPort = intPort;
            r.description = desc;
            bool ok = savePortForwards(list);
            if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
            return ok;
        }
    }
    if ((int)list.size() >= NAPT_MAX_PORTMAPS) {
        if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
        return false;
    }
    PortForwardRule rule;
    rule.protocol = proto;
    rule.externalPort = extPort;
    rule.internalIp = intIp;
    rule.internalPort = intPort;
    rule.description = desc;
    list.push_back(rule);
    bool ok = savePortForwards(list);
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return ok;
}

bool StorageManager::removePortForward(uint8_t proto, uint16_t extPort) {
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    auto list = getPortForwards();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->protocol == proto && it->externalPort == extPort) {
            list.erase(it);
            bool ok = savePortForwards(list);
            if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
            return ok;
        }
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return false;
}

// ==============================================================================
// CLIENT DEVICE ALIASES & 24-BIT IEEE OUI VENDOR LOOKUP
// ==============================================================================
static bool buildNormalizedAliasKey(const String& mac, char outKey[16]) {
    if (mac.length() != 17) return false;
    char hex[13];
    int p = 0;
    for (int i = 0; i < 17; i++) {
        char c = mac[i];
        if ((i % 3) == 2) {
            if (c != ':') return false;
        } else {
            if (!isxdigit((unsigned char)c)) return false;
            hex[p++] = (char)toupper((unsigned char)c);
        }
    }
    hex[12] = '\0';
    snprintf(outKey, 16, "al_%s", hex); // Exactly 15 chars ("al_" + 12 hex)
    return true;
}

String StorageManager::getClientAlias(const String& mac) {
    char key[16];
    if (!buildNormalizedAliasKey(mac, key)) return "";
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    String alias = prefs.getString(key, "");
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return alias;
}

bool StorageManager::setClientAlias(const String& mac, const String& alias) {
    char key[16];
    if (!buildNormalizedAliasKey(mac, key)) return false;
    if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY);
    if (alias.length() == 0) {
        prefs.remove(key);
    } else {
        prefs.putString(key, alias.substring(0, 32));
    }
    if (storageMutex) xSemaphoreGiveRecursive(storageMutex);
    return true;
}

struct OuiVendorEntry {
    uint32_t oui; // 24-bit 0xXXYYZZ
    const char* vendor;
};

// Sorted ascending by 24-bit IEEE OUI for zero-allocation binary search
static const OuiVendorEntry kOuiTable[] = {
    { 0x0013CE, "Intel Wi-Fi" },
    { 0x001500, "Intel Wi-Fi" },
    { 0x00166B, "Samsung" },
    { 0x185936, "Xiaomi" },
    { 0x240AC4, "Espressif IoT" },
    { 0x246F28, "Espressif IoT" },
    { 0x281878, "Microsoft" },
    { 0x30AEA4, "Espressif IoT" },
    { 0x3C22FB, "Apple" },
    { 0x3C5AB4, "Google" },
    { 0x3C6105, "Espressif IoT" },
    { 0x3C71BF, "Espressif IoT" },
    { 0x4851B7, "Intel Wi-Fi" },
    { 0x50C7BF, "TP-Link" },
    { 0x546009, "Google" },
    { 0x60A44C, "ASUSTeK" },
    { 0x640980, "Xiaomi" },
    { 0x6805CA, "Intel Wi-Fi" },
    { 0x709E29, "Sony PlayStation" },
    { 0x7840E4, "Samsung" },
    { 0x7C49EB, "Xiaomi" },
    { 0xA0C589, "Intel Wi-Fi" },
    { 0xACBC32, "Apple" },
    { 0xBCD074, "Apple" },
    { 0xDC9840, "Microsoft" },
    { 0xE848B8, "TP-Link" },
    { 0xF01898, "Apple" },
    { 0xF40343, "Google" },
    { 0xF4D488, "Apple" },
    { 0xF8461C, "Sony PlayStation" }
};

String StorageManager::getVendorName(const String& mac) {
    if (mac.length() < 8) return "Unknown Device";
    unsigned int b0 = 0, b1 = 0, b2 = 0;
    if (sscanf(mac.c_str(), "%2x:%2x:%2x", &b0, &b1, &b2) != 3) {
        return "Connected Client";
    }

    // Check IEEE 802 locally administered / randomized MAC bit (bit 1 of octet 0)
    if ((b0 & 0x02) != 0) {
        return "Private / Randomized MAC";
    }

    uint32_t target = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | (uint32_t)b2;
    size_t lo = 0;
    size_t hi = sizeof(kOuiTable) / sizeof(kOuiTable[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kOuiTable[mid].oui == target) {
            return String(kOuiTable[mid].vendor);
        }
        if (kOuiTable[mid].oui < target) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return "Connected Client";
}
