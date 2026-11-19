#pragma once
// Pure logic — no Arduino headers.
//
// The networks offered on the Settings page after a scan. Several access
// points often share one name (a router and an extender, or a mesh), and
// hidden networks answer with no name at all. Neither is a separate choice,
// so each name appears once, at the strongest signal it was heard with,
// nameless entries are left out, and the list stays strongest first.
#include <cstddef>
#include <cstdint>
#include <cstring>

struct ScanEntry {
    char ssid[33];
    int32_t rssi;
};

// Adds one scan result to `list`, which holds `count` of at most `cap`
// entries. Returns the new count. When the list is full, a newcomer weaker
// than everything kept is dropped, and a stronger one pushes out the weakest.
inline size_t scan_add(ScanEntry* list, size_t count, size_t cap,
                       const char* ssid, int32_t rssi) {
    if (list == nullptr || cap == 0 || ssid == nullptr || ssid[0] == '\0') {
        return count;
    }
    for (size_t i = 0; i < count; ++i) {
        if (std::strncmp(list[i].ssid, ssid, 32) != 0) continue;
        if (rssi > list[i].rssi) {
            list[i].rssi = rssi;
            for (size_t j = i; j > 0 && list[j - 1].rssi < list[j].rssi; --j) {
                const ScanEntry t = list[j - 1];
                list[j - 1] = list[j];
                list[j] = t;
            }
        }
        return count;
    }
    size_t at = count;
    while (at > 0 && list[at - 1].rssi < rssi) --at;
    if (count >= cap) {
        if (at >= cap) return count;
        count = cap - 1;
    }
    for (size_t j = count; j > at; --j) list[j] = list[j - 1];
    std::strncpy(list[at].ssid, ssid, 32);
    list[at].ssid[32] = '\0';
    list[at].rssi = rssi;
    return count + 1;
}
