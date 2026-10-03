#pragma once
// Small fixed-size event history. Kept in RAM deliberately: device events can
// be frequent and writing flash on every transition would wear LittleFS.
#include <cstddef>
#include <cstdint>
#include "mac.h"

enum class EventType : uint8_t {
    DeviceSeen = 0,
    DeviceBack = 1,
    DeviceOffline = 2,
    Hostname = 3,
    // No longer logged, from 0.9.7: two a pass crowded device events out of
    // the history. Kept so the numbering and the API names do not change.
    ScanStarted = 4,
    ScanFinished = 5,
};

struct DeviceEvent {
    uint32_t at_s;
    EventType type;
    Mac mac;
    uint32_t ip;
    char text[64];
};

template <size_t N>
class EventLog {
  public:
    EventLog() : head_(0), count_(0) {}

    void add(EventType type, const Mac& mac, uint32_t ip, const char* text) {
        DeviceEvent& e = items_[head_];
        e.at_s = 0;
        e.type = type;
        e.mac = mac;
        e.ip = ip;
        size_t i = 0;
        if (text != nullptr) {
            while (text[i] && i < sizeof(e.text) - 1) {
                e.text[i] = text[i];
                ++i;
            }
        }
        e.text[i] = '\0';
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    size_t size() const { return count_; }

    // Newest-first access. The returned object remains valid until the next add.
    DeviceEvent& newest(size_t index) {
        const size_t pos = (head_ + N - 1 - (index % N)) % N;
        return items_[pos];
    }
    const DeviceEvent& newest(size_t index) const {
        const size_t pos = (head_ + N - 1 - (index % N)) % N;
        return items_[pos];
    }

    void stamp_last(uint32_t at_s) {
        if (count_ != 0) newest(0).at_s = at_s;
    }

  private:
    DeviceEvent items_[N];
    size_t head_;
    size_t count_;
};

inline const char* event_type_text(EventType t) {
    switch (t) {
        case EventType::DeviceSeen: return "seen";
        case EventType::DeviceBack: return "back";
        case EventType::DeviceOffline: return "offline";
        case EventType::Hostname: return "hostname";
        case EventType::ScanStarted: return "scan";
        case EventType::ScanFinished: return "scan_done";
    }
    return "event";
}
