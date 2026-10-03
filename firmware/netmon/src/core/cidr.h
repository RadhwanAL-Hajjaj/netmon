#pragma once
// Pure logic — no Arduino headers. Addresses are host-order uint32_t.
#include <cstdint>
#include <cstddef>

inline uint32_t ipv4_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

inline bool ipv4_parse(const char* s, uint32_t& out) {
    if (s == nullptr || *s == '\0') return false;
    uint32_t parts[4] = {0, 0, 0, 0};
    size_t idx = 0;
    int digits = 0;
    uint32_t acc = 0;
    for (const char* p = s;; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            acc = acc * 10 + static_cast<uint32_t>(*p - '0');
            if (acc > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0) return false;
            if (idx > 3) return false;
            parts[idx++] = acc;
            acc = 0;
            digits = 0;
            if (*p == '\0') break;
        } else {
            return false;
        }
    }
    if (idx != 4) return false;
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

inline void ipv4_format(uint32_t ip, char out[16]) {
    const uint32_t o[4] = {(ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                           (ip >> 8) & 0xFF, ip & 0xFF};
    size_t w = 0;
    for (size_t i = 0; i < 4; ++i) {
        uint32_t v = o[i];
        if (v >= 100) out[w++] = static_cast<char>('0' + (v / 100));
        if (v >= 10) out[w++] = static_cast<char>('0' + ((v / 10) % 10));
        out[w++] = static_cast<char>('0' + (v % 10));
        if (i != 3) out[w++] = '.';
    }
    out[w] = '\0';
}

inline uint8_t mask_to_prefix(uint32_t mask) {
    uint8_t n = 0;
    while (mask & 0x80000000u) {
        ++n;
        mask <<= 1;
    }
    return n;
}

inline uint32_t subnet_of(uint32_t ip, uint32_t mask) {
    return ip & mask;
}

inline void cidr_format(uint32_t ip, uint32_t mask, char out[20]) {
    char addr[16];
    ipv4_format(subnet_of(ip, mask), addr);
    size_t w = 0;
    for (const char* p = addr; *p; ++p) out[w++] = *p;
    out[w++] = '/';
    uint8_t prefix = mask_to_prefix(mask);
    if (prefix >= 10) out[w++] = static_cast<char>('0' + (prefix / 10));
    out[w++] = static_cast<char>('0' + (prefix % 10));
    out[w] = '\0';
}

inline bool subnet_contains(uint32_t subnet, uint32_t mask, uint32_t ip) {
    return (ip & mask) == (subnet & mask);
}

// Sweep range excludes the network and broadcast addresses.
inline uint32_t host_first(uint32_t subnet, uint32_t mask) {
    return (subnet & mask) + 1;
}

inline uint32_t host_last(uint32_t subnet, uint32_t mask) {
    const uint32_t broadcast = (subnet & mask) | (~mask);
    return broadcast > 0 ? broadcast - 1 : 0;
}
