#include "dns_cache.h"

DnsCache& DnsCache::instance() {
    static DnsCache instance;
    return instance;
}

DnsCache::DnsCache() : hitsTotal(0), missesTotal(0) {
    cacheTable.reserve(DNS_CACHE_MAX_ENTRIES);
}

void DnsCache::init() {
    clear();
    Serial.println("[DNS-CACHE] Sub-Millisecond SRAM DNS Acceleration Daemon initialized (Capacity: 64 hosts).");
}

bool DnsCache::lookup(const String& domain, IPAddress& outIp) {
    unsigned long now = millis();
    String d = domain;
    d.toLowerCase();

    for (size_t i = 0; i < cacheTable.size(); i++) {
        if (cacheTable[i].domain == d) {
            if (now < cacheTable[i].expireTimeMs) {
                outIp = cacheTable[i].ip;
                cacheTable[i].hits++;
                hitsTotal++;
                return true;
            } else {
                cacheTable.erase(cacheTable.begin() + i);
                break;
            }
        }
    }

    missesTotal++;
    return false;
}

void DnsCache::insert(const String& domain, IPAddress ip, uint32_t ttlSec) {
    if (domain.length() == 0 || ip == IPAddress(0, 0, 0, 0)) return;

    unsigned long now = millis();
    String d = domain;
    d.toLowerCase();
    unsigned long expire = now + (ttlSec * 1000UL);

    for (auto& entry : cacheTable) {
        if (entry.domain == d) {
            entry.ip = ip;
            entry.expireTimeMs = expire;
            return;
        }
    }

    if (cacheTable.size() >= DNS_CACHE_MAX_ENTRIES) {
        prune();
    }

    if (cacheTable.size() >= DNS_CACHE_MAX_ENTRIES) {
        size_t lfuIdx = 0;
        uint32_t minHits = 0xFFFFFFFF;
        for (size_t i = 0; i < cacheTable.size(); i++) {
            if (cacheTable[i].hits < minHits) {
                minHits = cacheTable[i].hits;
                lfuIdx = i;
            }
        }
        cacheTable.erase(cacheTable.begin() + lfuIdx);
    }

    DnsCacheEntry newEntry;
    newEntry.domain = d;
    newEntry.ip = ip;
    newEntry.expireTimeMs = expire;
    newEntry.hits = 1;
    cacheTable.push_back(newEntry);
}

void DnsCache::prune() {
    unsigned long now = millis();
    for (auto it = cacheTable.begin(); it != cacheTable.end();) {
        if (now >= it->expireTimeMs) {
            it = cacheTable.erase(it);
        } else {
            ++it;
        }
    }
}

void DnsCache::clear() {
    cacheTable.clear();
    hitsTotal = 0;
    missesTotal = 0;
}

DnsCacheStats DnsCache::getStats() {
    prune();
    DnsCacheStats s;
    s.totalHits = hitsTotal;
    s.totalMisses = missesTotal;
    s.activeEntries = cacheTable.size();
    s.capacity = DNS_CACHE_MAX_ENTRIES;
    return s;
}

std::vector<DnsCacheEntry> DnsCache::getEntries() {
    prune();
    return cacheTable;
}
