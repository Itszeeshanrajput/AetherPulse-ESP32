#include "web_portal.h"
#include "portal_html.h"
#include "roaming_engine.h"
#include "napt_engine.h"
#include "storage_manager.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>

WebPortal& WebPortal::instance() {
    static WebPortal portal;
    return portal;
}

WebPortal::WebPortal() : server(80), dnsRunning(false) {}

void WebPortal::init() {
    registerRoutes();
    server.begin();
    Serial.println("[WEB] High-Performance Web Portal listening on port 80.");
}

void WebPortal::update() {
    bool online = RoamingEngine::instance().isConnected();
    if (online) {
        // Upstream online: Disengage wildcard DNS hijacking so clients query real public DNS (8.8.8.8 / upstream)
        if (dnsRunning) {
            dnsServer.stop();
            dnsRunning = false;
            Serial.println("[DNS] Upstream online: Captive DNS wildcard disengaged. Direct Internet routing active.");
        }
    } else {
        // Upstream offline: Engage Captive Portal so any browser request opens repeater configuration
        if (!dnsRunning) {
            dnsServer.start(53, "*", DEFAULT_AP_IP);
            dnsRunning = true;
            Serial.println("[DNS] Upstream offline: Captive DNS wildcard engaged for setup.");
        }
        dnsServer.processNextRequest();
    }
    server.handleClient();
}

bool WebPortal::isCaptivePortal() {
    String host = server.hostHeader();
    if (host.length() == 0) return false;
    if (host != "192.168.4.1" && host != "aetherpulse.local") {
        return true;
    }
    return false;
}

void WebPortal::registerRoutes() {
    server.on("/", HTTP_GET, [this]() {
        if (!RoamingEngine::instance().isConnected() && isCaptivePortal()) {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
            return;
        }
        handleRoot();
    });

    // Captive portal probes from Android, iOS, macOS, Windows
    // When ONLINE: Return 204 / 200 Success so devices recognize active internet
    // When OFFLINE: Redirect to 192.168.4.1 for setup
    auto sendProbeResponse = [this]() {
        if (RoamingEngine::instance().isConnected()) {
            String uri = server.uri();
            if (uri.indexOf("204") >= 0) {
                server.send(204, "text/plain", "");
            } else if (uri.indexOf("detect") >= 0) {
                server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
            } else if (uri.indexOf("connecttest") >= 0) {
                server.send(200, "text/plain", "Microsoft Connect Test");
            } else if (uri.indexOf("ncsi") >= 0) {
                server.send(200, "text/plain", "Microsoft NCSI");
            } else {
                server.send(200, "text/plain", "OK");
            }
        } else {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        }
    };

    server.on("/hotspot-detect.html", HTTP_GET, sendProbeResponse);
    server.on("/generate_204", HTTP_GET, sendProbeResponse);
    server.on("/gen_204", HTTP_GET, sendProbeResponse);
    server.on("/canonical.html", HTTP_GET, sendProbeResponse);
    server.on("/connecttest.txt", HTTP_GET, sendProbeResponse);
    server.on("/ncsi.txt", HTTP_GET, sendProbeResponse);
    server.on("/success.txt", HTTP_GET, sendProbeResponse);

    // REST API endpoints
    server.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    server.on("/api/scan", HTTP_GET, [this]() { handleApiScan(); });
    server.on("/api/connect", HTTP_POST, [this]() { handleApiConnect(); });
    server.on("/api/disconnect", HTTP_POST, [this]() { handleApiDisconnect(); });
    server.on("/api/known", HTTP_GET, [this]() { handleApiKnownNetworks(); });
    server.on("/api/known/add", HTTP_POST, [this]() { handleApiAddKnown(); });
    server.on("/api/known/delete", HTTP_POST, [this]() { handleApiDeleteKnown(); });
    server.on("/api/settings", HTTP_GET, [this]() { handleApiSettings(); });
    server.on("/api/settings", HTTP_POST, [this]() { handleApiSaveSettings(); });
    server.on("/api/clients", HTTP_GET, [this]() { handleApiClients(); });
    server.on("/api/reboot", HTTP_POST, [this]() { handleApiReboot(); });

    server.onNotFound([this]() { handleNotFound(); });
}

void WebPortal::handleRoot() {
    server.send_P(200, "text/html", PORTAL_HTML);
}

void WebPortal::handleNotFound() {
    if (!RoamingEngine::instance().isConnected()) {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
        return;
    }
    String uri = server.uri();
    if (uri.indexOf("204") >= 0) {
        server.send(204, "text/plain", "");
        return;
    }
    if (uri.indexOf("detect") >= 0 || uri.indexOf("test") >= 0 || uri.indexOf("success") >= 0) {
        server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
        return;
    }
    server.send(404, "text/plain", "404: Not Found");
}

void WebPortal::handleApiStatus() {
    auto& roam = RoamingEngine::instance();
    auto& napt = NaptEngine::instance();
    auto settings = StorageManager::instance().getSettings();

    JsonDocument doc;
    doc["connected"] = roam.isConnected();
    doc["upstream_ssid"] = roam.getCurrentSsid();
    doc["upstream_bssid"] = roam.getCurrentBssid();
    doc["upstream_rssi"] = roam.getCurrentRssi();
    doc["upstream_channel"] = roam.getCurrentChannel();
    doc["upstream_ip"] = roam.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
    doc["upstream_gw"] = roam.isConnected() ? WiFi.gatewayIP().toString() : "0.0.0.0";

    doc["softap_ssid"] = settings.apSSID;
    doc["softap_ip"] = WiFi.softAPIP().toString();
    doc["softap_clients"] = WiFi.softAPgetStationNum();
    doc["softap_max_clients"] = settings.apMaxClients;

    doc["napt_enabled"] = napt.isEnabled();
    doc["napt_flows"] = napt.getStats().activeFlows;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiScan() {
    auto& roam = RoamingEngine::instance();
    
    auto results = roam.getLastScanResults();
    if (results.empty() && !roam.isScanning()) {
        roam.startScan();
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& ap : results) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = ap.ssid;
        obj["bssid"] = ap.bssid;
        obj["rssi"] = ap.rssi;
        obj["channel"] = ap.channel;
        
        String authStr = "OPEN";
        switch (ap.encryptionType) {
            case WIFI_AUTH_WEP: authStr = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: authStr = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: authStr = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: authStr = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: authStr = "WPA3"; break;
            default: break;
        }
        obj["auth"] = authStr;
        obj["is_known"] = ap.isKnown;
    }

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiConnect() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String ssid = doc["ssid"] | "";
    String pass = doc["password"] | "";

    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"SSID required\"}");
        return;
    }

    // If password not passed, check known database
    if (pass.length() == 0) {
        auto known = StorageManager::instance().getKnownNetworks();
        for (const auto& k : known) {
            if (k.ssid == ssid) {
                pass = k.password;
                break;
            }
        }
    }

    bool success = RoamingEngine::instance().connectToNetwork(ssid, pass);
    JsonDocument respDoc;
    respDoc["success"] = success;
    respDoc["message"] = success ? "Connected successfully! NAPT routing active." : "Failed to connect to network.";
    
    String resp;
    serializeJson(respDoc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiDisconnect() {
    RoamingEngine::instance().disconnect();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Disconnected.\"}");
}

void WebPortal::handleApiKnownNetworks() {
    auto list = StorageManager::instance().getKnownNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& k : list) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = k.ssid;
        obj["priority"] = k.priority;
    }

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiAddKnown() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String ssid = doc["ssid"] | "";
    String pass = doc["password"] | "";
    int prio = doc["priority"] | 5;

    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"SSID is required\"}");
        return;
    }

    StorageManager::instance().addKnownNetwork(ssid, pass, prio);
    server.send(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleApiDeleteKnown() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String ssid = doc["ssid"] | "";

    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"SSID is required\"}");
        return;
    }

    StorageManager::instance().removeKnownNetwork(ssid);
    server.send(200, "application/json", "{\"success\":true}");
}

void WebPortal::handleApiSettings() {
    auto s = StorageManager::instance().getSettings();
    JsonDocument doc;
    doc["ap_ssid"] = s.apSSID;
    doc["ap_pass"] = s.apPassword;
    doc["ap_channel"] = s.apChannel;
    doc["tx_power"] = s.txPowerDbm;
    doc["auto_roam"] = s.autoRoamEnabled;
    doc["roam_threshold"] = s.roamThresholdDbm;
    doc["roam_hysteresis"] = s.roamHysteresisDbm;

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiSaveSettings() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    auto s = StorageManager::instance().getSettings();

    if (doc.containsKey("ap_ssid")) s.apSSID = doc["ap_ssid"].as<String>();
    if (doc.containsKey("ap_pass")) s.apPassword = doc["ap_pass"].as<String>();
    if (doc.containsKey("tx_power")) {
        float pwr = doc["tx_power"].as<float>();
        s.txPowerDbm = pwr;
        esp_wifi_set_max_tx_power((int)(pwr * 4.0f));
    }
    if (doc.containsKey("auto_roam")) s.autoRoamEnabled = doc["auto_roam"].as<bool>();

    StorageManager::instance().saveSettings(s);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Settings applied successfully.\"}");
}

void WebPortal::handleApiClients() {
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < sta_list.num; i++) {
        wifi_sta_info_t sta = sta_list.sta[i];
        JsonObject obj = arr.add<JsonObject>();
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta.mac[0], sta.mac[1], sta.mac[2], sta.mac[3], sta.mac[4], sta.mac[5]);
        obj["mac"] = mac_str;
        obj["rssi"] = sta.rssi;
    }

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}

void WebPortal::handleApiReboot() {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting ESP32...\"}");
    delay(500);
    ESP.restart();
}
