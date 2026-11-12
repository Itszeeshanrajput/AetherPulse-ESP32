#pragma once

#include "config.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <Update.h>
#include <ESPmDNS.h>

class WebPortal {
public:
    static WebPortal& instance();

    void init();
    void update(); // Called in loop

    void toggleAdBlock(bool enable);
    bool isAdBlockActive() const { return adBlockActive; }

private:
    WebPortal();
    WebServer server;
    DNSServer dnsServer;
    WiFiUDP dnsAdUdp;
    WiFiUDP dnsUpstreamUdp;
    bool dnsRunning;
    bool adBlockActive;

    // Traffic telemetry tracking
    unsigned long lastRateCalcTime;
    float rxRateKbps;
    float txRateKbps;

    void registerRoutes();
    
    // Page handlers
    void handleRoot();
    void handleNotFound();
    
    // REST API endpoints
    void handleApiStatus();
    void handleApiScan();
    void handleApiConnect();
    void handleApiDisconnect();
    void handleApiKnownNetworks();
    void handleApiAddKnown();
    void handleApiDeleteKnown();
    void handleApiSettings();
    void handleApiSaveSettings();
    void handleApiClients();
    void handleApiReboot();

    // v2.0 Titanium Endpoints
    void handleApiPortmapGet();
    void handleApiPortmapAdd();
    void handleApiPortmapDelete();
    void handleApiMacSet();
    void handleApiMacReset();
    void handleApiAdBlockToggle();

    // DNS AdBlock sinkhole
    void processAdBlockDns();

    // Captive Portal Helper
    bool isCaptivePortal();
};
