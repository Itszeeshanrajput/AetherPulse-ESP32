#pragma once

#include "config.h"
#include <lwip/ip_addr.h>

extern "C" {
#include <lwip/err.h>
#include <lwip/tcpip.h>
#include <lwip/lwip_napt.h>
}

struct NaptStats {
    uint32_t activeFlows;
    uint32_t maxFlows;
    uint32_t totalPacketsForwarded;
    uint32_t droppedPackets;
    bool isEnabled;
};

class NaptEngine {
public:
    static NaptEngine& instance();

    bool init();
    bool enable(IPAddress staIp);
    void disable();
    bool isEnabled() const { return enabled; }

    NaptStats getStats();

    // TCP MSS Clamping Hook
    void setMssClamping(uint16_t mss) { currentMss = mss; }
    uint16_t getMssClamping() const { return currentMss; }

private:
    NaptEngine();
    bool enabled;
    uint16_t currentMss;
    uint32_t boundStaIp;
};
