#include "roaming_engine.h"
#include "napt_engine.h"
#include <esp_mac.h>

RoamingEngine& RoamingEngine::instance() {
    static RoamingEngine r;
    return r;
}

RoamingEngine::RoamingEngine() 
    : state(ROAM_STATE_IDLE), scanning(false), lastScanTime(0), lastRoamTime(0),
      candidateFirstSeenTime(0), currentChannel(0), currentRssiFiltered(-95.0f),
      candidateRssi(-100.0f) {}

void RoamingEngine::init() {
    pinMode(PIN_RGB_RED, OUTPUT);
    pinMode(PIN_RGB_GREEN, OUTPUT);
    pinMode(PIN_RGB_BLUE, OUTPUT);
    
    // Default: Red (Not yet connected)
    digitalWrite(PIN_RGB_RED, LOW);
    digitalWrite(PIN_RGB_GREEN, HIGH);
    digitalWrite(PIN_RGB_BLUE, HIGH);

    Serial.println("[ROAM] 1D Kalman Roaming Engine & Anti-Jitter Subsystem initialized.");
}

bool RoamingEngine::isConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

void RoamingEngine::updateLedStatus() {
    if (isConnected()) {
        if (state == ROAM_STATE_HANDOVER || state == ROAM_STATE_EVALUATING) {
            // Yellow: Roaming / Evaluating (Red + Green)
            digitalWrite(PIN_RGB_RED, LOW);
            digitalWrite(PIN_RGB_GREEN, LOW);
            digitalWrite(PIN_RGB_BLUE, HIGH);
        } else {
            // Blue: Stable Connected & Repeating
            digitalWrite(PIN_RGB_RED, HIGH);
            digitalWrite(PIN_RGB_GREEN, HIGH);
            digitalWrite(PIN_RGB_BLUE, LOW);
        }
    } else {
        // Red: Disconnected / AP Setup mode
        digitalWrite(PIN_RGB_RED, LOW);
        digitalWrite(PIN_RGB_GREEN, HIGH);
        digitalWrite(PIN_RGB_BLUE, HIGH);
    }
}

bool RoamingEngine::connectToNetwork(const String& ssid, const String& password) {
    Serial.printf("[WIFI] Initiating association with SSID: '%s'...\n", ssid.c_str());
    state = ROAM_STATE_HANDOVER;
    updateLedStatus();

    WiFi.disconnect();
    delay(50);

    // Enforce WIFI_PS_NONE for zero modem-sleep jitter
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Single-Radio Channel Alignment & Fast Scan
    int targetChannel = 0;
    for (const auto& ap : lastScanResults) {
        if (ap.ssid == ssid) {
            targetChannel = ap.channel;
            break;
        }
    }

    if (targetChannel > 0) {
        Serial.printf("[WIFI] Channel alignment: Locking radio to Channel %d (skipping multi-band scan)\n", targetChannel);
        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
        WiFi.begin(ssid.c_str(), password.c_str(), targetChannel);
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(150);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        currentSsid = ssid;
        currentBssid = WiFi.BSSIDstr();
        currentChannel = WiFi.channel();
        rssiFilter.reset((float)WiFi.RSSI());
        currentRssiFiltered = (float)WiFi.RSSI();
        
        Serial.printf("[WIFI] CONNECTED to '%s' (BSSID: %s, Ch: %d, RSSI: %.1f dBm, IP: %s)\n",
                      currentSsid.c_str(), currentBssid.c_str(), currentChannel, 
                      currentRssiFiltered, WiFi.localIP().toString().c_str());

        // Bind NAPT routing to the new STA IP
        NaptEngine::instance().enable(WiFi.localIP());

        // Store into known networks if not present
        StorageManager::instance().addKnownNetwork(ssid, password, 1);

        state = ROAM_STATE_IDLE;
        updateLedStatus();
        return true;
    } else {
        Serial.printf("[WIFI] Association FAILED to '%s'\n", ssid.c_str());
        state = ROAM_STATE_IDLE;
        updateLedStatus();
        return false;
    }
}

void RoamingEngine::disconnect() {
    NaptEngine::instance().disable();
    WiFi.disconnect();
    currentSsid = "";
    currentBssid = "";
    currentChannel = 0;
    currentRssiFiltered = -100.0f;
    state = ROAM_STATE_IDLE;
    updateLedStatus();
    Serial.println("[WIFI] Disconnected from upstream network.");
}

bool RoamingEngine::applyClonedMac(const String& macStr) {
    if (macStr.length() != 17) return false;
    uint8_t mac[6];
    if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
        if (err == ESP_OK) {
            StorageManager::instance().setClonedMac(macStr);
            Serial.printf("[MAC] Station MAC successfully spoofed to: %s\n", macStr.c_str());
            return true;
        } else {
            Serial.printf("[MAC] esp_wifi_set_mac failed: 0x%x\n", err);
        }
    }
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
        Serial.printf("[MAC] Restored default factory MAC: %s\n", buf);
    }
}

void RoamingEngine::startScan() {
    if (scanning) return;
    scanning = true;
    Serial.println("[WIFI] Triggering non-blocking background 2.4 GHz RF scan...");
    WiFi.scanNetworks(true, false); // Async, show hidden = false
}

void RoamingEngine::processScanResults() {
    int n = WiFi.scanComplete();
    if (n < 0) return; // Still scanning or failed

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
    Serial.printf("[WIFI] Background scan finished. Discovered %d 2.4 GHz Access Points.\n", (int)lastScanResults.size());

    // Evaluate roaming candidates
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

    // Check if we have an active link
    if (!isConnected()) {
        // Try auto-connecting to any known network in range, ordered by priority
        auto known = StorageManager::instance().getKnownNetworks();
        for (const auto& k : known) {
            for (const auto& ap : lastScanResults) {
                if (ap.ssid == k.ssid && ap.rssi > -88) {
                    Serial.printf("[ROAM] Auto-connecting to priority network in range: %s (%d dBm, Prio: %d)\n",
                                  k.ssid.c_str(), ap.rssi, k.priority);
                    connectToNetwork(k.ssid, k.password);
                    return;
                }
            }
        }
        return;
    }

    // If connected, check signal degradation
    if (currentRssiFiltered < settings.roamThresholdDbm) {
        auto known = StorageManager::instance().getKnownNetworks();
        ScannedApInfo bestCand;
        bestCand.rssi = -100;
        String candPass = "";

        for (const auto& ap : lastScanResults) {
            for (const auto& k : known) {
                if (ap.ssid == k.ssid && (ap.bssid != currentBssid || ap.ssid != currentSsid)) {
                    if (ap.rssi > bestCand.rssi) {
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
                candidateRssi = (float)bestCand.rssi;
                candidateFirstSeenTime = millis();
                Serial.printf("[ROAM] Candidate identified: '%s' (%s, %.1f dBm vs current %.1f dBm). Evaluating dwell time...\n",
                              candidateSsid.c_str(), candidateBssid.c_str(), candidateRssi, currentRssiFiltered);
            } else {
                if (millis() - candidateFirstSeenTime >= ROAM_DWELL_TIME_MS) {
                    Serial.printf("[ROAM] Dwell time (3s) verified! Executing SEAMLESS HANDOVER to '%s'...\n", candidateSsid.c_str());
                    executeHandover();
                }
            }
        } else {
            state = ROAM_STATE_IDLE;
        }
    } else {
        if (state == ROAM_STATE_EVALUATING) {
            state = ROAM_STATE_IDLE;
        }
    }
}

void RoamingEngine::executeHandover() {
    state = ROAM_STATE_HANDOVER;
    updateLedStatus();

    String targetSsid = candidateSsid;
    String targetPass = candidatePass;

    connectToNetwork(targetSsid, targetPass);

    lastRoamTime = millis();
    state = ROAM_STATE_COOLDOWN;
    updateLedStatus();
    Serial.println("[ROAM] Handover complete. Entered 30s cooldown hold-down.");
}

void RoamingEngine::update() {
    // Process async scan if completed
    if (scanning && WiFi.scanComplete() >= 0) {
        processScanResults();
    }

    // Update Kalman filter on current link
    if (isConnected()) {
        float raw = (float)WiFi.RSSI();
        currentRssiFiltered = rssiFilter.update(raw);
    }

    // Periodic background scan every 25 seconds if idle
    if (!scanning && millis() - lastScanTime > 25000) {
        startScan();
    }

    updateLedStatus();
}
