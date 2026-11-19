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

// Passive ARP listening (0.16.1). Every ARP packet the station interface
// receives — replies to the sweep and the requests and announcements other
// devices broadcast — is copied into a small ring before lwIP sees it. The
// sweep used to learn only from lwIP's 10-slot ARP cache, so a reply that
// arrived after the 150 ms read, or was pushed out by the next batch, was
// lost. Install is idempotent and cheap; call it while connected.
bool arp_tap_install();
// Moves up to `max` captured senders into `out`; returns how many.
size_t arp_tap_drain(ArpHit* out, size_t max);
// Packets captured since boot, and how many were dropped because the ring
// was full between drains.
uint32_t arp_tap_total();
uint32_t arp_tap_dropped();

// How many slots the ARP cache has, for diagnostics on /api/health.
size_t arp_cache_capacity();

// The DHCP server that leased this board its own address, host order, or 0
// with a fixed address or no lease. The LAN watch counts it as expected.
uint32_t lan_dhcp_server();
