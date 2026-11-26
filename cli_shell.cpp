#include "cli_shell.h"
#include "roaming_engine.h"
#include "napt_engine.h"
#include "storage_manager.h"
#include "web_portal.h"
#include "dns_cache.h"
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <lwip/netif.h>
#include <lwip/ip4.h>
#include <lwip/tcpip.h>
#include <vector>
#include <cstdarg>

static std::vector<String> tokenizeArgs(const String& str) {
    std::vector<String> tokens;
    String cur = "";
    bool hasToken = false;
    bool inQuote = false;
    char quoteChar = 0;

    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if ((c == '"' || c == '\'') && !inQuote) {
            inQuote = true;
            quoteChar = c;
            hasToken = true;
        } else if (inQuote && c == quoteChar) {
            inQuote = false;
        } else if (!inQuote && (c == ' ' || c == '\t')) {
            if (hasToken) {
                tokens.push_back(cur);
                cur = "";
                hasToken = false;
            }
        } else {
            cur += c;
            hasToken = true;
        }
    }
    if (hasToken) tokens.push_back(cur);
    return tokens;
}

static bool validateMacFormat(const String& macStr, uint8_t outMac[6]) {
    if (macStr.length() != 17) return false;
    for (int i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (macStr[i] != ':') return false;
        } else if (!isxdigit((unsigned char)macStr[i])) {
            return false;
        }
    }
    return sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &outMac[0], &outMac[1], &outMac[2], &outMac[3], &outMac[4], &outMac[5]) == 6;
}

CliShell& CliShell::instance() {
    static CliShell shell;
    return shell;
}

CliShell::CliShell() : uartQueue(nullptr), ready(false), escState(0) {}

void CliShell::init() {
    rxBuffer.reserve(CLI_BUFFER_SIZE);

    Serial.flush();
    uart_driver_delete(UART_NUM_0);

    const uart_port_t uart_num = UART_NUM_0;
    uart_config_t uart_config = {
        .baud_rate = CLI_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, 1024, 1024, 24, &uartQueue, 0);
    ready = true;

    println();
    println("==================================================================");
    printf("  %s %s (ESP32 Repeater Core)\n", PROJECT_NAME, PROJECT_VERSION);
    println("  Non-blocking UART driver active (FIFO: 1024 B @ 115200 8N1)");
    println("  Type 'help' for interactive command list");
    println("==================================================================");
    print("aetherpulse> ");
}

void CliShell::print(const char* str) {
    if (str && strlen(str) > 0) {
        uart_write_bytes(UART_NUM_0, str, strlen(str));
    }
}

void CliShell::print(const String& str) {
    if (str.length() > 0) {
        uart_write_bytes(UART_NUM_0, str.c_str(), str.length());
    }
}

void CliShell::println(const char* str) {
    if (str && strlen(str) > 0) {
        uart_write_bytes(UART_NUM_0, str, strlen(str));
    }
    uart_write_bytes(UART_NUM_0, "\r\n", 2);
}

void CliShell::println(const String& str) {
    if (str.length() > 0) {
        uart_write_bytes(UART_NUM_0, str.c_str(), str.length());
    }
    uart_write_bytes(UART_NUM_0, "\r\n", 2);
}

void CliShell::printf(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len > 0) {
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        uart_write_bytes(UART_NUM_0, buf, len);
    }
}

void CliShell::processRxByte(char c) {
    // Strip ANSI escape sequences (e.g. arrow keys ESC [ A)
    if (escState == 1) {
        escState = (c == '[') ? 2 : 0;
        return;
    }
    if (escState == 2) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
            escState = 0;
        }
        return;
    }
    if (c == 0x1B) {
        escState = 1;
        return;
    }

    if (c == '\r' || c == '\n') {
        println();
        if (rxBuffer.length() > 0) {
            handleCommand(rxBuffer);
            rxBuffer = "";
        }
        print("aetherpulse> ");
    } else if (c == 8 || c == 127) {
        if (rxBuffer.length() > 0) {
            rxBuffer.remove(rxBuffer.length() - 1);
            print("\b \b");
        }
    } else if ((unsigned char)c >= 32 && (unsigned char)c < 127 && rxBuffer.length() < CLI_BUFFER_SIZE - 1) {
        rxBuffer += c;
        char echo[2] = {c, 0};
        print(echo);
    }
}

void CliShell::update() {
    StorageManager::instance().update();

    uart_event_t event;
    while (uartQueue && xQueueReceive(uartQueue, &event, 0) == pdTRUE) {
        if (event.type == UART_DATA) {
            size_t buffered = 0;
            while (uart_get_buffered_data_len(UART_NUM_0, &buffered) == ESP_OK && buffered > 0) {
                uint8_t temp[64];
                int toRead = (buffered < sizeof(temp)) ? (int)buffered : (int)sizeof(temp);
                int actual = uart_read_bytes(UART_NUM_0, temp, toRead, 0);
                if (actual <= 0) break;
                for (int i = 0; i < actual; i++) {
                    processRxByte((char)temp[i]);
                }
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            uart_flush_input(UART_NUM_0);
            xQueueReset(uartQueue);
        }
    }
}

void CliShell::handleCommand(const String& raw) {
    String line = raw;
    line.trim();
    if (line.length() == 0) return;

    int splitIdx = -1;
    for (int i = 0; i < (int)line.length(); i++) {
        if (line[i] == ' ' || line[i] == '\t') {
            splitIdx = i;
            break;
        }
    }
    String cmd = (splitIdx == -1) ? line : line.substring(0, splitIdx);
    String args = (splitIdx == -1) ? "" : line.substring(splitIdx + 1);
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
        println("[CLI] Evaluating multi-AP roam candidates...");
        RoamingEngine::instance().triggerRoamEvaluation();
    } else if (cmd == "set_tx") {
        handleSetTx(args);
    } else if (cmd == "list_known" || cmd == "list" || cmd == "saved") {
        auto known = StorageManager::instance().getKnownNetworks();
        printf("\n[SAVED / KNOWN NETWORKS (%d stored)]:\n", (int)known.size());
        println("+----+------------------------+----------+");
        println("| #  | SSID                   | PRIORITY |");
        println("+----+------------------------+----------+");
        for (size_t i = 0; i < known.size(); i++) {
            printf("| %-2d | %-22.22s | %-8d |\n",
                   (int)i + 1, known[i].ssid.c_str(), known[i].priority);
        }
        println("+----+------------------------+----------+\n");
    } else if (cmd == "add_known" || cmd == "add") {
        handleAddKnown(args);
    } else if (cmd == "del_known" || cmd == "del" || cmd == "remove" || cmd == "rm") {
        handleDelKnown(args);
    } else if (cmd == "mac") {
        printf("Current Station MAC: %s\n", WiFi.macAddress().c_str());
        auto s = StorageManager::instance().getSettings();
        if (s.clonedMac.length() > 0) {
            printf("Cloned Custom MAC:   %s (Active in NVS)\n", s.clonedMac.c_str());
        } else {
            println("Cloned Custom MAC:   None (Factory MAC)");
        }
    } else if (cmd == "set_mac") {
        auto tokens = tokenizeArgs(args);
        if (tokens.empty()) {
            println("Usage: set_mac <XX:XX:XX:XX:XX:XX>");
        } else if (RoamingEngine::instance().applyClonedMac(tokens[0])) {
            printf("[CLI] Station MAC set to %s\n", tokens[0].c_str());
        } else {
            println("[CLI] Error: Invalid unicast MAC format (use XX:XX:XX:XX:XX:XX).");
        }
    } else if (cmd == "reset_mac") {
        RoamingEngine::instance().resetMac();
    } else if (cmd == "portmap") {
        auto tokens = tokenizeArgs(args);
        if (tokens.size() >= 5 && tokens[0].equalsIgnoreCase("add")) {
            if (!tokens[1].equalsIgnoreCase("tcp") && !tokens[1].equalsIgnoreCase("udp")) {
                println("[CLI] Error: Protocol must be 'tcp' or 'udp'.");
                return;
            }
            uint8_t proto = tokens[1].equalsIgnoreCase("udp") ? 17 : 6;
            long extP = tokens[2].toInt();
            long intP = tokens[4].toInt();
            IPAddress ipTest;
            if (extP <= 0 || extP > 65535 || intP <= 0 || intP > 65535 || extP == 80 || extP == 53) {
                println("[CLI] Error: Invalid port (1-65535; ports 53 & 80 reserved).");
                return;
            }
            if (!ipTest.fromString(tokens[3]) || (uint32_t)ipTest == 0) {
                println("[CLI] Error: Invalid target IPv4 address.");
                return;
            }
            String desc = tokens.size() >= 6 ? tokens[5] : "";
            if (NaptEngine::instance().addPortForward(proto, (uint16_t)extP, tokens[3], (uint16_t)intP, desc)) {
                println("[CLI] Port forward rule registered.");
            } else {
                println("[CLI] Error: Failed to add rule (table full or NAPT error).");
            }
        } else if (tokens.size() >= 3 && tokens[0].equalsIgnoreCase("del")) {
            if (!tokens[1].equalsIgnoreCase("tcp") && !tokens[1].equalsIgnoreCase("udp")) {
                println("[CLI] Error: Protocol must be 'tcp' or 'udp'.");
                return;
            }
            uint8_t proto = tokens[1].equalsIgnoreCase("udp") ? 17 : 6;
            long extP = tokens[2].toInt();
            if (extP <= 0 || extP > 65535) {
                println("[CLI] Error: Invalid port.");
                return;
            }
            if (NaptEngine::instance().removePortForward(proto, (uint16_t)extP)) {
                println("[CLI] Port forward rule removed.");
            } else {
                println("[CLI] Rule not found.");
            }
        } else {
            auto rules = StorageManager::instance().getPortForwards();
            printf("\nACTIVE PORT FORWARD RULES (%d / %d):\n", (int)rules.size(), NAPT_MAX_PORTMAPS);
            println("+-------+----------+-----------------+----------+----------------------+");
            println("| PROTO | EXT PORT | TARGET IP       | INT PORT | DESCRIPTION          |");
            println("+-------+----------+-----------------+----------+----------------------+");
            for (const auto& r : rules) {
                printf("| %-5s | %-8u | %-15s | %-8u | %-20.20s |\n",
                       r.protocol == 6 ? "TCP" : "UDP", r.externalPort, r.internalIp.c_str(), r.internalPort, r.description.c_str());
            }
            println("+-------+----------+-----------------+----------+----------------------+\n");
            println("Usage:");
            println("  portmap add <tcp|udp> <ext_port> <int_ip> <int_port> [desc]");
            println("  portmap del <tcp|udp> <ext_port>\n");
        }
    } else if (cmd == "isolation") {
        auto tokens = tokenizeArgs(args);
        if (!tokens.empty()) {
            bool en = (tokens[0] == "on" || tokens[0] == "1" || tokens[0] == "true" || tokens[0] == "enable");
            StorageManager::instance().setClientIsolation(en);
            printf("[CLI] L2 Client Isolation: %s.\n", en ? "ENABLED" : "DISABLED");
        } else {
            auto s = StorageManager::instance().getSettings();
            printf("L2 Client Isolation: %s\n", s.clientIsolation ? "ENABLED" : "DISABLED");
        }
    } else if (cmd == "tpc") {
        auto tokens = tokenizeArgs(args);
        if (!tokens.empty()) {
            bool en = (tokens[0] == "on" || tokens[0] == "1" || tokens[0] == "true" || tokens[0] == "enable");
            StorageManager::instance().setDynamicTpc(en);
            if (!en) {
                auto s = StorageManager::instance().getSettings();
                RoamingEngine::instance().setTxPowerDbm(s.txPowerDbm);
            }
            printf("[CLI] Dynamic TPC: %s.\n", en ? "ENABLED" : "DISABLED");
        } else {
            auto s = StorageManager::instance().getSettings();
            printf("Dynamic TPC: %s (Current: %.2f dBm)\n",
                   s.dynamicTpcEnabled ? "ENABLED" : "DISABLED", RoamingEngine::instance().getCurrentTxPowerDbm());
        }
    } else if (cmd == "temp" || cmd == "temperature") {
        float tempC = temperatureRead();
        float tempF = (tempC * 1.8f) + 32.0f;
        printf("\n[THERMAL] Internal Die Temperature: %.1f C / %.1f F (%s)\n\n",
               tempC, tempF, tempC >= THERMAL_WARN_TEMP_C ? "WARNING: High" : "Optimal");
    } else if (cmd == "spectrum" || cmd == "congestion") {
        printSpectrum();
    } else if (cmd == "dns_cache" || cmd == "dns") {
        printDnsCache();
    } else if (cmd == "clear_dns") {
        DnsCache::instance().clear();
        println("[CLI] SRAM DNS Cache cleared.");
    } else if (cmd == "crashlog" || cmd == "crash") {
        handleCrashLog(args);
    } else if (cmd == "kick") {
        handleKick(args);
    } else if (cmd == "rename") {
        handleRename(args);
    } else if (cmd == "ping" || cmd == "test") {
        handlePing(args);
    } else if (cmd == "wan") {
        handleWan(args);
    } else if (cmd == "probe") {
        handleProbe(args);
    } else if (cmd == "netdebug") {
        LOCK_TCPIP_CORE();
        printf("netif_default=%p tcpip_input=%p dnsIP=%s\n",
               (void*)netif_default, (void*)tcpip_input, WiFi.dnsIP().toString().c_str());
        for (struct netif *n = netif_list; n != nullptr; n = n->next) {
            printf("  netif %c%c%u ptr=%p ip=%s mask=%s gw=%s flags=0x%02x napt=%u input=%p\n",
                   n->name[0], n->name[1], n->num, (void*)n,
                   IPAddress(netif_ip4_addr(n)->addr).toString().c_str(),
                   IPAddress(netif_ip4_netmask(n)->addr).toString().c_str(),
                   IPAddress(netif_ip4_gw(n)->addr).toString().c_str(),
                   n->flags, n->napt, (void*)n->input);
        }
        ip4_addr_t testDst;
        testDst.addr = inet_addr("1.1.1.1");
        struct netif *r = ip4_route(&testDst);
        printf("ip4_route(1.1.1.1)=%p (%c%c%u)\n",
               (void*)r, r ? r->name[0] : '-', r ? r->name[1] : '-', r ? r->num : 0);
        UNLOCK_TCPIP_CORE();
    } else if (cmd == "reboot") {
        println("[CLI] Flushing shadow journal & rebooting...");
        StorageManager::instance().commitJournal(true);
        delay(200);
        ESP.restart();
    } else {
        printf("Unknown command '%s'. Type 'help' for commands.\n", cmd.c_str());
    }
}

void CliShell::handleCrashLog(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (!tokens.empty() && tokens[0].equalsIgnoreCase("clear")) {
        StorageManager::clearCrashLog();
        println("[CRASH] RTC Slow SRAM crash buffer cleared.");
        return;
    }

    const auto& log = StorageManager::getCrashLog();
    println("\n================ RTC SLOW SRAM HARDWARE CRASH LOG ================");
    printf("  Buffer Magic:       0x%08lX (%s)\n",
           (unsigned long)log.magic, log.magic == CRASH_LOG_MAGIC ? "VALID" : "INVALID");
    printf("  Recorded Events:    %lu (Capacity: %d)\n",
           (unsigned long)log.count, CRASH_LOG_MAX_ENTRIES);
    println("------------------------------------------------------------------");

    if (log.count == 0) {
        println("  (Clean state: 0 abnormal resets recorded)\n");
    } else {
        println("+----+------------+----------------------+------------+------------+");
        println("| #  | UPTIME (S) | RESET REASON         | MIN HEAP   | DETAIL     |");
        println("+----+------------+----------------------+------------+------------+");
        uint32_t num = (log.count < CRASH_LOG_MAX_ENTRIES) ? log.count : CRASH_LOG_MAX_ENTRIES;
        uint32_t oldestIdx = (log.head + CRASH_LOG_MAX_ENTRIES - num) % CRASH_LOG_MAX_ENTRIES;
        for (uint32_t i = 0; i < num; i++) {
            uint32_t idx = (oldestIdx + i) % CRASH_LOG_MAX_ENTRIES;
            const auto& e = log.entries[idx];
            const char* reason = StorageManager::getResetReasonName((esp_reset_reason_t)e.reset_reason);
            printf("| %-2lu | %-10lu | %-20.20s | %-10lu | %-10.10s |\n",
                   (unsigned long)(i + 1), (unsigned long)e.timestamp_sec, reason,
                   (unsigned long)e.min_free_heap, e.reason_str);
        }
        println("+----+------------+----------------------+------------+------------+\n");
    }
    println("Usage: crashlog [clear]\n");
}

void CliShell::printHelp() {
    printf("\nAVAILABLE SERIAL CLI COMMANDS (%s):\n", PROJECT_VERSION);
    println("  status                             - System health, Wi-Fi link, NAPT & memory");
    println("  scan                               - Trigger 2.4 GHz scan & display AP table");
    println("  spectrum                           - 2.4 GHz channel congestion analysis");
    println("  temp                               - Internal ESP32 die temperature");
    println("  dns                                - SRAM DNS cache stats & active hosts");
    println("  clear_dns                          - Flush DNS cache");
    println("  connect <ssid> [pass]              - Associate with upstream AP");
    println("  disconnect                         - Drop upstream link");
    println("  roam                               - Trigger multi-AP roam evaluation");
    println("  clients                            - List connected SoftAP stations");
    println("  rename <mac> <alias>               - Assign custom alias to client MAC");
    println("  kick <mac>                         - Deauthenticate client station");
    println("  set_tx <dbm>                       - Set RF TX power (11.0 to 20.0 dBm)");
    println("  list / saved                       - Display saved failover profiles");
    println("  add <ssid> <pass> [prio]           - Save network to failover pool");
    println("  del / remove <ssid>                - Remove network from failover pool");
    println("  mac                                - Show station & cloned MAC status");
    println("  set_mac <mac>                      - Spoof station MAC (XX:XX:XX:XX:XX:XX)");
    println("  reset_mac                          - Restore factory MAC");
    println("  portmap [add|del] ...              - Manage lwIP DNAPT port forwarding");
    println("  isolation [on|off]                 - Toggle L2 client isolation");
    println("  tpc [on|off]                       - Toggle Dynamic Transmit Power Control");
    println("  crashlog [clear]                   - Inspect RTC Slow SRAM crash history");
    println("  wan [on|off|interval|failover|clear] - WAN Internet Health Watchdog controls");
    println("  probe [ip]                         - Non-blocking Anycast TCP probe (port 53)");
    println("  ping [host]                        - Verify DNS resolution & TCP connectivity");
    println("  reboot                             - Flush journal & restart ESP32\n");
}

void CliShell::printStatus() {
    auto& roam = RoamingEngine::instance();
    auto& napt = NaptEngine::instance();
    auto settings = StorageManager::instance().getSettings();
    auto dnsStats = DnsCache::instance().getStats();
    float tempC = temperatureRead();

    println("---------------- AETHERPULSE SYSTEM STATUS ----------------");
    printf("  Firmware:           %s %s @ %u MHz\n", PROJECT_NAME, PROJECT_VERSION, (unsigned)ESP.getCpuFreqMHz());
    printf("  Die Temperature:    %.1f C (%s)\n", tempC, tempC >= THERMAL_WARN_TEMP_C ? "HIGH" : "NORMAL");
    printf("  Free Heap:          %u B (Min: %u B, Largest: %u B)\n",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    printf("  Uptime:             %lu s\n", millis() / 1000);
    println("--- Upstream Station (STA) ---");
    printf("  Connected:          %s\n", roam.isConnected() ? "YES" : "NO");
    if (roam.isConnected()) {
        printf("  SSID / BSSID:       %s (%s)\n", roam.getCurrentSsid().c_str(), roam.getCurrentBssid().c_str());
        printf("  Channel:            %d (Cleanest: CH %d)\n", roam.getCurrentChannel(), roam.getBestChannel());
        printf("  Kalman RSSI:        %.1f dBm\n", roam.getCurrentRssi());
        printf("  STA IP / GW:        %s / %s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    }
    printf("  Station MAC:        %s\n", WiFi.macAddress().c_str());
    println("--- Repeater SoftAP ---");
    printf("  SoftAP SSID:        %s\n", settings.apSSID.c_str());
    printf("  SoftAP IP:          %s (http://%s.local)\n", WiFi.softAPIP().toString().c_str(), MDNS_HOSTNAME);
    printf("  Connected Clients:  %d / %d\n", WiFi.softAPgetStationNum(), settings.apMaxClients);
    println("--- Routing, DNS & Security ---");
    printf("  lwIP NAPT:          %s (MSS Clamped: %lu, Hairpin: %lu, Portmaps: %u)\n",
           napt.isEnabled() ? "ACTIVE" : "INACTIVE",
           (unsigned long)napt.getMssClampedCount(),
           (unsigned long)napt.getHairpinCount(),
           (unsigned)napt.getActivePortmapCount());
    printf("  WAN Health:         %s (RTT: %.1f ms | Watchdog: %s | Failovers: %lu)\n",
           roam.isWanOnline() ? "ONLINE" : (roam.isConnected() ? "DEGRADED" : "DOWN"),
           roam.getWanRttMs(),
           settings.wanWatchdogEnabled ? "ACTIVE" : "PAUSED",
           (unsigned long)roam.getWanFailoverCount());
    printf("  SRAM DNS Cache:     %lu Hits / %lu Misses (%lu / %lu Entries)\n",
           (unsigned long)dnsStats.totalHits, (unsigned long)dnsStats.totalMisses,
           (unsigned long)dnsStats.activeEntries, (unsigned long)dnsStats.capacity);
    printf("  L2 Isolation:       %s (Anti-ARP Spoofing Active)\n",
           settings.clientIsolation ? "ENABLED" : "DISABLED");
    printf("  Dynamic TPC:        %s (TX Power: %.2f dBm)\n",
           settings.dynamicTpcEnabled ? "ACTIVE" : "STATIC", roam.getCurrentTxPowerDbm());
    printf("  RTC Crash Log:      %lu events\n", (unsigned long)StorageManager::getCrashLog().count);
    println("-----------------------------------------------------------");
}

void CliShell::printScan() {
    auto& roam = RoamingEngine::instance();
    if (roam.isScanning()) {
        println("[CLI] Waiting for active background scan...");
        unsigned long w = millis();
        while (roam.isScanning() && millis() - w < 6000) {
            esp_task_wdt_reset();
            roam.update();
            delay(50);
        }
    } else {
        println("[CLI] Scanning 2.4 GHz spectrum...");
        roam.startScan();
        unsigned long w = millis();
        while (roam.isScanning() && millis() - w < 6000) {
            esp_task_wdt_reset();
            roam.update();
            delay(50);
        }
    }

    auto results = roam.getLastScanResults();
    printf("\nDISCOVERED 2.4 GHz NETWORKS (%d found):\n", (int)results.size());
    println("+----+------------------------+-------------------+---------+-----+--------+");
    println("| #  | SSID                   | BSSID             | RSSI    | CH  | AUTH   |");
    println("+----+------------------------+-------------------+---------+-----+--------+");
    for (size_t i = 0; i < results.size(); i++) {
        const auto& ap = results[i];
        const char* authmode = "OTHER";
        switch (ap.encryptionType) {
            case WIFI_AUTH_OPEN: authmode = "OPEN"; break;
            case WIFI_AUTH_WEP: authmode = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: authmode = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: authmode = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: authmode = "WPA/2"; break;
            case WIFI_AUTH_WPA3_PSK: authmode = "WPA3"; break;
        }
        printf("| %-2d | %-22.22s | %-17s | %3d dBm | %3d | %-6s |\n",
               (int)i + 1, ap.ssid.c_str(), ap.bssid.c_str(),
               (int)ap.rssi, (int)ap.channel, authmode);
    }
    println("+----+------------------------+-------------------+---------+-----+--------+");
}

void CliShell::printClients() {
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    if (esp_wifi_ap_get_sta_list(&sta_list) != ESP_OK) {
        println("[CLI] Failed to query SoftAP station list.");
        return;
    }
    printf("\nACTIVE CONNECTED REPEATER CLIENTS (%d connected):\n", sta_list.num);
    println("+----+-------------------+---------+----------------------+----------------------+");
    println("| #  | MAC ADDRESS       | RSSI    | VENDOR               | ALIAS / NAME         |");
    println("+----+-------------------+---------+----------------------+----------------------+");
    for (int i = 0; i < sta_list.num; i++) {
        wifi_sta_info_t sta = sta_list.sta[i];
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta.mac[0], sta.mac[1], sta.mac[2], sta.mac[3], sta.mac[4], sta.mac[5]);
        String alias = StorageManager::instance().getClientAlias(mac_str);
        String vendor = StorageManager::instance().getVendorName(mac_str);
        if (alias.length() == 0) alias = "-";
        printf("| %-2d | %-17s | %3d dBm | %-20.20s | %-20.20s |\n",
               i + 1, mac_str, sta.rssi, vendor.c_str(), alias.c_str());
    }
    println("+----+-------------------+---------+----------------------+----------------------+\n");
}

void CliShell::handleConnect(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.empty()) {
        println("Usage: connect <ssid> [password]");
        return;
    }
    String s = tokens[0];
    String p = (tokens.size() >= 2) ? tokens[1] : "";
    RoamingEngine::instance().connectToNetwork(s, p);
}

void CliShell::handleSetTx(const String& args) {
    float dbm = args.toFloat();
    float minDbm = MIN_TX_POWER_RAW * 0.25f;
    float maxDbm = MAX_TX_POWER_RAW * 0.25f;
    if (dbm < minDbm || dbm > maxDbm) {
        printf("Invalid dBm. Range: %.1f to %.1f dBm\n", minDbm, maxDbm);
        return;
    }
    RoamingEngine::instance().setTxPowerDbm(dbm);
    auto s = StorageManager::instance().getSettings();
    s.txPowerDbm = RoamingEngine::instance().getCurrentTxPowerDbm();
    StorageManager::instance().saveSettings(s);
}

void CliShell::handleAddKnown(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.size() < 2) {
        println("Usage: add <ssid> <password> [priority]");
        return;
    }
    String s = tokens[0];
    String p = tokens[1];
    int prio = 5;
    if (tokens.size() >= 3) {
        prio = tokens[2].toInt();
        if (prio <= 0) prio = 1;
    }
    if (StorageManager::instance().addKnownNetwork(s, p, prio)) {
        printf("[CLI] Network '%s' saved (priority %d).\n", s.c_str(), prio);
    } else {
        println("[CLI] Error: Could not save network (invalid SSID or pool full).");
    }
}

void CliShell::handleDelKnown(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.empty()) {
        println("Usage: del <ssid>");
        return;
    }
    if (StorageManager::instance().removeKnownNetwork(tokens[0])) {
        printf("[CLI] Network '%s' removed.\n", tokens[0].c_str());
    } else {
        printf("[CLI] Network '%s' not found.\n", tokens[0].c_str());
    }
}

void CliShell::handlePing(const String& args) {
    auto tokens = tokenizeArgs(args);
    String target = (!tokens.empty()) ? tokens[0] : "google.com";
    printf("\n[PING] Testing connectivity to '%s'...\n", target.c_str());

    esp_task_wdt_reset();
    IPAddress targetIp;
    if (WiFi.hostByName(target.c_str(), targetIp)) {
        printf("  DNS Resolution:     SUCCESS -> %s\n", targetIp.toString().c_str());
    } else {
        printf("  DNS Resolution:     FAILED for '%s'\n", target.c_str());
        return;
    }

    WiFiClient client;
    client.setTimeout(2);
    unsigned long t0 = millis();
    bool connected = false;
    const uint16_t ports[] = {80, 443, 53};
    for (uint16_t port : ports) {
        esp_task_wdt_reset();
        if (client.connect(targetIp, port, 2000)) {
            connected = true;
            break;
        }
    }
    unsigned long rtt = millis() - t0;

    if (connected) {
        printf("  TCP Handshake:      SUCCESS to %s (RTT: %lu ms)\n", targetIp.toString().c_str(), rtt);
        println("  Internet Status:    ONLINE & REACHABLE\n");
        client.stop();
    } else {
        println("  TCP Handshake:      FAILED (Host unreachable)\n");
    }
}

void CliShell::printSpectrum() {
    auto loads = RoamingEngine::instance().getChannelLoads();
    int best = RoamingEngine::instance().getBestChannel();
    int current = RoamingEngine::instance().getCurrentChannel();

    println("\n2.4 GHz RF SPECTRUM CONGESTION ANALYSIS:");
    println("+---------+----------------------+------+---------------------------+");
    println("| CHANNEL | CONGESTION DENSITY   | LOAD | STATUS                    |");
    println("+---------+----------------------+------+---------------------------+");
    for (int ch = 1; ch <= 13; ch++) {
        int load = loads[ch];
        if (load < 0) load = 0;
        if (load > 100) load = 100;
        int bars = (load * 20) / 100;
        char barStr[21];
        memset(barStr, ' ', 20);
        for (int b = 0; b < bars && b < 20; b++) barStr[b] = '#';
        barStr[20] = '\0';

        String status = "";
        if (ch == current) status += "[ACTIVE] ";
        if (ch == best) status += "*RECOMMENDED*";

        printf("| CH %-4d | [%-20s] | %3d%% | %-25.25s |\n",
               ch, barStr, load, status.c_str());
    }
    println("+---------+----------------------+------+---------------------------+");
    printf("Suggested Cleanest Frequency: Channel %d\n\n", best);
}

void CliShell::printDnsCache() {
    auto stats = DnsCache::instance().getStats();
    auto entries = DnsCache::instance().getEntries();
    uint32_t total = stats.totalHits + stats.totalMisses;
    float hitRate = total > 0 ? (float)stats.totalHits * 100.0f / total : 0.0f;

    println("\n--- SRAM DNS CACHE DAEMON ---");
    printf("  Total Hits / Misses:  %lu / %lu (%.1f %% hit rate)\n",
           (unsigned long)stats.totalHits, (unsigned long)stats.totalMisses, hitRate);
    printf("  Active Entries:       %lu / %lu\n",
           (unsigned long)stats.activeEntries, (unsigned long)stats.capacity);

    if (!entries.empty()) {
        println("\n+--------------------------------+-----------------+----------+----------+");
        println("| DOMAIN HOSTNAME                | RESOLVED IP     | TTL REM  | HITS     |");
        println("+--------------------------------+-----------------+----------+----------+");
        uint32_t now = millis();
        for (const auto& e : entries) {
            uint32_t elapsed = (uint32_t)(now - e.insert_time_ms);
            uint32_t remSec = (e.ttl_ms > elapsed) ? (e.ttl_ms - elapsed) / 1000 : 0;
            printf("| %-30.30s | %-15s | %5lu s  | %-8lu |\n",
                   e.domain.c_str(), e.ip.toString().c_str(), (unsigned long)remSec, (unsigned long)e.hits);
        }
        println("+--------------------------------+-----------------+----------+----------+\n");
    } else {
        println("  (DNS Cache is currently empty)\n");
    }
}

void CliShell::handleKick(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.empty()) {
        println("Usage: kick <XX:XX:XX:XX:XX:XX>");
        return;
    }
    uint8_t mac[6];
    if (validateMacFormat(tokens[0], mac)) {
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(mac, &aid) == ESP_OK) {
            esp_wifi_deauth_sta(aid);
            printf("[CLI] Station '%s' deauthenticated.\n", tokens[0].c_str());
            return;
        }
    }
    printf("[CLI] Station '%s' not found.\n", tokens[0].c_str());
}

void CliShell::handleRename(const String& args) {
    auto tokens = tokenizeArgs(args);
    if (tokens.size() < 2) {
        println("Usage: rename <XX:XX:XX:XX:XX:XX> <ALIAS>");
        return;
    }
    uint8_t mac[6];
    if (!validateMacFormat(tokens[0], mac)) {
        println("[CLI] Error: Invalid MAC format.");
        return;
    }
    if (StorageManager::instance().setClientAlias(tokens[0], tokens[1])) {
        printf("[CLI] Saved alias '%s' for %s\n", tokens[1].c_str(), tokens[0].c_str());
    } else {
        println("[CLI] Failed to save alias.");
    }
}

void CliShell::handleWan(const String& args) {
    auto tokens = tokenizeArgs(args);
    auto& roam = RoamingEngine::instance();
    auto s = StorageManager::instance().getSettings();

    if (tokens.empty()) {
        println("\n================ WAN INTERNET HEALTH WATCHDOG ================");
        printf("  Status:             %s\n", s.wanWatchdogEnabled ? "ACTIVE" : "PAUSED");
        printf("  Internet Link:      %s\n", roam.isWanOnline() ? "ONLINE" : (roam.isConnected() ? "NO INTERNET" : "DISCONNECTED"));
        printf("  Round-Trip Latency: %.1f ms\n", roam.getWanRttMs());
        printf("  Probe Interval:     %lu s (Healthy) / %d ms (Degraded)\n",
               (unsigned long)s.wanProbeIntervalSec, WAN_PROBE_INTERVAL_FAST_MS);
        printf("  Failure Strikes:    %d / %d\n", roam.getWanConsecutiveFails(), WAN_FAIL_THRESHOLD);
        printf("  Total Failovers:    %lu\n", (unsigned long)roam.getWanFailoverCount());

        auto backoffs = roam.getWanBackoffList();
        printf("  AP Backoffs (%d):\n", (int)backoffs.size());
        unsigned long now = millis();
        for (const auto& b : backoffs) {
            unsigned long elapsed = (unsigned long)(now - b.lastFailTime);
            unsigned long rem = (elapsed < b.backoffMs) ? ((b.backoffMs - elapsed) / 1000UL) : 0;
            printf("    - %-20.20s | Fails: %d | Cooldown: %lu s\n",
                   b.ssid.c_str(), b.failCount, rem);
        }
        println("--------------------------------------------------------------\n");
        return;
    }

    String sub = tokens[0];
    sub.toLowerCase();
    if (sub == "on" || sub == "enable" || sub == "1") {
        StorageManager::instance().setWanWatchdog(true, s.wanProbeIntervalSec);
        println("[WAN-WD] Engaged.");
    } else if (sub == "off" || sub == "disable" || sub == "0") {
        StorageManager::instance().setWanWatchdog(false, s.wanProbeIntervalSec);
        println("[WAN-WD] Paused.");
    } else if (sub == "interval" && tokens.size() >= 2) {
        uint32_t sec = tokens[1].toInt();
        if (sec >= 2 && sec <= 300) {
            StorageManager::instance().setWanWatchdog(s.wanWatchdogEnabled, sec);
            printf("[WAN-WD] Interval set to %lu s.\n", (unsigned long)sec);
        } else {
            println("[WAN-WD] Error: Interval must be 2..300 s.");
        }
    } else if (sub == "failover") {
        if (!roam.isConnected()) {
            println("[WAN-WD] Not connected to upstream AP.");
        } else {
            roam.triggerWanFailover();
        }
    } else if (sub == "clear") {
        roam.clearAllWanBackoff();
    } else {
        println("Usage: wan [on|off|interval <sec>|failover|clear]");
    }
}

void CliShell::handleProbe(const String& args) {
    auto tokens = tokenizeArgs(args);
    auto& roam = RoamingEngine::instance();

    if (!roam.isConnected()) {
        println("[PROBE] Error: STA disconnected.");
        return;
    }

    String target = (!tokens.empty()) ? tokens[0] : WAN_PRIMARY_PROBE_IP;
    printf("[PROBE] Probing %s:%d...\n", target.c_str(), WAN_PROBE_PORT);

    float rtt = 0.0f;
    unsigned long start = millis();
    bool ok = roam.executeWanProbe(&rtt, target.c_str());
    unsigned long elapsed = millis() - start;

    if (ok) {
        printf("[PROBE] OK | RTT: %.2f ms (Elapsed: %lu ms)\n", rtt, elapsed);
    } else {
        printf("[PROBE] FAILED (%lu ms)\n", elapsed);
    }
}
