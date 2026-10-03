#pragma once
// Pure logic. No Arduino, no ESP-IDF — this header is compiled by g++ in
// the host tests, which is the only verification this firmware gets.
#include <cstdint>
#include <cstddef>

struct Mac {
    uint8_t b[6];
};

inline int mac_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Accepts colon, hyphen, dot separated or bare hex. Rejects anything that
// does not yield exactly 12 hex digits.
inline bool mac_parse(const char* s, Mac& out) {
    if (s == nullptr) return false;
    uint8_t nib[12];
    size_t n = 0;
    for (const char* p = s; *p; ++p) {
        if (*p == ':' || *p == '-' || *p == '.') continue;
        int v = mac_hexval(*p);
        if (v < 0) return false;
        if (n >= 12) return false;
        nib[n++] = static_cast<uint8_t>(v);
    }
    if (n != 12) return false;
    for (size_t i = 0; i < 6; ++i) {
        out.b[i] = static_cast<uint8_t>((nib[i * 2] << 4) | nib[i * 2 + 1]);
    }
    return true;
}

inline void mac_format(const Mac& m, char out[18]) {
    static const char* hex = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < 6; ++i) {
        out[o++] = hex[(m.b[i] >> 4) & 0x0F];
        out[o++] = hex[m.b[i] & 0x0F];
        if (i != 5) out[o++] = ':';
    }
    out[o] = '\0';
}

inline bool mac_equal(const Mac& a, const Mac& b) {
    for (size_t i = 0; i < 6; ++i) {
        if (a.b[i] != b.b[i]) return false;
    }
    return true;
}

// Bit 1 of the first octet marks a locally administered address: a
// randomised phone MAC rather than a manufacturer assignment. Such an
// address will never match a vendor registry.
inline bool mac_is_local(const Mac& m) {
    return (m.b[0] & 0x02) != 0;
}

inline uint32_t mac_oui(const Mac& m) {
    return (static_cast<uint32_t>(m.b[0]) << 16) |
           (static_cast<uint32_t>(m.b[1]) << 8) |
           static_cast<uint32_t>(m.b[2]);
}
