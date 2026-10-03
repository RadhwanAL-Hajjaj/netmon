#pragma once
// Pure logic — no Arduino headers.
//
// The table is expected in static storage, sorted ascending by prefix.
// Binary search keeps lookup cheap enough to run inside a scan loop on a
// 240 MHz core.
//
// Note this device carries a trimmed table of common consumer prefixes,
// not the full IEEE registry — 1.1 MB does not fit
// alongside OTA on 4 MB of flash. Coverage here is deliberately narrower.
#include <cstdint>
#include <cstddef>

struct OuiEntry {
    uint32_t prefix;      // top three octets, e.g. 0xB827EB
    const char* vendor;
};

inline const char* oui_lookup(uint32_t oui, const OuiEntry* table, size_t n) {
    if (table == nullptr || n == 0) return nullptr;
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (table[mid].prefix == oui) return table[mid].vendor;
        if (table[mid].prefix < oui) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return nullptr;
}
