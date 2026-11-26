#pragma once

#include "config.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>

class WebPortal {
public:
    static WebPortal& instance();

    void init();
    void update();

    void onStaConnected();
    void onStaDisconnected();
    void startCaptiveDns();
    void stopCaptiveDns();
    bool isCaptiveDnsActive() const { return dnsRunning; }

private:
    WebPortal();
    WebServer server;
    DNSServer* dnsServer;
    bool dnsRunning;

    // Asynchronous connect & deferred reboot state
    bool pendingConnectScheduled;
    String pendingConnectSsid;
    String pendingConnectPass;
    unsigned long pendingConnectTime;

    bool pendingReboot;
    unsigned long pendingRebootTime;

    // OTA upload state tracking
    bool otaInProgress;
    bool otaValidHeader;

    // Measured traffic telemetry (Kbps)
    unsigned long lastRateCalcTime;
    float rxRateKbps;
    float txRateKbps;

    void registerRoutes();
    bool isCaptivePortal();
    bool isTrustedLocalRequest();
    void sendSecurityHeaders();

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
    void handleApiPortmapGet();
    void handleApiPortmapAdd();
    void handleApiPortmapDelete();
    void handleApiMacSet();
    void handleApiMacReset();
    void handleApiSpectrum();
    void handleApiDnsStats();
    void handleApiDnsClear();
    void handleApiClientsRename();
    void handleApiClientsKick();
    void handleApiSpeedtest();
    void handleApiTopology();
    void handleApiIsolationToggle();
    void handleApiTpcToggle();
    void handleApiWanToggle();
    void handleApiWanProbe();
};
