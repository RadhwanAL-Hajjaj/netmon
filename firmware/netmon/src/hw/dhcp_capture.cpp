#include "dhcp_capture.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// A device announces its hostname in the DHCP DISCOVER and REQUEST it
// broadcasts when it joins the network. The access point repeats every
// broadcast to each station under the network's group key, which this board
// holds, so those requests reach its own IP stack like any other broadcast.
// Listening on the DHCP server port collects them.
//
// This replaced promiscuous capture. On WPA2 every other station's frames are
// encrypted with that station's own key, so the promiscuous callback only ever
// saw ciphertext: on the network this was built for it decoded 0 DHCP packets
// in 41 minutes, while copying every data frame on the channel inside a
// critical section.
namespace {

WiFiUDP g_udp;
bool g_running = false;
DhcpListener g_state = DhcpListener::Off;
uint32_t g_received = 0;
uint8_t g_buf[1460];                  // what the UDP layer delivers at most
uint32_t g_last_empty_ms = 0;

// parsePacket() allocates and frees a 1460-byte buffer on every call, packet
// or not, and loop() runs thousands of times a second. An empty socket is
// asked again only after this long; a burst is still drained at once.
const uint32_t kIdlePollMs = 20;

}  // namespace

bool dhcp_capture_begin() {
    if (g_running) return true;
    if (!g_udp.begin(67)) {                   // 0.0.0.0:67, broadcasts included
        g_state = DhcpListener::Failed;
        return false;
    }
    g_running = true;
    g_state = DhcpListener::Listening;
    g_last_empty_ms = 0;
    return true;
}

void dhcp_capture_stop() {
    if (!g_running) return;
    g_udp.stop();
    g_running = false;
    g_state = DhcpListener::Paused;
}

bool dhcp_capture_poll(DhcpInfo& out) {
    if (!g_running) return false;
    const uint32_t now = millis();
    if (g_last_empty_ms != 0 && now - g_last_empty_ms < kIdlePollMs) return false;
    const int size = g_udp.parsePacket();
    if (size <= 0) {
        g_last_empty_ms = now == 0 ? 1 : now;
        return false;
    }
    g_last_empty_ms = 0;
    ++g_received;
    const int got = g_udp.read(g_buf, sizeof(g_buf));
    if (got <= 0) return false;
    return dhcp_from_bootp(g_buf, static_cast<size_t>(got), out);
}

DhcpListener dhcp_capture_state() { return g_state; }

uint32_t dhcp_capture_received() { return g_received; }

const char* dhcp_listener_text(DhcpListener s) {
    switch (s) {
        case DhcpListener::Listening: return "listening";
        case DhcpListener::Paused:    return "paused";
        case DhcpListener::Failed:    return "failed";
        case DhcpListener::Off:       break;
    }
    return "off";
}
