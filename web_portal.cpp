#include "web_portal.h"
#include "portal_html.h"
#include "roaming_engine.h"
#include "napt_engine.h"
#include "storage_manager.h"
#include "dns_cache.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

static bool isLocalHostMatch(String host) {
    host.trim();
    int colon = host.indexOf(':');
    if (colon >= 0) host = host.substring(0, colon);
    host.toLowerCase();
    if (host.length() == 0) return false;

    String apIp = WiFi.softAPIP().toString();
    String staIp = WiFi.localIP().toString();
    String mdnsHost = String(MDNS_HOSTNAME) + ".local";

    return (host == apIp || host == staIp || host == "192.168.4.1" ||
            host == "192.168.5.1" || host == mdnsHost || host == "localhost" || host == "127.0.0.1");
}

static bool extractJsonString(JsonDocument& doc, const char* key, String& out) {
    if (doc[key].is<const char*>()) {
        out = String(doc[key].as<const char*>());
        return true;
    }
    if (doc[key].is<String>()) {
        out = doc[key].as<String>();
        return true;
    }
    out = "";
    return false;
}

static String lookupClientIpByMac(const uint8_t mac[6]) {
    esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!apNetif) return "";

    esp_netif_pair_mac_ip_t pair;
    memcpy(pair.mac, mac, 6);
    pair.ip.addr = 0;

    if (esp_netif_dhcps_get_clients_by_mac(apNetif, 1, &pair) == ESP_OK && pair.ip.addr != 0) {
        IPAddress ip(pair.ip.addr);
        return ip.toString();
    }
    return "";
}

WebPortal& WebPortal::instance() {
    static WebPortal portal;
    return portal;
}

WebPortal::WebPortal()
    : server(80), dnsServer(nullptr), dnsRunning(false),
      pendingConnectScheduled(false), pendingConnectSsid(""), pendingConnectPass(""), pendingConnectTime(0),
      pendingReboot(false), pendingRebootTime(0),
      otaInProgress(false), otaValidHeader(false),
      lastRateCalcTime(0), rxRateKbps(0.0f), txRateKbps(0.0f) {}

void WebPortal::startCaptiveDns() {
    if (!dnsServer) {
        // Relinquish Port 53 cleanly from DnsCache before starting Captive DNSServer
        DnsCache::instance().stopAsyncProxy();
        dnsServer = new DNSServer();
        dnsServer->start(53, "*", WiFi.softAPIP());
        dnsRunning = true;
        apLog("[DNS] Captive portal wildcard engaged on port 53.\n");
    }
}

void WebPortal::stopCaptiveDns() {
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
        dnsRunning = false;
        apLog("[DNS] Captive wildcard stopped.\n");
    }
    if (!DnsCache::instance().isProxyRunning()) {
        DnsCache::instance().startAsyncProxy(53);
    }
}

void WebPortal::onStaConnected() {
    stopCaptiveDns();
}

void WebPortal::onStaDisconnected() {
    if (StorageManager::instance().getKnownNetworks().empty()) {
        startCaptiveDns();
    } else {
        stopCaptiveDns();
    }
}

void WebPortal::init() {
    // Ask WebServer to collect Host, Origin, and Referer for security validation
    const char* headerKeys[] = {"Host", "Origin", "Referer"};
    server.collectHeaders(headerKeys, 3);

    registerRoutes();
    server.begin();

    if (!RoamingEngine::instance().isConnected() && StorageManager::instance().getKnownNetworks().empty()) {
        startCaptiveDns();
    } else {
        stopCaptiveDns();
    }

    apLog("[WEB] %s %s Web Portal listening on port 80.\n", PROJECT_NAME, PROJECT_VERSION);
}

void WebPortal::update() {
    if (dnsRunning && dnsServer) {
        dnsServer->processNextRequest();
    }

    if (pendingReboot && millis() - pendingRebootTime >= 400) {
        pendingReboot = false;
        StorageManager::instance().commitJournal(true);
        ESP.restart();
    }

    if (pendingConnectScheduled && millis() - pendingConnectTime >= 250) {
        pendingConnectScheduled = false;
        RoamingEngine::instance().connectToNetwork(pendingConnectSsid, pendingConnectPass);
    }

    bool online = RoamingEngine::instance().isConnected();
    bool virginMode = StorageManager::instance().getKnownNetworks().empty();
    if (online || !virginMode) {
        if (dnsRunning) {
            stopCaptiveDns();
        }
    } else if (!dnsRunning) {
        startCaptiveDns();
    }

    unsigned long now = millis();
    unsigned long elapsed = now - lastRateCalcTime;
    if (elapsed >= 1000) {
        lastRateCalcTime = now;
        uint32_t upBytes = 0, downBytes = 0;
        NaptEngine::instance().consumeTrafficBytes(upBytes, downBytes);
        float sec = (float)elapsed / 1000.0f;
        txRateKbps = (sec > 0.0f) ? ((float)upBytes * 8.0f) / (sec * 1000.0f) : 0.0f;
        rxRateKbps = (sec > 0.0f) ? ((float)downBytes * 8.0f) / (sec * 1000.0f) : 0.0f;
    }

    server.handleClient();
}

bool WebPortal::isCaptivePortal() {
    String host = server.hostHeader();
    if (host.length() == 0) return false;
    return !isLocalHostMatch(host);
}

bool WebPortal::isTrustedLocalRequest() {
    String host = server.header("Host");
    if (host.length() > 0 && !isLocalHostMatch(host)) {
        server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Untrusted Host header\"}");
        return false;
    }
    String origin = server.header("Origin");
    if (origin.length() > 0) {
        int schemeEnd = origin.indexOf("://");
        String originHost = (schemeEnd >= 0) ? origin.substring(schemeEnd + 3) : origin;
        int slash = originHost.indexOf('/');
        if (slash >= 0) originHost = originHost.substring(0, slash);
        if (!isLocalHostMatch(originHost)) {
            server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Cross-origin request rejected\"}");
            return false;
        }
    }
    return true;
}

void WebPortal::sendSecurityHeaders() {
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
}

void WebPortal::registerRoutes() {
    server.on("/", HTTP_GET, [this]() {
        if (!RoamingEngine::instance().isConnected() && isCaptivePortal()) {
            String redirectUrl = String("http://") + WiFi.softAPIP().toString() + "/";
            server.sendHeader("Location", redirectUrl, true);
            server.send(302, "text/plain", "");
            return;
        }
        handleRoot();
    });
    server.on("/", HTTP_HEAD, [this]() {
        sendSecurityHeaders();
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Vary", "Accept-Encoding");
        server.send(200, "text/html", "");
    });

    auto sendProbeResponse = [this]() {
        if (RoamingEngine::instance().isConnected()) {
            String uri = server.uri();
            if (uri.indexOf("204") >= 0) {
                server.send(204, "text/plain", "");
            } else if (uri.indexOf("detect") >= 0) {
                server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
            } else {
                server.send(200, "text/plain", "Microsoft NCSI");
            }
        } else {
            String redirectUrl = String("http://") + WiFi.softAPIP().toString() + "/";
            server.sendHeader("Location", redirectUrl, true);
            server.send(302, "text/plain", "");
        }
    };

    server.on("/generate_204", HTTP_GET, sendProbeResponse);
    server.on("/gen_204", HTTP_GET, sendProbeResponse);
    server.on("/hotspot-detect.html", HTTP_GET, sendProbeResponse);
    server.on("/canonical.html", HTTP_GET, sendProbeResponse);
    server.on("/ncsi.txt", HTTP_GET, sendProbeResponse);
    server.on("/connecttest.txt", HTTP_GET, sendProbeResponse);

    server.on("/manifest.webmanifest", HTTP_GET, [this]() { handleManifest(); });

    // REST API (Read)
    server.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    server.on("/api/scan", HTTP_GET, [this]() { handleApiScan(); });
    server.on("/api/known", HTTP_GET, [this]() { handleApiKnownNetworks(); });
    server.on("/api/settings", HTTP_GET, [this]() { handleApiSettings(); });
    server.on("/api/clients", HTTP_GET, [this]() { handleApiClients(); });
    server.on("/api/portmap", HTTP_GET, [this]() { handleApiPortmapGet(); });
    server.on("/api/spectrum", HTTP_GET, [this]() { handleApiSpectrum(); });
    server.on("/api/dns/stats", HTTP_GET, [this]() { handleApiDnsStats(); });
    server.on("/api/speedtest", HTTP_GET, [this]() { handleApiSpeedtest(); });
    server.on("/api/topology", HTTP_GET, [this]() { handleApiTopology(); });

    // REST API (Mutations - validated against Host/Origin)
    server.on("/api/connect", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiConnect(); });
    server.on("/api/disconnect", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiDisconnect(); });
    server.on("/api/known/add", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiAddKnown(); });
    server.on("/api/known/delete", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiDeleteKnown(); });
    server.on("/api/settings", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiSaveSettings(); });
    server.on("/api/reboot", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiReboot(); });
    server.on("/api/portmap/add", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiPortmapAdd(); });
    server.on("/api/portmap/delete", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiPortmapDelete(); });
    server.on("/api/mac/set", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiMacSet(); });
    server.on("/api/mac/reset", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiMacReset(); });
    server.on("/api/dns/clear", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiDnsClear(); });
    server.on("/api/clients/rename", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiClientsRename(); });
    server.on("/api/clients/kick", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiClientsKick(); });
    server.on("/api/isolation/toggle", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiIsolationToggle(); });
    server.on("/api/tpc/toggle", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiTpcToggle(); });
    server.on("/api/wan/toggle", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiWanToggle(); });
    server.on("/api/wan/probe", HTTP_POST, [this]() { if (isTrustedLocalRequest()) handleApiWanProbe(); });

    // Hardened OTA Firmware Upload
    server.on("/api/firmware/update", HTTP_POST, [this]() {
        sendSecurityHeaders();
        server.sendHeader("Connection", "close");
        if (!isTrustedLocalRequest()) {
            Update.abort();
            otaInProgress = false;
            return;
        }
        if (otaInProgress || !otaValidHeader || Update.hasError() || !Update.isFinished()) {
            Update.abort();
            otaInProgress = false;
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid or incomplete firmware image\"}");
        } else {
            server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Firmware flashed. Rebooting...\"}");
            pendingReboot = true;
            pendingRebootTime = millis();
        }
    }, [this]() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            otaInProgress = true;
            otaValidHeader = false;
            if (!isLocalHostMatch(server.header("Host"))) {
                return;
            }
            apLog("[OTA] Starting firmware upload: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            esp_task_wdt_reset();
            if (!otaValidHeader && upload.currentSize > 0) {
                if (upload.buf[0] != 0xE9) {
                    apLog("[OTA] Rejected upload: invalid ESP32 magic byte 0x%02X\n", upload.buf[0]);
                    Update.abort();
                    otaInProgress = false;
                    return;
                }
                otaValidHeader = true;
            }
            if (otaValidHeader && !Update.hasError()) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (otaValidHeader && !Update.hasError()) {
                if (Update.end(true)) {
                    apLog("[OTA] Flashed %u bytes successfully.\n", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
            otaInProgress = false;
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Update.abort();
            otaInProgress = false;
            otaValidHeader = false;
            apLog("[OTA] Upload aborted by client.\n");
        }
    });

    server.onNotFound([this]() { handleNotFound(); });
}

void WebPortal::handleRoot() {
    sendSecurityHeaders();
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Vary", "Accept-Encoding");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send_P(200, "text/html", (PGM_P)PORTAL_HTML_GZ, PORTAL_HTML_GZ_LEN);
}

void WebPortal::handleManifest() {
    sendSecurityHeaders();
    server.send(200, "application/manifest+json",
        "{\"name\":\"AetherPulse\",\"short_name\":\"AetherPulse\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#09090b\",\"theme_color\":\"#09090b\"}");
}

void WebPortal::handleNotFound() {
    sendSecurityHeaders();
    if (!RoamingEngine::instance().isConnected() && isCaptivePortal()) {
        String redirectUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server.sendHeader("Location", redirectUrl, true);
        server.send(302, "text/plain", "");
    } else {
        server.send(404, "text/plain", "404 Not Found");
    }
}

void WebPortal::handleApiStatus() {
    sendSecurityHeaders();
    JsonDocument doc;
    auto& roam = RoamingEngine::instance();
    auto& napt = NaptEngine::instance();
    auto settings = StorageManager::instance().getSettings();
    auto dnsStats = DnsCache::instance().getStats();

    doc["connected"] = roam.isConnected();
    doc["scanning"] = roam.isScanning();
    doc["upstream_ssid"] = roam.getCurrentSsid();
    doc["upstream_bssid"] = roam.getCurrentBssid();
    doc["upstream_ip"] = roam.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
    doc["upstream_gw"] = roam.isConnected() ? WiFi.gatewayIP().toString() : "0.0.0.0";
    doc["upstream_channel"] = roam.getCurrentChannel();
    doc["upstream_rssi"] = roam.getCurrentRssi();

    doc["softap_ssid"] = settings.apSSID;
    doc["softap_ip"] = WiFi.softAPIP().toString();
    doc["softap_clients"] = WiFi.softAPgetStationNum();
    doc["softap_max_clients"] = settings.apMaxClients;

    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["largest_free_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    doc["uptime"] = millis() / 1000;
    doc["cpu_freq"] = ESP.getCpuFreqMHz();
    doc["die_temp"] = roundf(temperatureRead() * 10.0f) / 10.0f;

    doc["rx_rate"] = roundf(rxRateKbps * 10.0f) / 10.0f;
    doc["tx_rate"] = roundf(txRateKbps * 10.0f) / 10.0f;
    doc["cloned_mac"] = settings.clonedMac;
    doc["napt_enabled"] = napt.isEnabled();
    doc["napt_mss_clamped"] = napt.getMssClampedCount();
    doc["napt_hairpin"] = napt.getHairpinCount();
    doc["portmap_count"] = napt.getActivePortmapCount();

    doc["dns_hits"] = dnsStats.totalHits;
    doc["dns_misses"] = dnsStats.totalMisses;
    doc["dns_active_entries"] = dnsStats.activeEntries;
    doc["dns_capacity"] = dnsStats.capacity;
    doc["best_channel"] = roam.getBestChannel();

    doc["client_isolation"] = settings.clientIsolation;
    doc["dynamic_tpc"] = settings.dynamicTpcEnabled;
    doc["tx_power_dbm"] = roam.getCurrentTxPowerDbm();
    doc["tcp_mss"] = TCP_MSS_CLAMP_VALUE;
    doc["wan_online"] = roam.isWanOnline();
    doc["wan_rtt_ms"] = roam.getWanRttMs();
    doc["wan_health_watchdog"] = settings.wanWatchdogEnabled;
    doc["wan_consecutive_fails"] = roam.getWanConsecutiveFails();
    doc["wan_failover_count"] = roam.getWanFailoverCount();
    doc["wan_probe_interval"] = settings.wanProbeIntervalSec;
    doc["project_name"] = PROJECT_NAME;
    doc["version"] = PROJECT_VERSION;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiScan() {
    sendSecurityHeaders();
    auto& roam = RoamingEngine::instance();
    bool triggerNew = server.hasArg("refresh") && server.arg("refresh") == "1";
    if (triggerNew || roam.getLastScanResults().empty()) {
        roam.startScan();
    }

    auto results = roam.getLastScanResults();
    JsonDocument doc;
    doc["scanning"] = roam.isScanning();
    JsonArray arr = doc["networks"].to<JsonArray>();

    for (const auto& ap : results) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = ap.ssid;
        obj["bssid"] = ap.bssid;
        obj["rssi"] = ap.rssi;
        obj["channel"] = ap.channel;
        obj["is_known"] = ap.isKnown;

        switch (ap.encryptionType) {
            case WIFI_AUTH_OPEN: obj["auth"] = "Open"; break;
            case WIFI_AUTH_WEP:  obj["auth"] = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: obj["auth"] = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: obj["auth"] = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: obj["auth"] = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: obj["auth"] = "WPA3"; break;
            default: obj["auth"] = "Secured"; break;
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiConnect() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String ssid, pass;
    if (!extractJsonString(doc, "ssid", ssid) || ssid.length() == 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID is required\"}");
        return;
    }
    extractJsonString(doc, "password", pass);

    if (pass.length() == 0) {
        auto known = StorageManager::instance().getKnownNetworks();
        for (const auto& k : known) {
            if (k.ssid == ssid) {
                pass = k.password;
                break;
            }
        }
    }

    pendingConnectSsid = ssid;
    pendingConnectPass = pass;
    pendingConnectScheduled = true;
    pendingConnectTime = millis();

    JsonDocument res;
    res["status"] = "connecting";
    res["message"] = "Associating with '" + ssid + "'...";
    String out;
    serializeJson(res, out);
    server.send(200, "application/json", out);
}

void WebPortal::handleApiDisconnect() {
    sendSecurityHeaders();
    RoamingEngine::instance().disconnect();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Disconnected from upstream network.\"}");
}

void WebPortal::handleApiKnownNetworks() {
    sendSecurityHeaders();
    auto known = StorageManager::instance().getKnownNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& k : known) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = k.ssid;
        obj["priority"] = k.priority;
    }
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiAddKnown() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String ssid, pass;
    if (!extractJsonString(doc, "ssid", ssid) || ssid.length() == 0 || ssid.length() > 32) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Valid SSID (1-32 chars) required\"}");
        return;
    }
    extractJsonString(doc, "password", pass);
    int priority = doc["priority"] | 5;
    if (priority < 1) priority = 1;
    if (priority > 10) priority = 10;

    if (StorageManager::instance().addKnownNetwork(ssid, pass, priority)) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Network saved to failover pool.\"}");
    } else {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failover pool limit reached (max 10).\"}");
    }
}

void WebPortal::handleApiDeleteKnown() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String ssid;
    if (!extractJsonString(doc, "ssid", ssid) || ssid.length() == 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID is required\"}");
        return;
    }

    if (StorageManager::instance().removeKnownNetwork(ssid)) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Network removed.\"}");
    } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Network not found.\"}");
    }
}

void WebPortal::handleApiSettings() {
    sendSecurityHeaders();
    auto s = StorageManager::instance().getSettings();
    JsonDocument doc;
    doc["ap_ssid"] = s.apSSID;
    doc["ap_pass_set"] = (s.apPassword.length() > 0);
    doc["tx_power"] = s.txPowerDbm;
    doc["auto_roam"] = s.autoRoamEnabled;
    doc["cloned_mac"] = s.clonedMac;
    doc["client_isolation"] = s.clientIsolation;
    doc["dynamic_tpc"] = s.dynamicTpcEnabled;
    doc["wan_watchdog_enabled"] = s.wanWatchdogEnabled;
    doc["wan_probe_interval_sec"] = s.wanProbeIntervalSec;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiSaveSettings() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    auto s = StorageManager::instance().getSettings();
    String prevSsid = s.apSSID;
    String prevPass = s.apPassword;

    String nextSsid;
    if (extractJsonString(doc, "ap_ssid", nextSsid)) {
        if (nextSsid.length() == 0 || nextSsid.length() > 32) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"SoftAP SSID must be 1-32 characters\"}");
            return;
        }
        s.apSSID = nextSsid;
    }

    String nextPass;
    if (extractJsonString(doc, "ap_pass", nextPass) && nextPass.length() > 0) {
        if (nextPass.length() < 8 || nextPass.length() > 63) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"WPA2 passphrase must be 8-63 characters\"}");
            return;
        }
        s.apPassword = nextPass;
    }

    if (!doc["tx_power"].isNull()) {
        float pwr = doc["tx_power"].as<float>();
        float minDbm = MIN_TX_POWER_RAW * 0.25f;
        float maxDbm = MAX_TX_POWER_RAW * 0.25f;
        if (pwr < minDbm) pwr = minDbm;
        if (pwr > maxDbm) pwr = maxDbm;
        s.txPowerDbm = pwr;
    }
    if (!doc["auto_roam"].isNull()) s.autoRoamEnabled = doc["auto_roam"].as<bool>();
    if (!doc["client_isolation"].isNull()) s.clientIsolation = doc["client_isolation"].as<bool>();
    if (!doc["dynamic_tpc"].isNull()) s.dynamicTpcEnabled = doc["dynamic_tpc"].as<bool>();
    if (!doc["wan_watchdog_enabled"].isNull()) s.wanWatchdogEnabled = doc["wan_watchdog_enabled"].as<bool>();
    if (!doc["wan_probe_interval_sec"].isNull()) {
        uint32_t interval = doc["wan_probe_interval_sec"].as<uint32_t>();
        if (interval >= 2 && interval <= 300) {
            s.wanProbeIntervalSec = interval;
        }
    }

    StorageManager::instance().saveSettings(s);
    if (!s.dynamicTpcEnabled) {
        RoamingEngine::instance().setTxPowerDbm(s.txPowerDbm);
    }

    if (s.apSSID != prevSsid || s.apPassword != prevPass) {
        WiFi.AP.create(s.apSSID.c_str(), s.apPassword.c_str(), s.apChannel, s.apHidden, s.apMaxClients);
    }

    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings saved and applied.\"}");
}

void WebPortal::handleApiClients() {
    sendSecurityHeaders();
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        for (int i = 0; i < sta_list.num; i++) {
            JsonObject obj = arr.add<JsonObject>();
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     sta_list.sta[i].mac[0], sta_list.sta[i].mac[1], sta_list.sta[i].mac[2],
                     sta_list.sta[i].mac[3], sta_list.sta[i].mac[4], sta_list.sta[i].mac[5]);
            obj["mac"] = macStr;
            obj["rssi"] = sta_list.sta[i].rssi;
            obj["alias"] = StorageManager::instance().getClientAlias(macStr);
            obj["vendor"] = StorageManager::instance().getVendorName(macStr);
            obj["ip"] = lookupClientIpByMac(sta_list.sta[i].mac);
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiPortmapGet() {
    sendSecurityHeaders();
    auto rules = StorageManager::instance().getPortForwards();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& r : rules) {
        JsonObject obj = arr.add<JsonObject>();
        obj["proto"] = r.protocol;
        obj["extPort"] = r.externalPort;
        obj["intIp"] = r.internalIp;
        obj["intPort"] = r.internalPort;
        obj["desc"] = r.description;
    }
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiPortmapAdd() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    uint8_t proto = doc["proto"] | 6;
    uint16_t extPort = doc["extPort"] | 0;
    uint16_t intPort = doc["intPort"] | 0;
    String intIp, desc;
    extractJsonString(doc, "intIp", intIp);
    extractJsonString(doc, "desc", desc);

    if (proto != 6 && proto != 17) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Protocol must be TCP (6) or UDP (17)\"}");
        return;
    }
    if (extPort == 80 || extPort == 53 || extPort == 0 || intPort == 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid port (ports 0, 53, 80 are reserved)\"}");
        return;
    }
    IPAddress parsedIp;
    if (!parsedIp.fromString(intIp) || (uint32_t)parsedIp == 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid internal IPv4 address\"}");
        return;
    }

    if (NaptEngine::instance().addPortForward(proto, extPort, intIp, intPort, desc)) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Port forward rule added.\"}");
    } else {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Failed to add rule (table full or NAPT error).\"}");
    }
}

void WebPortal::handleApiPortmapDelete() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    uint8_t proto = doc["proto"] | 6;
    uint16_t extPort = doc["extPort"] | 0;
    if (extPort == 0 || (proto != 6 && proto != 17)) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid protocol or port\"}");
        return;
    }

    if (NaptEngine::instance().removePortForward(proto, extPort)) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rule deleted.\"}");
    } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Rule not found.\"}");
    }
}

void WebPortal::handleApiMacSet() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String mac;
    if (!extractJsonString(doc, "mac", mac) || !RoamingEngine::instance().applyClonedMac(mac)) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid unicast MAC (use XX:XX:XX:XX:XX:XX)\"}");
        return;
    }
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Cloned MAC applied.\"}");
}

void WebPortal::handleApiMacReset() {
    sendSecurityHeaders();
    RoamingEngine::instance().resetMac();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Factory hardware MAC restored.\"}");
}

void WebPortal::handleApiSpectrum() {
    sendSecurityHeaders();
    auto loads = RoamingEngine::instance().getChannelLoads();
    int best = RoamingEngine::instance().getBestChannel();

    JsonDocument doc;
    JsonArray arr = doc["channels"].to<JsonArray>();
    for (int i = 1; i <= 13; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["channel"] = i;
        obj["load"] = loads[i];
    }
    doc["best_channel"] = best;
    doc["current_channel"] = RoamingEngine::instance().getCurrentChannel();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiDnsStats() {
    sendSecurityHeaders();
    auto stats = DnsCache::instance().getStats();
    JsonDocument doc;
    doc["hits"] = stats.totalHits;
    doc["misses"] = stats.totalMisses;
    doc["active"] = stats.activeEntries;
    doc["capacity"] = stats.capacity;
    uint32_t total = stats.totalHits + stats.totalMisses;
    doc["hit_rate"] = total > 0 ? (float)stats.totalHits * 100.0f / total : 0.0f;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiDnsClear() {
    sendSecurityHeaders();
    DnsCache::instance().clear();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"DNS cache flushed.\"}");
}

void WebPortal::handleApiClientsRename() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String mac, alias;
    if (!extractJsonString(doc, "mac", mac)) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"MAC address required\"}");
        return;
    }
    extractJsonString(doc, "alias", alias);

    if (StorageManager::instance().setClientAlias(mac, alias)) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Client alias updated.\"}");
    } else {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid MAC address format.\"}");
    }
}

void WebPortal::handleApiClientsKick() {
    sendSecurityHeaders();
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    String macStr;
    if (!extractJsonString(doc, "mac", macStr) || macStr.length() != 17) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid MAC address\"}");
        return;
    }

    uint8_t mac[6];
    if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(mac, &aid) == ESP_OK) {
            esp_wifi_deauth_sta(aid);
            server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Client deauthenticated.\"}");
            return;
        }
    }
    server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Client not found.\"}");
}

void WebPortal::handleApiSpeedtest() {
    sendSecurityHeaders();
    const size_t totalBytes = 512 * 1024; // 512 KB link benchmark stream
    server.setContentLength(totalBytes);
    server.send(200, "application/octet-stream", "");

    uint8_t chunk[2048];
    memset(chunk, 0x5A, sizeof(chunk));
    size_t sent = 0;
    WiFiClient client = server.client();
    unsigned long lastProgress = millis();

    while (sent < totalBytes && client.connected()) {
        esp_task_wdt_reset();
        size_t toSend = (totalBytes - sent > sizeof(chunk)) ? sizeof(chunk) : (totalBytes - sent);
        size_t written = client.write(chunk, toSend);
        if (written > 0) {
            sent += written;
            lastProgress = millis();
        } else {
            if (millis() - lastProgress > 3000) break;
            delay(2);
        }
        yield();
    }
}

void WebPortal::handleApiTopology() {
    sendSecurityHeaders();
    auto& roam = RoamingEngine::instance();
    auto settings = StorageManager::instance().getSettings();

    JsonDocument doc;
    JsonObject upstream = doc["upstream"].to<JsonObject>();
    upstream["connected"] = roam.isConnected();
    upstream["ssid"] = roam.getCurrentSsid();
    upstream["bssid"] = roam.getCurrentBssid();
    upstream["rssi"] = roam.getCurrentRssi();
    upstream["channel"] = roam.getCurrentChannel();
    upstream["ip"] = roam.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
    upstream["gateway"] = roam.isConnected() ? WiFi.gatewayIP().toString() : "0.0.0.0";

    doc["wan_online"] = roam.isWanOnline();
    doc["wan_rtt_ms"] = roam.getWanRttMs();

    JsonObject repeater = doc["repeater"].to<JsonObject>();
    repeater["name"] = PROJECT_NAME;
    repeater["version"] = PROJECT_VERSION;
    repeater["ssid"] = settings.apSSID;
    repeater["ip"] = WiFi.softAPIP().toString();
    repeater["channel"] = roam.isConnected() ? roam.getCurrentChannel() : settings.apChannel;
    repeater["cpu_mhz"] = ESP.getCpuFreqMHz();
    repeater["free_heap"] = ESP.getFreeHeap();
    repeater["temp_c"] = roundf(temperatureRead() * 10.0f) / 10.0f;
    repeater["tx_power_dbm"] = roam.getCurrentTxPowerDbm();
    repeater["client_count"] = WiFi.softAPgetStationNum();
    repeater["isolation"] = settings.clientIsolation;
    repeater["tpc"] = settings.dynamicTpcEnabled;

    JsonArray clients = doc["clients"].to<JsonArray>();
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        for (int i = 0; i < sta_list.num; i++) {
            JsonObject c = clients.add<JsonObject>();
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     sta_list.sta[i].mac[0], sta_list.sta[i].mac[1], sta_list.sta[i].mac[2],
                     sta_list.sta[i].mac[3], sta_list.sta[i].mac[4], sta_list.sta[i].mac[5]);
            c["mac"] = macStr;
            c["rssi"] = sta_list.sta[i].rssi;
            c["alias"] = StorageManager::instance().getClientAlias(macStr);
            c["vendor"] = StorageManager::instance().getVendorName(macStr);
            String realIp = lookupClientIpByMac(sta_list.sta[i].mac);
            c["ip"] = realIp.length() > 0 ? realIp : "DHCP Pending";
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiIsolationToggle() {
    sendSecurityHeaders();
    auto s = StorageManager::instance().getSettings();
    bool nextState = !s.clientIsolation;
    StorageManager::instance().setClientIsolation(nextState);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["client_isolation"] = nextState;
    doc["message"] = nextState ? "L2 Client Isolation enabled." : "L2 Client Isolation disabled.";
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiTpcToggle() {
    sendSecurityHeaders();
    auto s = StorageManager::instance().getSettings();
    bool nextState = !s.dynamicTpcEnabled;
    StorageManager::instance().setDynamicTpc(nextState);
    if (!nextState) {
        RoamingEngine::instance().setTxPowerDbm(s.txPowerDbm);
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["dynamic_tpc"] = nextState;
    doc["message"] = nextState ? "Dynamic TPC enabled." : "Dynamic TPC disabled (static TX power active).";
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiWanToggle() {
    sendSecurityHeaders();
    auto s = StorageManager::instance().getSettings();
    bool nextState = !s.wanWatchdogEnabled;
    StorageManager::instance().setWanWatchdog(nextState, s.wanProbeIntervalSec);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["wan_watchdog_enabled"] = nextState;
    doc["message"] = nextState ? "WAN Health Watchdog enabled." : "WAN Health Watchdog paused.";
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiWanProbe() {
    sendSecurityHeaders();
    auto& roam = RoamingEngine::instance();
    float rtt = 0.0f;
    bool ok = roam.executeWanProbe(&rtt);

    JsonDocument doc;
    doc["status"] = ok ? "ok" : "fail";
    doc["wan_online"] = ok;
    doc["rtt_ms"] = roundf(rtt * 10.0f) / 10.0f;
    doc["message"] = ok ? "WAN internet reachability verified." : "WAN probe failed: Anycast targets unreachable.";
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void WebPortal::handleApiReboot() {
    sendSecurityHeaders();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting device...\"}");
    pendingReboot = true;
    pendingRebootTime = millis();
}
