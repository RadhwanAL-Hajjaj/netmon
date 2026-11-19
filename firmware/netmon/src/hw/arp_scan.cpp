#include "arp_scan.h"

#include <Arduino.h>
#include <WiFi.h>

// lwIP's ARP table lives behind these. Paths are stable across ESP32 core
// 2.x and 3.x; if a build fails here, that is the first thing to check.
#include "lwip/dhcp.h"
#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"   // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE
#include "lwip/pbuf.h"
#include "freertos/FreeRTOS.h"
#include <cstring>

// ARP_TABLE_SIZE comes from lwip/opt.h (via etharp.h) and is 10 on ESP-IDF,
// which is not configurable from the Arduino core. The fallback only exists
// so this file still parses if the include order ever changes.
#ifndef ARP_TABLE_SIZE
#define ARP_TABLE_SIZE 10
#endif

// Everything below touches lwIP's raw API. The Arduino core builds lwIP with
// CONFIG_LWIP_TCPIP_CORE_LOCKING=y and CONFIG_LWIP_CHECK_THREAD_SAFETY=y, so a
// raw call made from loop() without the core lock hits
// LWIP_ASSERT_CORE_LOCKED() and aborts the firmware — a panic and reboot on the
// first scan tick, not a subtle race. LOCK_TCPIP_CORE() is the macro ESP-IDF
// redefines to both take the mutex and mark this task as the holder, which is
// what that assertion actually tests. On older cores built without core
// locking the macro compiles away to nothing, leaving the previous behaviour.
namespace {

class CoreLock {
public:
    CoreLock() { LOCK_TCPIP_CORE(); }
    ~CoreLock() { UNLOCK_TCPIP_CORE(); }
    CoreLock(const CoreLock&) = delete;
    CoreLock& operator=(const CoreLock&) = delete;
};

// Caller must already hold the core lock: walking netif_list is itself a raw
// lwIP access.
struct netif* sta_netif_locked() {
    // netif_default is the station interface once associated. In SoftAP-only
    // mode there is nothing worth sweeping, so a null return is correct
    // rather than exceptional.
    for (struct netif* n = netif_list; n != nullptr; n = n->next) {
        if (netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n))) {
            return n;
        }
    }
    return netif_default;
}

}  // namespace

bool arp_request_batch(const uint32_t* ips, size_t n) {
    if (ips == nullptr || n == 0) return false;

    CoreLock lock;
    struct netif* nif = sta_netif_locked();
    if (nif == nullptr) return false;

    for (size_t i = 0; i < n; ++i) {
        ip4_addr_t target;
        // lwIP stores addresses network-order; our core keeps host-order.
        IP4_ADDR(&target,
                 static_cast<uint8_t>((ips[i] >> 24) & 0xFF),
                 static_cast<uint8_t>((ips[i] >> 16) & 0xFF),
                 static_cast<uint8_t>((ips[i] >> 8) & 0xFF),
                 static_cast<uint8_t>(ips[i] & 0xFF));
        etharp_request(nif, &target);
    }
    return true;
}

size_t arp_read_cache(ArpHit* out, size_t max) {
    if (out == nullptr || max == 0) return 0;

    CoreLock lock;
    size_t written = 0;
    for (size_t i = 0; i < ARP_TABLE_SIZE && written < max; ++i) {
        ip4_addr_t* ip = nullptr;
        struct netif* nif = nullptr;
        struct eth_addr* eth = nullptr;

        if (etharp_get_entry(i, &ip, &nif, &eth) != 1) continue;
        if (ip == nullptr || eth == nullptr) continue;

        const uint32_t host_order = lwip_ntohl(ip4_addr_get_u32(ip));
        if (host_order == 0) continue;

        out[written].ip = host_order;
        for (size_t b = 0; b < 6; ++b) {
            out[written].mac.b[b] = eth->addr[b];
        }
        ++written;
    }
    return written;
}

size_t arp_cache_capacity() { return ARP_TABLE_SIZE; }

uint32_t lan_dhcp_server() {
    CoreLock lock;
    struct netif* nif = sta_netif_locked();
    if (nif == nullptr || !dhcp_supplied_address(nif)) return 0;
    const struct dhcp* d = netif_dhcp_data(nif);
    if (d == nullptr) return 0;
    return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&d->server_ip_addr)));
}

// ---- Passive ARP listening ------------------------------------------------
//
// The station netif's input function is wrapped. The Wi-Fi driver calls it
// from its own task for every received frame, before lwIP's thread sees the
// packet; the wrapper only reads the first 42 bytes of ARP frames, copies the
// sender into a ring under a spinlock, and hands the pbuf on untouched. It
// never frees, holds or changes the pbuf, so lwIP behaves exactly as before.
namespace {

netif_input_fn g_orig_input = nullptr;
struct netif* g_tapped = nullptr;

constexpr size_t kTapCap = 64;
ArpHit g_ring[kTapCap];
size_t g_head = 0;            // next slot to write
size_t g_count = 0;           // entries waiting
uint32_t g_total = 0;
uint32_t g_dropped = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

err_t tap_input(struct pbuf* p, struct netif* inp) {
    // Ethernet (14) + ARP for IPv4 over Ethernet (28) = 42 bytes, all in the
    // first pbuf: the driver hands frames over as one contiguous buffer.
    if (p != nullptr && p->len >= 42 && p->payload != nullptr) {
        const uint8_t* f = static_cast<const uint8_t*>(p->payload);
        if (f[12] == 0x08 && f[13] == 0x06 &&               // ARP
            f[14] == 0x00 && f[15] == 0x01 &&               // hw: Ethernet
            f[16] == 0x08 && f[17] == 0x00 &&               // proto: IPv4
            f[18] == 6 && f[19] == 4) {
            const uint32_t ip = (uint32_t(f[28]) << 24) | (uint32_t(f[29]) << 16) |
                                (uint32_t(f[30]) << 8) | uint32_t(f[31]);
            // 0.0.0.0 is an address probe from a device that has none yet.
            if (ip != 0) {
                portENTER_CRITICAL(&g_mux);
                ++g_total;
                if (g_count < kTapCap) {
                    ArpHit& h = g_ring[g_head];
                    h.ip = ip;
                    std::memcpy(h.mac.b, f + 22, 6);
                    g_head = (g_head + 1) % kTapCap;
                    ++g_count;
                } else {
                    ++g_dropped;
                }
                portEXIT_CRITICAL(&g_mux);
            }
        }
    }
    netif_input_fn next = g_orig_input;
    if (next == nullptr) {             // cannot happen once installed
        pbuf_free(p);
        return ERR_OK;
    }
    return next(p, inp);
}

}  // namespace

bool arp_tap_install() {
    CoreLock lock;
    struct netif* nif = sta_netif_locked();
    if (nif == nullptr) return false;
    if (nif->input == tap_input) return true;          // already wrapped
    if (nif->input == nullptr) return false;
    // Set the onward pointer before the wrapper can run.
    g_orig_input = nif->input;
    g_tapped = nif;
    nif->input = tap_input;
    return true;
}

size_t arp_tap_drain(ArpHit* out, size_t max) {
    if (out == nullptr || max == 0) return 0;
    size_t n = 0;
    portENTER_CRITICAL(&g_mux);
    while (g_count > 0 && n < max) {
        const size_t tail = (g_head + kTapCap - g_count) % kTapCap;
        out[n++] = g_ring[tail];
        --g_count;
    }
    portEXIT_CRITICAL(&g_mux);
    return n;
}

uint32_t arp_tap_total() { return g_total; }
uint32_t arp_tap_dropped() { return g_dropped; }
