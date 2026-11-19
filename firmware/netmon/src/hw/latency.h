#pragma once
#include <cstdint>

struct LatencyInfo {
    bool valid;
    uint32_t rtt_ms;
    uint32_t checked_s;
    uint32_t failures;
    char method[12];
};

// Measures LAN gateway responsiveness using a short TCP connect. This avoids
// adding an external ping library and keeps the firmware self-contained.
bool latency_check(uint32_t gateway_ip, LatencyInfo& out);
