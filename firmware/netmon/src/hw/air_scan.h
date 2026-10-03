#pragma once
// The radio side of the Nearby page: Wi-Fi scans and Bluetooth LE bursts.
//
// Both run in the background and are polled from loop(), like everything else
// in this firmware. Neither decides when to run; air_plan.h does that, so the
// scans never land on top of an ARP sweep or a latency probe.
#include <cstddef>
#include <cstdint>

#include "../core/air_ble.h"
#include "../core/air_wifi.h"
#include "../core/mac.h"

// ---- Wi-Fi --------------------------------------------------------------

struct AirApReading {
    Mac bssid;
    char ssid[kSsidMax];
    int rssi;
    uint8_t channel;
    uint8_t auth;          // wifi_auth_mode_t
};

enum class AirScanState : uint8_t { Idle, Running, Done, Failed };

// Starts a scan of every channel in the background, hidden networks included.
// False when the Wi-Fi stack would not start one: a scan already running, or
// the station in the middle of joining.
bool air_wifi_begin();

// Where the scan started by air_wifi_begin() stands. Done hands over `count`
// results, readable with air_wifi_reading() until air_wifi_release(). A scan
// still running after `limit_ms` is stopped and reported as Failed.
AirScanState air_wifi_poll(uint32_t limit_ms, int& count);

// Runs a whole scan before returning, for the Settings page, which is
// waiting on it. Returns how many access points answered, or -1 when the scan
// failed or ran past `limit_ms`. The results are read and released the same
// way as a background scan's.
int air_wifi_scan_blocking(uint32_t limit_ms);

// For the Finder: one look for one access point, on one channel, read and
// released the same way as any other scan. About an eighth of a second, and
// none of it away from the network's own channel when the access point shares
// it. Channel 0 looks on every channel, for an access point that has moved.
bool air_wifi_begin_target(const Mac& bssid, uint8_t channel);

bool air_wifi_running();
bool air_wifi_reading(int index, AirApReading& out);
void air_wifi_release();

// ---- Bluetooth LE -------------------------------------------------------

// Starts the Bluetooth stack. False when it would not start, and the Nearby
// page then says so. Called once; later calls report the first answer.
bool air_ble_begin();
bool air_ble_ready();

// Listens for `ms` milliseconds in the background. False when the stack is
// not ready or a burst is already running. A burst runs, for
// air_ble_scanning(), until the stack has finished handing over its results,
// which is a moment after the radio stops.
bool air_ble_burst(uint32_t ms);

// For the Finder: listens for one device only, and hears every advertisement
// it sends rather than one a burst. The controller does the filtering, so a
// crowded room costs nothing, and the listener checks the address as well in
// case a controller will not take the filter. Passive: a scan response adds
// nothing once the device is known, and asking for one costs airtime. False
// when the stack is not ready or a burst is already running.
bool air_ble_focus_burst(const Mac& addr, BleAddrKind kind, uint32_t ms);
bool air_ble_scanning();

// Cuts a burst short. Only for a firmware update, which wants the radio to
// itself, and for a burst still open long after it should have ended: the
// stack's own bookkeeping is not safe to stop from here while it is
// mid-advertisement, so everything else lets a burst run its course.
void air_ble_stop();

// One queued sighting, parsed outside the Bluetooth task. False when the
// queue is empty.
bool air_ble_poll(BleSighting& out);

// Sightings thrown away because loop() did not empty the queue in time.
uint32_t air_ble_dropped();
