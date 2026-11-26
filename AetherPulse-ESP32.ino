#include "config.h"
#include "storage_manager.h"
#include "napt_engine.h"
#include "roaming_engine.h"
#include "web_portal.h"
#include "cli_shell.h"
#include "dns_cache.h"
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <cstdarg>

void apLog(const char* format, ...) {
    char buf[384];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len <= 0) return;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;

    if (CliShell::instance().isReady()) {
        CliShell::instance().print(buf);
    } else {
        Serial.write((const uint8_t*)buf, len);
    }
}

void setup() {
    Serial.begin(CLI_BAUD_RATE);
    delay(200);

    // Task Watchdog (TWDT) monitoring Core 1 (loopTask) with 15s timeout.
    // Core 0 idle task is intentionally excluded so Wi-Fi/lwIP bursts do not false-trip TWDT.
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 1),
        .trigger_panic = true
    };
    esp_err_t wdtErr = esp_task_wdt_reconfigure(&twdt_config);
    if (wdtErr == ESP_ERR_INVALID_STATE) {
        wdtErr = esp_task_wdt_init(&twdt_config);
    }
    esp_err_t addErr = esp_task_wdt_add(NULL);
    if (wdtErr == ESP_OK && (addErr == ESP_OK || addErr == ESP_ERR_INVALID_STATE)) {
        apLog("[TWDT] Task Watchdog active (Core 1, %dms timeout).\n", WDT_TIMEOUT_MS);
    } else {
        apLog("[TWDT] Watchdog init warning (wdt=0x%x, add=0x%x).\n", wdtErr, addErr);
    }

    // Ensure optional TFT backlight pin stays off to conserve power and reduce heat
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);

    // Initialize NVS Storage & Load Settings
    StorageManager::instance().begin();
    auto settings = StorageManager::instance().getSettings();

    // Initialize Sub-Millisecond SRAM DNS Cache
    DnsCache::instance().init();

    // Initialize Hardware & Roaming Engine
    RoamingEngine::instance().init();

    // Initialize NAPT routing engine
    NaptEngine::instance().init();

    // Register Network Event Listener for NAPT Lifecycle & Client Tracking
    Network.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
                apLog("\n[NET] STA IP: %s (GW: %s, DNS: %s)\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP().toString().c_str());
                if (RoamingEngine::instance().isSubnetConflict(WiFi.localIP())) {
                    IPAddress currentAp = WiFi.softAPIP();
                    IPAddress targetAp = (currentAp == DEFAULT_AP_IP) ? DEFAULT_AP_ALT_IP : DEFAULT_AP_IP;
                    IPAddress leaseStart(targetAp[0], targetAp[1], targetAp[2], 2);
                    apLog("[NET] Upstream subnet conflict detected! Shifting SoftAP to %s...\n",
                          targetAp.toString().c_str());
                    WiFi.AP.config(targetAp, targetAp, DEFAULT_AP_NETMASK, leaseStart, targetAp);
                    esp_wifi_deauth_sta(0);
                }
                NaptEngine::instance().enable(WiFi.localIP());
                WebPortal::instance().onStaConnected();
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
                apLog("\n[NET] STA Disconnected / Lost IP. Halting NAPT routing.\n");
                NaptEngine::instance().disable();
                WebPortal::instance().onStaDisconnected();
                break;
            }
            case ARDUINO_EVENT_WIFI_AP_START:
                apLog("\n[NET] SoftAP started.\n");
                if (WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0) {
                    NaptEngine::instance().enable(WiFi.localIP());
                }
                break;
            case ARDUINO_EVENT_WIFI_AP_STOP:
                apLog("\n[NET] SoftAP stopped.\n");
                break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                apLog("\n[NET] SoftAP client connected.\n");
                RoamingEngine::instance().updateDynamicTpc();
                break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                apLog("\n[NET] SoftAP client disconnected.\n");
                RoamingEngine::instance().updateDynamicTpc();
                break;
            default:
                break;
        }
    });

    // Bring up Dual Wi-Fi (AP + Station) cleanly through WiFiGeneric
    Network.begin();
    apLog("[WIFI] Configuring Dual SoftAP + Station radio...\n");
    WiFi.mode(WIFI_AP_STA);

    // Apply Cloned Station MAC if configured in NVS
    if (settings.clonedMac.length() == 17) {
        RoamingEngine::instance().applyClonedMac(settings.clonedMac);
    }

    // Enforce 802.11 b/g/n and HT20 on both STA and SoftAP
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

    // Disable modem sleep power-save for consistent low-latency forwarding
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Configure SoftAP IP, Gateway, Subnet, Lease Start, and DNS Offer before creating AP
    IPAddress ap_ip = DEFAULT_AP_IP;
    IPAddress ap_leaseStart(ap_ip[0], ap_ip[1], ap_ip[2], 2);
    WiFi.AP.begin();
    WiFi.AP.config(ap_ip, ap_ip, DEFAULT_AP_NETMASK, ap_leaseStart, ap_ip);
    WiFi.AP.create(settings.apSSID.c_str(),
                   settings.apPassword.c_str(),
                   settings.apChannel,
                   settings.apHidden,
                   settings.apMaxClients);
    WiFi.AP.waitStatusBits(ESP_NETIF_STARTED_BIT, 1000);

    // Tune SoftAP Beacon Interval & DTIM Period from config.h
    wifi_config_t ap_cfg;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
        ap_cfg.ap.beacon_interval = SOFTAP_BEACON_INTERVAL_TU;
        ap_cfg.ap.dtim_period = SOFTAP_DTIM_PERIOD;
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        apLog("[WIFI] SoftAP tuned: Beacon=%u TU, DTIM=%u.\n",
              SOFTAP_BEACON_INTERVAL_TU, SOFTAP_DTIM_PERIOD);
    }

    // Set calibrated RF Transmit Power clamped to physical limits
    RoamingEngine::instance().setTxPowerDbm(settings.txPowerDbm);

    // Start Web Management Portal (coordinates Port 53 between Captive DNS & DnsCache)
    WebPortal::instance().init();

    // Initialize mDNS Responder (http://aetherpulse.local) after HTTP server starts
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.setInstanceName("AetherPulse Wi-Fi Repeater");
        MDNS.addService("http", "tcp", 80);
        apLog("[mDNS] Responder active at http://%s.local\n", MDNS_HOSTNAME);
    }

    // Initialize Non-Blocking Serial CLI Shell (takes over UART0 cleanly)
    CliShell::instance().init();

    // Trigger initial background scan to auto-connect to known networks
    RoamingEngine::instance().startScan();

    apLog("\n>>> %s %s READY <<<\n", PROJECT_NAME, PROJECT_VERSION);
    apLog(">>> SoftAP SSID: '%s' | Portal: http://%s or http://%s.local <<<\n\n",
          settings.apSSID.c_str(), WiFi.softAPIP().toString().c_str(), MDNS_HOSTNAME);
}

void loop() {
    esp_task_wdt_reset();
    CliShell::instance().update();
    WebPortal::instance().update();
    RoamingEngine::instance().update();
    delay(2);
}
