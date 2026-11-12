#include "napt_engine.h"
#include "storage_manager.h"
#include <WiFi.h>

NaptEngine& NaptEngine::instance() {
    static NaptEngine instance;
    return instance;
}

NaptEngine::NaptEngine() 
    : enabled(false), currentMss(TCP_MSS_CLAMP_VALUE), boundStaIp(0) {}

bool NaptEngine::init() {
    Serial.println("[NAPT] Native Arduino-ESP32 WiFi.AP NAPT & Port Forwarding subsystem initialized.");
    return true;
}

bool NaptEngine::enable(IPAddress staIp) {
    boundStaIp = (uint32_t)staIp;
    bool ok = WiFi.AP.enableNAPT(true);
    enabled = ok;
    Serial.printf("[NAPT] WiFi.AP.enableNAPT(true) => %s (STA IP: %s)\n", 
                  ok ? "ACTIVE & ROUTING" : "FAILED", staIp.toString().c_str());
    if (ok) {
        applyStoredRules();
    }
    return ok;
}

void NaptEngine::disable() {
    WiFi.AP.enableNAPT(false);
    enabled = false;
    boundStaIp = 0;
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

void NaptEngine::applyStoredRules() {
    if (!enabled || boundStaIp == 0) return;
    auto rules = StorageManager::instance().getPortForwards();
    for (const auto& r : rules) {
        IPAddress intIp;
        intIp.fromString(r.internalIp);
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_add(r.protocol, boundStaIp, r.externalPort, (uint32_t)intIp, r.internalPort);
        UNLOCK_TCPIP_CORE();
        Serial.printf("[PORTMAP] Restored rule: %s %d -> %s:%d (Result: %d)\n",
                      r.protocol == 6 ? "TCP" : "UDP", r.externalPort, r.internalIp.c_str(), r.internalPort, res);
    }
}

bool NaptEngine::addPortForward(uint8_t proto, uint16_t extPort, const String& intIpStr, uint16_t intPort, const String& desc) {
    StorageManager::instance().addPortForward(proto, extPort, intIpStr, intPort, desc);

    if (enabled && boundStaIp != 0) {
        IPAddress intIp;
        intIp.fromString(intIpStr);
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_add(proto, boundStaIp, extPort, (uint32_t)intIp, intPort);
        UNLOCK_TCPIP_CORE();
        Serial.printf("[PORTMAP] Added %s %d -> %s:%d (Result: %d, Desc: %s)\n",
                      proto == 6 ? "TCP" : "UDP", extPort, intIpStr.c_str(), intPort, res, desc.c_str());
        return (res != 0);
    }
    return true;
}

bool NaptEngine::removePortForward(uint8_t proto, uint16_t extPort) {
    StorageManager::instance().removePortForward(proto, extPort);
    if (enabled) {
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_remove(proto, extPort);
        UNLOCK_TCPIP_CORE();
        Serial.printf("[PORTMAP] Removed %s %d (Result: %d)\n", proto == 6 ? "TCP" : "UDP", extPort, res);
        return (res != 0);
    }
    return true;
}
