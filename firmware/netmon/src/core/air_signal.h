#pragma once
// Pure logic — no Arduino headers.
//
// Signal strength as the Nearby tables keep it, for Wi-Fi and Bluetooth alike.
#include <cstdint>

// One reading of signal strength swings by several dB from one scan to the
// next, which on the radar is a blip hopping a third of the way across the
// scope while nothing moved. A reading is therefore averaged with the previous
// one when that is recent. A real move shows within a reading or two, and a
// device back after a long absence starts from its new reading, not its old.
static const uint32_t kAirSmoothWindowS = 60;

// Received power in dBm, held to what an int8_t and the physics allow.
inline int8_t air_rssi(int reading) {
    if (reading > 0) reading = 0;
    if (reading < -127) reading = -127;
    return static_cast<int8_t>(reading);
}

inline int8_t air_smooth_rssi(int8_t prev, uint32_t prev_s, int reading,
                              uint32_t now_s) {
    const int8_t r = air_rssi(reading);
    if (now_s < prev_s || now_s - prev_s > kAirSmoothWindowS) return r;
    return static_cast<int8_t>((static_cast<int>(prev) + r) / 2);
}
