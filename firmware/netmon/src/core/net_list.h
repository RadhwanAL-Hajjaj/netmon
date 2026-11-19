#pragma once
// Pure logic — no Arduino headers.
//
// The remembered-network list: up to N saved credentials, which the board
// tries in order at start-up. Order is the point. Every entry that does not
// answer costs a 12 second timeout before the next one is tried, so the
// network most likely to be in range belongs at the front, and the best
// evidence of that is the one somebody has just saved.
//
// Written against any credential type with char ssid[] and char pass[]
// members, so the host tests exercise the firmware's real NetworkCred.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "validate.h"

// Position of `ssid` among the first `count` entries, or -1.
template <typename Cred, size_t N>
inline int netlist_find(const Cred (&nets)[N], uint8_t count,
                        const char* ssid) {
    if (ssid == nullptr) return -1;
    const size_t n = static_cast<size_t>(count) < N ? static_cast<size_t>(count) : N;
    for (size_t i = 0; i < n; ++i) {
        if (std::strcmp(nets[i].ssid, ssid) == 0) return static_cast<int>(i);
    }
    return -1;
}

// Saves `ssid` at the front of the list.
//
// A network already remembered is MOVED to the front rather than updated where
// it stands. Updating in place is what left a network the board had visited
// once pinned ahead of the one it lives on, costing a timeout on every
// restart. A new network is pushed on the front, and once the list is full
// the oldest entry falls off the end.
//
// A blank `pass` keeps the password this SSID already has (resolve_password),
// so the form can be saved without retyping it. Both strings are copied out
// before anything moves: shifting the list overwrites the entries they may
// be read from, and either argument may itself point into the list.
template <typename Cred, size_t N>
inline bool netlist_remember(Cred (&nets)[N], uint8_t& count,
                             const char* ssid, const char* pass) {
    static_assert(N > 0 && N <= 255, "count is a uint8_t");
    if (ssid == nullptr || ssid[0] == '\0') return false;
    if (static_cast<size_t>(count) > N) count = static_cast<uint8_t>(N);

    const int at = netlist_find(nets, count, ssid);

    char name[sizeof(Cred::ssid)];
    char secret[sizeof(Cred::pass)];
    std::strncpy(name, ssid, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    std::strncpy(secret,
                 resolve_password(pass, at >= 0 ? nets[at].pass : nullptr),
                 sizeof(secret) - 1);
    secret[sizeof(secret) - 1] = '\0';

    Cred entry{};
    if (at >= 0) entry = nets[at];
    std::memcpy(entry.ssid, name, sizeof(name));
    std::memcpy(entry.pass, secret, sizeof(secret));

    // The slot the shift overwrites: this network's own old entry when it is
    // being promoted, otherwise the end of the list.
    size_t from = 0;
    if (at >= 0) {
        from = static_cast<size_t>(at);
    } else {
        if (static_cast<size_t>(count) < N) ++count;
        from = static_cast<size_t>(count) - 1;
    }
    for (size_t i = from; i > 0; --i) nets[i] = nets[i - 1];
    nets[0] = entry;
    return true;
}

// Removes `ssid`, closing the gap so the survivors keep their order. The
// freed slot is wiped: a forgotten password should not outlive its entry.
template <typename Cred, size_t N>
inline bool netlist_forget(Cred (&nets)[N], uint8_t& count, const char* ssid) {
    if (static_cast<size_t>(count) > N) count = static_cast<uint8_t>(N);
    const int at = netlist_find(nets, count, ssid);
    if (at < 0) return false;
    const size_t n = static_cast<size_t>(count);
    for (size_t i = static_cast<size_t>(at); i + 1 < n; ++i) nets[i] = nets[i + 1];
    --count;
    nets[count] = Cred{};
    return true;
}
