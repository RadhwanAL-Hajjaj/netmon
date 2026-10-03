#pragma once
// Pure logic — no Arduino headers.
//
// Paces a lookup that leaves the LAN. Two clocks matter and they answer
// different questions: last_ok_s says when a result was last obtained, and
// governs how stale the cached answer is allowed to get; last_try_s says when
// the network was last bothered, and stops a dead uplink turning into a retry
// loop against somebody else's service.
#include <cstdint>

inline bool should_refresh(uint32_t now_s, uint32_t last_try_s,
                           uint32_t last_ok_s, uint32_t interval_s,
                           uint32_t backoff_s) {
    if (last_try_s == 0) return true;              // never attempted

    // A clock that moved backwards (millis() wrap, or time set after boot)
    // makes every elapsed figure meaningless. Refusing costs one stale
    // reading; assuming a long gap could cost a burst of requests.
    if (now_s < last_try_s) return false;
    if (now_s - last_try_s < backoff_s) return false;

    if (last_ok_s == 0) return true;               // still chasing a first result
    if (now_s < last_ok_s) return false;
    return (now_s - last_ok_s) >= interval_s;
}
