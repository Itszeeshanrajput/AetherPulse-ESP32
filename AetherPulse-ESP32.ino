#include "config.h"
#include "storage_manager.h"
#include "napt_engine.h"
#include "roaming_engine.h"
#include "web_portal.h"
#include "cli_shell.h"

void setup() {
    Serial.begin(CLI_BAUD_RATE);
    delay(500);

    // Disable LCD Backlight (GPIO 27) to minimize heat and reserve 100% power/RAM
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);

    // Initialize NVS Storage & Load Settings
    StorageManager::instance().begin();
    auto settings = StorageManager::instance().getSettings();

    // Initialize Hardware & Roaming Engine
    RoamingEngine::instance().init();

    // Initialize NAPT routing engine
    NaptEngine::instance().init();

    // Register Network Event Listener for NAPT Lifecycle & Client Tracking
    Network.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                Serial.printf("\n[NET EVENT] STA Assigned IP: %s (GW: %s, DNS: %s)\n",
                              WiFi.localIP().toString().c_str(),
                              WiFi.gatewayIP().toString().c_str(),
                              WiFi.dnsIP().toString().c_str());
                WiFi.AP.enableNAPT(true);
                break;
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                Serial.println("\n[NET EVENT] STA Disconnected / Lost IP. Halting NAPT routing.");
                WiFi.AP.enableNAPT(false);
                break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
                Serial.println("\n[NET EVENT] Station client connected to AetherPulse SoftAP.");
                break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
                Serial.println("\n[NET EVENT] Station client disconnected from AetherPulse SoftAP.");
                break;
            default:
                break;
        }
    });

    // Start Dual Wi-Fi (AP + Station)
    Serial.println("[WIFI] Configuring Dual SoftAP + Station radio architecture...");
    WiFi.mode(WIFI_AP_STA);

    // Configure SoftAP IP, Gateway, Subnet, Lease Start, and DNS Offer (8.8.8.8)
    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress ap_gw(192, 168, 4, 1);
    IPAddress ap_mask(255, 255, 255, 0);
    IPAddress ap_leaseStart(192, 168, 4, 2);
    IPAddress ap_dns(8, 8, 8, 8); // Hand out Google DNS directly to repeater clients

    WiFi.AP.begin();
    WiFi.AP.config(ap_ip, ap_gw, ap_mask, ap_leaseStart, ap_dns);
    WiFi.AP.create(settings.apSSID.c_str(), 
                   settings.apPassword.c_str(), 
                   settings.apChannel, 
                   settings.apHidden, 
                   settings.apMaxClients);
    WiFi.AP.waitStatusBits(ESP_NETIF_STARTED_BIT, 1000);

    // Low-Level RF & Physical Layer Anti-Jitter Optimization
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable modem sleep power-save for zero-jitter ping

    wifi_config_t ap_cfg;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
        ap_cfg.ap.beacon_interval = SOFTAP_BEACON_INTERVAL_TU; // 103 TU offset prevents phase-lock collisions
        ap_cfg.ap.dtim_period = SOFTAP_DTIM_PERIOD;             // DTIM 1 for immediate delivery
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        Serial.printf("[WIFI] SoftAP tuned: DTIM=%d, Beacon=%d TU\n", SOFTAP_DTIM_PERIOD, SOFTAP_BEACON_INTERVAL_TU);
    }

    // Set calibrated RF Transmit Power
    int rawTx = (int)(settings.txPowerDbm * 4.0f);
    esp_wifi_set_max_tx_power(rawTx);
    Serial.printf("[RF] RF Output Power calibrated to %.2f dBm (raw: %d)\n", settings.txPowerDbm, rawTx);

    // Start Web Management Portal & Captive DNS
    WebPortal::instance().init();

    // Initialize Serial CLI Shell
    CliShell::instance().init();

    // Trigger initial background scan to auto-connect to known networks
    RoamingEngine::instance().startScan();

    Serial.printf("\n>>> %s %s READY <<<\n", PROJECT_NAME, PROJECT_VERSION);
    Serial.printf(">>> Connect to Wi-Fi '%s' (Pass: '%s') or browse to http://192.168.4.1 <<<\n\n",
                  settings.apSSID.c_str(), settings.apPassword.c_str());
}

void loop() {
    CliShell::instance().update();
    WebPortal::instance().update();
    RoamingEngine::instance().update();
    delay(2);
}
