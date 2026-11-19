#pragma once
// Pure logic — no Arduino headers.
#include <cstdint>

enum class Status : uint8_t { Unknown = 0, Known = 1, Private = 2 };

// first_scan_s == 0 means discovery has never succeeded on this network.
// The window cannot have expired before it started, so treat it as open:
// a device that cannot scan must never accuse anything. This is exactly
// the bug that flagged all 33 devices on the NAS service.
inline bool in_learning_window(uint32_t now_s, uint32_t first_scan_s,
                               uint32_t window_s) {
    if (first_scan_s == 0) return true;
    if (window_s == 0) return false;
    if (now_s < first_scan_s) return true;      // clock stepped backwards
    return (now_s - first_scan_s) < window_s;
}

inline Status classify_new(uint32_t now_s, uint32_t first_scan_s,
                           uint32_t window_s) {
    return in_learning_window(now_s, first_scan_s, window_s) ? Status::Known
                                                             : Status::Unknown;
}

inline const char* status_text(Status s) {
    switch (s) {
        case Status::Known:   return "known";
        case Status::Private: return "private";
        default:              return "unknown";
    }
}

// A randomised (locally administered) MAC belongs to a device asserting
// privacy, and it changes on every reconnect — judging it by the learning
// window would raise an alert each time a phone rotated. Those are Private.
// An unknown MANUFACTURER-assigned MAC is the case worth alerting on, and
// that still goes through the window.
inline Status classify_device(uint32_t now_s, uint32_t first_scan_s,
                              uint32_t window_s, bool randomised) {
    if (randomised) return Status::Private;
    return classify_new(now_s, first_scan_s, window_s);
}

// From 0.12 the board remembers, per network, the devices it recognises
// (baseline.h). A device on that list is known whenever it turns up, and once
// a learning window has closed on the network the list is the whole answer:
// anything else with a manufacturer MAC is unknown from the first sweep after
// a restart. Before then, the window decides as it always has.
inline Status classify_remembered(uint32_t now_s, uint32_t first_scan_s,
                                  uint32_t window_s, bool randomised,
                                  bool listed, bool learned) {
    if (randomised) return Status::Private;
    if (listed) return Status::Known;
    if (learned) return Status::Unknown;
    return classify_new(now_s, first_scan_s, window_s);
}

// Whether new devices are still taken as known: the window is open and nothing
// has been learned on this network before.
inline bool still_learning(uint32_t now_s, uint32_t first_scan_s,
                           uint32_t window_s, bool learned) {
    return !learned && in_learning_window(now_s, first_scan_s, window_s);
}
