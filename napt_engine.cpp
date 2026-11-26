#include "napt_engine.h"
#include "storage_manager.h"
#include <WiFi.h>

// ==============================================================================
// Helper: Safe IPv4 Header Extractor (Ethernet II first, raw IPv4 fallback)
// ==============================================================================
static inline struct ip_hdr* getIpHdr(struct pbuf *p, uint16_t *offset_out = nullptr) {
    if (!p || !p->payload || p->len < 20) return nullptr;
    const uint8_t *buf = (const uint8_t *)p->payload;

    // 1. Standard Ethernet II frame (14 bytes MAC header, ETH_PAD_SIZE == 0)
    if (p->len >= 34) {
        uint16_t eth_type = ((uint16_t)buf[12] << 8) | buf[13];
        if (eth_type == 0x0800 && (buf[14] >> 4) == 4 && (buf[14] & 0x0F) >= 5) {
            if (offset_out) *offset_out = 14;
            return (struct ip_hdr *)(buf + 14);
        }
    }

    // 2. Direct L3 IPv4 packet fallback (no L2 header)
    if ((buf[0] >> 4) == 4 && (buf[0] & 0x0F) >= 5) {
        if (offset_out) *offset_out = 0;
        return (struct ip_hdr *)buf;
    }

    return nullptr;
}

NaptEngine& NaptEngine::instance() {
    static NaptEngine instance;
    return instance;
}

NaptEngine::NaptEngine()
    : enabled(false),
      currentMss(TCP_MSS_CLAMP_VALUE),
      boundStaIp(0),
      apIp(0),
      cachedPortmapCount(0),
      portmapLock(portMUX_INITIALIZER_UNLOCKED),
      mssClampedPackets(0),
      hairpinPackets(0),
      apRxBytes(0),
      staRxBytes(0),
      apNetif(nullptr),
      staNetif(nullptr),
      origApInput(nullptr),
      origStaInput(nullptr) {
    memset(cachedPortmaps, 0, sizeof(cachedPortmaps));
}

bool NaptEngine::init() {
    apLog("[NAPT] %s %s lwIP NAPT Engine initialized (Flows: %d max, Portmaps: %d max, MSS: %d B).\n",
          PROJECT_NAME, PROJECT_VERSION, NAPT_MAX_FLOWS, NAPT_MAX_PORTMAPS, currentMss);
    return true;
}

bool NaptEngine::enable(IPAddress staIp) {
    uint32_t newStaIp = (uint32_t)staIp;
    if (newStaIp == 0) {
        apLog("[NAPT] Refusing to enable NAPT with 0.0.0.0 STA IP.\n");
        return false;
    }

    // Verify both netif NAPT state and input hooks before short-circuiting
    if (enabled.load() && boundStaIp == newStaIp) {
        bool alreadyIntact = false;
        LOCK_TCPIP_CORE();
        struct netif *curAp = nullptr;
        struct netif *curSta = nullptr;
        for (struct netif *n = netif_list; n != nullptr; n = n->next) {
            if (n->name[0] == 'a' && n->name[1] == 'p') curAp = n;
            else if (n->name[0] == 's' && n->name[1] == 't') curSta = n;
        }
        alreadyIntact = (curAp != nullptr && curAp == apNetif && curAp->napt != 0 && curAp->input == naptApInputHook &&
                         curSta != nullptr && curSta == staNetif && curSta->input == naptStaInputHook);
        UNLOCK_TCPIP_CORE();
        if (alreadyIntact) {
            return true;
        }
        apLog("[NAPT] SoftAP/STA netif state drifted — re-applying NAPT & hooks...\n");
    }

    boundStaIp = newStaIp;
    apIp = (uint32_t)WiFi.softAPIP();

    bool ok = WiFi.AP.enableNAPT(true);
    if (!ok) {
        enabled.store(false);
        apLog("[NAPT] WiFi.AP.enableNAPT(true) FAILED.\n");
        return false;
    }

    // Attach netif input hooks atomically under lwIP core lock
    LOCK_TCPIP_CORE();
    apNetif = nullptr;
    staNetif = nullptr;
    for (struct netif *n = netif_list; n != nullptr; n = n->next) {
        if (n->name[0] == 'a' && n->name[1] == 'p') {
            apNetif = n;
        } else if (n->name[0] == 's' && n->name[1] == 't') {
            staNetif = n;
        }
    }
    if (apNetif && apNetif->input != naptApInputHook) {
        origApInput = apNetif->input;
        apNetif->input = naptApInputHook;
    }
    if (staNetif && staNetif->input != naptStaInputHook) {
        origStaInput = staNetif->input;
        staNetif->input = naptStaInputHook;
    }
    UNLOCK_TCPIP_CORE();

    enabled.store(true);
    apLog("[NAPT] Active (STA: %s, SoftAP GW: %s, MSS Clamp: %d B)\n",
          staIp.toString().c_str(), WiFi.softAPIP().toString().c_str(), currentMss);

    applyStoredRules();
    return true;
}

void NaptEngine::disable() {
    enabled.store(false);

    LOCK_TCPIP_CORE();
    if (apNetif && origApInput && apNetif->input == naptApInputHook) {
        apNetif->input = origApInput;
    }
    if (staNetif && origStaInput && staNetif->input == naptStaInputHook) {
        staNetif->input = origStaInput;
    }
    origApInput = nullptr;
    origStaInput = nullptr;
    apNetif = nullptr;
    staNetif = nullptr;
    UNLOCK_TCPIP_CORE();

    WiFi.AP.enableNAPT(false);
    boundStaIp = 0;
    apIp = 0;

    portENTER_CRITICAL(&portmapLock);
    memset(cachedPortmaps, 0, sizeof(cachedPortmaps));
    cachedPortmapCount = 0;
    portEXIT_CRITICAL(&portmapLock);

    apLog("[NAPT] Disabled.\n");
}

void NaptEngine::consumeTrafficBytes(uint32_t& outUpBytes, uint32_t& outDownBytes) {
    outUpBytes = apRxBytes.exchange(0);
    outDownBytes = staRxBytes.exchange(0);
}

// ==============================================================================
// TCP MSS Clamping (RFC 1624 Alignment-Aware Checksum Adjustment)
// ==============================================================================

bool NaptEngine::clampTcpMss(struct pbuf *p, uint16_t ip_offset, uint16_t max_mss) {
    if (!p || p->ref != 1 || p->type_internal == PBUF_ROM) return false;

    struct ip_hdr *iphdr = (struct ip_hdr *)((uint8_t *)p->payload + ip_offset);
    if (IPH_PROTO(iphdr) != IP_PROTO_TCP) return false;

    // Do not touch non-first IP fragments
    if ((IPH_OFFSET(iphdr) & PP_HTONS(IP_OFFMASK)) != 0) return false;

    uint16_t ip_hl = IPH_HL(iphdr) * 4;
    if (ip_hl < 20) return false;

    uint16_t ip_tot_len = ntohs(IPH_LEN(iphdr));
    if (ip_tot_len < ip_hl + sizeof(struct tcp_hdr) || p->len < ip_offset + ip_hl + sizeof(struct tcp_hdr)) {
        return false;
    }

    struct tcp_hdr *tcphdr = (struct tcp_hdr *)((uint8_t *)iphdr + ip_hl);
    uint8_t flags = TCPH_FLAGS(tcphdr);
    if (!(flags & TCP_SYN)) return false;

    uint16_t tcp_hdr_len = TCPH_HDRLEN(tcphdr) * 4;
    if (tcp_hdr_len <= 20 || tcp_hdr_len > 60 || p->len < ip_offset + ip_hl + tcp_hdr_len) {
        return false;
    }

    uint8_t *tcp_base = (uint8_t *)tcphdr;
    uint8_t *opt = tcp_base + 20;
    uint8_t *opt_end = tcp_base + tcp_hdr_len;

    while (opt < opt_end) {
        uint8_t kind = opt[0];
        if (kind == 0) break;             // EOL
        if (kind == 1) { opt++; continue; } // NOP
        if (opt + 1 >= opt_end) break;
        uint8_t len = opt[1];
        if (len < 2 || opt + len > opt_end) break;

        if (kind == 2 && len == 4) {
            uint16_t current_mss = ((uint16_t)opt[2] << 8) | opt[3];
            if (current_mss > max_mss) {
                size_t val_offset = (size_t)(&opt[2] - tcp_base);
                uint16_t chksum = tcphdr->chksum;

                if ((val_offset & 1) == 0) {
                    // Even 16-bit word alignment inside TCP header
                    fast_rfc1624_update16(&chksum, htons(current_mss), htons(max_mss));
                } else {
                    // Odd alignment: opt[2] is low byte of word (val_offset-1), opt[3] is high byte of next word
                    uint8_t new_hi = (uint8_t)(max_mss >> 8);
                    uint8_t new_lo = (uint8_t)(max_mss & 0xFF);
                    uint16_t old_w1 = ((uint16_t)opt[1] << 8) | opt[2];
                    uint16_t new_w1 = ((uint16_t)opt[1] << 8) | new_hi;
                    fast_rfc1624_update16(&chksum, htons(old_w1), htons(new_w1));

                    uint8_t next_b = (&opt[4] < opt_end) ? opt[4] : 0;
                    uint16_t old_w2 = ((uint16_t)opt[3] << 8) | next_b;
                    uint16_t new_w2 = ((uint16_t)new_lo << 8) | next_b;
                    fast_rfc1624_update16(&chksum, htons(old_w2), htons(new_w2));
                }

                tcphdr->chksum = chksum;
                opt[2] = (uint8_t)(max_mss >> 8);
                opt[3] = (uint8_t)(max_mss & 0xFF);
                mssClampedPackets.fetch_add(1);
                return true;
            }
            break;
        }
        opt += len;
    }
    return false;
}

// ==============================================================================
// Dual-NAT (DNAT + SNAT) Loopback / Hairpinning
// ==============================================================================

bool NaptEngine::handleHairpin(struct pbuf *p, uint16_t ip_offset, struct netif *ap_netif) {
    if (!p || !enabled.load() || boundStaIp == 0 || !ap_netif || p->ref != 1 || p->type_internal == PBUF_ROM) {
        return false;
    }

    struct ip_hdr *iphdr = (struct ip_hdr *)((uint8_t *)p->payload + ip_offset);
    if (iphdr->dest.addr != boundStaIp) return false;
    if ((IPH_OFFSET(iphdr) & PP_HTONS(IP_OFFMASK | IP_MF)) != 0) return false;

    uint8_t proto = IPH_PROTO(iphdr);
    uint16_t ip_hl = IPH_HL(iphdr) * 4;
    if (ip_hl < 20) return false;

    uint32_t int_ip = 0;
    uint16_t int_port = 0;
    uint32_t ap_gw_ip = (apIp != 0) ? apIp : netif_ip4_addr(ap_netif)->addr;

    if (proto == IP_PROTO_TCP && p->len >= ip_offset + ip_hl + sizeof(struct tcp_hdr)) {
        struct tcp_hdr *tcphdr = (struct tcp_hdr *)((uint8_t *)iphdr + ip_hl);
        uint16_t ext_port = ntohs(tcphdr->dest);
        if (!findPortForwardTarget(proto, ext_port, &int_ip, &int_port)) return false;

        uint32_t old_dest = iphdr->dest.addr;
        uint16_t old_dport = tcphdr->dest;
        uint16_t new_dport = htons(int_port);
        uint32_t old_src = iphdr->src.addr;

        uint16_t tcp_chk = tcphdr->chksum;
        uint16_t ip_chk = iphdr->_chksum;

        fast_rfc1624_update32(&tcp_chk, old_dest, int_ip);
        fast_rfc1624_update16(&tcp_chk, old_dport, new_dport);
        fast_rfc1624_update32(&tcp_chk, old_src, ap_gw_ip);

        fast_rfc1624_update32(&ip_chk, old_dest, int_ip);
        fast_rfc1624_update32(&ip_chk, old_src, ap_gw_ip);

        iphdr->dest.addr = int_ip;
        iphdr->src.addr = ap_gw_ip;
        tcphdr->dest = new_dport;
        tcphdr->chksum = tcp_chk;
        iphdr->_chksum = ip_chk;
    } else if (proto == IP_PROTO_UDP && p->len >= ip_offset + ip_hl + sizeof(struct udp_hdr)) {
        struct udp_hdr *udphdr = (struct udp_hdr *)((uint8_t *)iphdr + ip_hl);
        uint16_t ext_port = ntohs(udphdr->dest);
        if (!findPortForwardTarget(proto, ext_port, &int_ip, &int_port)) return false;

        uint32_t old_dest = iphdr->dest.addr;
        uint16_t old_dport = udphdr->dest;
        uint16_t new_dport = htons(int_port);
        uint32_t old_src = iphdr->src.addr;

        uint16_t udp_chk = udphdr->chksum;
        uint16_t ip_chk = iphdr->_chksum;

        if (udp_chk != 0) {
            fast_rfc1624_update32(&udp_chk, old_dest, int_ip);
            fast_rfc1624_update16(&udp_chk, old_dport, new_dport);
            fast_rfc1624_update32(&udp_chk, old_src, ap_gw_ip);
            if (udp_chk == 0) udp_chk = 0xFFFF; // RFC 768
        }

        fast_rfc1624_update32(&ip_chk, old_dest, int_ip);
        fast_rfc1624_update32(&ip_chk, old_src, ap_gw_ip);

        iphdr->dest.addr = int_ip;
        iphdr->src.addr = ap_gw_ip;
        udphdr->dest = new_dport;
        udphdr->chksum = udp_chk;
        iphdr->_chksum = ip_chk;
    } else {
        return false;
    }

    // Advance pbuf payload past L2 header before netif->output (which expects L3 IP header)
    if (ip_offset > 0 && pbuf_remove_header(p, ip_offset) != 0) {
        return false;
    }

    ip4_addr_t target_dest;
    target_dest.addr = int_ip;

    LOCK_TCPIP_CORE();
    err_t out_err = ap_netif->output(ap_netif, p, &target_dest);
    UNLOCK_TCPIP_CORE();

    pbuf_free(p);
    if (out_err == ERR_OK) {
        hairpinPackets.fetch_add(1);
    }
    return true;
}

// ==============================================================================
// Port Forwarding / DNAPT Management
// ==============================================================================

bool NaptEngine::findPortForwardTarget(uint8_t proto, uint16_t extPort, uint32_t *intIp, uint16_t *intPort) const {
    portENTER_CRITICAL(&portmapLock);
    for (uint8_t i = 0; i < cachedPortmapCount; i++) {
        if (cachedPortmaps[i].active && cachedPortmaps[i].protocol == proto && cachedPortmaps[i].extPort == extPort) {
            if (intIp) *intIp = cachedPortmaps[i].intIp;
            if (intPort) *intPort = cachedPortmaps[i].intPort;
            portEXIT_CRITICAL(&portmapLock);
            return true;
        }
    }
    portEXIT_CRITICAL(&portmapLock);
    return false;
}

void NaptEngine::applyStoredRules() {
    if (!enabled.load() || boundStaIp == 0) return;
    auto rules = StorageManager::instance().getPortForwards();

    portENTER_CRITICAL(&portmapLock);
    cachedPortmapCount = 0;
    portEXIT_CRITICAL(&portmapLock);

    for (const auto& r : rules) {
        IPAddress intIp;
        if (!intIp.fromString(r.internalIp) || (uint32_t)intIp == 0) {
            continue;
        }
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_add(r.protocol, boundStaIp, r.externalPort, (uint32_t)intIp, r.internalPort);
        UNLOCK_TCPIP_CORE();

        if (res != 0) {
            portENTER_CRITICAL(&portmapLock);
            if (cachedPortmapCount < NAPT_MAX_PORTMAPS) {
                cachedPortmaps[cachedPortmapCount].protocol = r.protocol;
                cachedPortmaps[cachedPortmapCount].extPort = r.externalPort;
                cachedPortmaps[cachedPortmapCount].intIp = (uint32_t)intIp;
                cachedPortmaps[cachedPortmapCount].intPort = r.internalPort;
                cachedPortmaps[cachedPortmapCount].active = true;
                cachedPortmapCount++;
            }
            portEXIT_CRITICAL(&portmapLock);
        }

        apLog("[PORTMAP] Restored %s %u -> %s:%u (ok=%d)\n",
              r.protocol == 6 ? "TCP" : "UDP", r.externalPort, r.internalIp.c_str(), r.internalPort, (int)res);
    }
}

bool NaptEngine::addPortForward(uint8_t proto, uint16_t extPort, const String& intIpStr, uint16_t intPort, const String& desc) {
    if (proto != 6 && proto != 17) return false;
    if (extPort == 0 || extPort == 80 || extPort == 53 || intPort == 0) return false;

    IPAddress intIp;
    if (!intIp.fromString(intIpStr) || (uint32_t)intIp == 0) {
        return false;
    }

    if (enabled.load() && boundStaIp != 0) {
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_add(proto, boundStaIp, extPort, (uint32_t)intIp, intPort);
        UNLOCK_TCPIP_CORE();
        if (res == 0) {
            apLog("[PORTMAP] lwIP ip_portmap_add failed for %s %u\n", proto == 6 ? "TCP" : "UDP", extPort);
            return false;
        }
    }

    if (!StorageManager::instance().addPortForward(proto, extPort, intIpStr, intPort, desc)) {
        if (enabled.load()) {
            LOCK_TCPIP_CORE();
            ip_portmap_remove(proto, extPort);
            UNLOCK_TCPIP_CORE();
        }
        return false;
    }

    portENTER_CRITICAL(&portmapLock);
    bool found = false;
    for (uint8_t i = 0; i < cachedPortmapCount; i++) {
        if (cachedPortmaps[i].protocol == proto && cachedPortmaps[i].extPort == extPort) {
            cachedPortmaps[i].intIp = (uint32_t)intIp;
            cachedPortmaps[i].intPort = intPort;
            cachedPortmaps[i].active = true;
            found = true;
            break;
        }
    }
    if (!found && cachedPortmapCount < NAPT_MAX_PORTMAPS) {
        cachedPortmaps[cachedPortmapCount].protocol = proto;
        cachedPortmaps[cachedPortmapCount].extPort = extPort;
        cachedPortmaps[cachedPortmapCount].intIp = (uint32_t)intIp;
        cachedPortmaps[cachedPortmapCount].intPort = intPort;
        cachedPortmaps[cachedPortmapCount].active = true;
        cachedPortmapCount++;
    }
    portEXIT_CRITICAL(&portmapLock);

    apLog("[PORTMAP] Added %s %u -> %s:%u\n", proto == 6 ? "TCP" : "UDP", extPort, intIpStr.c_str(), intPort);
    return true;
}

bool NaptEngine::removePortForward(uint8_t proto, uint16_t extPort) {
    if (proto != 6 && proto != 17) return false;
    bool removedNvs = StorageManager::instance().removePortForward(proto, extPort);

    portENTER_CRITICAL(&portmapLock);
    for (uint8_t i = 0; i < cachedPortmapCount; i++) {
        if (cachedPortmaps[i].protocol == proto && cachedPortmaps[i].extPort == extPort) {
            for (uint8_t j = i; j < cachedPortmapCount - 1; j++) {
                cachedPortmaps[j] = cachedPortmaps[j + 1];
            }
            cachedPortmapCount--;
            cachedPortmaps[cachedPortmapCount].active = false;
            break;
        }
    }
    portEXIT_CRITICAL(&portmapLock);

    if (enabled.load()) {
        LOCK_TCPIP_CORE();
        u8_t res = ip_portmap_remove(proto, extPort);
        UNLOCK_TCPIP_CORE();
        return (res != 0) || removedNvs;
    }
    return removedNvs;
}

// ==============================================================================
// Netif Input Interception Hooks
// ==============================================================================

err_t NaptEngine::naptApInputHook(struct pbuf *p, struct netif *inp) {
    NaptEngine &engine = NaptEngine::instance();
    if (p != nullptr && engine.isEnabled()) {
        engine.apRxBytes.fetch_add(p->tot_len);
        uint16_t ip_offset = 0;
        struct ip_hdr *iphdr = getIpHdr(p, &ip_offset);
        if (iphdr != nullptr) {
            if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
                engine.clampTcpMss(p, ip_offset, engine.currentMss);
            }
            if (engine.handleHairpin(p, ip_offset, inp)) {
                return ERR_OK;
            }
        }
    }

    if (engine.origApInput) {
        return engine.origApInput(p, inp);
    }
    return tcpip_input(p, inp);
}

err_t NaptEngine::naptStaInputHook(struct pbuf *p, struct netif *inp) {
    NaptEngine &engine = NaptEngine::instance();
    if (p != nullptr && engine.isEnabled()) {
        engine.staRxBytes.fetch_add(p->tot_len);
        uint16_t ip_offset = 0;
        struct ip_hdr *iphdr = getIpHdr(p, &ip_offset);
        if (iphdr != nullptr && IPH_PROTO(iphdr) == IP_PROTO_TCP) {
            engine.clampTcpMss(p, ip_offset, engine.currentMss);
        }
    }

    if (engine.origStaInput) {
        return engine.origStaInput(p, inp);
    }
    return tcpip_input(p, inp);
}
