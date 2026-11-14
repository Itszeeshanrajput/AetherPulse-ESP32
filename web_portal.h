#pragma once

#include "config.h"
#include "dns_cache.h"
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
    DNSServer* dnsServer;
    WiFiUDP dnsAdUdp;
    WiFiUDP dnsUpstreamUdp;
    bool dnsRunning;
    bool adBlockActive;

    void startCaptiveDns();
    void stopCaptiveDns();

    // Traffic telemetry tracking
    unsigned long lastRateCalcTime;
    float rxRateKbps;
    float txRateKbps;

    void registerRoutes();
    
    // Page handlers
    void handleRoot();
    void handleNotFound();
    void handleManifest();
    
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

    // v3.0 Quantum Endpoints
    void handleApiSpectrum();
    void handleApiDnsStats();
    void handleApiDnsClear();
    void handleApiClientsRename();
    void handleApiClientsKick();
    void handleApiSpeedtest();

    // High-performance Port 53 DNS AdBlock Sinkhole
    void processAdBlockDns();

    // Captive Portal Helper
    bool isCaptivePortal();
};
