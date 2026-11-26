#include "dns_cache.h"
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <esp_random.h>

// ==============================================================================
// 1. Static Domain Helper Implementation
// ==============================================================================

StaticDomain::StaticDomain() {
    data[0] = '\0';
}

StaticDomain::StaticDomain(const char* s) {
    set(s);
}

void StaticDomain::set(const char* s) {
    if (s) {
        size_t i = 0;
        while (s[i] != '\0' && i < sizeof(data) - 1) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            data[i] = c;
            i++;
        }
        data[i] = '\0';
    } else {
        data[0] = '\0';
    }
}

static_dns_entry_t::static_dns_entry_t()
    : ip(0, 0, 0, 0), insert_time_ms(0), ttl_ms(0), hits(0), hash(0), valid(false) {
    domain.data[0] = '\0';
}

static inline uint32_t fnv1a_32_str(const char* str) {
    uint32_t hash = 2166136261u;
    if (!str) return hash;
    while (*str) {
        char c = *str++;
        if (c >= 'A' && c <= 'Z') c += 32;
        hash ^= (uint8_t)c;
        hash *= 16777619u;
    }
    return hash;
}

static inline bool isEntryExpired(const static_dns_entry_t& e, uint32_t now) {
    return (uint32_t)(now - e.insert_time_ms) >= e.ttl_ms;
}

// ==============================================================================
// 2. DnsCache Singleton & Lifecycle
// ==============================================================================

DnsCache& DnsCache::instance() {
    static DnsCache instance;
    return instance;
}

DnsCache::DnsCache()
    : _local_sock(-1),
      _upstream_sock(-1),
      _local_port(53),
      _proxy_running(false),
      _task_exited(true),
      _proxy_task_handle(NULL),
      _spinlock(portMUX_INITIALIZER_UNLOCKED),
      _hitsTotal(0),
      _missesTotal(0),
      _evictionsTotal(0),
      _expiredTotal(0) {
    memset(_table, 0, sizeof(_table));
    memset(_inflight, 0, sizeof(_inflight));
}

DnsCache::~DnsCache() {
    stopAsyncProxy();
}

void DnsCache::init() {
    clear();
    apLog("[DNS-CACHE] Zero-Allocation SRAM DNS Engine initialized (%d slots).\n", DNS_CACHE_MAX_ENTRIES);
}

// ==============================================================================
// 3. Cache Lookup & Insert (Wraparound-Safe)
// ==============================================================================

bool DnsCache::lookup(const char* domain, IPAddress& outIp, uint32_t* outRemTtlSec) {
    if (!domain || *domain == '\0') return false;

    StaticDomain norm(domain);
    uint32_t h = fnv1a_32_str(norm.c_str());
    uint32_t now = millis();

    portENTER_CRITICAL(&_spinlock);
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (_table[i].valid && _table[i].hash == h &&
            strcasecmp(_table[i].domain.c_str(), norm.c_str()) == 0) {
            if (!isEntryExpired(_table[i], now)) {
                outIp = _table[i].ip;
                if (outRemTtlSec) {
                    uint32_t elapsed = (uint32_t)(now - _table[i].insert_time_ms);
                    uint32_t remMs = (_table[i].ttl_ms > elapsed) ? (_table[i].ttl_ms - elapsed) : 0;
                    *outRemTtlSec = (remMs + 999UL) / 1000UL;
                    if (*outRemTtlSec == 0) *outRemTtlSec = 1;
                }
                _table[i].hits++;
                _hitsTotal++;
                portEXIT_CRITICAL(&_spinlock);
                return true;
            }
            _table[i].valid = false;
            _expiredTotal++;
            break;
        }
    }
    _missesTotal++;
    portEXIT_CRITICAL(&_spinlock);
    return false;
}

void DnsCache::insert(const char* domain, IPAddress ip, uint32_t ttlSec) {
    if (!domain || *domain == '\0' || (uint32_t)ip == 0) return;

    if (ttlSec < 15) ttlSec = 15;
    if (ttlSec > 3600) ttlSec = 3600;
    uint32_t ttlMs = ttlSec * 1000UL;

    StaticDomain norm(domain);
    uint32_t h = fnv1a_32_str(norm.c_str());
    uint32_t now = millis();

    portENTER_CRITICAL(&_spinlock);

    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (_table[i].valid && _table[i].hash == h &&
            strcasecmp(_table[i].domain.c_str(), norm.c_str()) == 0) {
            _table[i].ip = ip;
            _table[i].insert_time_ms = now;
            _table[i].ttl_ms = ttlMs;
            _table[i].hits++;
            portEXIT_CRITICAL(&_spinlock);
            return;
        }
    }

    int targetSlot = -1;
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (!_table[i].valid || isEntryExpired(_table[i], now)) {
            if (_table[i].valid) _expiredTotal++;
            targetSlot = (int)i;
            break;
        }
    }

    // LFU eviction with oldest-insert tie-breaker
    if (targetSlot < 0) {
        uint32_t minHits = 0xFFFFFFFF;
        uint32_t maxAge = 0;
        for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
            uint32_t age = (uint32_t)(now - _table[i].insert_time_ms);
            if (_table[i].hits < minHits || (_table[i].hits == minHits && age >= maxAge)) {
                minHits = _table[i].hits;
                maxAge = age;
                targetSlot = (int)i;
            }
        }
        _evictionsTotal++;
    }

    if (targetSlot >= 0) {
        _table[targetSlot].domain = norm;
        _table[targetSlot].ip = ip;
        _table[targetSlot].insert_time_ms = now;
        _table[targetSlot].ttl_ms = ttlMs;
        _table[targetSlot].hits = 1;
        _table[targetSlot].hash = h;
        _table[targetSlot].valid = true;
    }

    portEXIT_CRITICAL(&_spinlock);
}

// ==============================================================================
// 4. Prune, Clear, and Thread-Safe Snapshots
// ==============================================================================

void DnsCache::prune() {
    uint32_t now = millis();
    portENTER_CRITICAL(&_spinlock);
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (_table[i].valid && isEntryExpired(_table[i], now)) {
            _table[i].valid = false;
            _expiredTotal++;
        }
    }
    portEXIT_CRITICAL(&_spinlock);
}

void DnsCache::clear() {
    portENTER_CRITICAL(&_spinlock);
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        _table[i].valid = false;
        _table[i].domain.data[0] = '\0';
        _table[i].ip = IPAddress(0, 0, 0, 0);
        _table[i].insert_time_ms = 0;
        _table[i].ttl_ms = 0;
        _table[i].hits = 0;
        _table[i].hash = 0;
    }
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        _inflight[i].active = false;
    }
    _hitsTotal = 0;
    _missesTotal = 0;
    _evictionsTotal = 0;
    _expiredTotal = 0;
    portEXIT_CRITICAL(&_spinlock);
}

DnsCacheStats DnsCache::getStats() {
    prune();
    DnsCacheStats s;
    portENTER_CRITICAL(&_spinlock);
    s.totalHits = _hitsTotal;
    s.totalMisses = _missesTotal;
    uint32_t active = 0;
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (_table[i].valid) active++;
    }
    s.activeEntries = active;
    s.capacity = DNS_CACHE_MAX_ENTRIES;
    s.totalEvictions = _evictionsTotal;
    s.totalExpired = _expiredTotal;
    uint32_t inflight = 0;
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        if (_inflight[i].active) inflight++;
    }
    s.inFlightRequests = inflight;
    s.staticSramBytes = getStaticMemoryUsage();
    portEXIT_CRITICAL(&_spinlock);
    return s;
}

DnsEntrySnapshot DnsCache::getEntries() {
    prune();
    DnsEntrySnapshot snap;
    portENTER_CRITICAL(&_spinlock);
    for (size_t i = 0; i < DNS_CACHE_MAX_ENTRIES; i++) {
        if (_table[i].valid) {
            snap.add(_table[i]);
        }
    }
    portEXIT_CRITICAL(&_spinlock);
    return snap;
}

uint32_t DnsCache::getActiveInFlightCount() const {
    uint32_t active = 0;
    portENTER_CRITICAL(&_spinlock);
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        if (_inflight[i].active) active++;
    }
    portEXIT_CRITICAL(&_spinlock);
    return active;
}

size_t DnsCache::getStaticMemoryUsage() const {
    return sizeof(_table) + sizeof(_inflight);
}

// ==============================================================================
// 5. Wire-Format DNS Label Parser & Response Builders
// ==============================================================================

bool DnsCache::parse_dns_labels(const uint8_t* buffer, size_t buf_len, size_t offset,
                                char* out_domain, size_t max_out_len, size_t* out_next_offset) {
    if (!buffer || !out_domain || max_out_len == 0 || offset < 12 || offset >= buf_len) {
        if (out_domain && max_out_len > 0) out_domain[0] = '\0';
        return false;
    }

    size_t curr = offset;
    size_t out_pos = 0;
    bool jumped = false;
    size_t jump_return_offset = 0;
    int jumps = 0;

    while (curr < buf_len) {
        uint8_t len = buffer[curr];
        if (len == 0) {
            if (out_next_offset) {
                *out_next_offset = jumped ? jump_return_offset : (curr + 1);
            }
            out_domain[out_pos] = '\0';
            return (out_pos > 0);
        }

        if ((len & 0xC0) == 0xC0) {
            if (curr + 1 >= buf_len) {
                out_domain[0] = '\0';
                return false;
            }
            uint16_t ptr = ((uint16_t)(len & 0x3F) << 8) | buffer[curr + 1];
            // Require strictly backward compression pointers inside DNS payload (>= 12)
            if (ptr < 12 || ptr >= curr) {
                out_domain[0] = '\0';
                return false;
            }
            if (!jumped) {
                jump_return_offset = curr + 2;
                jumped = true;
            }
            if (++jumps > 8) {
                out_domain[0] = '\0';
                return false;
            }
            curr = ptr;
            continue;
        }

        if ((len & 0xC0) != 0 || len > 63) {
            out_domain[0] = '\0';
            return false;
        }

        curr++;
        if (curr + len > buf_len) {
            out_domain[0] = '\0';
            return false;
        }

        if (out_pos > 0) {
            if (out_pos + 1 >= max_out_len) {
                out_domain[0] = '\0';
                return false;
            }
            out_domain[out_pos++] = '.';
        }

        for (uint8_t i = 0; i < len; i++) {
            if (out_pos + 1 >= max_out_len) {
                out_domain[0] = '\0';
                return false;
            }
            char c = (char)buffer[curr + i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out_domain[out_pos++] = c;
        }
        curr += len;
    }

    out_domain[0] = '\0';
    return false;
}

size_t DnsCache::buildDnsAResponse(const uint8_t* query, size_t questionEndOffset, IPAddress ip,
                                   uint32_t ttlSec, uint8_t* outResp, size_t maxRespLen) {
    if (!query || !outResp || questionEndOffset < 17 || questionEndOffset + 16 > maxRespLen) {
        return 0;
    }

    // Copy header + clean Question section ONLY (strips EDNS0 OPT so ARCOUNT=0 is RFC-valid)
    memcpy(outResp, query, questionEndOffset);
    outResp[2] = 0x81 | (query[2] & 0x01); // QR=1, preserve RD
    outResp[3] = 0x80;                     // RA=1, RCODE=0 (NOERROR)
    outResp[4] = 0x00; outResp[5] = 0x01;  // QDCOUNT = 1
    outResp[6] = 0x00; outResp[7] = 0x01;  // ANCOUNT = 1
    outResp[8] = 0x00; outResp[9] = 0x00;  // NSCOUNT = 0
    outResp[10] = 0x00; outResp[11] = 0x00; // ARCOUNT = 0

    size_t pos = questionEndOffset;
    outResp[pos++] = 0xc0; outResp[pos++] = 0x0c; // Pointer to QNAME
    outResp[pos++] = 0x00; outResp[pos++] = 0x01; // Type A
    outResp[pos++] = 0x00; outResp[pos++] = 0x01; // Class IN
    outResp[pos++] = (uint8_t)((ttlSec >> 24) & 0xFF);
    outResp[pos++] = (uint8_t)((ttlSec >> 16) & 0xFF);
    outResp[pos++] = (uint8_t)((ttlSec >> 8) & 0xFF);
    outResp[pos++] = (uint8_t)(ttlSec & 0xFF);
    outResp[pos++] = 0x00; outResp[pos++] = 0x04; // RDLENGTH 4
    outResp[pos++] = ip[0];
    outResp[pos++] = ip[1];
    outResp[pos++] = ip[2];
    outResp[pos++] = ip[3];

    return pos;
}

size_t DnsCache::buildDnsNoDataResponse(const uint8_t* query, size_t questionEndOffset,
                                        uint8_t* outResp, size_t maxRespLen) {
    if (!query || !outResp || questionEndOffset < 17 || questionEndOffset > maxRespLen) {
        return 0;
    }
    memcpy(outResp, query, questionEndOffset);
    outResp[2] = 0x81 | (query[2] & 0x01); // QR=1, preserve RD
    outResp[3] = 0x80;                     // RA=1, RCODE=0 (NOERROR / NODATA)
    outResp[4] = 0x00; outResp[5] = 0x01;  // QDCOUNT = 1
    outResp[6] = 0x00; outResp[7] = 0x00;  // ANCOUNT = 0
    outResp[8] = 0x00; outResp[9] = 0x00;  // NSCOUNT = 0
    outResp[10] = 0x00; outResp[11] = 0x00; // ARCOUNT = 0
    return questionEndOffset;
}

// ==============================================================================
// 6. Non-Blocking Asynchronous UDP DNS Proxy Worker
// ==============================================================================

uint16_t DnsCache::generateUniqueUpstreamTxid() {
    for (int attempt = 0; attempt < 16; attempt++) {
        uint16_t cand = (uint16_t)(esp_random() & 0xFFFF);
        if (cand == 0) cand = 1;
        bool clash = false;
        for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
            if (_inflight[i].active && _inflight[i].upstream_txid == cand) {
                clash = true;
                break;
            }
        }
        if (!clash) return cand;
    }
    return (uint16_t)((esp_random() | 1) & 0xFFFF);
}

int DnsCache::allocateInFlightSlot(uint32_t now) {
    int oldestSlot = -1;
    uint32_t maxAge = 0;

    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        if (!_inflight[i].active) {
            return i;
        }
        uint32_t age = (uint32_t)(now - _inflight[i].dispatch_ms);
        if (age > DNS_INFLIGHT_TIMEOUT_MS) {
            _inflight[i].active = false;
            return i;
        }
        if (age >= maxAge) {
            maxAge = age;
            oldestSlot = i;
        }
    }

    if (oldestSlot >= 0) {
        _inflight[oldestSlot].active = false;
        return oldestSlot;
    }
    return -1;
}

void DnsCache::pruneInFlight(uint32_t now) {
    portENTER_CRITICAL(&_spinlock);
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        if (_inflight[i].active && ((uint32_t)(now - _inflight[i].dispatch_ms) > DNS_INFLIGHT_TIMEOUT_MS)) {
            _inflight[i].active = false;
        }
    }
    portEXIT_CRITICAL(&_spinlock);
}

void DnsCache::handleClientQuery(const uint8_t* buf, int len, const struct sockaddr_in& client_addr, socklen_t addr_len) {
    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    if (qdcount == 0) return;

    char domain[DNS_MAX_DOMAIN_LEN];
    size_t nextOffset = 0;
    if (!parse_dns_labels(buf, (size_t)len, 12, domain, sizeof(domain), &nextOffset)) {
        return;
    }
    if (nextOffset + 4 > (size_t)len) return;

    uint16_t qtype = ((uint16_t)buf[nextOffset] << 8) | buf[nextOffset + 1];
    uint16_t qclass = ((uint16_t)buf[nextOffset + 2] << 8) | buf[nextOffset + 3];
    size_t questionEndOffset = nextOffset + 4;

    // 1. AAAA (QTYPE 28) on IPv4-only NAPT: instant RFC 4074 NODATA (prevents Happy Eyeballs stalls)
    if (qtype == 28 && qclass == 1) {
        uint8_t resp[512];
        size_t respLen = buildDnsNoDataResponse(buf, questionEndOffset, resp, sizeof(resp));
        if (respLen > 0) {
            sendto(_local_sock, resp, respLen, 0, (struct sockaddr*)&client_addr, addr_len);
        }
        return;
    }

    // 2. IPv4 Type-A SRAM Cache lookup
    if (qtype == 1 && qclass == 1) {
        IPAddress cachedIp;
        uint32_t remTtl = DNS_CACHE_DEFAULT_TTL;
        if (lookup(domain, cachedIp, &remTtl)) {
            uint8_t resp[512];
            size_t respLen = buildDnsAResponse(buf, questionEndOffset, cachedIp, remTtl, resp, sizeof(resp));
            if (respLen > 0) {
                sendto(_local_sock, resp, respLen, 0, (struct sockaddr*)&client_addr, addr_len);
            }
            return;
        }
    }

    // 3. Cache Miss (or non-A/AAAA query): forward asynchronously with randomized TXID
    uint16_t clientTxid = ((uint16_t)buf[0] << 8) | buf[1];
    uint32_t now = millis();
    uint16_t upstreamTxid = 0;

    portENTER_CRITICAL(&_spinlock);
    int slot = allocateInFlightSlot(now);
    if (slot >= 0) {
        upstreamTxid = generateUniqueUpstreamTxid();
        _inflight[slot].active = true;
        _inflight[slot].client_txid = clientTxid;
        _inflight[slot].upstream_txid = upstreamTxid;
        _inflight[slot].qtype = qtype;
        _inflight[slot].client_addr = client_addr;
        _inflight[slot].client_addr_len = addr_len;
        _inflight[slot].dispatch_ms = now;
        strncpy(_inflight[slot].domain, domain, sizeof(_inflight[slot].domain) - 1);
        _inflight[slot].domain[sizeof(_inflight[slot].domain) - 1] = '\0';
    }
    portEXIT_CRITICAL(&_spinlock);

    if (slot < 0) return;

    uint8_t forwardBuf[512];
    int fwdLen = (len > (int)sizeof(forwardBuf)) ? (int)sizeof(forwardBuf) : len;
    memcpy(forwardBuf, buf, fwdLen);
    forwardBuf[0] = (upstreamTxid >> 8) & 0xFF;
    forwardBuf[1] = upstreamTxid & 0xFF;

    IPAddress upstreamDns = WiFi.dnsIP();
    if ((uint32_t)upstreamDns == 0) {
        upstreamDns = IPAddress(8, 8, 8, 8);
    }

    struct sockaddr_in up_dest;
    memset(&up_dest, 0, sizeof(up_dest));
    up_dest.sin_family = AF_INET;
    up_dest.sin_addr.s_addr = (uint32_t)upstreamDns;
    up_dest.sin_port = htons(53);
    sendto(_upstream_sock, forwardBuf, fwdLen, 0, (struct sockaddr*)&up_dest, sizeof(up_dest));

    if (upstreamDns != IPAddress(8, 8, 8, 8)) {
        struct sockaddr_in g_dest;
        memset(&g_dest, 0, sizeof(g_dest));
        g_dest.sin_family = AF_INET;
        g_dest.sin_addr.s_addr = inet_addr("8.8.8.8");
        g_dest.sin_port = htons(53);
        sendto(_upstream_sock, forwardBuf, fwdLen, 0, (struct sockaddr*)&g_dest, sizeof(g_dest));
    }
}

void DnsCache::handleUpstreamResponse(const uint8_t* buf, int len, const struct sockaddr_in& from_addr) {
    if (len < 12) return;

    // Validate upstream source port and IP
    if (ntohs(from_addr.sin_port) != 53) return;
    uint32_t upIp = (uint32_t)WiFi.dnsIP();
    uint32_t gIp = inet_addr("8.8.8.8");
    if (from_addr.sin_addr.s_addr != gIp && (upIp == 0 || from_addr.sin_addr.s_addr != upIp)) {
        return;
    }

    uint16_t upTxid = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t clientTxid = 0;
    uint16_t qtype = 0;
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = 0;
    char expectedDomain[DNS_MAX_DOMAIN_LEN] = {0};

    portENTER_CRITICAL(&_spinlock);
    int slot = -1;
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        if (_inflight[i].active && _inflight[i].upstream_txid == upTxid) {
            slot = i;
            clientTxid = _inflight[i].client_txid;
            qtype = _inflight[i].qtype;
            clientAddr = _inflight[i].client_addr;
            clientAddrLen = _inflight[i].client_addr_len;
            strncpy(expectedDomain, _inflight[i].domain, sizeof(expectedDomain) - 1);
            break;
        }
    }
    portEXIT_CRITICAL(&_spinlock);

    if (slot < 0) return;

    // Verify QNAME in response matches original query before accepting/caching
    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    uint16_t ancount = ((uint16_t)buf[6] << 8) | buf[7];
    uint8_t rcode = buf[3] & 0x0F;

    size_t pos = 12;
    if (qdcount >= 1) {
        char respDomain[DNS_MAX_DOMAIN_LEN];
        size_t qNext = 0;
        if (!parse_dns_labels(buf, (size_t)len, 12, respDomain, sizeof(respDomain), &qNext)) {
            return;
        }
        if (strcasecmp(respDomain, expectedDomain) != 0) {
            return;
        }
        pos = qNext + 4;
    }

    // Parse Answer RRs structurally for Type A records when RCODE == 0
    if (rcode == 0 && qtype == 1 && ancount >= 1 && pos < (size_t)len) {
        for (uint16_t a = 0; a < ancount && pos < (size_t)len; a++) {
            char rrName[DNS_MAX_DOMAIN_LEN];
            size_t rrNext = 0;
            if (!parse_dns_labels(buf, (size_t)len, pos, rrName, sizeof(rrName), &rrNext)) {
                break;
            }
            if (rrNext + 10 > (size_t)len) break;

            uint16_t rtype = ((uint16_t)buf[rrNext] << 8) | buf[rrNext + 1];
            uint16_t rclass = ((uint16_t)buf[rrNext + 2] << 8) | buf[rrNext + 3];
            uint32_t rttl = ((uint32_t)buf[rrNext + 4] << 24) |
                            ((uint32_t)buf[rrNext + 5] << 16) |
                            ((uint32_t)buf[rrNext + 6] << 8) |
                            (uint32_t)buf[rrNext + 7];
            uint16_t rdlen = ((uint16_t)buf[rrNext + 8] << 8) | buf[rrNext + 9];

            size_t rdataPos = rrNext + 10;
            if (rdataPos + rdlen > (size_t)len) break;

            if (rtype == 1 && rclass == 1 && rdlen == 4) {
                IPAddress resolved(buf[rdataPos], buf[rdataPos + 1], buf[rdataPos + 2], buf[rdataPos + 3]);
                if ((uint32_t)resolved != 0) {
                    insert(expectedDomain, resolved, rttl);
                }
                break;
            }
            pos = rdataPos + rdlen;
        }
    }

    // Restore client TXID and relay response
    uint8_t clientBuf[512];
    int sendLen = (len > (int)sizeof(clientBuf)) ? (int)sizeof(clientBuf) : len;
    memcpy(clientBuf, buf, sendLen);
    clientBuf[0] = (clientTxid >> 8) & 0xFF;
    clientBuf[1] = clientTxid & 0xFF;

    sendto(_local_sock, clientBuf, sendLen, 0,
           (struct sockaddr*)&clientAddr, clientAddrLen);

    portENTER_CRITICAL(&_spinlock);
    if (_inflight[slot].active && _inflight[slot].upstream_txid == upTxid) {
        _inflight[slot].active = false;
    }
    portEXIT_CRITICAL(&_spinlock);
}

void DnsCache::pollAsync(uint32_t timeoutMs) {
    if (_local_sock < 0 && _upstream_sock < 0) return;

    fd_set rfds;
    FD_ZERO(&rfds);
    int max_fd = -1;

    if (_local_sock >= 0) {
        FD_SET(_local_sock, &rfds);
        if (_local_sock > max_fd) max_fd = _local_sock;
    }
    if (_upstream_sock >= 0) {
        FD_SET(_upstream_sock, &rfds);
        if (_upstream_sock > max_fd) max_fd = _upstream_sock;
    }
    if (max_fd < 0) return;

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);
    uint32_t now = millis();

    if (ret > 0) {
        if (_local_sock >= 0 && FD_ISSET(_local_sock, &rfds)) {
            for (int i = 0; i < 8; i++) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                uint8_t rx_buf[512];
                int n = recvfrom(_local_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr*)&client_addr, &addr_len);
                if (n < 0) break;
                if (n >= 12 && (rx_buf[2] & 0x80) == 0) {
                    handleClientQuery(rx_buf, n, client_addr, addr_len);
                }
            }
        }

        if (_upstream_sock >= 0 && FD_ISSET(_upstream_sock, &rfds)) {
            for (int i = 0; i < 8; i++) {
                struct sockaddr_in from_addr;
                socklen_t from_len = sizeof(from_addr);
                uint8_t rx_buf[512];
                int n = recvfrom(_upstream_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr*)&from_addr, &from_len);
                if (n < 0) break;
                if (n >= 12 && (rx_buf[2] & 0x80) != 0) {
                    handleUpstreamResponse(rx_buf, n, from_addr);
                }
            }
        }
    }

    pruneInFlight(now);
}

void DnsCache::dnsProxyTask(void* param) {
    DnsCache* self = (DnsCache*)param;
    while (self->_proxy_running) {
        self->pollAsync(10);
    }
    self->_proxy_task_handle = NULL;
    self->_task_exited = true;
    vTaskDelete(NULL);
}

bool DnsCache::startAsyncProxy(uint16_t listenPort) {
    if (_proxy_running && _local_port == listenPort) {
        return true;
    }
    stopAsyncProxy();
    _local_port = listenPort;

    _local_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_local_sock < 0) {
        apLog("[DNS-PROXY] Failed to create local socket\n");
        return false;
    }

    int opt = 1;
    setsockopt(_local_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(_local_sock, F_GETFL, 0);
    fcntl(_local_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(_local_port);

    if (bind(_local_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        apLog("[DNS-PROXY] Failed to bind local socket on port %u\n", _local_port);
        close(_local_sock);
        _local_sock = -1;
        return false;
    }

    _upstream_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_upstream_sock < 0) {
        close(_local_sock);
        _local_sock = -1;
        return false;
    }

    flags = fcntl(_upstream_sock, F_GETFL, 0);
    fcntl(_upstream_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in up_bind;
    memset(&up_bind, 0, sizeof(up_bind));
    up_bind.sin_family = AF_INET;
    up_bind.sin_addr.s_addr = htonl(INADDR_ANY);
    up_bind.sin_port = htons(0);

    if (bind(_upstream_sock, (struct sockaddr*)&up_bind, sizeof(up_bind)) < 0) {
        close(_local_sock);
        close(_upstream_sock);
        _local_sock = -1;
        _upstream_sock = -1;
        return false;
    }

    portENTER_CRITICAL(&_spinlock);
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        _inflight[i].active = false;
    }
    portEXIT_CRITICAL(&_spinlock);

    _proxy_running = true;
    _task_exited = false;

    BaseType_t res = xTaskCreatePinnedToCore(
        dnsProxyTask,
        "AP_DnsProxy",
        4096,
        this,
        3,
        &_proxy_task_handle,
        1
    );

    if (res != pdPASS) {
        _proxy_task_handle = NULL;
        _task_exited = true;
        _proxy_running = false;
        close(_local_sock);
        close(_upstream_sock);
        _local_sock = -1;
        _upstream_sock = -1;
        return false;
    }

    apLog("[DNS-PROXY] Async UDP DNS proxy active on Core 1 (Port %u)\n", _local_port);
    return true;
}

void DnsCache::stopAsyncProxy() {
    _proxy_running = false;
    if (_proxy_task_handle != NULL) {
        uint32_t waitStart = millis();
        while (!_task_exited && (millis() - waitStart < 150)) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        _proxy_task_handle = NULL;
    }
    if (_local_sock >= 0) {
        close(_local_sock);
        _local_sock = -1;
    }
    if (_upstream_sock >= 0) {
        close(_upstream_sock);
        _upstream_sock = -1;
    }
    portENTER_CRITICAL(&_spinlock);
    for (int i = 0; i < DNS_INFLIGHT_MAX_SLOTS; i++) {
        _inflight[i].active = false;
    }
    portEXIT_CRITICAL(&_spinlock);
}
