#include "cli_shell.h"
#include "roaming_engine.h"
#include "napt_engine.h"
#include "storage_manager.h"
#include <esp_wifi.h>
#include <vector>

static std::vector<String> tokenizeArgs(const String& str) {
    std::vector<String> tokens;
    String cur = "";
    bool inQuote = false;
    char quoteChar = 0;
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if ((c == '"' || c == '\'') && !inQuote) {
            inQuote = true;
            quoteChar = c;
        } else if (inQuote && c == quoteChar) {
            inQuote = false;
        } else if (!inQuote && (c == ' ' || c == '\t')) {
            if (cur.length() > 0) {
                tokens.push_back(cur);
                cur = "";
            }
        } else {
            cur += c;
        }
    }
    if (cur.length() > 0) tokens.push_back(cur);
    return tokens;
}

CliShell& CliShell::instance() {
    static CliShell shell;
    return shell;
}

CliShell::CliShell() {}

void CliShell::init() {
    rxBuffer.reserve(CLI_BUFFER_SIZE);
    Serial.println();
    Serial.println("==================================================================");
    Serial.printf("  %s %s\n", PROJECT_NAME, PROJECT_VERSION);
    Serial.println("  Type 'help' for interactive command list");
    Serial.println("==================================================================");
    Serial.print("aetherpulse> ");
}

void CliShell::update() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (rxBuffer.length() > 0) {
                Serial.println();
                handleCommand(rxBuffer);
                rxBuffer = "";
                Serial.print("aetherpulse> ");
            }
        } else if (c == 8 || c == 127) { // Backspace
            if (rxBuffer.length() > 0) {
                rxBuffer.remove(rxBuffer.length() - 1);
                Serial.print("\b \b");
            }
        } else if (rxBuffer.length() < CLI_BUFFER_SIZE) {
            rxBuffer += c;
            Serial.print(c); // Local echo
        }
    }
}

void CliShell::handleCommand(const String& raw) {
    String line = raw;
    line.trim();
    if (line.length() == 0) return;

    int firstSpace = line.indexOf(' ');
    String cmd = (firstSpace == -1) ? line : line.substring(0, firstSpace);
    String args = (firstSpace == -1) ? "" : line.substring(firstSpace + 1);
    args.trim();

    cmd.toLowerCase();

    if (cmd == "help") {
        printHelp();
    } else if (cmd == "status") {
        printStatus();
    } else if (cmd == "scan") {
        printScan();
    } else if (cmd == "clients") {
        printClients();
    } else if (cmd == "connect") {
        handleConnect(args);
    } else if (cmd == "disconnect") {
        RoamingEngine::instance().disconnect();
    } else if (cmd == "roam") {
        Serial.println("[CLI] Forcing immediate multi-AP roam re-evaluation...");
        RoamingEngine::instance().triggerRoamEvaluation();
    } else if (cmd == "set_tx") {
        handleSetTx(args);
    } else if (cmd == "list_known" || cmd == "list" || cmd == "saved") {
        auto known = StorageManager::instance().getKnownNetworks();
        Serial.printf("\n[SAVED / KNOWN NETWORKS (%d stored)]:\n", (int)known.size());
        Serial.println("+----+------------------------+----------------------+----------+");
        Serial.println("| #  | SSID                   | PASSWORD             | PRIORITY |");
        Serial.println("+----+------------------------+----------------------+----------+");
        for (size_t i = 0; i < known.size(); i++) {
            Serial.printf("| %-2d | %-22.22s | %-20.20s | %-8d |\n", 
                          (int)i+1, known[i].ssid.c_str(), known[i].password.c_str(), known[i].priority);
        }
        Serial.println("+----+------------------------+----------------------+----------+\n");
    } else if (cmd == "add_known" || cmd == "add") {
        handleAddKnown(args);
    } else if (cmd == "del_known" || cmd == "del" || cmd == "remove" || cmd == "rm") {
        handleDelKnown(args);
    } else if (cmd == "ping" || cmd == "test") {
        handlePing(args);
    } else if (cmd == "reboot") {
        Serial.println("[CLI] Rebooting ESP32...");
        delay(500);
        ESP.restart();
    } else {
        Serial.printf("Unknown command '%s'. Type 'help' for command manual.\n", cmd.c_str());
    }
}

void CliShell::printHelp() {
    Serial.println("\nAVAILABLE SERIAL CLI COMMANDS:");
    Serial.println("  status                            - Display system health, Wi-Fi link, NAPT & memory");
    Serial.println("  scan                              - Trigger 2.4 GHz RF scan & display AP table");
    Serial.println("  connect <ssid> <pass>             - Manually connect to an upstream AP (use \"\" for spaces)");
    Serial.println("  disconnect                        - Drop upstream connection & return to standalone AP");
    Serial.println("  roam                              - Trigger immediate background multi-AP roam check");
    Serial.println("  clients                           - List all active stations connected to the repeater SoftAP");
    Serial.println("  set_tx <dbm>                      - Calibrate RF output power (+10.0 to +20.5 dBm)");
    Serial.println("  list / saved                      - Display stored failover Wi-Fi network profiles");
    Serial.println("  add <ssid> <pass> [prio]          - Add or update a saved network (e.g. add \"HomeNetwork\" \"SecretPass123\" 8)");
    Serial.println("  del / remove <ssid>               - Remove network from saved database (e.g. del \"HomeNetwork\")");
    Serial.println("  ping <host>                       - Verify DNS resolution & TCP internet route (e.g. ping google.com)");
    Serial.println("  reboot                            - Perform clean software reset of the ESP32\n");
}

void CliShell::printStatus() {
    auto& roam = RoamingEngine::instance();
    auto& napt = NaptEngine::instance();
    auto settings = StorageManager::instance().getSettings();

    Serial.println("---------------- AETHERPULSE SYSTEM STATUS ----------------");
    Serial.printf("  Device:             ESP32-D0WD-V3 (Rev 3.1) @ %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Free DRAM (Heap):   %d bytes (Min Free: %d bytes)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    Serial.printf("  Uptime:             %lu seconds\n", millis() / 1000);
    Serial.println("--- Upstream Station (STA) Link ---");
    Serial.printf("  Connected:          %s\n", roam.isConnected() ? "YES" : "NO");
    if (roam.isConnected()) {
        Serial.printf("  SSID:               %s\n", roam.getCurrentSsid().c_str());
        Serial.printf("  BSSID:              %s\n", roam.getCurrentBssid().c_str());
        Serial.printf("  Channel:            %d\n", roam.getCurrentChannel());
        Serial.printf("  Filtered RSSI:      %.1f dBm (Kalman Smoothed)\n", roam.getCurrentRssi());
        Serial.printf("  Assigned IP:        %s (GW: %s)\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    }
    Serial.println("--- Repeater SoftAP ---");
    Serial.printf("  SoftAP SSID:        %s\n", settings.apSSID.c_str());
    Serial.printf("  SoftAP IP:          %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  Connected Clients:  %d / %d\n", WiFi.softAPgetStationNum(), settings.apMaxClients);
    Serial.println("--- L3 Routing & NAPT ---");
    Serial.printf("  lwIP NAPT:          %s\n", napt.isEnabled() ? "ACTIVE" : "INACTIVE");
    Serial.printf("  TCP MSS Clamping:   %d bytes\n", napt.getMssClamping());
    Serial.printf("  Modem Sleep:        DISABLED (WIFI_PS_NONE for zero jitter)\n");
    Serial.printf("  Beacon Interval:    %d TU (Prime offset anti-collision)\n", SOFTAP_BEACON_INTERVAL_TU);
    Serial.printf("  TX Power:           %.2f dBm\n", settings.txPowerDbm);
    Serial.println("-----------------------------------------------------------");
}

void CliShell::printScan() {
    Serial.println("[CLI] Performing live Wi-Fi scan, please wait...");
    int n = WiFi.scanNetworks(false, false);
    Serial.printf("\nDISCOVERED 2.4 GHz NETWORKS (%d found):\n", n);
    Serial.println("+----+------------------------+-------------------+---------+-----+--------+");
    Serial.println("| #  | SSID                   | BSSID             | RSSI    | CH  | AUTH   |");
    Serial.println("+----+------------------------+-------------------+---------+-----+--------+");
    for (int i = 0; i < n; i++) {
        String authmode;
        switch (WiFi.encryptionType(i)) {
            case WIFI_AUTH_OPEN: authmode = "OPEN"; break;
            case WIFI_AUTH_WEP: authmode = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: authmode = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: authmode = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: authmode = "WPA/2"; break;
            case WIFI_AUTH_WPA3_PSK: authmode = "WPA3"; break;
            default: authmode = "OTHER"; break;
        }
        Serial.printf("| %-2d | %-22.22s | %-17s | %3d dBm | %3d | %-6s |\n",
                      i+1, WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), 
                      WiFi.RSSI(i), WiFi.channel(i), authmode.c_str());
    }
    Serial.println("+----+------------------------+-------------------+---------+-----+--------+");
    WiFi.scanDelete();
}

void CliShell::printClients() {
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);
    Serial.printf("\nACTIVE CONNECTED REPEATER CLIENTS (%d connected):\n", sta_list.num);
    Serial.println("+----+-------------------+---------+");
    Serial.println("| #  | MAC ADDRESS       | RSSI    |");
    Serial.println("+----+-------------------+---------+");
    for (int i = 0; i < sta_list.num; i++) {
        wifi_sta_info_t sta = sta_list.sta[i];
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta.mac[0], sta.mac[1], sta.mac[2], sta.mac[3], sta.mac[4], sta.mac[5]);
        Serial.printf("| %-2d | %-17s | %3d dBm |\n", i+1, mac_str, sta.rssi);
    }
    Serial.println("+----+-------------------+---------+");
}

void CliShell::handleConnect(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.size() < 1) {
        Serial.println("Usage: connect <ssid> [password] (use \"\" for spaces)");
        return;
    }
    String s = tokens[0];
    String p = (tokens.size() >= 2) ? tokens[1] : "";
    RoamingEngine::instance().connectToNetwork(s, p);
}

void CliShell::handleSetTx(const String& args) {
    float dbm = args.toFloat();
    if (dbm < 10.0f || dbm > 20.5f) {
        Serial.println("Invalid dBm. Range: 10.0 to 20.5 dBm");
        return;
    }
    int raw = (int)(dbm * 4.0f);
    esp_wifi_set_max_tx_power(raw);
    auto s = StorageManager::instance().getSettings();
    s.txPowerDbm = dbm;
    StorageManager::instance().saveSettings(s);
    Serial.printf("[CLI] RF Transmit Power calibrated to %.2f dBm (raw: %d).\n", dbm, raw);
}

void CliShell::handleAddKnown(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.size() < 2) {
        Serial.println("Usage: add <ssid> <password> [priority] (use \"\" for names with spaces)");
        Serial.println("Example: add \"HomeNetwork\" \"SecretPass123\" 8");
        return;
    }
    String s = tokens[0];
    String p = tokens[1];
    int prio = 5;
    if (tokens.size() >= 3) {
        prio = tokens[2].toInt();
        if (prio <= 0) prio = 1;
    }
    StorageManager::instance().addKnownNetwork(s, p, prio);
    Serial.printf("[CLI] Network '%s' successfully saved with priority %d.\n", s.c_str(), prio);
}

void CliShell::handleDelKnown(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.empty()) {
        Serial.println("Usage: del <ssid> (use \"\" for names with spaces)");
        Serial.println("Example: del \"HomeNetwork\"");
        return;
    }
    String s = tokens[0];
    if (StorageManager::instance().removeKnownNetwork(s)) {
        Serial.printf("[CLI] Network '%s' removed from saved database.\n", s.c_str());
    } else {
        Serial.printf("[CLI] Network '%s' was not found in saved database.\n", s.c_str());
    }
}

void CliShell::handlePing(const String& args) {
    auto tokens = tokenizeArgs(args);
    String target = (tokens.size() > 0) ? tokens[0] : "google.com";
    Serial.printf("\n[PING] Testing connectivity to '%s'...\n", target.c_str());

    IPAddress targetIp;
    if (WiFi.hostByName(target.c_str(), targetIp)) {
        Serial.printf("  DNS Resolution:     SUCCESS -> %s\n", targetIp.toString().c_str());
    } else {
        Serial.printf("  DNS Resolution:     FAILED for '%s'\n", target.c_str());
        return;
    }

    WiFiClient client;
    client.setTimeout(3);
    unsigned long t0 = millis();
    bool connected = client.connect(targetIp, 80) || client.connect(targetIp, 443) || client.connect(targetIp, 53);
    unsigned long rtt = millis() - t0;

    if (connected) {
        Serial.printf("  TCP Handshake:      SUCCESS to %s (RTT: %lu ms)\n", targetIp.toString().c_str(), rtt);
        Serial.println("  Internet Status:    100% ONLINE & REACHABLE\n");
        client.stop();
    } else {
        Serial.printf("  TCP Handshake:      FAILED (Host unreachable or port filtered)\n\n");
    }
}
