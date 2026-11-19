#pragma once
#include <cstddef>
#include <cstdint>
#include "../core/dhcp_parse.h"

// Collects the DHCP requests other devices broadcast, while the board is
// associated as a station. Nothing is parsed outside loop().
bool dhcp_capture_begin();

// Stops collecting; used around a Wi-Fi scan and a firmware upload.
void dhcp_capture_stop();

// Parses at most one received message; true when it was DHCP.
bool dhcp_capture_poll(DhcpInfo& out);

// Whether the listener is up. Off: never started, as in setup mode. Paused:
// stopped for a Wi-Fi scan or a firmware upload. Failed: port 67 could not be
// opened.
enum class DhcpListener : uint8_t { Off, Listening, Paused, Failed };
DhcpListener dhcp_capture_state();
const char* dhcp_listener_text(DhcpListener s);

// Datagrams received on port 67 since boot, DHCP or not. Climbing while the
// DHCP count stays flat would mean packets arrive but do not parse; staying
// at 0 means nothing reaches the board at all.
uint32_t dhcp_capture_received();
