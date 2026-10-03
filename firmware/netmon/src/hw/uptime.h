#pragma once
#include <cstdint>

#include <esp_timer.h>

// Seconds since boot, from the 64-bit microsecond timer.
//
// Every timestamp the firmware keeps comes from here: sightings, events, the
// learning window, latency checks and uptime. They used to be millis() / 1000,
// and millis() is a 32-bit count of milliseconds that wraps after 49.7 days.
// The quotient then falls from 4,294,967 back to 0, which reopened the
// learning window (new devices were accepted as known), stopped devices ever
// ageing out as offline, and made the board look freshly restarted. This one
// runs for 136 years before a 32-bit count of seconds wraps.
inline uint32_t uptime_s() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}
