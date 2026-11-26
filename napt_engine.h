#pragma once

#include "config.h"
#include <Arduino.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/netif.h>
#include <freertos/FreeRTOS.h>
#include <atomic>

extern "C" {
#include <lwip/err.h>
#include <lwip/tcpip.h>
#include <lwip/lwip_napt.h>
#include <lwip/prot/ip.h>
#include <lwip/prot/ip4.h>
#include <lwip/prot/tcp.h>
#include <lwip/prot/udp.h>
}

// ==============================================================================
// Fast RFC 1624 Incremental Checksum Updating Routines (Equation 3)
// ==============================================================================

static inline void fast_rfc1624_update32(uint16_t *chksum, uint32_t old_u32, uint32_t new_u32) {
    uint32_t sum = (~(*chksum)) & 0xFFFF;
    sum += (~old_u32) >> 16;
    sum += (~old_u32) & 0xFFFF;
    sum += new_u32 >> 16;
    sum += new_u32 & 0xFFFF;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    *chksum = (uint16_t)(~sum);
}

static inline void fast_rfc1624_update16(uint16_t *chksum, uint16_t old_u16, uint16_t new_u16) {
    uint32_t sum = (~(*chksum)) & 0xFFFF;
    sum += (~old_u16) & 0xFFFF;
    sum += new_u16;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    *chksum = (uint16_t)(~sum);
}

// In-Memory Fast Cache for Port Forwarding Rules (Dual-NAT Hairpinning)
struct CachedPortForward {
    uint8_t  protocol;
    uint16_t extPort;
    uint32_t intIp;
    uint16_t intPort;
    bool     active;
};

class NaptEngine {
public:
    static NaptEngine& instance();

    bool init();
    bool enable(IPAddress staIp);
    void disable();
    bool isEnabled() const { return enabled.load(); }

    // Port Forwarding / DNAPT
    bool addPortForward(uint8_t proto, uint16_t extPort, const String& intIp, uint16_t intPort, const String& desc = "");
    bool removePortForward(uint8_t proto, uint16_t extPort);
    void applyStoredRules();

    // Real-time Telemetry Accessors
    uint32_t getMssClampedCount() const { return mssClampedPackets.load(); }
    uint32_t getHairpinCount() const { return hairpinPackets.load(); }
    uint8_t getActivePortmapCount() const { return cachedPortmapCount; }
    void consumeTrafficBytes(uint32_t& outUpBytes, uint32_t& outDownBytes);

    // Packet Hooks (TCP MSS Clamping + Dual-NAT Hairpinning)
    bool clampTcpMss(struct pbuf *p, uint16_t ip_offset, uint16_t max_mss);
    bool handleHairpin(struct pbuf *p, uint16_t ip_offset, struct netif *ap_netif);

private:
    NaptEngine();
    ~NaptEngine() = default;

    std::atomic<bool> enabled;
    uint16_t currentMss;
    uint32_t boundStaIp;
    uint32_t apIp;

    CachedPortForward cachedPortmaps[NAPT_MAX_PORTMAPS];
    uint8_t cachedPortmapCount;
    mutable portMUX_TYPE portmapLock;

    std::atomic<uint32_t> mssClampedPackets;
    std::atomic<uint32_t> hairpinPackets;
    std::atomic<uint32_t> apRxBytes;   // LAN -> WAN (upload)
    std::atomic<uint32_t> staRxBytes;  // WAN -> LAN (download)

    struct netif *apNetif;
    struct netif *staNetif;
    netif_input_fn origApInput;
    netif_input_fn origStaInput;

    bool findPortForwardTarget(uint8_t proto, uint16_t extPort, uint32_t *intIp, uint16_t *intPort) const;

    static err_t naptApInputHook(struct pbuf *p, struct netif *inp);
    static err_t naptStaInputHook(struct pbuf *p, struct netif *inp);
};
