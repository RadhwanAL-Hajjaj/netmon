#include "latency.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "../core/cidr.h"
#include "uptime.h"
#include <cstring>

bool latency_check(uint32_t gateway_ip, LatencyInfo& out) {
    out.valid = false;
    out.rtt_ms = 0;
    out.checked_s = uptime_s();     // the sketch's clock, which does not wrap
    strncpy(out.method, "tcp", sizeof(out.method) - 1);
    out.method[sizeof(out.method) - 1] = '\0';

    if (WiFi.status() != WL_CONNECTED || gateway_ip == 0) return false;

    const IPAddress gw(static_cast<uint8_t>((gateway_ip >> 24) & 0xFF),
                       static_cast<uint8_t>((gateway_ip >> 16) & 0xFF),
                       static_cast<uint8_t>((gateway_ip >> 8) & 0xFF),
                       static_cast<uint8_t>(gateway_ip & 0xFF));

    // Try common management ports. A refused TCP connection is still a
    // network response and gives us a useful reachability measurement.
    const uint16_t ports[] = {80, 443};
    for (uint16_t port : ports) {
        WiFiClient c;
        const uint32_t start = millis();
        if (c.connect(gw, port, 500)) {
            out.rtt_ms = millis() - start;
            out.valid = true;
            c.stop();
            return true;
        }
        c.stop();
    }
    ++out.failures;
    return false;
}
