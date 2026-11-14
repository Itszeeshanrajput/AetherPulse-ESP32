#pragma once

#include "config.h"
#include <vector>

struct DnsCacheEntry {
    String domain;
    IPAddress ip;
    unsigned long expireTimeMs;
    uint32_t hits;
};

struct DnsCacheStats {
    uint32_t totalHits;
    uint32_t totalMisses;
    uint32_t activeEntries;
    uint32_t capacity;
};

class DnsCache {
public:
    static DnsCache& instance();

    void init();
    bool lookup(const String& domain, IPAddress& outIp);
    void insert(const String& domain, IPAddress ip, uint32_t ttlSec = DNS_CACHE_DEFAULT_TTL);
    void prune();
    void clear();

    DnsCacheStats getStats();
    std::vector<DnsCacheEntry> getEntries();

private:
    DnsCache();
    std::vector<DnsCacheEntry> cacheTable;
    uint32_t hitsTotal;
    uint32_t missesTotal;
};
