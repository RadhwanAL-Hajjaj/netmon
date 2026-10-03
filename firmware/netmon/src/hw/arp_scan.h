#pragma once
#include <cstdint>
#include <cstddef>

#include "../core/mac.h"

struct ArpHit {
    uint32_t ip;
    Mac mac;
};

// Sends ARP requests for a batch of addresses. Returns false when there is
// no usable network interface (not associated, or SoftAP only).
bool arp_request_batch(const uint32_t* ips, size_t n);

// Reads whatever the ARP cache currently holds. Returns how many entries
// were written to `out`. The cache is small — see sweep.h for why the
// sweep is batched around that.
size_t arp_read_cache(ArpHit* out, size_t max);

// How many slots the ARP cache has, for diagnostics on /api/health.
size_t arp_cache_capacity();
