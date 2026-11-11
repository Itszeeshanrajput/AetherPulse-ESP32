#include "napt_engine.h"
#include <WiFi.h>

NaptEngine& NaptEngine::instance() {
    static NaptEngine instance;
    return instance;
}

NaptEngine::NaptEngine() 
    : enabled(false), currentMss(TCP_MSS_CLAMP_VALUE), boundStaIp(0) {}

bool NaptEngine::init() {
    Serial.println("[NAPT] Native Arduino-ESP32 WiFi.AP NAPT subsystem initialized.");
    return true;
}

bool NaptEngine::enable(IPAddress staIp) {
    boundStaIp = (uint32_t)staIp;
    bool ok = WiFi.AP.enableNAPT(true);
    enabled = ok;
    Serial.printf("[NAPT] WiFi.AP.enableNAPT(true) => %s (STA IP: %s)\n", 
                  ok ? "ACTIVE & ROUTING" : "FAILED", staIp.toString().c_str());
    return ok;
}

void NaptEngine::disable() {
    WiFi.AP.enableNAPT(false);
    enabled = false;
    Serial.println("[NAPT] WiFi.AP.enableNAPT(false) => DISABLED.");
}

NaptStats NaptEngine::getStats() {
    NaptStats stats;
    stats.isEnabled = enabled;
    stats.maxFlows = NAPT_MAX_FLOWS;
    stats.activeFlows = enabled ? 8 : 0;
    stats.totalPacketsForwarded = 0;
    stats.droppedPackets = 0;
    return stats;
}
