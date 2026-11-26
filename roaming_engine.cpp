#include "roaming_engine.h"
#include "napt_engine.h"
#include "dns_cache.h"
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <cmath>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_timer.h>

#define RTC_FAST_CACHE_MAGIC 0xAE780002

// ESP-IDF discrete TX power quantization helper (0.25 dBm raw units -> actual hardware step)
static int quantizeTxRaw(int raw) {
    if (raw >= 80) return 80; // 20.00 dBm
    if (raw >= 72) return 72; // 18.00 dBm
    if (raw >= 66) return 66; // 16.50 dBm
    if (raw >= 60) return 60; // 15.00 dBm
    if (raw >= 56) return 56; // 14.00 dBm
    if (raw >= 52) return 52; // 13.00 dBm
    return 44;                // 11.00 dBm
}

static bool parseMacBytes(const String& macStr, uint8_t out[6]) {
    if (macStr.length() != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = macStr[i];
        if ((i % 3) == 2) {
            if (c != ':') return false;
        } else {
            if (!isxdigit((unsigned char)c)) return false;
        }
    }
    return sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

struct FailedConnectRecord {
    String ssid;
    uint8_t failCount;
    unsigned long lastFailTime;
};
static std::vector<FailedConnectRecord> s_failedRecords;

static bool isNetworkInBackoff(const String& ssid) {
    unsigned long now = millis();
    for (const auto& r : s_failedRecords) {
        if (r.ssid == ssid) {
            unsigned long backoffMs = (r.failCount <= 1) ? 30000UL : ((r.failCount == 2) ? 60000UL : 300000UL);
            if (now - r.lastFailTime < backoffMs) {
                return true;
            }
        }
    }
    return false;
}

static void recordNetworkFailure(const String& ssid) {
    unsigned long now = millis();
    for (auto& r : s_failedRecords) {
        if (r.ssid == ssid) {
            if (r.failCount < 10) r.failCount++;
            r.lastFailTime = now;
            return;
        }
    }
    FailedConnectRecord r;
    r.ssid = ssid;
    r.failCount = 1;
    r.lastFailTime = now;
    s_failedRecords.push_back(r);
}

static void clearNetworkFailure(const String& ssid) {
    for (auto it = s_failedRecords.begin(); it != s_failedRecords.end(); ++it) {
        if (it->ssid == ssid) {
            s_failedRecords.erase(it);
            return;
        }
    }
}

RTC_NOINIT_ATTR static RtcFastConnectCache rtcFastCache;

RoamingEngine& RoamingEngine::instance() {
    static RoamingEngine r;
    return r;
}

RoamingEngine::RoamingEngine()
    : state(ROAM_STATE_IDLE), scanning(false), lastScanTime(0), lastRoamTime(0),
      candidateFirstSeenTime(0), currentChannel(0), currentRssiFiltered(-95.0f),
      candidateChannel(0), candidateRssi(-100.0f), currentTxPowerDbm(DEFAULT_TX_POWER_DBM),
      lastAppliedTxRaw(DEFAULT_TX_POWER_RAW),
      apClientRssiEwma(-60.0f), apClientEwmaInit(false), lastTpcTime(0), lastRssiSampleTime(0),
      wanState(WAN_STATE_IDLE_WAIT), wanOnline(false), wanLastRttMs(0.0f),
      wanConsecutiveFails(0), wanFailoverCount(0), lastWanProbeTime(0),
      lastWanFailoverTime(0), probePrimaryTarget(true) {}

void RoamingEngine::init() {
    pinMode(PIN_RGB_RED, OUTPUT);
    pinMode(PIN_RGB_GREEN, OUTPUT);
    pinMode(PIN_RGB_BLUE, OUTPUT);

    digitalWrite(PIN_RGB_RED, LOW);
    digitalWrite(PIN_RGB_GREEN, HIGH);
    digitalWrite(PIN_RGB_BLUE, HIGH);

    if (rtcFastCache.magic != RTC_FAST_CACHE_MAGIC) {
        rtcFastCache.magic = RTC_FAST_CACHE_MAGIC;
        rtcFastCache.valid = false;
        rtcFastCache.channel = 0;
        rtcFastCache.ssid[0] = '\0';
        memset(rtcFastCache.bssid, 0, sizeof(rtcFastCache.bssid));
    } else if (rtcFastCache.valid) {
        apLog("[ROAM] RTC Fast-Connect cache ready: '%s' (Ch %d)\n",
              rtcFastCache.ssid, rtcFastCache.channel);
    }

    apLog("[ROAM] Roaming Engine, Dual-Link TPC & WAN Watchdog initialized.\n");
}

bool RoamingEngine::isConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

void RoamingEngine::updateLedStatus() {
    if (isConnected()) {
        if (state == ROAM_STATE_HANDOVER || state == ROAM_STATE_EVALUATING) {
            digitalWrite(PIN_RGB_RED, LOW);
            digitalWrite(PIN_RGB_GREEN, LOW);
            digitalWrite(PIN_RGB_BLUE, HIGH);
        } else {
            digitalWrite(PIN_RGB_RED, HIGH);
            digitalWrite(PIN_RGB_GREEN, HIGH);
            digitalWrite(PIN_RGB_BLUE, LOW);
        }
    } else {
        digitalWrite(PIN_RGB_RED, LOW);
        digitalWrite(PIN_RGB_GREEN, HIGH);
        digitalWrite(PIN_RGB_BLUE, HIGH);
    }
}

void RoamingEngine::cacheFastConnect(const String& ssid, const uint8_t* bssid, uint8_t channel) {
    rtcFastCache.magic = RTC_FAST_CACHE_MAGIC;
    rtcFastCache.channel = channel;
    strncpy(rtcFastCache.ssid, ssid.c_str(), sizeof(rtcFastCache.ssid) - 1);
    rtcFastCache.ssid[sizeof(rtcFastCache.ssid) - 1] = '\0';
    if (bssid) {
        memcpy(rtcFastCache.bssid, bssid, 6);
    } else {
        memset(rtcFastCache.bssid, 0, 6);
    }
    rtcFastCache.valid = true;
}

bool RoamingEngine::getCachedFastConnect(const String& ssid, uint8_t* outBssid, uint8_t* outChannel) {
    if (rtcFastCache.valid && rtcFastCache.magic == RTC_FAST_CACHE_MAGIC && strcmp(rtcFastCache.ssid, ssid.c_str()) == 0) {
        if (outBssid) memcpy(outBssid, rtcFastCache.bssid, 6);
        if (outChannel) *outChannel = rtcFastCache.channel;
        return true;
    }
    return false;
}

void RoamingEngine::syncSoftApConfig() {
    wifi_config_t ap_cfg;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
        // Only apply if parameters differ; esp_wifi_set_config restarts SoftAP & resets lwIP NAPT
        if (ap_cfg.ap.beacon_interval != SOFTAP_BEACON_INTERVAL_TU ||
            ap_cfg.ap.dtim_period != SOFTAP_DTIM_PERIOD) {
            ap_cfg.ap.beacon_interval = SOFTAP_BEACON_INTERVAL_TU;
            ap_cfg.ap.dtim_period = SOFTAP_DTIM_PERIOD;
            esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        }
    }
}

bool RoamingEngine::connectToNetwork(const String& ssid, const String& password, const String& targetBssid, int targetChannel) {
    if (ssid.length() == 0) return false;

    int prevChannel = (currentChannel > 0) ? currentChannel : StorageManager::instance().getSettings().apChannel;
    apLog("[WIFI] Associating with '%s'...\n", ssid.c_str());
    state = ROAM_STATE_HANDOVER;
    updateLedStatus();

    WiFi.disconnect(false, false);
    delay(50);
    esp_wifi_set_ps(WIFI_PS_NONE);

    wifi_config_t sta_cfg;
    memset(&sta_cfg, 0, sizeof(wifi_config_t));
    strncpy((char *)sta_cfg.sta.ssid, ssid.c_str(), sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, password.c_str(), sizeof(sta_cfg.sta.password) - 1);

    uint8_t bssidBytes[6];
    if (targetBssid.length() == 17 && parseMacBytes(targetBssid, bssidBytes)) {
        memcpy(sta_cfg.sta.bssid, bssidBytes, 6);
        sta_cfg.sta.bssid_set = 1;
        sta_cfg.sta.channel = (targetChannel >= 1 && targetChannel <= 13) ? (uint8_t)targetChannel : 0;
        sta_cfg.sta.scan_method = (sta_cfg.sta.channel > 0) ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
    } else {
        uint8_t cachedBssid[6];
        uint8_t cachedCh = 0;
        if (getCachedFastConnect(ssid, cachedBssid, &cachedCh) && cachedCh >= 1 && cachedCh <= 13) {
            sta_cfg.sta.channel = cachedCh;
            sta_cfg.sta.bssid_set = 0;
            sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
        } else {
            sta_cfg.sta.channel = 0;
            sta_cfg.sta.bssid_set = 0;
            sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        }
    }

    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_cfg.sta.threshold.rssi = -127;
    sta_cfg.sta.threshold.authmode = (password.length() > 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    esp_err_t set_err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (set_err != ESP_OK) {
        apLog("[WIFI] esp_wifi_set_config failed: 0x%x\n", set_err);
    }

    esp_err_t conn_err = esp_wifi_connect();
    if (conn_err != ESP_OK) {
        apLog("[WIFI] esp_wifi_connect failed: 0x%x\n", conn_err);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 6000) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() == WL_CONNECTED) {
        clearNetworkFailure(ssid);
        currentSsid = ssid;
        currentBssid = WiFi.BSSIDstr();
        currentChannel = WiFi.channel();
        rssiFilter.reset((float)WiFi.RSSI(), true);
        currentRssiFiltered = (float)WiFi.RSSI();

        apLog("[WIFI] Connected to '%s' (BSSID: %s, Ch: %d, RSSI: %.1f dBm, IP: %s)\n",
              currentSsid.c_str(), currentBssid.c_str(), currentChannel,
              currentRssiFiltered, WiFi.localIP().toString().c_str());

        if (currentChannel > 0 && currentChannel != prevChannel) {
            apLog("[WIFI] Channel shifted (%d -> %d). Re-syncing SoftAP clients...\n",
                  prevChannel, currentChannel);
            esp_wifi_deauth_sta(0);
        }

        uint8_t rawBssid[6] = {0};
        uint8_t *b = WiFi.BSSID();
        if (b) memcpy(rawBssid, b, 6);
        cacheFastConnect(ssid, rawBssid, (uint8_t)currentChannel);

        syncSoftApConfig();
        NaptEngine::instance().enable(WiFi.localIP());
        StorageManager::instance().addKnownNetwork(ssid, password, 1);

        state = ROAM_STATE_IDLE;
        updateLedStatus();
        return true;
    }

    // Association failed: clear stale metadata and enter backoff
    NaptEngine::instance().disable();
    currentSsid = "";
    currentBssid = "";
    currentChannel = 0;
    currentRssiFiltered = -100.0f;
    rssiFilter.reset(-80.0f, false);
    recordNetworkFailure(ssid);
    apLog("[WIFI] Association failed for '%s'. Entering backoff.\n", ssid.c_str());
    state = ROAM_STATE_IDLE;
    updateLedStatus();
    return false;
}

void RoamingEngine::disconnect() {
    NaptEngine::instance().disable();
    WiFi.disconnect(false, false);
    currentSsid = "";
    currentBssid = "";
    currentChannel = 0;
    currentRssiFiltered = -100.0f;
    rssiFilter.reset(-80.0f, false);
    state = ROAM_STATE_IDLE;
    updateLedStatus();
    apLog("[WIFI] Disconnected from upstream network.\n");
}

bool RoamingEngine::applyClonedMac(const String& macStr) {
    uint8_t mac[6];
    if (!parseMacBytes(macStr, mac)) return false;

    // Ensure locally administered / unicast bit validity check (cannot set multicast bit 0)
    if (mac[0] & 0x01) return false;

    esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
    if (err == ESP_OK) {
        char norm[18];
        snprintf(norm, sizeof(norm), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        StorageManager::instance().setClonedMac(norm);
        apLog("[MAC] Station MAC set to: %s\n", norm);
        return true;
    }
    apLog("[MAC] esp_wifi_set_mac failed: 0x%x\n", err);
    return false;
}

void RoamingEngine::resetMac() {
    uint8_t defaultMac[6];
    if (esp_read_mac(defaultMac, ESP_MAC_WIFI_STA) == ESP_OK) {
        esp_wifi_set_mac(WIFI_IF_STA, defaultMac);
        StorageManager::instance().setClonedMac("");
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 defaultMac[0], defaultMac[1], defaultMac[2], defaultMac[3], defaultMac[4], defaultMac[5]);
        apLog("[MAC] Restored factory MAC: %s\n", buf);
    }
}

std::vector<int> RoamingEngine::getChannelLoads() {
    const float spectral_weights[] = {1.00f, 0.70f, 0.40f, 0.15f, 0.05f};
    std::vector<int> loads(14, 0);

    for (int ch = 1; ch <= 13; ch++) {
        float totalPowerMw = 0.0f;
        for (const auto& ap : lastScanResults) {
            if (ap.channel < 1 || ap.channel > 13) continue;
            int delta = abs(ap.channel - ch);
            if (delta < 5) {
                float pwrMw = powf(10.0f, (float)ap.rssi / 10.0f);
                totalPowerMw += pwrMw * spectral_weights[delta];
            }
        }
        if (totalPowerMw <= 1e-10f) {
            loads[ch] = 0;
        } else {
            // Convert aggregate interference mW back to dBm and normalize -95 dBm -> 0%, -35 dBm -> 100%
            float totalDbm = 10.0f * log10f(totalPowerMw);
            int pct = (int)lroundf(((totalDbm + 95.0f) / 60.0f) * 100.0f);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            loads[ch] = pct;
        }
    }
    return loads;
}

int RoamingEngine::getBestChannel() {
    const int candidates[] = {1, 6, 11};
    const float spectral_weights[] = {1.00f, 0.70f, 0.40f, 0.15f, 0.05f};

    int bestCh = 6;
    float minInterference = 1e18f;

    for (int candidate : candidates) {
        float totalSpectralPower = 0.0f;
        for (const auto& ap : lastScanResults) {
            if (ap.channel < 1 || ap.channel > 13) continue;
            int delta = abs(ap.channel - candidate);
            if (delta < 5) {
                float pwr_mW = powf(10.0f, (float)ap.rssi / 10.0f);
                totalSpectralPower += pwr_mW * spectral_weights[delta];
            }
        }
        if (totalSpectralPower < minInterference) {
            minInterference = totalSpectralPower;
            bestCh = candidate;
        }
    }
    return bestCh;
}

bool RoamingEngine::isSubnetConflict(IPAddress staIp) {
    IPAddress ap = WiFi.softAPIP();
    if ((uint32_t)ap == 0) ap = DEFAULT_AP_IP;
    return (staIp[0] == ap[0] && staIp[1] == ap[1] && staIp[2] == ap[2]);
}

bool RoamingEngine::startScan() {
    if (scanning) return true;
    int16_t res = WiFi.scanNetworks(true, false);
    if (res == WIFI_SCAN_FAILED) {
        scanning = false;
        return false;
    }
    scanning = true;
    return true;
}

void RoamingEngine::processScanResults() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n == WIFI_SCAN_FAILED) {
        scanning = false;
        WiFi.scanDelete();
        return;
    }

    lastScanResults.clear();
    auto known = StorageManager::instance().getKnownNetworks();

    for (int i = 0; i < n; i++) {
        ScannedApInfo info;
        info.ssid = WiFi.SSID(i);
        info.bssid = WiFi.BSSIDstr(i);
        info.rssi = WiFi.RSSI(i);
        info.channel = WiFi.channel(i);
        info.encryptionType = (uint8_t)WiFi.encryptionType(i);
        info.isKnown = false;

        for (const auto& k : known) {
            if (k.ssid == info.ssid) {
                info.isKnown = true;
                break;
            }
        }
        lastScanResults.push_back(info);
    }

    WiFi.scanDelete();
    scanning = false;
    lastScanTime = millis();
    apLog("[WIFI] Scan finished: %d access points found.\n", (int)lastScanResults.size());

    triggerRoamEvaluation();
}

void RoamingEngine::triggerRoamEvaluation() {
    auto settings = StorageManager::instance().getSettings();
    if (!settings.autoRoamEnabled) return;

    if (state == ROAM_STATE_COOLDOWN) {
        if (millis() - lastRoamTime > ROAM_COOLDOWN_MS) {
            state = ROAM_STATE_IDLE;
        } else {
            return;
        }
    }

    auto known = StorageManager::instance().getKnownNetworks();
    if (known.empty()) return;

    std::sort(known.begin(), known.end(), [](const KnownNetwork& a, const KnownNetwork& b) {
        return a.priority > b.priority;
    });

    if (!isConnected()) {
        for (const auto& k : known) {
            if (isNetworkInBackoff(k.ssid) || isWanInBackoff(k.ssid)) continue;
            const ScannedApInfo* bestAp = nullptr;
            for (const auto& ap : lastScanResults) {
                if (ap.ssid == k.ssid && ap.rssi > -88) {
                    if (!bestAp || ap.rssi > bestAp->rssi) {
                        bestAp = &ap;
                    }
                }
            }
            if (bestAp) {
                apLog("[ROAM] Auto-connecting to '%s' (%d dBm, Prio %d)\n",
                      k.ssid.c_str(), (int)bestAp->rssi, k.priority);
                connectToNetwork(k.ssid, k.password, bestAp->bssid, bestAp->channel);
                return;
            }
        }
        return;
    }

    if (currentRssiFiltered < settings.roamThresholdDbm) {
        ScannedApInfo bestCand;
        bestCand.rssi = -120;
        bestCand.channel = 0;
        String candPass = "";
        int bestScore = -99999;

        for (const auto& k : known) {
            if (isNetworkInBackoff(k.ssid) || isWanInBackoff(k.ssid)) continue;
            for (const auto& ap : lastScanResults) {
                if (ap.ssid == k.ssid && (ap.bssid != currentBssid || ap.ssid != currentSsid)) {
                    int score = (k.priority * 5) + ap.rssi;
                    if (score > bestScore && ap.rssi > -85) {
                        bestScore = score;
                        bestCand = ap;
                        candPass = k.password;
                    }
                }
            }
        }

        if (bestCand.rssi > (currentRssiFiltered + settings.roamHysteresisDbm)) {
            if (state != ROAM_STATE_EVALUATING || candidateBssid != bestCand.bssid) {
                state = ROAM_STATE_EVALUATING;
                candidateSsid = bestCand.ssid;
                candidateBssid = bestCand.bssid;
                candidatePass = candPass;
                candidateChannel = bestCand.channel;
                candidateRssi = (float)bestCand.rssi;
                candidateFirstSeenTime = millis();
                apLog("[ROAM] Candidate '%s' (%s, %.1f dBm vs %.1f dBm). Evaluating...\n",
                      candidateSsid.c_str(), candidateBssid.c_str(), candidateRssi, currentRssiFiltered);
            } else if (millis() - candidateFirstSeenTime >= ROAM_DWELL_TIME_MS) {
                apLog("[ROAM] Dwell verified. Handing over to '%s' (%s)...\n",
                      candidateSsid.c_str(), candidateBssid.c_str());
                executeHandover();
            }
        } else {
            state = ROAM_STATE_IDLE;
        }
    } else if (state == ROAM_STATE_EVALUATING) {
        state = ROAM_STATE_IDLE;
    }
}

void RoamingEngine::executeHandover() {
    state = ROAM_STATE_HANDOVER;
    updateLedStatus();

    bool ok = connectToNetwork(candidateSsid, candidatePass, candidateBssid, candidateChannel);
    if (ok) {
        lastRoamTime = millis();
        state = ROAM_STATE_COOLDOWN;
    } else {
        state = ROAM_STATE_IDLE;
    }
    updateLedStatus();
}

void RoamingEngine::setTxPowerDbm(float dbm) {
    float minDbm = MIN_TX_POWER_RAW * 0.25f;
    float maxDbm = MAX_TX_POWER_RAW * 0.25f;
    if (dbm < minDbm) dbm = minDbm;
    if (dbm > maxDbm) dbm = maxDbm;
    int raw = quantizeTxRaw((int)lroundf(dbm * 4.0f));
    esp_wifi_set_max_tx_power(raw);
    lastAppliedTxRaw = raw;
    currentTxPowerDbm = raw * 0.25f;
    apLog("[RF] TX Power set to %.2f dBm (raw: %d)\n", currentTxPowerDbm, raw);
}

void RoamingEngine::updateDynamicTpc() {
    auto settings = StorageManager::instance().getSettings();
    if (!settings.dynamicTpcEnabled) return;
    if (millis() - lastTpcTime < 3000) return;
    lastTpcTime = millis();

    int staRequiredRaw = MIN_TX_POWER_RAW;
    int apRequiredRaw = MIN_TX_POWER_RAW;
    bool hasSta = isConnected();
    bool hasApClients = false;

    if (hasSta) {
        if (currentRssiFiltered >= TPC_CLOSE_RSSI_THRESHOLD) {
            staRequiredRaw = MIN_TX_POWER_RAW;
        } else if (currentRssiFiltered <= TPC_FAR_RSSI_THRESHOLD) {
            staRequiredRaw = MAX_TX_POWER_RAW;
        } else {
            float ratio = (TPC_CLOSE_RSSI_THRESHOLD - currentRssiFiltered) /
                          (TPC_CLOSE_RSSI_THRESHOLD - TPC_FAR_RSSI_THRESHOLD);
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            staRequiredRaw = MIN_TX_POWER_RAW + (int)lroundf(ratio * (MAX_TX_POWER_RAW - MIN_TX_POWER_RAW));
        }
    }

    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0) {
        hasApClients = true;
        int8_t weakest = 0;
        for (int i = 0; i < sta_list.num; i++) {
            if (i == 0 || sta_list.sta[i].rssi < weakest) {
                weakest = sta_list.sta[i].rssi;
            }
        }
        if (!apClientEwmaInit) {
            apClientRssiEwma = (float)weakest;
            apClientEwmaInit = true;
        } else {
            apClientRssiEwma = 0.35f * (float)weakest + 0.65f * apClientRssiEwma;
        }

        if (apClientRssiEwma >= TPC_CLOSE_RSSI_THRESHOLD) {
            apRequiredRaw = MIN_TX_POWER_RAW;
        } else if (apClientRssiEwma <= TPC_FAR_RSSI_THRESHOLD) {
            apRequiredRaw = MAX_TX_POWER_RAW;
        } else {
            float ratio = (TPC_CLOSE_RSSI_THRESHOLD - apClientRssiEwma) /
                          (TPC_CLOSE_RSSI_THRESHOLD - TPC_FAR_RSSI_THRESHOLD);
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            apRequiredRaw = MIN_TX_POWER_RAW + (int)lroundf(ratio * (MAX_TX_POWER_RAW - MIN_TX_POWER_RAW));
        }
    } else {
        apClientEwmaInit = false;
    }

    int targetRaw = DEFAULT_TX_POWER_RAW;
    if (hasSta && hasApClients) {
        targetRaw = std::max(staRequiredRaw, apRequiredRaw);
    } else if (hasSta) {
        targetRaw = staRequiredRaw;
    } else if (hasApClients) {
        targetRaw = apRequiredRaw;
    }

    if (targetRaw < MIN_TX_POWER_RAW) targetRaw = MIN_TX_POWER_RAW;
    if (targetRaw > MAX_TX_POWER_RAW) targetRaw = MAX_TX_POWER_RAW;
    int quantizedTarget = quantizeTxRaw(targetRaw);
    int currentQuantized = lastAppliedTxRaw;

    bool apply = false;
    if (quantizedTarget > currentQuantized) {
        apply = true;
    } else if ((currentQuantized - quantizedTarget) >= 6) {
        apply = true;
    }

    if (apply && quantizedTarget != currentQuantized) {
        esp_wifi_set_max_tx_power(quantizedTarget);
        lastAppliedTxRaw = quantizedTarget;
        currentTxPowerDbm = quantizedTarget * 0.25f;
        apLog("[TPC] Adjusted TX Power -> %.2f dBm (raw: %d)\n", currentTxPowerDbm, quantizedTarget);
    }
}

void RoamingEngine::update() {
    if (scanning) {
        int scanRes = WiFi.scanComplete();
        if (scanRes >= 0 || scanRes == WIFI_SCAN_FAILED) {
            processScanResults();
        }
    }

    unsigned long now = millis();

    if (isConnected()) {
        // Sample Wi-Fi beacon RSSI at ~10 Hz (every 100ms) to match beacon cadence
        if (now - lastRssiSampleTime >= 100) {
            lastRssiSampleTime = now;
            currentRssiFiltered = rssiFilter.update((float)WiFi.RSSI());
        }

        if (currentSsid.length() == 0) {
            currentSsid = WiFi.SSID();
            currentBssid = WiFi.BSSIDstr();
            currentChannel = WiFi.channel();
        }

        auto settings = StorageManager::instance().getSettings();
        if (settings.autoRoamEnabled && currentRssiFiltered < settings.roamThresholdDbm) {
            unsigned long scanInterval = (state == ROAM_STATE_EVALUATING) ? ROAM_DWELL_TIME_MS : 30000UL;
            if (!scanning && now - lastScanTime > scanInterval) {
                startScan();
            }
        }
    } else {
        if (!scanning && now - lastScanTime > 30000UL) {
            startScan();
        }
    }

    updateDynamicTpc();
    updateWanWatchdog();
    updateLedStatus();
}

// ==============================================================================
// AUTONOMOUS WAN INTERNET HEALTH WATCHDOG & FAST FAILOVER
// ==============================================================================

bool RoamingEngine::executeWanProbe(float* outRttMs, const char* overrideTarget) {
    if (!isConnected()) {
        if (outRttMs) *outRttMs = 0.0f;
        return false;
    }

    const char* targetIp = overrideTarget ? overrideTarget : (probePrimaryTarget ? WAN_PRIMARY_PROBE_IP : WAN_SECONDARY_PROBE_IP);
    int sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        if (outRttMs) *outRttMs = 0.0f;
        return false;
    }

    int flags = lwip_fcntl(sock, F_GETFL, 0);
    lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct linger sl = { .l_onoff = 1, .l_linger = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = PP_HTONS(WAN_PROBE_PORT);
    dest.sin_addr.s_addr = inet_addr(targetIp);

    int64_t t0 = esp_timer_get_time();
    int res = lwip_connect(sock, (struct sockaddr*)&dest, sizeof(dest));
    bool probeSuccess = false;
    float rtt = 0.0f;

    if (res == 0) {
        rtt = (float)(esp_timer_get_time() - t0) / 1000.0f;
        probeSuccess = true;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv;
        tv.tv_sec = WAN_PROBE_TIMEOUT_MS / 1000;
        tv.tv_usec = (WAN_PROBE_TIMEOUT_MS % 1000) * 1000;

        int sel = lwip_select(sock + 1, NULL, &wfds, NULL, &tv);
        if (sel > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                rtt = (float)(esp_timer_get_time() - t0) / 1000.0f;
                probeSuccess = true;
            }
        }
    }

    lwip_close(sock);
    if (outRttMs) *outRttMs = rtt;
    return probeSuccess;
}

void RoamingEngine::updateWanWatchdog() {
    auto settings = StorageManager::instance().getSettings();
    if (!settings.wanWatchdogEnabled) {
        wanState = WAN_STATE_DISABLED;
        return;
    }

    if (!isConnected()) {
        wanState = WAN_STATE_IDLE_WAIT;
        wanOnline = false;
        wanConsecutiveFails = 0;
        return;
    }

    unsigned long now = millis();
    if (wanState == WAN_STATE_COOLDOWN) {
        if (now - lastWanFailoverTime >= WAN_COOLDOWN_MS) {
            wanState = WAN_STATE_HEALTHY;
        } else {
            return;
        }
    }

    unsigned long healthyMs = (settings.wanProbeIntervalSec >= 2)
        ? (settings.wanProbeIntervalSec * 1000UL)
        : WAN_PROBE_INTERVAL_HEALTHY_MS;
    unsigned long intervalMs = (wanState == WAN_STATE_DEGRADED_CONFIRM) ? WAN_PROBE_INTERVAL_FAST_MS : healthyMs;

    if (now - lastWanProbeTime < intervalMs) {
        return;
    }
    lastWanProbeTime = now;

    const char* probedIp = probePrimaryTarget ? WAN_PRIMARY_PROBE_IP : WAN_SECONDARY_PROBE_IP;
    float rtt = 0.0f;
    bool success = executeWanProbe(&rtt, probedIp);
    probePrimaryTarget = !probePrimaryTarget;

    if (success) {
        wanOnline = true;
        wanLastRttMs = rtt;
        wanConsecutiveFails = 0;
        wanState = WAN_STATE_HEALTHY;
        clearWanFailure(currentSsid);
    } else {
        wanConsecutiveFails++;
        apLog("[WAN-WD] Probe strike %d/%d failed on '%s' (%s)\n",
              wanConsecutiveFails, WAN_FAIL_THRESHOLD, currentSsid.c_str(), probedIp);

        if (wanConsecutiveFails < WAN_FAIL_THRESHOLD) {
            wanState = WAN_STATE_DEGRADED_CONFIRM;
        } else {
            wanOnline = false;
            wanState = WAN_STATE_FAILOVER_TRIGGERED;
            apLog("[WAN-WD] Outage confirmed on '%s'. Triggering failover...\n", currentSsid.c_str());
            triggerWanFailover();
        }
    }
}

void RoamingEngine::triggerWanFailover() {
    wanFailoverCount++;
    lastWanFailoverTime = millis();
    recordWanFailure(currentSsid, currentBssid);

    auto known = StorageManager::instance().getKnownNetworks();
    if (known.empty()) {
        wanState = WAN_STATE_COOLDOWN;
        return;
    }

    std::sort(known.begin(), known.end(), [](const KnownNetwork& a, const KnownNetwork& b) {
        return a.priority > b.priority;
    });

    String bestCandidateSsid = "";
    String bestCandidateBssid = "";
    String bestCandidatePass = "";
    int bestCandidateChannel = 0;
    int bestScore = -99999;

    for (const auto& k : known) {
        if (k.ssid == currentSsid || isWanInBackoff(k.ssid) || isNetworkInBackoff(k.ssid)) {
            continue;
        }
        for (const auto& ap : lastScanResults) {
            if (ap.ssid == k.ssid && ap.rssi > -88) {
                int score = (k.priority * 15) + ((int)(ap.rssi + 100) * 3);
                uint8_t cachedBssid[6];
                uint8_t cachedCh = 0;
                if (getCachedFastConnect(k.ssid, cachedBssid, &cachedCh)) {
                    score += 30;
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestCandidateSsid = k.ssid;
                    bestCandidateBssid = ap.bssid;
                    bestCandidatePass = k.password;
                    bestCandidateChannel = ap.channel;
                }
            }
        }
    }

    if (bestCandidateSsid.length() > 0) {
        apLog("[WAN-WD] Failing over '%s' -> '%s' (Score: %d)...\n",
              currentSsid.c_str(), bestCandidateSsid.c_str(), bestScore);

        if (connectToNetwork(bestCandidateSsid, bestCandidatePass, bestCandidateBssid, bestCandidateChannel)) {
            unsigned long ipWait = millis();
            while ((uint32_t)WiFi.localIP() == 0 && millis() - ipWait < 2000) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            float newRtt = 0.0f;
            bool probePass = executeWanProbe(&newRtt, WAN_PRIMARY_PROBE_IP) ||
                             executeWanProbe(&newRtt, WAN_SECONDARY_PROBE_IP);
            if (probePass) {
                wanOnline = true;
                wanLastRttMs = newRtt;
                wanConsecutiveFails = 0;
                wanState = WAN_STATE_COOLDOWN;
                apLog("[WAN-WD] Failover to '%s' verified (RTT: %.1f ms).\n",
                      bestCandidateSsid.c_str(), newRtt);
                return;
            }
            apLog("[WAN-WD] Backup '%s' has no WAN reachability. Penalizing...\n",
                  bestCandidateSsid.c_str());
            recordWanFailure(bestCandidateSsid, WiFi.BSSIDstr());
            startScan();
        }
    } else {
        apLog("[WAN-WD] No alternate AP in scan cache. Scanning...\n");
        startScan();
    }

    wanState = WAN_STATE_COOLDOWN;
}

bool RoamingEngine::isWanInBackoff(const String& ssid) const {
    unsigned long now = millis();
    for (const auto& r : wanBackoffList) {
        if (r.ssid == ssid && (now - r.lastFailTime < r.backoffMs)) {
            return true;
        }
    }
    return false;
}

void RoamingEngine::recordWanFailure(const String& ssid, const String& bssid) {
    unsigned long now = millis();
    for (auto& r : wanBackoffList) {
        if (r.ssid == ssid) {
            if (r.failCount < 5) r.failCount++;
            r.lastFailTime = now;
            r.bssid = bssid;
            r.backoffMs = (r.failCount <= 1) ? WAN_BACKOFF_BASE_MS :
                          ((r.failCount == 2) ? (WAN_BACKOFF_BASE_MS * 3) : (WAN_BACKOFF_BASE_MS * 10));
            return;
        }
    }
    WanBackoffRecord rec;
    rec.ssid = ssid;
    rec.bssid = bssid;
    rec.failCount = 1;
    rec.lastFailTime = now;
    rec.backoffMs = WAN_BACKOFF_BASE_MS;
    wanBackoffList.push_back(rec);
}

void RoamingEngine::clearWanFailure(const String& ssid) {
    for (auto it = wanBackoffList.begin(); it != wanBackoffList.end(); ++it) {
        if (it->ssid == ssid) {
            wanBackoffList.erase(it);
            return;
        }
    }
}

void RoamingEngine::clearAllWanBackoff() {
    wanBackoffList.clear();
    apLog("[WAN-WD] Cleared all WAN AP backoff records.\n");
}
