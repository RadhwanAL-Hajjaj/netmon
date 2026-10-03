#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

static WifiState g_state = WifiState::Connecting;
static char g_ssid[33] = {0};

static const char* kHostname = "netmon";
static const char* kApSsid = "netmon-setup";
static const uint32_t kPerNetworkTimeoutMs = 12000;

// How each network fared during this boot's join attempts, for the network
// history table. Kept by SSID, not position, so it stays true when the list is
// reordered or an entry is forgotten before the next restart.
struct BootTry {
    char ssid[33];
    JoinResult result;
    uint32_t ms;
    uint8_t reason;           // the Wi-Fi stack's code, 0 for none
};
static const uint8_t kMaxTries = 4;
static BootTry g_tries[kMaxTries];
static uint8_t g_try_count = 0;

static void record_try(const char* ssid, JoinResult r, uint32_t ms,
                       uint8_t reason) {
    if (g_try_count >= kMaxTries) return;
    BootTry& t = g_tries[g_try_count++];
    strncpy(t.ssid, ssid, sizeof(t.ssid) - 1);
    t.ssid[sizeof(t.ssid) - 1] = '\0';
    t.result = r;
    t.ms = ms;
    t.reason = reason;
}

// The network being tried, and the most telling reason the station has been
// dropped from it so far. Written by the Wi-Fi event task, read by wifi_begin.
static char g_target[33] = {0};
static volatile JoinResult g_drop = JoinResult::NotTried;
static volatile uint8_t g_drop_reason = 0;

static uint8_t drop_rank(JoinResult r) {
    switch (r) {
        case JoinResult::Refused:  return 3;
        case JoinResult::Failed:   return 2;
        case JoinResult::NotFound: return 1;
        default:                   return 0;
    }
}

// A rejected key says more than a failed association, which says more than
// not finding the network at all, so the strongest reason seen is the one kept.
static void on_join_dropped(arduino_event_id_t, arduino_event_info_t info) {
    const wifi_event_sta_disconnected_t& d = info.wifi_sta_disconnected;
    if (d.ssid_len > 0 &&
        (static_cast<size_t>(d.ssid_len) != strlen(g_target) ||
         memcmp(d.ssid, g_target, d.ssid_len) != 0)) {
        return;                                // a late event about another network
    }
    JoinResult r = JoinResult::Failed;         // in range, but could not associate
    switch (d.reason) {
        case WIFI_REASON_ASSOC_LEAVE:
            return;                            // our own WiFi.disconnect()
        case WIFI_REASON_NO_AP_FOUND:
            r = JoinResult::NotFound;
            break;
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            r = JoinResult::Refused;           // the key was not accepted
            break;
        default:
            break;
    }
    if (drop_rank(r) > drop_rank(g_drop)) {
        g_drop = r;
        g_drop_reason = d.reason;
    }
}

static uint32_t to_u32(const IPAddress& a) {
    return (static_cast<uint32_t>(a[0]) << 24) |
           (static_cast<uint32_t>(a[1]) << 16) |
           (static_cast<uint32_t>(a[2]) << 8) | static_cast<uint32_t>(a[3]);
}

static IPAddress from_u32(uint32_t v) {
    return IPAddress(static_cast<uint8_t>((v >> 24) & 0xFF),
                     static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>(v & 0xFF));
}

WifiState wifi_begin(const Settings& s) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);

    // Sent in the DHCP request, so the router's client list names this board
    // instead of leaving it blank among the other Espressif MACs. Must be set
    // after mode() and before begin(), or the request goes out without it.
    WiFi.setHostname(kHostname);

    if (!s.use_dhcp && s.static_ip != 0) {
        const bool ok = WiFi.config(from_u32(s.static_ip), from_u32(s.static_gw),
                                    from_u32(s.static_mask),
                                    from_u32(s.static_dns));
        Serial.print(F("[wifi] static config "));
        Serial.println(ok ? F("applied") : F("REJECTED"));
    }

    // Several access points can share one network name: a router and an
    // extender, or a mesh. The core's default fast scan stops at the first
    // one it hears, which had this board 50 dB down on a far access point
    // while the same network was right beside it. Scanning every channel lets
    // the strongest win, for a second or two more at start-up.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

    // Why an attempt failed comes from the station's own disconnect events,
    // not WiFi.status(). The status is only a summary: it drops back to plain
    // "disconnected" between the core's retries, and a wrong WPA2 password
    // usually shows up as a handshake timeout that the status never reports
    // as a failed connection at all.
    const auto watch =
        WiFi.onEvent(on_join_dropped, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    for (uint8_t i = 0; i < s.net_count; ++i) {
        Serial.print(F("[wifi] trying "));
        Serial.println(s.nets[i].ssid);
        strncpy(g_target, s.nets[i].ssid, sizeof(g_target) - 1);
        g_target[sizeof(g_target) - 1] = '\0';
        g_drop = JoinResult::NotTried;
        g_drop_reason = 0;
        WiFi.begin(s.nets[i].ssid, s.nets[i].pass);
        const uint32_t started = millis();
        while (millis() - started < kPerNetworkTimeoutMs) {
            if (WiFi.status() == WL_CONNECTED) {
                WiFi.removeEvent(watch);
                record_try(s.nets[i].ssid, JoinResult::Joined, millis() - started, 0);
                strncpy(g_ssid, s.nets[i].ssid, sizeof(g_ssid) - 1);
                g_ssid[sizeof(g_ssid) - 1] = '\0';
                g_state = WifiState::Connected;
                Serial.print(F("[wifi] connected, ip "));
                Serial.println(WiFi.localIP());
                return g_state;
            }
            delay(200);
        }
        const JoinResult seen = g_drop;
        const JoinResult why = seen == JoinResult::NotTried ? JoinResult::NoAnswer : seen;
        record_try(s.nets[i].ssid, why, millis() - started, g_drop_reason);
        Serial.print(F("[wifi] gave up on "));
        Serial.print(s.nets[i].ssid);
        Serial.print(F(": "));
        Serial.print(join_result_text(why));
        Serial.print(F(", reason "));
        Serial.println(g_drop_reason);
        WiFi.disconnect();
    }
    WiFi.removeEvent(watch);

    // Nothing answered. Raise our own AP so the config page stays reachable
    // — without this the device is unusable on arrival at a new site.
    Serial.println(F("[wifi] no known network; starting SoftAP"));
    // AP_STA, not AP. scanNetworks() calls enableSTA(true) internally, so an
    // AP-only radio would switch modes the moment someone pressed Scan — and
    // the phone sitting on the setup network is what pressed it. Starting in
    // both modes means the scan changes nothing.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid);
    strncpy(g_ssid, kApSsid, sizeof(g_ssid) - 1);
    g_ssid[sizeof(g_ssid) - 1] = '\0';
    g_state = WifiState::SoftAP;
    return g_state;
}

WifiState wifi_state() { return g_state; }

uint32_t wifi_local_ip() {
    return g_state == WifiState::SoftAP ? to_u32(WiFi.softAPIP())
                                        : to_u32(WiFi.localIP());
}

uint32_t wifi_netmask() { return to_u32(WiFi.subnetMask()); }
uint32_t wifi_gateway() { return to_u32(WiFi.gatewayIP()); }
uint32_t wifi_dns() { return to_u32(WiFi.dnsIP()); }

Mac wifi_mac() {
    // The station MAC, which is what other hosts on the LAN see. In SoftAP
    // mode WiFi.macAddress() still returns it, so the identity the dashboard
    // shows for this board does not change with the connection state.
    Mac m{};
    WiFi.macAddress(m.b);
    return m;
}
const char* wifi_current_ssid() { return g_ssid; }

JoinResult wifi_boot_result(const char* ssid, uint32_t& ms, uint8_t& reason) {
    ms = 0;
    reason = 0;
    if (ssid == nullptr) return JoinResult::NotTried;
    for (uint8_t i = 0; i < g_try_count; ++i) {
        if (strcmp(g_tries[i].ssid, ssid) == 0) {
            ms = g_tries[i].ms;
            reason = g_tries[i].reason;
            return g_tries[i].result;
        }
    }
    return JoinResult::NotTried;
}

const char* join_result_text(JoinResult r) {
    switch (r) {
        case JoinResult::Joined:   return "joined";
        case JoinResult::NotFound: return "not_found";
        case JoinResult::Failed:   return "failed";
        case JoinResult::Refused:  return "refused";
        case JoinResult::NoAnswer: return "no_answer";
        case JoinResult::NotTried: break;
    }
    return "not_tried";
}
