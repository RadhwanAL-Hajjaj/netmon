#pragma once
// Pure logic — no Arduino headers, no dynamic allocation. Capacity is a
// compile-time constant so RAM use on a 320 KB device is exactly what the
// host tests exercise.
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "classify.h"
#include "mac.h"

struct Device {
    Mac mac;
    uint32_t ip;
    uint32_t first_seen;    // ever
    uint32_t online_since;  // start of the current unbroken stretch
    uint32_t last_seen;
    Status status;
    bool online;
    char hostname[64];
};

template <size_t N>
class DeviceTable {
  public:
    DeviceTable() : count_(0) { std::memset(items_, 0, sizeof(items_)); }

    size_t size() const { return count_; }
    size_t capacity() const { return N; }
    Device& at(size_t i) { return items_[i]; }
    const Device& at(size_t i) const { return items_[i]; }

    Device* find(const Mac& m) {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].mac, m)) return &items_[i];
        }
        return nullptr;
    }

    Device* upsert(const Mac& m, uint32_t ip, uint32_t now, Status if_new,
                   bool& created) {
        created = false;
        Device* found = find(m);
        if (found != nullptr) {
            // Uptime measures the current stretch, so returning from an
            // absence restarts it. An ordinary refresh of a device that never
            // left must not, or every scan pass would reset it to zero.
            if (!found->online) found->online_since = now;
            found->last_seen = now;
            found->online = true;
            if (ip != 0) found->ip = ip;
            return found;                  // status is decided once, at creation
        }

        Device* slot = count_ < N ? &items_[count_++] : evict_candidate();

        std::memset(slot, 0, sizeof(*slot));
        slot->mac = m;
        slot->ip = ip;
        slot->first_seen = now;
        slot->online_since = now;
        slot->last_seen = now;
        slot->status = if_new;
        slot->online = true;
        created = true;
        return slot;
    }

    // upsert(), and also whether the device had been offline until this
    // sighting. The sweep and the DHCP listener both record sightings through
    // this, so a device coming back is noticed however it is found.
    Device* sight(const Mac& m, uint32_t ip, uint32_t now, Status if_new,
                  bool& created, bool& returned) {
        const Device* before = find(m);
        returned = before != nullptr && !before->online;
        return upsert(m, ip, now, if_new, created);
    }

    // How many devices were sighted at or after `since`. `others` leaves out
    // `self` and whatever holds `gateway_ip`: the two a sweep "finds" even
    // when it reaches nothing else, because this board records itself and the
    // gateway's ARP entry stays cached from ordinary traffic.
    size_t seen_since(uint32_t since, const Mac& self, uint32_t gateway_ip,
                      size_t& others) const {
        size_t seen = 0;
        others = 0;
        for (size_t i = 0; i < count_; ++i) {
            const Device& d = items_[i];
            if (d.last_seen < since) continue;
            ++seen;
            if (!mac_equal(d.mac, self) && d.ip != gateway_ip) ++others;
        }
        return seen;
    }

    bool set_hostname(const Mac& m, const char* name) {
        Device* d = find(m);
        if (d == nullptr || name == nullptr) return false;
        std::strncpy(d->hostname, name, sizeof(d->hostname) - 1);
        d->hostname[sizeof(d->hostname) - 1] = '\0';
        return true;
    }

    // Returns how many devices newly transitioned to offline.
    size_t mark_offline(uint32_t now, uint32_t after_s) {
        size_t changed = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (!items_[i].online) continue;
            if (now > items_[i].last_seen &&
                (now - items_[i].last_seen) > after_s) {
                items_[i].online = false;
                ++changed;
            }
        }
        return changed;
    }

  private:
    // Which row gives way when the table is full. Offline rows before online
    // ones; among them a randomised address first, since it rotates and will
    // not come back as itself, then an unknown device, and a known device
    // last. A known device dropped here and seen again after the learning
    // window has closed would be re-created as unknown: a false alarm. Within
    // a tier the one seen longest ago goes. Before, the least recently seen
    // row went whatever it was.
    static int evict_tier(const Device& d) {
        const int trust = d.status == Status::Private   ? 0
                          : d.status == Status::Unknown ? 1
                                                        : 2;
        return (d.online ? 3 : 0) + trust;
    }

    Device* evict_candidate() {
        Device* pick = &items_[0];
        int pick_tier = evict_tier(*pick);
        for (size_t i = 1; i < count_; ++i) {
            const int t = evict_tier(items_[i]);
            if (t < pick_tier ||
                (t == pick_tier && items_[i].last_seen < pick->last_seen)) {
                pick = &items_[i];
                pick_tier = t;
            }
        }
        return pick;
    }

    Device items_[N];
    size_t count_;
};
