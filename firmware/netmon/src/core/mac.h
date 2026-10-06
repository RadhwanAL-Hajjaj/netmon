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

// --- The board's own Wi-Fi address, when the owner sets one (0.13) --------------

enum class MacRule : uint8_t { Ok, Unreadable, Group, Zero, SetupClash };

// An address the board may take for itself on Wi-Fi: one device's (bit 0 of
// the first octet clear: not a group address), not all zeros, and not the
// address the board's own setup network uses, which the Wi-Fi stack needs to
// be different. A manufacturer's address (copying a device the router
// already knows) and a locally administered one are both allowed.
inline MacRule mac_rule(const char* text, const Mac& setup_ap, Mac& out) {
    if (!mac_parse(text, out)) return MacRule::Unreadable;
    if (out.b[0] & 0x01) return MacRule::Group;
    bool zero = true;
    for (size_t i = 0; i < 6; ++i) {
        if (out.b[i] != 0) zero = false;
    }
    if (zero) return MacRule::Zero;
    if (mac_equal(out, setup_ap)) return MacRule::SetupClash;
    return MacRule::Ok;
}

inline const char* mac_rule_text(MacRule r) {
    switch (r) {
        case MacRule::Unreadable: return "Enter a MAC address like 02:1A:2B:3C:4D:5E.";
        case MacRule::Group: return "That is a group (multicast) address; the first pair of digits must be even.";
        case MacRule::Zero: return "00:00:00:00:00:00 is not a usable address.";
        case MacRule::SetupClash: return "That is the address the board's setup network uses; pick another.";
        case MacRule::Ok: break;
    }
    return "";
}

// A random locally administered unicast address, from four random bytes per
// call: the first octet ends in binary 10, so it can never be a maker's.
inline Mac mac_random_local(uint32_t r1, uint32_t r2) {
    Mac m{};
    m.b[0] = static_cast<uint8_t>((r1 & 0xFC) | 0x02);
    m.b[1] = static_cast<uint8_t>(r1 >> 8);
    m.b[2] = static_cast<uint8_t>(r1 >> 16);
    m.b[3] = static_cast<uint8_t>(r1 >> 24);
    m.b[4] = static_cast<uint8_t>(r2);
    m.b[5] = static_cast<uint8_t>(r2 >> 8);
    return m;
}
