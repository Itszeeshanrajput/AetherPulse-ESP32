#pragma once

#include "config.h"
#include <Arduino.h>
#include <IPAddress.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_attr.h>
#include <vector>

// ==============================================================================
// AETHERPULSE v3.1.0 - ZERO-ALLOCATION SRAM DNS PROXY & CACHE ENGINE
// ==============================================================================

#define DNS_INFLIGHT_MAX_SLOTS      16
#define DNS_INFLIGHT_TIMEOUT_MS     2500
#define DNS_MAX_DOMAIN_LEN          128

struct StaticDomain {
    char data[DNS_MAX_DOMAIN_LEN];

    StaticDomain();
    explicit StaticDomain(const char* s);
    void set(const char* s);
    const char* c_str() const { return data; }
};

struct static_dns_entry_t {
    StaticDomain domain;
    IPAddress ip;
    uint32_t insert_time_ms;
    uint32_t ttl_ms;
    uint32_t hits;
    uint32_t hash;
    bool valid;

    static_dns_entry_t();
};

typedef static_dns_entry_t DnsCacheEntry;

struct DnsCacheStats {
    uint32_t totalHits;
    uint32_t totalMisses;
    uint32_t activeEntries;
    uint32_t capacity;
    uint32_t totalEvictions;
    uint32_t totalExpired;
    uint32_t inFlightRequests;
    uint32_t staticSramBytes;
};

class DnsEntrySnapshot {
private:
    std::vector<static_dns_entry_t> _entries;

public:
    DnsEntrySnapshot() = default;
    void reserve(size_t n) { _entries.reserve(n); }
    void add(const static_dns_entry_t& e) { _entries.push_back(e); }
    bool empty() const { return _entries.empty(); }
    size_t size() const { return _entries.size(); }
    std::vector<static_dns_entry_t>::const_iterator begin() const { return _entries.begin(); }
    std::vector<static_dns_entry_t>::const_iterator end() const { return _entries.end(); }
};

struct dns_inflight_slot_t {
    bool active;
    uint16_t client_txid;
    uint16_t upstream_txid;
    uint16_t qtype;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    uint32_t dispatch_ms;
    char domain[DNS_MAX_DOMAIN_LEN];
};

class DnsCache {
public:
    static DnsCache& instance();

    void init();
    void prune();
    void clear();

    bool lookup(const char* domain, IPAddress& outIp, uint32_t* outRemTtlSec = nullptr);
    void insert(const char* domain, IPAddress ip, uint32_t ttlSec = DNS_CACHE_DEFAULT_TTL);

    static bool parse_dns_labels(const uint8_t* buffer, size_t buf_len, size_t offset,
                                 char* out_domain, size_t max_out_len, size_t* out_next_offset = nullptr);
    static size_t buildDnsAResponse(const uint8_t* query, size_t questionEndOffset, IPAddress ip,
                                    uint32_t ttlSec, uint8_t* outResp, size_t maxRespLen);
    static size_t buildDnsNoDataResponse(const uint8_t* query, size_t questionEndOffset,
                                         uint8_t* outResp, size_t maxRespLen);

    bool startAsyncProxy(uint16_t listenPort = 53);
    void stopAsyncProxy();
    void pollAsync(uint32_t timeoutMs = 0);
    bool isProxyRunning() const { return _proxy_running; }

    DnsCacheStats getStats();
    DnsEntrySnapshot getEntries();
    uint32_t getActiveInFlightCount() const;
    size_t getStaticMemoryUsage() const;

private:
    DnsCache();
    ~DnsCache();

    static_dns_entry_t _table[DNS_CACHE_MAX_ENTRIES];
    dns_inflight_slot_t _inflight[DNS_INFLIGHT_MAX_SLOTS];

    int _local_sock;
    int _upstream_sock;
    uint16_t _local_port;
    volatile bool _proxy_running;
    volatile bool _task_exited;
    TaskHandle_t _proxy_task_handle;

    mutable portMUX_TYPE _spinlock;

    uint32_t _hitsTotal;
    uint32_t _missesTotal;
    uint32_t _evictionsTotal;
    uint32_t _expiredTotal;

    int allocateInFlightSlot(uint32_t now);
    void pruneInFlight(uint32_t now);
    uint16_t generateUniqueUpstreamTxid();
    void handleClientQuery(const uint8_t* buf, int len, const struct sockaddr_in& client_addr, socklen_t addr_len);
    void handleUpstreamResponse(const uint8_t* buf, int len, const struct sockaddr_in& from_addr);

    static void dnsProxyTask(void* param);
};
