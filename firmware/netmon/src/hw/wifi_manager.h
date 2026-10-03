#pragma once
#include <cstdint>

#include "../core/mac.h"
#include "config_store.h"

enum class WifiState { Connecting, Connected, SoftAP };

WifiState wifi_begin(const Settings& s);
WifiState wifi_state();
uint32_t wifi_local_ip();
uint32_t wifi_netmask();
uint32_t wifi_gateway();
uint32_t wifi_dns();
Mac wifi_mac();
const char* wifi_current_ssid();

// How a remembered network fared when the board last started. NotTried means
// an earlier network in the list joined first; Failed means it was in range
// but the connection did not complete; Refused means the key was rejected.
enum class JoinResult : uint8_t { NotTried = 0, Joined, NotFound, Failed, Refused, NoAnswer };

// Result for `ssid` from this boot's join attempts, with how long the attempt
// took in `ms` (0 when not tried) and the Wi-Fi stack's reason code for a
// failure in `reason` (0 when it gave none). Looked up by name, so it stays
// true when the list is reordered or trimmed before the next restart.
JoinResult wifi_boot_result(const char* ssid, uint32_t& ms, uint8_t& reason);
const char* join_result_text(JoinResult r);
