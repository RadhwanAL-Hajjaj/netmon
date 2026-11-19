#pragma once
// Pure logic — no Arduino headers.
//
// When the Nearby scans may have the radio.
//
// The board has one 2.4 GHz radio, and it already has work that matters more:
// the ARP sweep and the gateway latency probe are what the dashboard reports.
// A Wi-Fi scan takes the radio off the network's channel for a second or two,
// and a Bluetooth scan takes turns with Wi-Fi on it. Either one run during a
// sweep costs ARP replies, so devices are marked offline that never left, and
// run during a probe it inflates the latency figure. So a Nearby scan starts
// only when nothing else has the radio and nothing of netmon's own is due, and
// the sweep and the probe never start while a Nearby scan has it. They wait
// for it instead, five seconds at most.
//
// Scanning is quick while somebody has the Nearby page open (reading
// /api/nearby counts as watching for 20 seconds) and slow otherwise: every
// background interval from the settings, or not at all when that is 0. In
// setup mode it runs only while watched. The phone doing the setup is joined
// to the board's own access point, which goes quiet whenever a Wi-Fi scan has
// the radio on another channel, and nobody wants that for a page they are
// not looking at.
//
// Finding one device (air_find.h) takes the place of all of that while it
// lasts: the board listens for that device and nothing else, as often as the
// radio allows, and the ordinary scans wait until it is over. The sweep and the
// probe still come first.
#include <cstdint>

enum class AirJob : uint8_t { None = 0, Wifi, Ble, FindWifi, FindBle };

// What is being found, if anything: a Wi-Fi access point or a Bluetooth device.
enum class FindKind : uint8_t { None = 0, Wifi, Ble };

static const uint32_t kAirWatchMs = 20000;      // a read of /api/nearby lasts this long
static const uint32_t kAirLiveWifiMs = 15000;   // Wi-Fi scan start to start, watched
static const uint32_t kAirLiveBleMs = 8000;     // Bluetooth burst start to start, watched
static const uint32_t kAirBleBurstMs = 5000;    // how long one Bluetooth burst listens
static const uint32_t kAirWifiLimitMs = 10000;  // a Wi-Fi scan still running then has failed

// While finding. A Bluetooth listen is short so that the sweep, when it falls
// due, waits two seconds at most; the next starts as soon as one ends. A Wi-Fi
// look is a scan of one channel for one access point, about an eighth of a
// second, and one starts every 800 ms. A listen or look that would not start
// is tried again a second later, not on every pass of loop().
static const uint32_t kAirFindBurstMs = 2000;
static const uint32_t kAirFindWifiGapMs = 800;
static const uint32_t kAirFindRetryMs = 1000;

// A Bluetooth burst still open this long after it should have ended is
// stopped: one the stack never closed would keep the sweep and the probe,
// which wait for it, off the air for good.
static const uint32_t kAirBleOverrunMs = 4000;

// Background interval limits, in seconds. 0 turns background scanning off.
static const uint32_t kAirBackgroundMinS = 30;
static const uint32_t kAirBackgroundMaxS = 3600;

inline bool air_background_valid(uint32_t s) {
    return s == 0 || (s >= kAirBackgroundMinS && s <= kAirBackgroundMaxS);
}

struct AirRadio {
    bool enabled;       // switched on, and for Bluetooth, the stack started
    bool requested;     // somebody pressed "Scan now"
    bool ever;          // has run since start-up
    uint32_t last_ms;   // when it last started
};

// The finder's own timer. `kind` is None unless a device is being found and
// its radio is switched on.
struct AirFind {
    FindKind kind;
    bool ever;          // has listened or looked for this device yet
    bool failed;        // the last listen or look would not start
    uint32_t last_ms;   // when the last one started
};

struct AirInputs {
    uint32_t now_ms;
    uint32_t last_watch_ms;   // the last read of /api/nearby; 0 for never
    bool on_lan;              // joined to a network, not in setup mode
    bool radio_busy;          // a sweep pass, a Nearby scan or an update has it
    bool netmon_due;          // the sweep or the latency probe is waiting for it
    uint32_t background_s;
    AirRadio wifi;
    AirRadio ble;
    AirFind find;
};

inline bool air_watching(uint32_t now_ms, uint32_t last_watch_ms) {
    return last_watch_ms != 0 && now_ms - last_watch_ms < kAirWatchMs;
}

// Time between runs of one radio, or 0 when it runs only on request.
inline uint32_t air_interval_ms(bool watching, bool on_lan, uint32_t live_ms,
                                uint32_t background_s) {
    if (watching) return live_ms;
    if (!on_lan) return 0;
    return background_s * 1000u;
}

inline bool air_radio_due(const AirRadio& r, uint32_t now_ms, uint32_t interval_ms) {
    if (!r.enabled) return false;
    if (r.requested) return true;
    if (interval_ms == 0) return false;
    return !r.ever || now_ms - r.last_ms >= interval_ms;
}

inline bool air_find_due(const AirFind& f, uint32_t now_ms) {
    if (f.kind == FindKind::None) return false;
    if (!f.ever) return true;
    uint32_t gap = f.kind == FindKind::Wifi ? kAirFindWifiGapMs : 0;
    if (f.failed && gap < kAirFindRetryMs) gap = kAirFindRetryMs;
    return now_ms - f.last_ms >= gap;
}

// The scan to start now, if any. While a device is being found, only the
// finder's own listens and looks. Otherwise, when both radios are due, the one
// that has waited longer goes first, and one that has never run counts as
// having waited longest, so neither radio can keep the other off the air.
inline AirJob air_next(const AirInputs& in) {
    if (in.radio_busy || in.netmon_due) return AirJob::None;
    if (in.find.kind != FindKind::None) {
        if (!air_find_due(in.find, in.now_ms)) return AirJob::None;
        return in.find.kind == FindKind::Wifi ? AirJob::FindWifi : AirJob::FindBle;
    }
    const bool watching = air_watching(in.now_ms, in.last_watch_ms);
    const bool wifi = air_radio_due(
        in.wifi, in.now_ms,
        air_interval_ms(watching, in.on_lan, kAirLiveWifiMs, in.background_s));
    const bool ble = air_radio_due(
        in.ble, in.now_ms,
        air_interval_ms(watching, in.on_lan, kAirLiveBleMs, in.background_s));
    if (wifi && ble) {
        if (!in.wifi.ever) return AirJob::Wifi;
        if (!in.ble.ever) return AirJob::Ble;
        const uint32_t wifi_wait = in.now_ms - in.wifi.last_ms;
        const uint32_t ble_wait = in.now_ms - in.ble.last_ms;
        return wifi_wait >= ble_wait ? AirJob::Wifi : AirJob::Ble;
    }
    if (wifi) return AirJob::Wifi;
    if (ble) return AirJob::Ble;
    return AirJob::None;
}
