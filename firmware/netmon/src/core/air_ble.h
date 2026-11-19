#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// The Bluetooth LE devices the board hears advertising around it, for the
// Nearby page. The scanner sketch kept these in bleList; this is the same list
// with a fixed capacity and under host test.
//
// Sightings come from the Bluetooth stack's own task, so they are not written
// here directly: the radio side queues a BleSighting and loop() hands it to
// sight(). Nothing in this table is touched outside loop().
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "air_signal.h"
#include "air_text.h"
#include "ble_adv.h"
#include "ble_type.h"
#include "mac.h"

// How a device addresses itself. Most phones, watches and earbuds use a
// private address that changes every quarter of an hour or so, which is why
// the same phone in the same room turns up as a new device several times an
// hour. Shown on the page, so a list full of strangers can be read for what
// it usually is. Static random addresses stay put until a reset, and public
// addresses are the manufacturer's, like a Wi-Fi MAC.
enum class BleAddrKind : uint8_t { Public = 0, Static = 1, Private = 2 };

inline const char* ble_kind_text(BleAddrKind k) {
    switch (k) {
        case BleAddrKind::Public:  return "public";
        case BleAddrKind::Static:  return "static";
        case BleAddrKind::Private: return "private";
    }
    return "private";
}

// Legacy advertising carries at most 29 bytes of name; one more for the
// terminator and the buffer is a round 32.
static const size_t kBleNameMax = 32;

// One advertisement, as the radio side hands it over: the address and the
// raw bytes, advertising data then scan response. Taken apart by sight(), in
// loop(), so the Bluetooth task does no more than copy.
struct BleSighting {
    Mac addr;              // most significant byte first, as printed
    BleAddrKind kind;
    int8_t rssi;
    uint8_t len;
    uint8_t payload[kBlePayloadMax];
    uint32_t at_ms;        // millis() when reported, for the Finder
    // Heard by a Finder listen, which reports each advertisement as it comes.
    // An ordinary burst reports some devices only when it ends, so at_ms can
    // be seconds after the signal was measured: no use for the Finder.
    bool focused;
};

struct BleDevice {
    Mac addr;
    BleAddrKind kind;
    int8_t rssi;           // dBm, smoothed: see air_smooth_rssi()
    bool has_company;
    uint16_t company;      // Bluetooth SIG company identifier: ble_vendor()
    BleType type;          // the strongest guess so far: ble_type.h
    uint8_t type_strength;
    const char* model;     // a product named by the data itself, or nullptr
    char name[kBleNameMax];
    uint32_t first_seen_s;
    uint32_t last_seen_s;
    uint32_t sightings;
};

template <size_t N>
class BleAirTable {
  public:
    BleAirTable() : count_(0) { std::memset(items_, 0, sizeof(items_)); }

    size_t size() const { return count_; }
    size_t capacity() const { return N; }
    const BleDevice& at(size_t i) const { return items_[i]; }

    // The device with this address, or nullptr.
    const BleDevice* get(const Mac& m) const {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].addr, m)) return &items_[i];
        }
        return nullptr;
    }

    // Records one advertisement. A full table makes room by forgetting the
    // device heard longest ago, so this always keeps the newcomer.
    void sight(const BleSighting& s, uint32_t now_s) {
        BleDevice* d = find(s.addr);
        if (d == nullptr) {
            d = count_ < N ? &items_[count_++] : stalest();
            std::memset(d, 0, sizeof(*d));
            d->addr = s.addr;
            d->first_seen_s = now_s;
            d->rssi = air_rssi(s.rssi);
        } else {
            d->rssi = air_smooth_rssi(d->rssi, d->last_seen_s, s.rssi, now_s);
        }
        d->kind = s.kind;
        d->last_seen_s = now_s;
        if (d->sightings < 0xFFFFFFFFu) ++d->sightings;

        BleAdvInfo a;
        ble_adv_parse(s.payload, s.len < sizeof(s.payload) ? s.len : sizeof(s.payload), a);
        // Kept once heard. A device sends its name in some packets and not in
        // others (often only in the scan response), and a name dropping out of
        // the list every other burst would read as a different device.
        char clean[kBleNameMax];
        if (a.name != nullptr &&
            air_text_clean(reinterpret_cast<const char*>(a.name), a.name_len, clean, sizeof(clean))) {
            std::memcpy(d->name, clean, sizeof(clean));
        }
        if (a.has_company) {
            d->has_company = true;
            d->company = a.company;
        }
        // The strongest guess any packet has given stands; an equal one only
        // adds a product name the first did not have.
        const BleGuess g = ble_classify(a);
        if (g.strength > d->type_strength ||
            (g.strength > 0 && g.strength == d->type_strength && d->model == nullptr &&
             g.model != nullptr)) {
            d->type = g.type;
            d->type_strength = g.strength;
            d->model = g.model;
        }
    }

    // Forgets every device not heard for more than `ttl_s`, keeping the rest
    // in their order. Returns how many went.
    size_t expire(uint32_t now_s, uint32_t ttl_s) {
        size_t kept = 0;
        for (size_t i = 0; i < count_; ++i) {
            const BleDevice& d = items_[i];
            const bool stale = now_s > d.last_seen_s && now_s - d.last_seen_s > ttl_s;
            if (stale) continue;
            if (kept != i) items_[kept] = items_[i];
            ++kept;
        }
        const size_t gone = count_ - kept;
        for (size_t i = kept; i < count_; ++i) std::memset(&items_[i], 0, sizeof(items_[i]));
        count_ = kept;
        return gone;
    }

  private:
    BleDevice* find(const Mac& m) {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].addr, m)) return &items_[i];
        }
        return nullptr;
    }

    // The device heard longest ago. Between two heard in the same second, a
    // private address goes first, since it has likely changed already, and
    // then the weaker of the two.
    BleDevice* stalest() {
        BleDevice* pick = &items_[0];
        for (size_t i = 1; i < count_; ++i) {
            BleDevice& d = items_[i];
            if (d.last_seen_s != pick->last_seen_s) {
                if (d.last_seen_s < pick->last_seen_s) pick = &d;
                continue;
            }
            const bool dp = d.kind == BleAddrKind::Private;
            const bool pp = pick->kind == BleAddrKind::Private;
            if (dp != pp) {
                if (dp) pick = &d;
                continue;
            }
            if (d.rssi < pick->rssi) pick = &d;
        }
        return pick;
    }

    BleDevice items_[N];
    size_t count_;
};
