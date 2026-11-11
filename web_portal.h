#pragma once

#include "config.h"
#include <WebServer.h>
#include <DNSServer.h>

class WebPortal {
public:
    static WebPortal& instance();

    void init();
    void update(); // Called in loop

private:
    WebPortal();
    WebServer server;
    DNSServer dnsServer;
    bool dnsRunning;

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

    // Captive Portal Helper
    bool isCaptivePortal();
};
