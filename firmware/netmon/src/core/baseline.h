#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// What the board has learned about the network it is on, kept on flash so a
// restart, a power cut or a firmware update does not open the learning window
// again. Until 0.12 the known/unknown baseline lived in RAM, so after every
// restart the first ten minutes counted whatever was on the network as known,
// an intruder included, and nothing could mark a new device of your own as
// known short of restarting the board.
//
// One record per network, by name, since the board moves between up to four:
// the devices it recognises, the MAC that answers for the router's address,
// the DHCP servers that hand out addresses there, and the network's own access
// points with the security each one offers. lan_guard.h watches the last three.
//
// The file holds one fact per line, "key<TAB>value". Keys this firmware does not
// know are skipped and a damaged line costs only itself, like names.txt.
//
//   v        1
//   ssid     6D792D6E6574            the name's bytes in hex: a name may hold
//                                     tabs, newlines, anything
//   learned  1
//   router   AABBCCDDEEFF
//   dhcp     C0A80201                 an IPv4 address, as eight hex digits
//   ap       AABBCCDDEEFF<TAB>3       a BSSID and its wifi_auth_mode_t
//   known    AABBCCDDEEFF             most recently seen first
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mac.h"

static const size_t kBaseKnownMax = 128;   // devices recognised on one network
static const size_t kBaseDhcpMax = 4;      // DHCP servers seen handing out addresses
static const size_t kBaseApMax = 12;       // router, extenders and mesh nodes
static const size_t kBaseLineMax = 80;     // longest line written, terminator included

struct BaseAp {
    Mac bssid;
    uint8_t auth;          // wifi_auth_mode_t, as air_wifi.h keeps it
};

struct Baseline {
    char ssid[33];
    // A learning window has closed on this network. From then on the list is
    // the reference: a manufacturer MAC not on it is unknown at once.
    bool learned;
    bool has_router;
    Mac router;            // what answered for the gateway's address
    uint8_t dhcp_count;
    uint32_t dhcp[kBaseDhcpMax];
    uint8_t ap_count;
    BaseAp aps[kBaseApMax];
    uint16_t known_count;
    Mac known[kBaseKnownMax];
    uint32_t used[kBaseKnownMax];   // recency, for making room; not written
    uint32_t clock;
};

// Empties the record and gives it to `ssid`.
inline void base_reset(Baseline& b, const char* ssid) {
    std::memset(&b, 0, sizeof(b));
    if (ssid != nullptr) {
        std::strncpy(b.ssid, ssid, sizeof(b.ssid) - 1);
        b.ssid[sizeof(b.ssid) - 1] = '\0';
    }
}

// ---- recognised devices ------------------------------------------------------

inline int base_known_find(const Baseline& b, const Mac& m) {
    for (size_t i = 0; i < b.known_count; ++i) {
        if (mac_equal(b.known[i], m)) return static_cast<int>(i);
    }
    return -1;
}

inline bool base_known_has(const Baseline& b, const Mac& m) {
    return base_known_find(b, m) >= 0;
}

// Marks a listed device as seen just now. Recency only decides which entry
// makes room when the list is full, so it is kept in RAM and never on its own
// a reason to write the file.
inline void base_known_touch(Baseline& b, const Mac& m) {
    const int i = base_known_find(b, m);
    if (i >= 0) b.used[i] = ++b.clock;
}

// Adds a device. True when that changed the list. When the list is full, the
// device seen longest ago makes room.
inline bool base_known_add(Baseline& b, const Mac& m) {
    const int at = base_known_find(b, m);
    if (at >= 0) {
        b.used[at] = ++b.clock;
        return false;
    }
    size_t slot = b.known_count;
    if (b.known_count < kBaseKnownMax) {
        ++b.known_count;
    } else {
        slot = 0;
        for (size_t i = 1; i < b.known_count; ++i) {
            if (b.used[i] < b.used[slot]) slot = i;
        }
    }
    b.known[slot] = m;
    b.used[slot] = ++b.clock;
    return true;
}

// Takes a device off the list, closing the gap. True when it was on it.
inline bool base_known_remove(Baseline& b, const Mac& m) {
    const int at = base_known_find(b, m);
    if (at < 0) return false;
    for (size_t i = static_cast<size_t>(at); i + 1 < b.known_count; ++i) {
        b.known[i] = b.known[i + 1];
        b.used[i] = b.used[i + 1];
    }
    --b.known_count;
    b.known[b.known_count] = Mac{};
    b.used[b.known_count] = 0;
    return true;
}

// ---- DHCP servers and access points ------------------------------------------

inline bool base_has_dhcp(const Baseline& b, uint32_t ip) {
    for (size_t i = 0; i < b.dhcp_count; ++i) {
        if (b.dhcp[i] == ip) return true;
    }
    return false;
}

// True when it was not there before. When the list is full the oldest goes:
// one somebody has just accepted must not be refused for want of room.
inline bool base_add_dhcp(Baseline& b, uint32_t ip) {
    if (ip == 0 || base_has_dhcp(b, ip)) return false;
    if (b.dhcp_count >= kBaseDhcpMax) {
        for (size_t i = 0; i + 1 < kBaseDhcpMax; ++i) b.dhcp[i] = b.dhcp[i + 1];
        b.dhcp_count = static_cast<uint8_t>(kBaseDhcpMax - 1);
    }
    b.dhcp[b.dhcp_count++] = ip;
    return true;
}

inline const BaseAp* base_find_ap(const Baseline& b, const Mac& bssid) {
    for (size_t i = 0; i < b.ap_count; ++i) {
        if (mac_equal(b.aps[i].bssid, bssid)) return &b.aps[i];
    }
    return nullptr;
}

// Adds an access point, or records the security it offers now. True when
// that changed anything. Full, the oldest goes, as for DHCP servers.
inline bool base_put_ap(Baseline& b, const Mac& bssid, uint8_t auth) {
    for (size_t i = 0; i < b.ap_count; ++i) {
        if (!mac_equal(b.aps[i].bssid, bssid)) continue;
        if (b.aps[i].auth == auth) return false;
        b.aps[i].auth = auth;
        return true;
    }
    if (b.ap_count >= kBaseApMax) {
        for (size_t i = 0; i + 1 < kBaseApMax; ++i) b.aps[i] = b.aps[i + 1];
        b.ap_count = static_cast<uint8_t>(kBaseApMax - 1);
    }
    b.aps[b.ap_count].bssid = bssid;
    b.aps[b.ap_count].auth = auth;
    ++b.ap_count;
    return true;
}

// ---- the file ---------------------------------------------------------------

// The file for a network: /base-<8 hex digits>.txt, from an FNV-1a hash of
// its name. Two names that share a hash share a file, and the ssid line
// inside tells them apart, so the worst a collision does is start over.
inline void base_file_name(const char* ssid, char (&out)[24]) {
    uint32_t h = 2166136261u;
    for (const char* p = ssid; p != nullptr && *p; ++p) {
        h ^= static_cast<unsigned char>(*p);
        h *= 16777619u;
    }
    static const char* hex = "0123456789abcdef";
    std::memcpy(out, "/base-", 6);
    for (int i = 0; i < 8; ++i) out[6 + i] = hex[(h >> (28 - 4 * i)) & 0x0F];
    std::memcpy(out + 14, ".txt", 5);
}

inline size_t base_put_hex(char* out, const uint8_t* bytes, size_t n) {
    static const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = hex[(bytes[i] >> 4) & 0x0F];
        out[2 * i + 1] = hex[bytes[i] & 0x0F];
    }
    return 2 * n;
}

inline size_t base_put_mac(char* out, const Mac& m) { return base_put_hex(out, m.b, 6); }

inline size_t base_put_ip(char* out, uint32_t ip) {
    const uint8_t b[4] = {static_cast<uint8_t>(ip >> 24), static_cast<uint8_t>(ip >> 16),
                          static_cast<uint8_t>(ip >> 8), static_cast<uint8_t>(ip)};
    return base_put_hex(out, b, 4);
}

// Exactly `n` hex digits into n / 2 bytes; anything else is refused.
inline bool base_get_hex(const char* s, size_t n, uint8_t* out) {
    for (size_t i = 0; i < n; ++i) {
        if (mac_hexval(s[i]) < 0) return false;
    }
    for (size_t i = 0; i < n / 2; ++i) {
        out[i] = static_cast<uint8_t>((mac_hexval(s[2 * i]) << 4) | mac_hexval(s[2 * i + 1]));
    }
    return true;
}

// Writes the record a line at a time to `sink`, which takes (const char*,
// size_t) and returns false to stop. True when every line went.
template <typename Sink>
inline bool base_write(const Baseline& b, Sink&& sink) {
    char line[kBaseLineMax];
    size_t n = 0;
    auto put = [&](const char* key) {
        n = std::strlen(key);
        std::memcpy(line, key, n);
        line[n++] = '\t';
    };
    auto end = [&]() -> bool {
        line[n++] = '\n';
        line[n] = '\0';
        return sink(static_cast<const char*>(line), n);
    };
    put("v");
    line[n++] = '1';
    if (!end()) return false;
    put("ssid");
    n += base_put_hex(line + n, reinterpret_cast<const uint8_t*>(b.ssid), std::strlen(b.ssid));
    if (!end()) return false;
    if (b.learned) {
        put("learned");
        line[n++] = '1';
        if (!end()) return false;
    }
    if (b.has_router) {
        put("router");
        n += base_put_mac(line + n, b.router);
        if (!end()) return false;
    }
    for (size_t i = 0; i < b.dhcp_count; ++i) {
        put("dhcp");
        n += base_put_ip(line + n, b.dhcp[i]);
        if (!end()) return false;
    }
    for (size_t i = 0; i < b.ap_count; ++i) {
        put("ap");
        n += base_put_mac(line + n, b.aps[i].bssid);
        line[n++] = '\t';
        const uint8_t a = b.aps[i].auth;
        if (a >= 100) line[n++] = static_cast<char>('0' + a / 100);
        if (a >= 10) line[n++] = static_cast<char>('0' + (a / 10) % 10);
        line[n++] = static_cast<char>('0' + a % 10);
        if (!end()) return false;
    }
    // Most recently seen first, so that a list read back keeps its order of
    // eviction. A plain selection by `used`: 128 entries at most, written
    // rarely.
    bool done[kBaseKnownMax] = {false};
    for (size_t k = 0; k < b.known_count; ++k) {
        size_t pick = kBaseKnownMax;
        for (size_t i = 0; i < b.known_count; ++i) {
            if (done[i]) continue;
            if (pick == kBaseKnownMax || b.used[i] > b.used[pick]) pick = i;
        }
        done[pick] = true;
        put("known");
        n += base_put_mac(line + n, b.known[pick]);
        if (!end()) return false;
    }
    return true;
}

// Applies one line of the file, with or without its line ending. False when
// the line is malformed or unknown, which loses that line and no more.
inline bool base_read_line(Baseline& b, const char* line) {
    if (line == nullptr) return false;
    const char* tab = std::strchr(line, '\t');
    if (tab == nullptr) return false;
    const size_t klen = static_cast<size_t>(tab - line);
    const char* v = tab + 1;
    size_t vlen = 0;
    while (v[vlen] && v[vlen] != '\n' && v[vlen] != '\r') ++vlen;
    auto key = [&](const char* k) {
        return std::strlen(k) == klen && std::strncmp(line, k, klen) == 0;
    };

    if (key("v")) return vlen > 0;
    if (key("ssid")) {
        if (vlen % 2 != 0 || vlen / 2 > sizeof(b.ssid) - 1) return false;
        char name[sizeof(b.ssid)] = {0};
        if (!base_get_hex(v, vlen, reinterpret_cast<uint8_t*>(name))) return false;
        if (std::strlen(name) != vlen / 2) return false;      // a NUL inside
        std::memcpy(b.ssid, name, sizeof(name));
        return true;
    }
    if (key("learned")) {
        b.learned = vlen == 1 && v[0] == '1';
        return true;
    }
    if (key("router")) {
        Mac m{};
        if (vlen != 12 || !base_get_hex(v, 12, m.b)) return false;
        b.router = m;
        b.has_router = true;
        return true;
    }
    if (key("dhcp")) {
        uint8_t q[4];
        if (vlen != 8 || !base_get_hex(v, 8, q)) return false;
        const uint32_t ip = (static_cast<uint32_t>(q[0]) << 24) | (static_cast<uint32_t>(q[1]) << 16) |
                            (static_cast<uint32_t>(q[2]) << 8) | q[3];
        if (b.dhcp_count >= kBaseDhcpMax) return false;
        return base_add_dhcp(b, ip);
    }
    if (key("ap")) {
        Mac m{};
        if (vlen < 14 || v[12] != '\t' || !base_get_hex(v, 12, m.b)) return false;
        unsigned auth = 0;
        for (size_t i = 13; i < vlen; ++i) {
            if (v[i] < '0' || v[i] > '9' || i > 15) return false;
            auth = auth * 10 + static_cast<unsigned>(v[i] - '0');
        }
        if (auth > 255 || b.ap_count >= kBaseApMax) return false;
        return base_put_ap(b, m, static_cast<uint8_t>(auth));
    }
    if (key("known")) {
        Mac m{};
        if (vlen != 12 || !base_get_hex(v, 12, m.b)) return false;
        if (b.known_count >= kBaseKnownMax || base_known_has(b, m)) return false;
        // Lines come most recent first: each one read is older than the last.
        b.known[b.known_count] = m;
        b.used[b.known_count] = 0;
        ++b.known_count;
        return true;
    }
    return false;
}

// Once every line is in: recency from the order they came in, newest first.
inline void base_read_done(Baseline& b) {
    for (size_t i = 0; i < b.known_count; ++i) {
        b.used[i] = static_cast<uint32_t>(b.known_count - i);
    }
    b.clock = b.known_count;
}
