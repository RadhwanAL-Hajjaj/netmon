#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// The Wi-Fi access points the board hears around it, for the Nearby page.
// This is what the scanner sketch kept in its wifiList, kept the way the rest
// of this firmware keeps things: a fixed capacity and under host test.
//
// A scan fills the table through heard() and closes with scan_end(). An access
// point heard in either of the last two completed scans is live, in range; one
// missed in two scans in a row moves to the history, which holds what has been
// heard since start-up and is gone. A single missed scan is not taken as
// absence. A weak network often sits one scan out and is back in the next, and
// treating that as leaving made rows jump between the two lists and blips
// blink on the radar. A scan that fails is never closed, so it ages nothing.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "air_signal.h"
#include "air_text.h"
#include "mac.h"

static const size_t kSsidMax = 33;           // 32 bytes and the terminator

struct AirAp {
    Mac bssid;
    char ssid[kSsidMax];   // empty for a hidden network that never gave a name
    int8_t rssi;           // dBm, smoothed: see air_smooth_rssi()
    uint8_t channel;
    uint8_t auth;          // the Wi-Fi stack's wifi_auth_mode_t: air_auth_text()
    uint8_t misses;        // completed scans in a row that did not hear it
    uint32_t heard_in;     // number of the last scan that heard it
    uint32_t first_seen_s;
    uint32_t last_seen_s;
};

// The security a network advertises, from the Wi-Fi stack's wifi_auth_mode_t.
// By number rather than by name: the stack has added modes over the years,
// always at the end, and naming a mode an older core does not define would
// stop the firmware compiling there.
inline const char* air_auth_text(uint8_t auth) {
    switch (auth) {
        case 0:  return "Open";
        case 1:  return "WEP";
        case 2:  return "WPA";
        case 3:  return "WPA2";
        case 4:  return "WPA/WPA2";
        case 5:  return "WPA2-Enterprise";
        case 6:  return "WPA3";
        case 7:  return "WPA2/WPA3";
        case 8:  return "WAPI";
        case 9:  return "OWE";
        case 10: return "WPA3-Enterprise 192";
        case 11: return "WPA3";              // WPA3_EXT_PSK, reported as WPA3
        case 12: return "WPA3";              // WPA3_EXT_PSK_MIXED_MODE, likewise
        case 13: return "DPP";
        case 14: return "WPA3-Enterprise";
        case 15: return "WPA2/WPA3-Enterprise";
        case 16: return "WPA-Enterprise";
        default: return "Unknown";
    }
}

// How much protection a network advertises, for noticing a downgrade: an
// access point with your network's name offering less than it used to is how
// an evil twin lures devices across. Higher is stronger. 255 for a mode number
// this code does not know, so that nothing is concluded from it.
inline uint8_t air_auth_rank(uint8_t auth) {
    switch (auth) {
        case 0:  return 0;      // Open
        case 1:  return 1;      // WEP
        case 9:  return 1;      // OWE: encrypted, but anybody may join
        case 2:  return 2;      // WPA
        case 16: return 2;      // WPA-Enterprise
        case 4:  return 3;      // WPA/WPA2
        case 3:  return 4;      // WPA2
        case 5:  return 4;      // WPA2-Enterprise
        case 8:  return 4;      // WAPI
        case 7:  return 5;      // WPA2/WPA3
        case 12: return 5;      // WPA3 extended key, transition mode
        case 15: return 5;      // WPA2/WPA3-Enterprise
        case 6:  return 6;      // WPA3
        case 10: return 6;      // WPA3-Enterprise 192
        case 11: return 6;      // WPA3 extended key
        case 13: return 6;      // DPP
        case 14: return 6;      // WPA3-Enterprise
        default: return 255;
    }
}

// Centre frequency of a 2.4 GHz channel in MHz, 0 for anything else. The
// ESP32's radio has no 5 GHz band, so a channel outside 1 to 14 is not one
// this board can have heard.
inline uint16_t air_channel_mhz(uint8_t channel) {
    if (channel == 14) return 2484;
    if (channel >= 1 && channel <= 13) return static_cast<uint16_t>(2407 + 5 * channel);
    return 0;
}

template <size_t N>
class WifiAirTable {
  public:
    static const uint8_t kLiveMisses = 2;

    WifiAirTable() : count_(0), seq_(1), scans_(0), last_scan_s_(0) {
        std::memset(items_, 0, sizeof(items_));
    }

    size_t size() const { return count_; }
    size_t capacity() const { return N; }
    const AirAp& at(size_t i) const { return items_[i]; }

    // The access point with this BSSID, or nullptr.
    const AirAp* get(const Mac& m) const {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].bssid, m)) return &items_[i];
        }
        return nullptr;
    }

    // Completed scans since start-up, and when the last one finished.
    uint32_t scans() const { return scans_; }
    uint32_t last_scan_s() const { return last_scan_s_; }

    // In range: heard in one of the last two completed scans.
    static bool live(const AirAp& a) { return a.misses < kLiveMisses; }

    // Heard in the last completed scan itself. What the Settings page lists:
    // a network to join should be one that answered just now.
    bool heard_last(const AirAp& a) const { return scans_ > 0 && a.misses == 0; }

    size_t live_count() const {
        size_t n = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (live(items_[i])) ++n;
        }
        return n;
    }

    // One access point from the scan in progress. False when it was not kept,
    // because the table is full of networks that matter more: see make_room().
    // The Finder's one-channel looks come through here too, without a
    // scan_end(): one access point answering says nothing about the others.
    bool heard(const Mac& bssid, const char* ssid, int rssi, uint8_t channel,
               uint8_t auth, uint32_t now_s) {
        AirAp* a = find(bssid);
        if (a == nullptr) {
            a = make_room(rssi);
            if (a == nullptr) return false;
            std::memset(a, 0, sizeof(*a));
            a->bssid = bssid;
            a->first_seen_s = now_s;
            a->last_seen_s = now_s;
            a->rssi = air_rssi(rssi);
        } else {
            a->rssi = air_smooth_rssi(a->rssi, a->last_seen_s, rssi, now_s);
            a->last_seen_s = now_s;
        }
        // A hidden network answers with no name, and sometimes gives it away
        // later, to a phone that already knows it. A name once heard is kept
        // rather than blanked by the next nameless answer.
        char clean[kSsidMax];
        if (air_text_clean(ssid, kSsidMax - 1, clean, sizeof(clean))) {
            std::memcpy(a->ssid, clean, sizeof(clean));
        }
        a->channel = channel;
        a->auth = auth;
        a->heard_in = seq_;
        return true;
    }

    // Closes the scan in progress: everything it did not hear has missed one
    // more, everything it heard has missed none.
    void scan_end(uint32_t now_s) {
        for (size_t i = 0; i < count_; ++i) {
            AirAp& a = items_[i];
            if (a.heard_in == seq_) {
                a.misses = 0;
            } else if (a.misses < 255) {
                ++a.misses;
            }
        }
        ++seq_;
        ++scans_;
        last_scan_s_ = now_s;
    }

  private:
    AirAp* find(const Mac& m) {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].bssid, m)) return &items_[i];
        }
        return nullptr;
    }

    // A slot for a newcomer heard at `rssi`. While there is room, a new one.
    // Once full, the history gives way first, the network heard longest ago
    // going; it is not in range, and what is in range is what the page is
    // for. With no history left, the weakest network gives way, but only to
    // a stronger newcomer. Without that rule the order a scan reports its
    // results in would decide which networks are kept.
    AirAp* make_room(int rssi) {
        if (count_ < N) return &items_[count_++];
        AirAp* oldest = nullptr;
        AirAp* weakest = nullptr;
        for (size_t i = 0; i < count_; ++i) {
            AirAp& a = items_[i];
            if (a.heard_in == seq_) {
                // Heard in this very scan: competes on strength only.
            } else if (!live(a)) {
                if (oldest == nullptr || a.last_seen_s < oldest->last_seen_s) oldest = &a;
                continue;
            }
            if (weakest == nullptr || a.rssi < weakest->rssi) weakest = &a;
        }
        if (oldest != nullptr) return oldest;
        if (weakest != nullptr && weakest->rssi < rssi) return weakest;
        return nullptr;
    }

    AirAp items_[N];
    size_t count_;
    uint32_t seq_;          // number of the scan in progress
    uint32_t scans_;
    uint32_t last_scan_s_;
};
