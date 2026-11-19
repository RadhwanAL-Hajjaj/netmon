#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// The LAN watch, from 0.12. On a home network a few things should never change
// without somebody changing them, and each is what an attacker on the network
// has to change:
//
//   - the MAC that answers for the router's address. ARP spoofing, the usual
//     way to sit in the middle of everybody's traffic, makes it another one.
//   - one MAC per address. Two taking turns at an address is a clash, or a
//     device answering for somebody else's.
//   - the DHCP server. A second one hands out itself as gateway or DNS; a
//     client that takes its offer names it, in option 54 of the REQUEST it
//     broadcasts, which the board hears.
//   - the network's own access points. An evil twin is an access point with
//     your network's name, often offering weaker security or none, that
//     devices join by mistake.
//
// The board can only see what reaches it. It reads the router's MAC from its
// own ARP cache, which a spoofer poisons by answering the board as well; it
// hears DHCP requests the access point repeats; it hears access points only
// while the Nearby page's Wi-Fi scans are on. None of this proves an attack,
// and the pages say so: router replaced, extender roaming, new mesh node.
// What it does is say "this changed", once, and let somebody decide.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "air_wifi.h"
#include "baseline.h"
#include "event_log.h"
#include "mac.h"

enum class GuardKind : uint8_t {
    RouterChanged = 0,
    IpConflict = 1,
    DhcpServer = 2,
    RogueAp = 3,
    WeakAp = 4,
};

inline const char* guard_kind_text(GuardKind k) {
    switch (k) {
        case GuardKind::RouterChanged: return "router_changed";
        case GuardKind::IpConflict:    return "ip_conflict";
        case GuardKind::DhcpServer:    return "dhcp_server";
        case GuardKind::RogueAp:       return "rogue_ap";
        case GuardKind::WeakAp:        return "weak_ap";
    }
    return "alert";
}

inline bool guard_kind_parse(const char* s, GuardKind& out) {
    if (s == nullptr) return false;
    for (uint8_t i = 0; i <= static_cast<uint8_t>(GuardKind::WeakAp); ++i) {
        const GuardKind k = static_cast<GuardKind>(i);
        if (std::strcmp(s, guard_kind_text(k)) == 0) {
            out = k;
            return true;
        }
    }
    return false;
}

// The event each kind is logged as. The names match guard_kind_text().
inline EventType guard_kind_event(GuardKind k) {
    switch (k) {
        case GuardKind::RouterChanged: return EventType::RouterChanged;
        case GuardKind::IpConflict:    return EventType::IpConflict;
        case GuardKind::DhcpServer:    return EventType::DhcpServer;
        case GuardKind::RogueAp:       return EventType::RogueAp;
        case GuardKind::WeakAp:        return EventType::WeakAp;
    }
    return EventType::RouterChanged;
}

// ---- what is noticed now ------------------------------------------------------

// One problem the board is watching. Which device, by kind:
//   RouterChanged  mac the new MAC, other the router's on record, ip the gateway
//   IpConflict     mac the one that took the address back, other its rival,
//                  ip the address
//   DhcpServer     ip the server, mac the latest device it answered
//   RogueAp        mac the access point's BSSID
//   WeakAp         mac the access point's BSSID
struct GuardEntry {
    GuardKind kind;
    Mac mac;
    Mac other;
    uint32_t ip;
    uint32_t first_s;      // first noticed
    uint32_t last_s;       // most recently noticed
    uint32_t told_s;       // last logged as an event
};

// A DHCP server is one problem whichever device it answers; everything else is
// one problem per MAC, so a spoofer answering for twenty addresses is one.
inline bool guard_same(const GuardEntry& e, GuardKind k, const Mac& mac, uint32_t ip) {
    if (e.kind != k) return false;
    return k == GuardKind::DhcpServer ? e.ip == ip : mac_equal(e.mac, mac);
}

template <size_t N>
class GuardLog {
  public:
    GuardLog() : count_(0) { std::memset(items_, 0, sizeof(items_)); }

    size_t size() const { return count_; }
    const GuardEntry& at(size_t i) const { return items_[i]; }

    // A problem noticed, again or for the first time. True when it should be
    // logged now: the first time, and again once `quiet_s` has passed since it
    // was last logged. The sweep reads the ARP cache forty times a pass, and a
    // spoofer flipping the router's entry would otherwise fill the event list.
    bool raise(GuardKind k, const Mac& mac, const Mac& other, uint32_t ip,
               uint32_t now, uint32_t quiet_s) {
        GuardEntry* e = find(k, mac, ip);
        if (e == nullptr) {
            e = make_room();
            std::memset(e, 0, sizeof(*e));
            e->kind = k;
            e->mac = mac;
            e->other = other;
            e->ip = ip;
            e->first_s = now;
            e->last_s = now;
            e->told_s = now;
            return true;
        }
        if (k == GuardKind::DhcpServer) {
            e->mac = mac;
        } else {
            e->other = other;
            e->ip = ip;
        }
        e->last_s = now;
        if (now >= e->told_s && now - e->told_s < quiet_s) return false;
        e->told_s = now;
        return true;
    }

    const GuardEntry* get(GuardKind k, const Mac& mac, uint32_t ip) const {
        for (size_t i = 0; i < count_; ++i) {
            if (guard_same(items_[i], k, mac, ip)) return &items_[i];
        }
        return nullptr;
    }

    // Somebody accepted the change, or dismissed it. True when it was there.
    bool clear(GuardKind k, const Mac& mac, uint32_t ip) {
        for (size_t i = 0; i < count_; ++i) {
            if (!guard_same(items_[i], k, mac, ip)) continue;
            drop(i);
            return true;
        }
        return false;
    }

    void clear_all() {
        count_ = 0;
        std::memset(items_, 0, sizeof(items_));
    }

    // What has not been noticed for `after_s` has stopped, and goes.
    void expire(uint32_t now, uint32_t after_s) {
        size_t i = 0;
        while (i < count_) {
            const GuardEntry& e = items_[i];
            if (now > e.last_s && now - e.last_s > after_s) {
                drop(i);
            } else {
                ++i;
            }
        }
    }

  private:
    GuardEntry* find(GuardKind k, const Mac& mac, uint32_t ip) {
        for (size_t i = 0; i < count_; ++i) {
            if (guard_same(items_[i], k, mac, ip)) return &items_[i];
        }
        return nullptr;
    }

    // Full, the one noticed longest ago gives way.
    GuardEntry* make_room() {
        if (count_ < N) return &items_[count_++];
        size_t pick = 0;
        for (size_t i = 1; i < count_; ++i) {
            if (items_[i].last_s < items_[pick].last_s) pick = i;
        }
        return &items_[pick];
    }

    void drop(size_t i) {
        for (size_t j = i; j + 1 < count_; ++j) items_[j] = items_[j + 1];
        --count_;
        std::memset(&items_[count_], 0, sizeof(items_[count_]));
    }

    GuardEntry items_[N];
    size_t count_;
};

// ---- two devices, one address ------------------------------------------------

// lwIP's ARP cache keeps one MAC per address, the last to answer, so a clash
// never shows as two answers at once. It shows as the address changing hands
// back and forth between the same two MACs: A, B, A, B, each change within a
// few minutes of the one before. A DHCP server never does that. It hands an
// address on only after its holder has gone, and the holder then comes back
// somewhere else. Three changes are asked for, not two: a sleep proxy (an
// Apple TV answering for a sleeping Mac) hands an address over and back once
// each time the Mac wakes for a minute, and that is not a clash.
struct IpClaim {
    uint32_t ip;
    Mac mac;               // answering now
    Mac other;             // answered before it
    uint32_t seen_s;       // when `mac` last answered
    uint32_t changed_s;    // when the address last changed hands
    uint8_t changes;       // changes in a row between the same two, each soon after the last
    bool has_other;
};

inline bool guard_recent(uint32_t now, uint32_t then, uint32_t window_s) {
    return now < then || now - then <= window_s;      // a clock stepping back counts as recent
}

template <size_t N>
class IpClaims {
  public:
    static const uint8_t kChanges = 3;

    IpClaims() : count_(0) { std::memset(items_, 0, sizeof(items_)); }

    size_t size() const { return count_; }

    // One ARP answer: `mac` holds `ip` now. True once the address has changed
    // hands kChanges times between the same two devices, each change within
    // `window_s` of the last, and on every change after that; `rival` is then
    // the one it went from.
    bool observe(uint32_t ip, const Mac& mac, uint32_t now, uint32_t window_s,
                 Mac& rival) {
        IpClaim* c = find(ip);
        if (c == nullptr) {
            c = make_room();
            std::memset(c, 0, sizeof(*c));
            c->ip = ip;
            c->mac = mac;
            c->seen_s = now;
            return false;
        }
        if (mac_equal(c->mac, mac)) {
            c->seen_s = now;
            return false;
        }
        const bool same_two = c->has_other && mac_equal(c->other, mac);
        const bool soon = c->changes > 0 && guard_recent(now, c->changed_s, window_s);
        c->changes = same_two && soon ? (c->changes < 255 ? c->changes + 1 : 255) : 1;
        const Mac displaced = c->mac;
        c->other = displaced;
        c->has_other = true;
        c->mac = mac;
        c->seen_s = now;
        c->changed_s = now;
        if (c->changes < kChanges) return false;
        rival = displaced;
        return true;
    }

    void clear() {
        count_ = 0;
        std::memset(items_, 0, sizeof(items_));
    }

  private:
    IpClaim* find(uint32_t ip) {
        for (size_t i = 0; i < count_; ++i) {
            if (items_[i].ip == ip) return &items_[i];
        }
        return nullptr;
    }

    IpClaim* make_room() {
        if (count_ < N) return &items_[count_++];
        size_t pick = 0;
        for (size_t i = 1; i < count_; ++i) {
            if (items_[i].seen_s < items_[pick].seen_s) pick = i;
        }
        return &items_[pick];
    }

    IpClaim items_[N];
    size_t count_;
};

// ---- judgements -------------------------------------------------------------

enum class RouterVerdict : uint8_t { Same, Learn, Changed };

// What answered for the router's address, against the record. The first MAC
// heard there on a network becomes the record.
inline RouterVerdict guard_router_check(const Baseline& b, const Mac& seen) {
    if (!b.has_router) return RouterVerdict::Learn;
    return mac_equal(b.router, seen) ? RouterVerdict::Same : RouterVerdict::Changed;
}

// Whether a DHCP server is one this network uses: the one that gave this
// board its own address, one learned while learning or accepted since, or,
// while none is on record, the router.
inline bool guard_dhcp_expected(const Baseline& b, uint32_t server,
                                uint32_t own_server, uint32_t gateway) {
    if (server == 0) return true;              // nothing named, nothing to judge
    if (own_server != 0 && server == own_server) return true;
    if (base_has_dhcp(b, server)) return true;
    return b.dhcp_count == 0 && server == gateway;
}

enum class ApVerdict : uint8_t { Fine, Learn, Unknown, Weaker, Stronger };

// An access point heard with this network's name. While learning, whatever is
// heard is the norm, and so it is for a network learned while no scan ran
// (Nearby Wi-Fi switched off): with nothing on record, the first scan that
// hears the name records what it hears. After that, an unknown BSSID is an
// evil twin until somebody says otherwise, and a known one offering less than
// it did is a downgrade. Offering more is fine, and becomes the new norm; so
// does the first mode heard from one accepted while it was out of range,
// recorded with no mode at all.
inline ApVerdict guard_ap_check(const Baseline& b, const Mac& bssid, uint8_t auth) {
    if (!b.learned || b.ap_count == 0) return ApVerdict::Learn;
    const BaseAp* known = base_find_ap(b, bssid);
    if (known == nullptr) return ApVerdict::Unknown;
    const uint8_t was = air_auth_rank(known->auth);
    const uint8_t now = air_auth_rank(auth);
    if (now == 255 || was == now) return ApVerdict::Fine;
    if (was == 255) return ApVerdict::Stronger;
    return now < was ? ApVerdict::Weaker : ApVerdict::Stronger;
}
