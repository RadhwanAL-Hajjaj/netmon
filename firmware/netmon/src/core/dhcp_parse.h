#pragma once
// Pure logic — no Arduino headers.
//
// Walks a captured 802.11 frame down to DHCP options:
//   802.11 data header -> LLC/SNAP -> IPv4 -> UDP(67/68) -> DHCP
// Every step bounds-checks before reading. These frames come off the air
// from devices we do not control, and this code runs in a WiFi callback
// where an over-read corrupts memory rather than throwing.
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "mac.h"

struct DhcpInfo {
    Mac client;
    uint32_t requested_ip;
    uint32_t server_ip;
    uint32_t lease_s;
    uint8_t msg_type;
    char hostname[64];
    bool has_hostname;
};

// A DHCP message from its first byte, as a UDP socket delivers it: the
// 236-byte BOOTP header, the magic cookie, then options. It comes from a
// device we do not control, so every read is bounds-checked first.
inline bool dhcp_from_bootp(const uint8_t* d, size_t dlen, DhcpInfo& out) {
    std::memset(&out, 0, sizeof(out));
    if (d == nullptr || dlen < 240) return false;

    if (!(d[236] == 0x63 && d[237] == 0x82 && d[238] == 0x53 && d[239] == 0x63)) {
        return false;
    }
    std::memcpy(out.client.b, d + 28, 6);
    // yiaddr is the address assigned by a DHCP OFFER/ACK. It is useful when
    // option 50 is absent, which is common outside a DHCP REQUEST.
    out.requested_ip = (static_cast<uint32_t>(d[16]) << 24) |
                       (static_cast<uint32_t>(d[17]) << 16) |
                       (static_cast<uint32_t>(d[18]) << 8) |
                       static_cast<uint32_t>(d[19]);

    size_t i = 240;
    while (i < dlen) {
        const uint8_t code = d[i];
        if (code == 255) break;                         // END
        if (code == 0) { ++i; continue; }               // PAD
        if (i + 1 >= dlen) return false;
        const uint8_t olen = d[i + 1];
        if (i + 2 + olen > dlen) return false;          // option overruns frame
        const uint8_t* val = d + i + 2;

        if (code == 53 && olen >= 1) {
            out.msg_type = val[0];
        } else if (code == 50 && olen == 4) {
            out.requested_ip = (static_cast<uint32_t>(val[0]) << 24) |
                               (static_cast<uint32_t>(val[1]) << 16) |
                               (static_cast<uint32_t>(val[2]) << 8) |
                               static_cast<uint32_t>(val[3]);
        } else if (code == 54 && olen == 4) {
            out.server_ip = (static_cast<uint32_t>(val[0]) << 24) |
                             (static_cast<uint32_t>(val[1]) << 16) |
                             (static_cast<uint32_t>(val[2]) << 8) |
                             static_cast<uint32_t>(val[3]);
        } else if (code == 51 && olen == 4) {
            out.lease_s = (static_cast<uint32_t>(val[0]) << 24) |
                          (static_cast<uint32_t>(val[1]) << 16) |
                          (static_cast<uint32_t>(val[2]) << 8) |
                          static_cast<uint32_t>(val[3]);
        } else if (code == 12 && olen > 0) {
            size_t copy = olen;
            if (copy > sizeof(out.hostname) - 1) copy = sizeof(out.hostname) - 1;
            std::memcpy(out.hostname, val, copy);
            out.hostname[copy] = '\0';
            out.has_hostname = true;
        }
        i += 2u + olen;
    }
    // A client renewing or rebinding its lease names its address in ciaddr
    // and sends no option 50.
    if (out.requested_ip == 0) {
        out.requested_ip = (static_cast<uint32_t>(d[12]) << 24) |
                           (static_cast<uint32_t>(d[13]) << 16) |
                           (static_cast<uint32_t>(d[14]) << 8) |
                           static_cast<uint32_t>(d[15]);
    }
    return true;
}

inline bool dhcp_from_ieee80211(const uint8_t* f, size_t len, DhcpInfo& out) {
    std::memset(&out, 0, sizeof(out));
    if (f == nullptr || len < 24) return false;

    // Frame control: bits 2-3 type, bits 4-7 subtype. Type 2 is data.
    const uint8_t type = static_cast<uint8_t>((f[0] >> 2) & 0x03);
    const uint8_t subtype = static_cast<uint8_t>((f[0] >> 4) & 0x0F);
    if (type != 2) return false;

    size_t o = 24;
    const uint8_t to_ds = f[1] & 0x01;
    const uint8_t from_ds = static_cast<uint8_t>((f[1] >> 1) & 0x01);
    if (to_ds && from_ds) o += 6;          // 4-address format
    if (subtype & 0x08) o += 2;            // QoS data carries a QoS control field
    if (o + 8 > len) return false;

    // LLC/SNAP with an IPv4 ethertype.
    if (!(f[o] == 0xAA && f[o + 1] == 0xAA && f[o + 2] == 0x03)) return false;
    if (!(f[o + 6] == 0x08 && f[o + 7] == 0x00)) return false;
    o += 8;

    if (o + 20 > len) return false;
    if ((f[o] >> 4) != 4) return false;                 // IPv4
    const size_t ihl = static_cast<size_t>(f[o] & 0x0F) * 4u;
    if (ihl < 20 || o + ihl > len) return false;
    if (f[o + 9] != 17) return false;                   // UDP
    o += ihl;

    if (o + 8 > len) return false;
    const uint16_t sport = static_cast<uint16_t>((f[o] << 8) | f[o + 1]);
    const uint16_t dport = static_cast<uint16_t>((f[o + 2] << 8) | f[o + 3]);
    const bool dhcp_ports = (sport == 67 || sport == 68) &&
                            (dport == 67 || dport == 68);
    if (!dhcp_ports) return false;
    o += 8;

    // BOOTP fixed header is 236 bytes, then a 4-byte magic cookie.
    if (o + 240 > len) return false;
    return dhcp_from_bootp(f + o, len - o, out);
}
