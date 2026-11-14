#include "cli_shell.h"
#include "roaming_engine.h"
#include "napt_engine.h"
#include "storage_manager.h"
#include "web_portal.h"
#include "dns_cache.h"
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
    } else if (cmd == "mac") {
        Serial.printf("Current Station MAC: %s\n", WiFi.macAddress().c_str());
        auto s = StorageManager::instance().getSettings();
        if (s.clonedMac.length() > 0) {
            Serial.printf("Cloned Custom MAC:   %s (Active in NVS)\n", s.clonedMac.c_str());
        } else {
            Serial.println("Cloned Custom MAC:   None (Using hardware factory MAC)");
        }
    } else if (cmd == "set_mac") {
        auto tokens = tokenizeArgs(args);
        if (tokens.empty()) {
            Serial.println("Usage: set_mac <XX:XX:XX:XX:XX:XX>");
        } else {
            if (RoamingEngine::instance().applyClonedMac(tokens[0])) {
                Serial.printf("[CLI] Station MAC successfully changed to %s\n", tokens[0].c_str());
            } else {
                Serial.println("[CLI] Error: Invalid MAC format. Use XX:XX:XX:XX:XX:XX");
            }
        }
    } else if (cmd == "reset_mac") {
        RoamingEngine::instance().resetMac();
        Serial.println("[CLI] Station MAC restored to hardware default.");
    } else if (cmd == "portmap") {
        auto tokens = tokenizeArgs(args);
        if (tokens.size() >= 5 && tokens[0] == "add") {
            uint8_t proto = (tokens[1].equalsIgnoreCase("udp")) ? 17 : 6;
            uint16_t extPort = tokens[2].toInt();
            String intIp = tokens[3];
            uint16_t intPort = tokens[4].toInt();
            String desc = tokens.size() >= 6 ? tokens[5] : "";
            NaptEngine::instance().addPortForward(proto, extPort, intIp, intPort, desc);
            Serial.println("[CLI] Port forward rule registered.");
        } else if (tokens.size() >= 3 && tokens[0] == "del") {
            uint8_t proto = (tokens[1].equalsIgnoreCase("udp")) ? 17 : 6;
            uint16_t extPort = tokens[2].toInt();
            NaptEngine::instance().removePortForward(proto, extPort);
            Serial.println("[CLI] Port forward rule removed.");
        } else {
            auto rules = StorageManager::instance().getPortForwards();
            Serial.printf("\nACTIVE PORT FORWARD RULES (%d stored):\n", (int)rules.size());
            Serial.println("+-------+----------+-----------------+----------+----------------------+");
            Serial.println("| PROTO | EXT PORT | TARGET IP       | INT PORT | DESCRIPTION          |");
            Serial.println("+-------+----------+-----------------+----------+----------------------+");
            for (const auto& r : rules) {
                Serial.printf("| %-5s | %-8d | %-15s | %-8d | %-20.20s |\n",
                              r.protocol == 6 ? "TCP" : "UDP", r.externalPort, r.internalIp.c_str(), r.internalPort, r.description.c_str());
            }
            Serial.println("+-------+----------+-----------------+----------+----------------------+\n");
            Serial.println("Usage:");
            Serial.println("  portmap add <tcp|udp> <ext_port> <int_ip> <int_port> [desc]");
            Serial.println("  portmap del <tcp|udp> <ext_port>\n");
        }
    } else if (cmd == "adblock") {
        auto tokens = tokenizeArgs(args);
        if (tokens.size() > 0) {
            bool en = (tokens[0] == "on" || tokens[0] == "1" || tokens[0] == "true" || tokens[0] == "enable");
            WebPortal::instance().toggleAdBlock(en);
            Serial.printf("[CLI] DNS AdBlock sinkhole is now %s.\n", en ? "ENABLED" : "DISABLED");
        } else {
            bool active = WebPortal::instance().isAdBlockActive();
            uint32_t count = StorageManager::instance().getAdBlockCount();
            Serial.printf("DNS AdBlocker Sinkhole: %s\n", active ? "ENABLED" : "DISABLED");
            Serial.printf("Total Blocked Queries:  %lu\n", (unsigned long)count);
            Serial.println("Use 'adblock on' or 'adblock off' to toggle.\n");
        }
    } else if (cmd == "isolation") {
        auto tokens = tokenizeArgs(args);
        if (tokens.size() > 0) {
            bool en = (tokens[0] == "on" || tokens[0] == "1" || tokens[0] == "true" || tokens[0] == "enable");
            StorageManager::instance().setClientIsolation(en);
            Serial.printf("[CLI] Layer-2 Client Isolation is now %s.\n", en ? "ENABLED" : "DISABLED");
        } else {
            auto s = StorageManager::instance().getSettings();
            Serial.printf("Layer-2 Client Isolation: %s\n", s.clientIsolation ? "ENABLED" : "DISABLED");
            Serial.println("Use 'isolation on' or 'isolation off' to toggle.\n");
        }
    } else if (cmd == "tpc") {
        auto tokens = tokenizeArgs(args);
        if (tokens.size() > 0) {
            bool en = (tokens[0] == "on" || tokens[0] == "1" || tokens[0] == "true" || tokens[0] == "enable");
            StorageManager::instance().setDynamicTpc(en);
            if (!en) {
                auto s = StorageManager::instance().getSettings();
                RoamingEngine::instance().setTxPowerDbm(s.txPowerDbm);
            }
            Serial.printf("[CLI] Dynamic Transmit Power Control (TPC) is now %s.\n", en ? "ENABLED" : "DISABLED");
        } else {
            auto s = StorageManager::instance().getSettings();
            Serial.printf("Dynamic Transmit Power Control: %s (Current: %.2f dBm)\n", 
                          s.dynamicTpcEnabled ? "ENABLED" : "DISABLED", RoamingEngine::instance().getCurrentTxPowerDbm());
            Serial.println("Use 'tpc on' or 'tpc off' to toggle.\n");
        }
    } else if (cmd == "temp" || cmd == "temperature") {
        float tempC = temperatureRead();
        float tempF = (tempC * 1.8f) + 32.0f;
        Serial.printf("\n[THERMAL] ESP32 Internal Die Temperature: %.1f °C / %.1f °F (%s)\n\n",
                      tempC, tempF, tempC >= THERMAL_WARN_TEMP_C ? "WARNING: High Temperature" : "Optimal Range");
    } else if (cmd == "spectrum" || cmd == "congestion") {
        printSpectrum();
    } else if (cmd == "dns_cache" || cmd == "dns") {
        printDnsCache();
    } else if (cmd == "clear_dns") {
        DnsCache::instance().clear();
        Serial.println("[CLI] Sub-Millisecond SRAM DNS Cache cleared.");
    } else if (cmd == "kick") {
        handleKick(args);
    } else if (cmd == "rename") {
        handleRename(args);
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
    Serial.println("  spectrum                          - Real-time 2.4 GHz spectrum congestion analysis");
    Serial.println("  temp                              - Display on-chip internal die temperature");
    Serial.println("  dns                               - Display sub-millisecond SRAM DNS cache stats & hosts");
    Serial.println("  clear_dns                         - Flush DNS cache table");
    Serial.println("  connect <ssid> <pass>             - Manually connect to an upstream AP (use \"\" for spaces)");
    Serial.println("  disconnect                        - Drop upstream connection & return to standalone AP");
    Serial.println("  roam                              - Trigger immediate background multi-AP roam check");
    Serial.println("  clients                           - List active stations connected to repeater SoftAP");
    Serial.println("  rename <mac> <alias>              - Assign friendly custom name to client device");
    Serial.println("  kick <mac>                        - Disconnect / deauthenticate client station");
    Serial.println("  set_tx <dbm>                      - Calibrate RF output power (+10.0 to +20.5 dBm)");
    Serial.println("  list / saved                      - Display stored failover Wi-Fi network profiles");
    Serial.println("  add <ssid> <pass> [prio]          - Add or update a saved network (e.g. add \"HomeNetwork\" \"Pass\" 8)");
    Serial.println("  del / remove <ssid>               - Remove network from saved database (e.g. del \"HomeNetwork\")");
    Serial.println("  mac                               - Display current station MAC & cloned status");
    Serial.println("  set_mac <mac>                     - Clone/spoof Station MAC (XX:XX:XX:XX:XX:XX)");
    Serial.println("  reset_mac                         - Restore default hardware factory MAC");
    Serial.println("  portmap [add|del] ...             - List, add, or remove lwIP DNAPT port forwarding rules");
    Serial.println("  adblock [on|off]                  - Check or toggle hardware DNS AdBlocker sinkhole");
    Serial.println("  isolation [on|off]                - Check or toggle Layer-2 client isolation");
    Serial.println("  tpc [on|off]                      - Check or toggle Dynamic Transmit Power Control (TPC)");
    Serial.println("  ping <host>                       - Verify DNS resolution & TCP internet route (e.g. ping google.com)");
    Serial.println("  reboot                            - Perform clean software reset of the ESP32\n");
}

void CliShell::printStatus() {
    auto& roam = RoamingEngine::instance();
    auto& napt = NaptEngine::instance();
    auto settings = StorageManager::instance().getSettings();
    auto dnsStats = DnsCache::instance().getStats();

    Serial.println("---------------- AETHERPULSE SYSTEM STATUS ----------------");
    Serial.printf("  Device:             ESP32-D0WD-V3 (Rev 3.1) @ %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Die Temperature:    %.1f °C (Thermal state: %s)\n", 
                  temperatureRead(), temperatureRead() >= THERMAL_WARN_TEMP_C ? "HIGH" : "NORMAL");
    Serial.printf("  Free DRAM (Heap):   %d bytes (Min Free: %d bytes)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    Serial.printf("  Uptime:             %lu seconds\n", millis() / 1000);
    Serial.println("--- Upstream Station (STA) Link ---");
    Serial.printf("  Connected:          %s\n", roam.isConnected() ? "YES" : "NO");
    if (roam.isConnected()) {
        Serial.printf("  SSID:               %s\n", roam.getCurrentSsid().c_str());
        Serial.printf("  BSSID:              %s\n", roam.getCurrentBssid().c_str());
        Serial.printf("  Channel:            %d (Best Recommended: CH %d)\n", roam.getCurrentChannel(), roam.getBestChannel());
        Serial.printf("  Filtered RSSI:      %.1f dBm (Kalman Smoothed)\n", roam.getCurrentRssi());
        Serial.printf("  Assigned IP:        %s (GW: %s)\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    }
    Serial.printf("  Station MAC:        %s\n", WiFi.macAddress().c_str());
    Serial.println("--- Repeater SoftAP ---");
    Serial.printf("  SoftAP SSID:        %s\n", settings.apSSID.c_str());
    Serial.printf("  SoftAP IP:          %s (http://%s.local)\n", WiFi.softAPIP().toString().c_str(), MDNS_HOSTNAME);
    Serial.printf("  Connected Clients:  %d / %d\n", WiFi.softAPgetStationNum(), settings.apMaxClients);
    Serial.println("--- L3 Routing, NAPT & DNS Acceleration ---");
    Serial.printf("  lwIP DNAPT:         %s\n", napt.isEnabled() ? "ACTIVE" : "INACTIVE");
    Serial.printf("  Port Forward Rules: %d rules loaded\n", (int)StorageManager::instance().getPortForwards().size());
    Serial.printf("  SRAM DNS Cache:     %lu Hits / %lu Misses (%lu Active Entries)\n",
                  (unsigned long)dnsStats.totalHits, (unsigned long)dnsStats.totalMisses, (unsigned long)dnsStats.activeEntries);
    Serial.printf("  DNS AdBlocker:      %s (%lu blocked queries)\n", 
                  WebPortal::instance().isAdBlockActive() ? "ACTIVE" : "DISABLED", 
                  (unsigned long)StorageManager::instance().getAdBlockCount());
    Serial.printf("  Task Watchdog TWDT: 15s Hardware Timeout\n");
    Serial.printf("  Modem Sleep:        DISABLED (WIFI_PS_NONE for zero jitter)\n");
    Serial.printf("  Beacon Interval:    %d TU (Anti-Phase Scheduled)\n", SOFTAP_BEACON_INTERVAL_TU);
    Serial.printf("  TCP MSS Clamping:   %d bytes (RFC 1624 anti-blackhole)\n", TCP_MSS_CLAMP_VALUE);
    Serial.printf("  Layer-2 Isolation:  %s\n", settings.clientIsolation ? "ENABLED" : "DISABLED");
    Serial.printf("  Dynamic TPC:        %s (RF Power: %.2f dBm)\n", settings.dynamicTpcEnabled ? "ACTIVE" : "DISABLED", roam.getCurrentTxPowerDbm());
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
            case WIFI_AUTH_WEP:  authmode = "WEP"; break;
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
    Serial.println("+----+-------------------+---------+----------------------+----------------------+");
    Serial.println("| #  | MAC ADDRESS       | RSSI    | VENDOR               | ALIAS / NAME         |");
    Serial.println("+----+-------------------+---------+----------------------+----------------------+");
    for (int i = 0; i < sta_list.num; i++) {
        wifi_sta_info_t sta = sta_list.sta[i];
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta.mac[0], sta.mac[1], sta.mac[2], sta.mac[3], sta.mac[4], sta.mac[5]);
        String alias = StorageManager::instance().getClientAlias(mac_str);
        String vendor = StorageManager::instance().getVendorName(mac_str);
        if (alias.length() == 0) alias = "-";
        Serial.printf("| %-2d | %-17s | %3d dBm | %-20.20s | %-20.20s |\n", 
                      i+1, mac_str, sta.rssi, vendor.c_str(), alias.c_str());
    }
    Serial.println("+----+-------------------+---------+----------------------+----------------------+\n");
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

void CliShell::printSpectrum() {
    auto loads = RoamingEngine::instance().getChannelLoads();
    int best = RoamingEngine::instance().getBestChannel();
    int current = RoamingEngine::instance().getCurrentChannel();

    Serial.println("\n2.4 GHz RF SPECTRUM CONGESTION ANALYSIS:");
    Serial.println("+---------+----------------------+------+---------------------------+");
    Serial.println("| CHANNEL | CONGESTION DENSITY   | LOAD | STATUS                    |");
    Serial.println("+---------+----------------------+------+---------------------------+");
    for (int ch = 1; ch <= 13; ch++) {
        int bars = (loads[ch] * 20) / 100;
        char barStr[25];
        memset(barStr, ' ', 20);
        for (int b = 0; b < bars && b < 20; b++) barStr[b] = '#';
        barStr[20] = '\0';

        String status = "";
        if (ch == current) status += "[ACTIVE] ";
        if (ch == best) status += "*RECOMMENDED OPTIMAL*";

        Serial.printf("| CH %-4d | [%-20s] | %3d%% | %-25.25s |\n",
                      ch, barStr, loads[ch], status.c_str());
    }
    Serial.println("+---------+----------------------+------+---------------------------+");
    Serial.printf("Suggested Cleanest Frequency: Channel %d\n\n", best);
}

void CliShell::printDnsCache() {
    auto stats = DnsCache::instance().getStats();
    auto entries = DnsCache::instance().getEntries();
    uint32_t total = stats.totalHits + stats.totalMisses;
    float hitRate = total > 0 ? (float)stats.totalHits * 100.0f / total : 0.0f;

    Serial.println("\n--- SUB-MILLISECOND SRAM DNS CACHE DAEMON ---");
    Serial.printf("  Total Cache Hits:     %lu\n", (unsigned long)stats.totalHits);
    Serial.printf("  Total Cache Misses:   %lu\n", (unsigned long)stats.totalMisses);
    Serial.printf("  Cache Hit Ratio:      %.1f %%\n", hitRate);
    Serial.printf("  Active Table Entries: %lu / %lu\n", (unsigned long)stats.activeEntries, (unsigned long)stats.capacity);

    if (!entries.empty()) {
        Serial.println("\n+--------------------------------+-----------------+----------+----------+");
        Serial.println("| DOMAIN HOSTNAME                | RESOLVED IP     | TTL REM  | HITS     |");
        Serial.println("+--------------------------------+-----------------+----------+----------+");
        unsigned long now = millis();
        for (const auto& e : entries) {
            uint32_t remSec = (e.expireTimeMs > now) ? (e.expireTimeMs - now) / 1000 : 0;
            Serial.printf("| %-30.30s | %-15s | %5lu s  | %-8lu |\n",
                          e.domain.c_str(), e.ip.toString().c_str(), (unsigned long)remSec, (unsigned long)e.hits);
        }
        Serial.println("+--------------------------------+-----------------+----------+----------+\n");
    } else {
        Serial.println("  (DNS Cache is currently empty)\n");
    }
}

void CliShell::handleKick(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.empty()) {
        Serial.println("Usage: kick <MAC_ADDRESS> (e.g. kick 12:34:56:78:9A:BC)");
        return;
    }
    String macStr = tokens[0];
    uint8_t mac[6];
    if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(mac, &aid) == ESP_OK) {
            esp_wifi_deauth_sta(aid);
            Serial.printf("[CLI] Station '%s' has been deauthenticated.\n", macStr.c_str());
            return;
        }
    }
    Serial.printf("[CLI] Could not find connected station with MAC '%s'.\n", macStr.c_str());
}

void CliShell::handleRename(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.size() < 2) {
        Serial.println("Usage: rename <MAC_ADDRESS> <ALIAS> (e.g. rename 12:34:56:78:9A:BC \"Living Room TV\")");
        return;
    }
    StorageManager::instance().setClientAlias(tokens[0], tokens[1]);
    Serial.printf("[CLI] Saved alias '%s' for MAC %s\n", tokens[1].c_str(), tokens[0].c_str());
}
