#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// The Finder: one device, followed closely enough to walk up to it.
//
// The Nearby scans hear a device once a burst or once a scan, every few
// seconds at best, and smooth what they hear. Walking towards something needs
// every reading as it comes, so while a device is being found the board
// listens for that device and nothing else (air_plan.h says when) and keeps
// each reading here, raw and numbered, for the page to collect.
//
// The page asks about once a second, and asking is what keeps the finder
// going: a page closed, or a phone put away, ends it within kAirFindIdleMs, and
// the scans go back to everything.
//
// One antenna cannot tell where a signal comes from, so nothing here is a
// direction. The page gets one from the readings themselves: stronger or
// weaker as you walk, and strongest facing the device as you turn on the spot
// with the board held against you, because a body blocks 2.4 GHz well.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "air_ble.h"
#include "air_plan.h"
#include "air_signal.h"
#include "mac.h"

// Not asked about for this long, finding stops.
static const uint32_t kAirFindIdleMs = 15000;

// An access point unheard for this long is looked for on every channel, once
// per this long: a router that restarts may come back on another channel.
static const uint32_t kAirFindWideAfterMs = 20000;

inline bool air_find_wide(uint32_t unheard_ms, uint32_t since_wide_ms) {
    return unheard_ms > kAirFindWideAfterMs && since_wide_ms > kAirFindWideAfterMs;
}

// While somebody turns on the spot for a direction, a pause in listening
// leaves a blind sector in the turn and skews the answer, and the sweep, once
// a minute and some seconds long, would land in a good share of turns. So the
// Finder may hold the sweep and the latency probe off for the length of a
// turn: a minute at most, and once in two minutes, so that however many turns
// are made the sweep still runs at least that often.
static const uint32_t kAirFindHoldMaxMs = 60000;
static const uint32_t kAirFindHoldEveryMs = 120000;

struct FindHold {
    bool ever;
    uint32_t from_ms;
    uint32_t for_ms;
};

inline bool air_find_holding(const FindHold& h, uint32_t now_ms) {
    return h.ever && now_ms - h.from_ms < h.for_ms;
}

inline uint32_t air_find_held_for(const FindHold& h, uint32_t now_ms) {
    return air_find_holding(h, now_ms) ? h.for_ms - (now_ms - h.from_ms) : 0;
}

// Asks for a hold of `want_ms`. False, with nothing held, when the last one
// was given less than kAirFindHoldEveryMs ago.
inline bool air_find_hold(FindHold& h, uint32_t now_ms, uint32_t want_ms) {
    if (want_ms == 0) return false;
    if (h.ever && now_ms - h.from_ms < kAirFindHoldEveryMs) return false;
    h.ever = true;
    h.from_ms = now_ms;
    h.for_ms = want_ms < kAirFindHoldMaxMs ? want_ms : kAirFindHoldMaxMs;
    return true;
}

// Ends a hold early, when the turn is over. When it was given still counts
// towards the next.
inline void air_find_unhold(FindHold& h) { h.for_ms = 0; }

// What a request to find a device does. `current`: it is the device being
// found now, so the Finder carries on as it is. `known`: the Nearby tables
// have it, so a fresh start, with what they know. `previous`: it is the
// device the Finder had before it last stopped, and the tables have since
// forgotten it, as they forget a Bluetooth device unheard for five minutes;
// it starts again from what the Finder kept. A page coming back after a
// minute away, looking for something still out of range, is that case.
enum class FindStart : uint8_t { Continue, Fresh, Resume, Unknown };

inline FindStart air_find_start(bool current, bool known, bool previous) {
    if (current) return FindStart::Continue;
    if (known) return FindStart::Fresh;
    if (previous) return FindStart::Resume;
    return FindStart::Unknown;
}

// Milliseconds from `at_ms` to `now_ms`. A reading stamped by the Bluetooth
// task can be a moment newer than the now loop() read before collecting it;
// that counts as 0, not as 49 days.
inline uint32_t air_ms_ago(uint32_t now_ms, uint32_t at_ms) {
    const uint32_t d = now_ms - at_ms;
    return d > 0x80000000u ? 0 : d;
}

struct FindReading {
    uint32_t seq;      // numbered from 1 and never reused, across targets too
    uint32_t at_ms;    // millis() when heard
    int8_t rssi;       // dBm, as heard: not smoothed
};

// The last N readings of the device being found. Numbering carries on across
// targets, so a page asking for "everything after 118" can never be handed
// another device's readings under old numbers: restart() makes everything
// so far invisible instead of starting again from 1.
template <size_t N>
class FindTrace {
  public:
    FindTrace() : seq_(0), floor_(0) { std::memset(items_, 0, sizeof(items_)); }

    void restart() { floor_ = seq_; }

    void add(uint32_t at_ms, int rssi) {
        ++seq_;
        FindReading& r = items_[seq_ % N];
        r.seq = seq_;
        r.at_ms = at_ms;
        r.rssi = air_rssi(rssi);
    }

    uint32_t last_seq() const { return seq_; }
    size_t size() const {
        const uint32_t n = seq_ - floor_;
        return n < N ? n : N;
    }
    bool any() const { return seq_ != floor_; }
    // Only when any().
    const FindReading& newest() const { return items_[seq_ % N]; }

    // Readings numbered after `after`, oldest first, at most `max` of them:
    // the newest `max` when there are more.
    size_t since(uint32_t after, FindReading* out, size_t max) const {
        if (!any() || max == 0 || after >= seq_) return 0;
        uint32_t first = seq_ - static_cast<uint32_t>(size()) + 1;
        if (after >= first) first = after + 1;
        if (seq_ - first + 1 > max) first = seq_ - static_cast<uint32_t>(max) + 1;
        size_t n = 0;
        for (uint32_t s = first; s <= seq_; ++s) out[n++] = items_[s % N];
        return n;
    }

  private:
    FindReading items_[N];
    uint32_t seq_;
    uint32_t floor_;     // readings up to this number belong to an earlier target
};

struct FindTarget {
    FindKind kind;
    Mac addr;              // BSSID or Bluetooth address, as printed
    BleAddrKind addr_kind; // Bluetooth: how it addresses itself, for the filter
    uint8_t channel;       // Wi-Fi: where to look
    uint32_t started_ms;
    uint32_t asked_ms;     // the page's last request
};

inline bool air_find_live(const FindTarget& t, uint32_t now_ms) {
    return t.kind != FindKind::None && now_ms - t.asked_ms < kAirFindIdleMs;
}
