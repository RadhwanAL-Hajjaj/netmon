#pragma once
// Pure logic — no Arduino headers.
//
// Paces an ARP sweep across a subnet in small batches.
//
// lwIP's ARP cache on ESP32 holds only about ten entries and is not
// readily enlarged from Arduino, so firing requests at every host and
// then reading the table would surface only the last handful. Instead the
// sweep walks the subnet a few addresses at a time: request a batch, let
// replies settle, read the table, continue. A /24 is roughly 32 batches
// of 8 — a few seconds per full pass, which is nothing on a 60s timer.
#include <cstdint>
#include <cstddef>

#include "cidr.h"
#include "validate.h"

template <size_t MAX_BATCH>
class SweepPlan {
  public:
    SweepPlan() : first_(0), last_(0), next_(0), total_(0), active_(false) {}

    // Returns false when the subnet must not be swept at all — a zero mask
    // from SoftAP, or a range too large to walk.
    bool begin(uint32_t subnet, uint32_t mask) {
        first_ = last_ = next_ = total_ = 0;
        active_ = false;
        if (!sweepable(subnet, mask)) return false;

        const uint32_t lo = host_first(subnet, mask);
        const uint32_t hi = host_last(subnet, mask);
        if (lo > hi) return false;      // /31 and /32 hold no usable hosts

        first_ = lo;
        last_ = hi;
        next_ = lo;
        // Counted explicitly rather than derived from first_/last_: on the
        // refused path those are both zero, and last_ - first_ + 1 would
        // report an empty range as holding one host.
        total_ = hi - lo + 1;
        active_ = true;
        return true;
    }

    bool active() const { return active_; }

    // Fills `out` with the next addresses to probe. Returns how many were
    // written; 0 once the pass is complete.
    size_t next_batch(uint32_t* out, size_t max) {
        if (!active_ || out == nullptr || max == 0) return 0;
        const size_t cap = (max < MAX_BATCH) ? max : MAX_BATCH;
        size_t n = 0;
        while (n < cap && next_ <= last_) {
            out[n++] = next_++;
            if (next_ == 0) break;      // wrapped past 0xFFFFFFFF
        }
        if (next_ > last_ || next_ == 0) active_ = false;
        return n;
    }

    uint32_t total() const { return total_; }

    uint32_t remaining() const {
        if (!active_) return 0;
        return (last_ >= next_) ? (last_ - next_ + 1) : 0;
    }

  private:
    uint32_t first_;
    uint32_t last_;
    uint32_t next_;
    uint32_t total_;
    bool active_;
};
