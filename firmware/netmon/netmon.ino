// ESP32 Network Monitor — complete firmware.
// Device discovery, DHCP hostname capture, classification, event history,
// gateway latency, ISP lookup, configuration, OTA and captive portal; and,
// from 0.10.0, the Wi-Fi + BLE scanner: the Nearby page, with a radar for
// Wi-Fi networks and one for Bluetooth devices. From 0.11.0 those are tabs of
// their own, beside a Finder for walking up to one device, and the Map page
// draws the local network. From 0.12.0 the API also answers over Bluetooth,
// to phones paired with a 6-digit code (src/core/ble_link.h), and the board
// keeps a saved report of each network it has been on (src/core/report.h).
// From 0.13.0 everything needs signing in (src/core/auth.h), the owner can set
// the pairing code and the board's Wi-Fi MAC address, and pairing is started
// by the phone alone, which is what made it fail before. From 0.15.0 the
// Devices page can scan a device's common TCP ports (src/hw/port_scan.h), and
// the board opens a pairing window at start-up, so a phone can pair without
// anything on the board's Wi-Fi.
//
// Board:     ESP32 Dev Module
// Partition: Minimal SPIFFS (1.9MB APP with OTA)
//            Bluetooth does not fit beside everything else in the default
//            1.2 MB. Changing the scheme needs one flash over USB; see
//            CHANGELOG.md (0.10.0) for what that does to saved settings.
// Secrets:   copy secrets.example.h to secrets.h, next to this file, and set
//            your own update password. secrets.h is never committed.
// Libraries: ArduinoJson v7, NimBLE-Arduino v2 (h2zero)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <cstring>
#include <memory>
#include <new>

#include "src/core/air_ble.h"
#include "src/core/air_find.h"
#include "src/core/air_plan.h"
#include "src/core/air_wifi.h"
#include "src/core/auth.h"
#include "src/core/baseline.h"
#include "src/core/ble_link.h"
#include "src/core/ble_type.h"
#include "src/core/ble_vendor.h"
#include "src/core/cidr.h"
#include "src/core/classify.h"
#include "src/core/device_table.h"
#include "src/core/event_log.h"
#include "src/core/lan_guard.h"
#include "src/core/name_cache.h"
#include "src/core/net_list.h"
#include "src/core/origin.h"
#include "src/core/oui_table.h"
#include "src/core/refresh.h"
#include "src/hw/port_scan.h"
#include "src/core/report.h"
#include "src/core/scan_list.h"
#include "src/core/sweep.h"
#include "src/core/validate.h"
#include "src/hw/air_scan.h"
#include "src/hw/arp_scan.h"
#include "src/hw/auth_store.h"
#include "src/hw/baseline_store.h"
#include "src/hw/ble_link.h"
#include "src/hw/config_store.h"
#include "src/hw/dhcp_capture.h"
#include "src/hw/isp_lookup.h"
#include "src/hw/latency.h"
#include "src/hw/name_store.h"
#include "src/hw/pages.h"
#include "src/hw/report_store.h"
#include "src/hw/scanner_import.h"
#include "src/hw/uptime.h"
#include "src/hw/wifi_manager.h"

// The update password guards the web firmware updater, POST /api/update and
// ArduinoOTA. It is kept out of the repository, in secrets.h.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "netmon: copy secrets.example.h to secrets.h (next to netmon.ino) and set NETMON_UPDATE_PASSWORD"
#endif
static_assert(sizeof(NETMON_UPDATE_PASSWORD) > 8,
              "netmon: NETMON_UPDATE_PASSWORD in secrets.h needs at least 8 characters");
static_assert(!same_text(NETMON_UPDATE_PASSWORD, "change-me"),
              "netmon: choose your own NETMON_UPDATE_PASSWORD in secrets.h");

static const char* kFirmwareVersion = "0.15.0-ports";
static const char* kOtaHostname = "netmon";
static const char* kOtaPassword = NETMON_UPDATE_PASSWORD;

static Settings g_settings;
static WebServer g_server(80);

// --- One API, two ways in ------------------------------------------------------
//
// Every /api/ endpoint answers the same over Wi-Fi, through the web server,
// and over the Bluetooth link (src/hw/ble_link.h) to a paired phone. The
// handlers never talk to either directly: they read the request and answer
// through the few functions here, which go to whichever the request came in
// on. The pages themselves are Wi-Fi only.
enum class Via : uint8_t { Http, Link };
static Via g_via = Via::Http;
static const LinkIncoming* g_link_req = nullptr;    // while serving the link

static bool api_has_arg(const char* name) {
    if (g_via == Via::Link) {
        char v[2];
        return link_arg(g_link_req->req.query, name, v, sizeof(v));
    }
    return g_server.hasArg(name);
}

static String api_arg(const char* name) {
    if (g_via == Via::Link) {
        char v[64];
        return link_arg(g_link_req->req.query, name, v, sizeof(v)) ? String(v) : String();
    }
    return g_server.arg(name);
}

static String api_body() {
    if (g_via == Via::Link) {
        return String(g_link_req->req.body, static_cast<unsigned int>(g_link_req->req.body_len));
    }
    return g_server.arg("plain");
}

// Only Origin, Host, X-Netmon-Key and Authorization are ever asked for.
// Nothing on the link has an Origin or a Host: no web page sends over it.
static String api_header(const char* name) {
    if (g_via == Via::Link) {
        if (strcasecmp(name, "X-Netmon-Key") == 0) return String(g_link_req->req.key);
        if (strcasecmp(name, "Authorization") == 0) return String(g_link_req->req.auth);
        return String();
    }
    return g_server.header(name);
}

static void api_send(int code, const char* type, const String& body) {
    if (g_via == Via::Link) {
        ble_link_answer_status(static_cast<uint16_t>(code));
        ble_link_answer_add(body);
        return;
    }
    g_server.send(code, type, body);
}

// An answer sent in pieces, so a long one never needs one long allocation.
static void api_begin_pieces(int code, const char* type) {
    if (g_via == Via::Link) {
        ble_link_answer_status(static_cast<uint16_t>(code));
        return;
    }
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(code, type, "");
}

static void api_piece(const String& part) {
    if (part.length() == 0) return;
    if (g_via == Via::Link) {
        ble_link_answer_add(part);
        return;
    }
    g_server.sendContent(part);
}

static void api_end_pieces() {
    if (g_via == Via::Http) g_server.sendContent("");    // the terminating empty chunk
}

// --- Signing in (0.13) ------------------------------------------------------------
//
// Every page and API call needs a session; see src/core/auth.h. The browser
// carries it in a cookie, the app as "Authorization: Bearer <token>", over
// Wi-Fi and over the Bluetooth link alike.
static AuthState g_auth;
static LoginThrottle<8> g_throttle;
static int g_session = -1;                 // the session of the request being served
static const char* kCookie = "nm_s";
static uint32_t g_auth_purged_ms = 0;

static uint32_t now_s();
static uint32_t clock_unix();

// Where a request comes from, for counting wrong passwords: its IPv4
// address, or the Bluetooth connection it came in on.
static uint32_t api_client() {
    if (g_via == Via::Link) return 0xB1E00000u | g_link_req->conn;
    return static_cast<uint32_t>(g_server.client().remoteIP());
}

// The session the request carries, or -1. A session used now counts as used.
static int session_of_request() {
    char tok[kTokenHex + 8];
    bool have = false;
    const String a = api_header("Authorization");
    if (a.length() > 0) have = bearer_value(a.c_str(), tok, sizeof(tok));
    if (!have && g_via == Via::Http) {
        const String c = g_server.header("Cookie");
        have = c.length() > 0 && cookie_value(c.c_str(), kCookie, tok, sizeof(tok));
    }
    if (!have || !token_well_formed(tok)) return -1;
    uint8_t h[32];
    token_hash(tok, h);
    const int i = g_auth.sessions.find(h, now_s(), clock_unix());
    if (i >= 0) g_auth.sessions.touch(i, now_s());
    return i;
}

// 401 with a mark the pages and the app tell apart from a wrong update
// password: X-Netmon-Login over Wi-Fi, "login" in the body either way (the
// link carries no headers back).
static void send_login_required() {
    if (g_via == Via::Http) {
        g_server.sendHeader("X-Netmon-Login", "required");
        g_server.sendHeader("Cache-Control", "no-store");
    }
    api_send(401, "application/json",
             F("{\"error\":\"Sign in to the monitor first.\",\"login\":true,\"netmon\":true}"));
}

static bool require_session() {
    g_session = session_of_request();
    if (g_session >= 0) return true;
    send_login_required();
    return false;
}

static void auth_store() {
    if (!auth_save(g_auth)) Serial.println(F("[auth] could not write /auth.json"));
}

// The clock has just been learned: sessions made without it get dated.
static void clock_learned() {
    if (g_auth.sessions.date_undated(now_s(), clock_unix())) auth_store();
}

// Forgotten sessions out of the file now and then: they would be refused
// anyway, but the file is the list of who may come back.
static void auth_tick() {
    if (millis() - g_auth_purged_ms < 3600000UL) return;
    g_auth_purged_ms = millis();
    if (g_auth.sessions.purge(now_s(), clock_unix())) auth_store();
}

static void set_session_cookie(const char* token, bool remember) {
    String c = kCookie;
    c += '=';
    c += token;
    c += F("; Path=/; HttpOnly; SameSite=Lax");
    if (remember) {
        c += F("; Max-Age=");
        c += String(kSessionLongS);
    }
    g_server.sendHeader("Set-Cookie", c);
}

static void clear_session_cookie() {
    String c = kCookie;
    c += F("=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
    g_server.sendHeader("Set-Cookie", c);
}

// Pages need a session as the API does; without one the browser goes to the
// sign-in page, which comes back here afterwards.
static void append_url_encoded(String& out, const String& s) {
    static const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += c;
        } else {
            out += '%';
            out += hex[(static_cast<uint8_t>(c) >> 4) & 15];
            out += hex[static_cast<uint8_t>(c) & 15];
        }
    }
}

static bool page_gate() {
    g_via = Via::Http;
    if (session_of_request() >= 0) return true;
    String here = g_server.uri();
    if (g_server.args() > 0) {
        here += '?';
        for (int i = 0; i < g_server.args(); ++i) {
            if (i) here += '&';
            here += g_server.argName(i);
            here += '=';
            here += g_server.arg(i);
        }
    }
    String to = F("/login?next=");
    append_url_encoded(to, here);
    g_server.sendHeader("Location", to, true);
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(302, "text/plain", "");
    return false;
}

static void send_page(const char* html) {
    if (!page_gate()) return;
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send_P(200, "text/html", html);
}

// Before a deliberate restart: lets the answer reach whoever asked.
static void api_settle(uint32_t http_ms) {
    if (g_via == Via::Link) {
        ble_link_answer_end();
        ble_link_flush(2000);
        return;
    }
    delay(http_ms);
}

// Only runs in setup mode. Android and iOS both probe a known URL after
// joining a network and, finding no internet, quietly route everything over
// mobile data — so typing 192.168.4.1 can fail to reach a board you are
// standing next to. Answering every name with our own address turns that
// probe into a "sign in to network" notice that opens the settings page.
static DNSServer g_dns;
static bool g_portal = false;

static const size_t kMaxDevices = 64;
static DeviceTable<kMaxDevices> g_devices;
static SweepPlan<6> g_sweep;
static EventLog<48> g_events;
static LatencyInfo g_latency{};
static uint32_t g_last_probe_ms = 0;
static uint32_t g_dhcp_packets = 0;
static bool g_dhcp_info_valid = false;
static Mac g_dhcp_client{};
static uint32_t g_dhcp_ip = 0;
static uint32_t g_dhcp_server = 0;
static uint32_t g_dhcp_lease_s = 0;
static uint8_t g_dhcp_msg_type = 0;
static uint32_t g_dhcp_last_s = 0;
static char g_dhcp_hostname[64] = {0};

// Hostnames by MAC, kept in flash. A device only names itself when it joins
// the network, so what is learned has to outlive a restart to be much use.
static Names g_names;
static bool g_names_dirty = false;
static uint32_t g_names_changed_ms = 0;

// What the board has learned about the network it is on, kept on flash from
// 0.14: the devices it recognises, the router's MAC, the DHCP servers and the
// network's own access points. See baseline.h. Loaded once the board has
// joined, by baseline_tick(), and for that network only.
static Baseline g_base;
static bool g_base_loaded = false;
static bool g_base_dirty = false;
static uint32_t g_base_changed_ms = 0;

// The LAN watch: router MAC, address clashes, DHCP servers, access points.
// See lan_guard.h. What it notices is logged once, then again hourly while it
// carries on, and forgotten a day after it stops.
static GuardLog<16> g_guard;
static IpClaims<64> g_claims;
static uint32_t g_own_dhcp = 0;             // the server that leased this board its address
static uint32_t g_guard_expired_ms = 0;
static const uint32_t kGuardQuietS = 3600;
static const uint32_t kGuardForgetS = 86400;

// The sweep is a small state machine driven from loop(), never a blocking
// walk: the web server shares this thread, and a multi-second busy loop
// would make the dashboard unresponsive during every scan.
static uint32_t g_last_scan_ms = 0;
static uint32_t g_batch_sent_ms = 0;
static bool g_batch_pending = false;
static uint32_t g_pass_hits = 0;
static uint32_t g_passes_done = 0;
static uint32_t g_pass_started_ms = 0;
static uint32_t g_pass_started_s = 0;
static uint32_t g_last_pass_ms = 0;      // duration of the most recent pass
static uint32_t g_pass_seen = 0;         // distinct devices seen in that pass

// Anchors the learning window. Zero means discovery has never succeeded,
// which classify.h treats as "window still open" — a device that cannot
// scan must never accuse anything.
static uint32_t g_first_scan_s = 0;

// Who this connection belongs to, according to the outside world. Cached
// hard: the answer changes when the ISP re-addresses the line, which is rare,
// and every check is a request made to somebody else's service.
static IspInfo g_isp{};
static uint32_t g_isp_try_s = 0;
static uint32_t g_isp_ok_s = 0;
static const uint32_t kIspIntervalS = 21600;   // six hours
static const uint32_t kIspBackoffS = 120;      // floor between attempts

// Saved reports, one per network (src/core/report.h), and the clock that
// dates them. The board has no clock of its own: it learns the time from the
// provider lookup's Date header, or from the app or a page.
static Clock g_clock{};
static ReportSlot g_reports[kReportSlots];
static const size_t kReportReserve = 24576;     // left free for settings and names
static bool g_report_first_done = false;
static uint32_t g_report_last_ms = 0;

static const uint32_t kSettleMs = 150;   // let ARP replies land before reading

// lwIP's ARP table holds ARP_TABLE_SIZE (10) entries and we do not get it to
// ourselves: the gateway, the DNS resolver and anything the sketch talks to
// occupy slots too, and lwIP evicts the oldest to make room. A batch of 8 could
// therefore have its earliest replies pushed out before the settle read, and
// those hosts would go missing from the pass. Six leaves four slots for
// everything else. The cost is only sweep time — 254 hosts take 43 batches
// instead of 32, about 6.5s against a 60s scan interval.
static const size_t kBatch = 6;

// --- Nearby: what the Wi-Fi + BLE scanner sketch did ----------------------
//
// The access points and Bluetooth devices around the board, for the Nearby
// page and its two radars. Both tables are only ever touched from loop().
// When the scans may have the radio is air_plan.h's decision; the short of it
// is that the sweep and the latency probe come first.
static const size_t kAirAps = 64;
static const size_t kAirBleDevices = 64;
static WifiAirTable<kAirAps> g_air_aps;
static BleAirTable<kAirBleDevices> g_air_ble;
static AirRadio g_air_wifi{};             // .enabled is filled in from settings
static AirRadio g_air_bt{};
static AirJob g_air_job = AirJob::None;   // the Nearby scan holding the radio
static uint32_t g_air_watch_ms = 0;       // the last read of /api/nearby
static uint32_t g_air_wifi_failures = 0;
static uint32_t g_air_wifi_took_ms = 0;   // how long the last scan had the radio
static uint32_t g_air_bursts = 0;
static uint32_t g_air_burst_end_s = 0;
static uint32_t g_air_expired_ms = 0;
static bool g_updating = false;           // a firmware update has the board

// Port scanner: probes a fixed list of common TCP ports on one LAN host.
// One scan at a time; results survive until a new scan starts.
static PortScanner g_portscan;

// The Finder: one device listened for closely while somebody walks up to it.
// See air_find.h. What the device called itself is copied when it is picked,
// since the Nearby table forgets a Bluetooth device unheard for five minutes.
static FindTarget g_find{};
static FindTrace<64> g_find_trace;
static AirFind g_find_plan{};
static char g_find_name[kSsidMax] = {0};   // Bluetooth name or SSID
static uint32_t g_find_wide_ms = 0;        // the last look on every channel
// The device found last, kept when the Finder stops, so a page that comes
// back to it can carry on after the Nearby table has forgotten it.
static FindTarget g_find_prev{};
static char g_find_prev_name[kSsidMax] = {0};
// The sweep and the probe held off while somebody turns for a direction.
static FindHold g_find_hold{};
// When the Bluetooth burst holding the radio started, and how long it was for.
static uint32_t g_air_ble_started_ms = 0;
static uint32_t g_air_ble_len_ms = 0;

// A Bluetooth device not heard for five minutes is forgotten. Phones change
// address every quarter of an hour or so, and without this the list would
// fill with addresses nobody uses any more.
static const uint32_t kAirBleForgetS = 300;

// Seconds since boot, for every timestamp the firmware keeps. See uptime.h
// for why this is no longer millis() / 1000.
static uint32_t now_s() { return uptime_s(); }

static void append_json_escaped(String& out, const char* s);
static void before_restart();
static void guard_aps(uint32_t now);

// Who has the radio. A pass is in flight from its first batch until the last
// batch's replies are read. See air_plan.h for why the two never overlap.
static bool sweep_running() { return g_sweep.active() || g_batch_pending; }
static bool air_busy() { return g_air_job != AirJob::None; }

// Ends finding, keeping what it was finding for a page that comes back to it.
static void find_stop() {
    if (g_find.kind != FindKind::None) {
        g_find_prev = g_find;
        std::memcpy(g_find_prev_name, g_find_name, sizeof(g_find_prev_name));
    }
    g_find = FindTarget{};
    g_find_plan = AirFind{};
    air_find_unhold(g_find_hold);
}

// Whether the radio the Finder needs is switched on, and for Bluetooth,
// started. When it is not, the ordinary scans carry on and the Finder says why.
static bool find_radio_on() {
    if (g_find.kind == FindKind::Wifi) return g_settings.air_wifi;
    if (g_find.kind == FindKind::Ble) return g_settings.air_ble && air_ble_ready();
    return false;
}

static bool finding() {
    return air_find_live(g_find, millis()) && find_radio_on();
}

static bool find_holding() {
    return finding() && air_find_holding(g_find_hold, millis());
}

static bool sweep_due() {
    return wifi_state() == WifiState::Connected && !sweep_running() &&
           millis() - g_last_scan_ms >= g_settings.scan_interval_s * 1000UL;
}

// Moves a finished Wi-Fi scan's results into the Nearby table and frees them.
static void air_wifi_absorb(int count) {
    const uint32_t now = now_s();
    AirApReading r;
    for (int i = 0; i < count; ++i) {
        if (air_wifi_reading(i, r)) {
            g_air_aps.heard(r.bssid, r.ssid, r.rssi, r.channel, r.auth, now);
        }
    }
    air_wifi_release();
    g_air_aps.scan_end(now);
    guard_aps(now);
}

// A finished Finder look: at most the one access point it asked for. Into the
// Nearby table too, but with no scan_end(), which would count every other
// network as missed by a scan that never listened for them.
static void find_wifi_absorb(int count) {
    const uint32_t now = now_s();
    AirApReading r;
    for (int i = 0; i < count; ++i) {
        if (!air_wifi_reading(i, r) || !mac_equal(r.bssid, g_find.addr)) continue;
        g_find_trace.add(millis(), r.rssi);
        g_air_aps.heard(r.bssid, r.ssid, r.rssi, r.channel, r.auth, now);
        if (r.channel != 0) g_find.channel = r.channel;
        break;
    }
    air_wifi_release();
}

// Collects the Nearby Wi-Fi scan or Finder look when it has finished; with
// `wait`, waits for it. True once it is over, whether it found anything or
// failed.
static bool air_wifi_collect(bool wait) {
    if (g_air_job != AirJob::Wifi && g_air_job != AirJob::FindWifi) return true;
    const bool look = g_air_job == AirJob::FindWifi;
    int count = 0;
    AirScanState st = air_wifi_poll(kAirWifiLimitMs, count);
    while (wait && st == AirScanState::Running) {
        delay(20);
        st = air_wifi_poll(kAirWifiLimitMs, count);
    }
    if (st == AirScanState::Running) return false;
    if (look) {
        if (st == AirScanState::Done) {
            find_wifi_absorb(count);
        } else {
            g_find_plan.failed = true;
        }
    } else {
        if (st == AirScanState::Done) {
            air_wifi_absorb(count);
        } else {
            ++g_air_wifi_failures;
        }
        g_air_wifi_took_ms = millis() - g_air_wifi.last_ms;
    }
    g_air_job = AirJob::None;
    return true;
}

static uint32_t derived_subnet() {
    if (g_settings.subnet_override != 0) return g_settings.subnet_override;
    return subnet_of(wifi_local_ip(), wifi_netmask());
}

static uint32_t derived_mask() {
    if (g_settings.mask_override != 0) return g_settings.mask_override;
    return wifi_netmask();
}

// Seconds left before new devices start being classified as unknown. Reports
// the full window while the baseline is unanchored, since the clock has not
// started yet, and none at all once this network's list has been learned.
static uint32_t baseline_remaining_s() {
    if (g_base.learned) return 0;
    const uint32_t window = g_settings.learning_window_s;
    if (g_first_scan_s == 0) return window;
    const uint32_t now = now_s();
    if (now < g_first_scan_s) return window;      // clock stepped backwards
    const uint32_t elapsed = now - g_first_scan_s;
    return elapsed >= window ? 0u : window - elapsed;
}

static void handle_health() {
    char ip[16], gw[16], cidr[20];
    ipv4_format(wifi_local_ip(), ip);
    ipv4_format(wifi_gateway(), gw);
    cidr_format(derived_subnet(), derived_mask(), cidr);

    const char* state = wifi_state() == WifiState::Connected ? "connected"
                        : wifi_state() == WifiState::SoftAP ? "softap"
                                                            : "connecting";

    String body;
    body.reserve(480);
    body += F("{\"status\":\"ok\",\"version\":\"");
    body += kFirmwareVersion;
    body += F("\",\"wifi\":\"");
    body += state;
    body += F("\",\"ssid\":\"");
    append_json_escaped(body, wifi_current_ssid());
    body += F("\",\"ip\":\"");
    body += ip;
    body += F("\",\"gateway\":\"");
    body += gw;
    body += F("\",\"subnet\":\"");
    body += cidr;
    body += F("\",\"rssi\":");
    body += String(WiFi.RSSI());
    body += F(",\"uptime_s\":");
    // The same clock as every at_s and last-seen figure, so the pages can
    // subtract one from the other.
    body += String(now_s());
    body += F(",\"free_heap\":");
    body += String(ESP.getFreeHeap());
    body += F(",\"sweepable\":");
    body += (sweepable(derived_subnet(), derived_mask()) ? F("true") : F("false"));
    body += F(",\"devices\":");
    body += String(g_devices.size());
    body += F(",\"scan_passes\":");
    body += String(g_passes_done);
    body += F(",\"scan_remaining\":");
    body += String(g_sweep.remaining());
    body += F(",\"last_pass_ms\":");
    body += String(g_last_pass_ms);
    body += F(",\"pass_seen\":");
    body += String(g_pass_seen);
    body += F(",\"pass_merges\":");
    body += String(g_pass_hits);
    body += F(",\"arp_cache\":");
    body += String(arp_cache_capacity());
    body += F(",\"latency_valid\":");
    body += (g_latency.valid ? F("true") : F("false"));
    body += F(",\"latency_ms\":");
    body += String(g_latency.rtt_ms);
    body += F(",\"latency_age_s\":");
    const uint32_t health_now = now_s();
    body += String(g_latency.checked_s && health_now >= g_latency.checked_s
                       ? health_now - g_latency.checked_s : 0);
    body += F(",\"dhcp_packets\":");
    body += String(g_dhcp_packets);
    body += F(",\"events\":");
    body += String(g_events.size());
    // baseline_open used to report (g_first_scan_s == 0), which is whether the
    // window has been ANCHORED — the opposite of what the name promises, and
    // it flipped to false one minute after boot while the 600s window was
    // still wide open. This is the field you read to answer "will a new device
    // be flagged as an intruder?", so it now answers exactly that.
    // From 0.14 a network learned before is never "open" again: its saved
    // list decides from the first sweep after a restart.
    body += F(",\"baseline_open\":");
    body += (still_learning(now_s(), g_first_scan_s, g_settings.learning_window_s,
                            g_base.learned) ? F("true") : F("false"));
    body += F(",\"baseline_anchored\":");
    body += (g_first_scan_s != 0 ? F("true") : F("false"));
    body += F(",\"baseline_closes_in_s\":");
    body += String(baseline_remaining_s());
    body += F(",\"names_known\":");
    body += String(g_names.size());
    // From 0.14: the list kept on flash for this network, and what the LAN
    // watch has noticed. /api/guard has the detail.
    body += F(",\"baseline_saved\":");
    body += (g_base.learned ? F("true") : F("false"));
    body += F(",\"known_saved\":");
    body += String(g_base.known_count);
    body += F(",\"alerts\":");
    body += String(g_guard.size());
    // From 0.12: who this board is, so a phone that reaches it both over
    // Wi-Fi and over Bluetooth knows the two are one board, and whether the
    // Bluetooth link is on. From 0.13 that is the chip's own Wi-Fi address
    // whatever address the owner has set, so the board stays the same board;
    // "wifi_mac" is the address it goes by on the network now.
    char id[18], active[18];
    mac_format(wifi_factory_mac(), id);
    mac_format(wifi_mac(), active);
    body += F(",\"mac\":\"");
    body += id;
    body += F("\",\"wifi_mac\":\"");
    body += active;
    body += F("\",\"ble_link\":");
    body += (ble_link_enabled() ? F("true") : F("false"));
    // Whether the board knows the time, from 0.12. The app and the pages
    // send theirs when it does not; see POST /api/clock.
    body += F(",\"clock\":");
    body += (clock_known(g_clock) ? F("true") : F("false"));
    body += F("}");

    api_send(200, "application/json", body);
}

static void handle_root() {
    if (!page_gate()) return;
    String body;
    body.reserve(256);
    body += F("<!doctype html><meta name=viewport content='width=device-width,"
              "initial-scale=1'><h1>Network Monitor</h1><p>Firmware ");
    body += kFirmwareVersion;
    body += F("</p><p><a href='/settings'>Settings</a> &middot; "
              "<a href='/map'>Map</a> &middot; "
              "<a href='/nearby'>Nearby</a> &middot; "
              "<a href='/nearby#finder'>Finder</a> &middot; "
              "<a href='/api/health'>/api/health</a></p>");
    g_server.send(200, "text/html", body);
}

static void handle_settings_page() { send_page(SETTINGS_HTML); }

// The Settings page's list of networks to join: those heard in the latest
// scan, one row per name at its strongest, hidden ones left out, strongest
// first. Since 0.10.0 it comes out of the Nearby table, so a scan finished in
// the last ten seconds is used as it is, and one in progress is waited for
// rather than refused: the radio runs one scan at a time. Otherwise the scan
// runs here, while the browser waits, and the Nearby page gets it too.
//
// The DHCP listener is no longer paused for the scan. That was promiscuous
// capture's need; the UDP listener that replaced it misses nothing it would
// otherwise hear, since off channel there is nothing to hear. With the Nearby
// page scanning every 15 seconds, stopping and restarting it each time would
// only have been more chances for the restart to fail.
static void handle_scan() {
    air_wifi_collect(true);
    const uint32_t now = now_s();
    const bool fresh = g_air_aps.scans() > 0 && now >= g_air_aps.last_scan_s() &&
                       now - g_air_aps.last_scan_s() <= 10;
    if (!fresh) {
        const int n = air_wifi_scan_blocking(kAirWifiLimitMs);
        if (n < 0) {
            api_send(503, "application/json",
                          F("{\"error\":\"Wi-Fi scan failed or is busy\"}"));
            return;
        }
        air_wifi_absorb(n);
    }

    static const size_t kMaxList = 24;
    ScanEntry list[kMaxList];
    size_t n = 0;
    for (size_t i = 0; i < g_air_aps.size(); ++i) {
        const AirAp& a = g_air_aps.at(i);
        if (g_air_aps.heard_last(a)) n = scan_add(list, n, kMaxList, a.ssid, a.rssi);
    }

    String body = "[";
    for (size_t i = 0; i < n; ++i) {
        if (i) body += ',';
        body += F("{\"ssid\":\"");
        // Anyone in radio range picks these names, control characters included.
        append_json_escaped(body, list[i].ssid);
        body += F("\",\"rssi\":");
        body += String(list[i].rssi);
        body += '}';
    }
    body += ']';
    api_send(200, "application/json", body);
}

// Settings, forget and restart are refused when a page on another site sent
// them; see origin.h. The Android app sends no Origin and is not affected, and
// nothing that comes over the Bluetooth link has one.
static bool refuse_other_site() {
    const String origin = api_header("Origin");
    const String host = g_via == Via::Http ? g_server.hostHeader() : String();
    if (origin_allowed(origin.c_str(), host.c_str())) return false;
    Serial.print(F("[http] refused a POST sent from "));
    Serial.println(origin);
    api_send(403, "application/json",
                  F("{\"error\":\"refused: the request came from a page on "
                    "another site\"}"));
    return true;
}

static void send_config_error(const char* text) {
    String body = F("{\"error\":\"");
    body += text;
    body += F("\"}");
    api_send(400, "application/json", body);
}

static void handle_config_post() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }

    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    ConfigError err = validate_credentials(ssid, pass);
    if (err != ConfigError::None) {
        send_config_error(config_error_text(err));
        return;
    }

    const bool use_dhcp = doc["use_dhcp"] | true;
    uint32_t ip = 0, mask = 0, gw = 0, dns = 0;
    if (!use_dhcp) {
        err = validate_static(doc["ip"] | "", doc["mask"] | "", doc["gw"] | "");
        if (err != ConfigError::None) {
            send_config_error(config_error_text(err));
            return;
        }
        ipv4_parse(doc["ip"] | "", ip);
        ipv4_parse(doc["mask"] | "", mask);
        ipv4_parse(doc["gw"] | "", gw);
        if (!ipv4_parse(doc["dns"] | "", dns)) dns = gw;
    }

    // Absent means unchanged, not "reset to the factory value". The settings
    // form no longer carries these fields, so defaulting to the constants
    // would have quietly undone any customised interval on every save.
    const uint32_t scan_s = doc["scan_interval_s"] | g_settings.scan_interval_s;
    const uint32_t probe_s = doc["probe_interval_s"] | g_settings.probe_interval_s;
    const uint32_t offline_s = doc["offline_after_s"] | g_settings.offline_after_s;
    const uint32_t learn_s = doc["learning_window_s"] | g_settings.learning_window_s;
    err = validate_intervals(scan_s, probe_s, offline_s, learn_s);
    if (err != ConfigError::None) {
        send_config_error(config_error_text(err));
        return;
    }

    Settings s = g_settings;

    // Up to four networks, tried newest first at start-up. The one being
    // saved goes to the front whether it is new or already known: updating a
    // known SSID in place is what kept a network the board had visited once
    // ahead of the one it lives on, at a 12 second timeout per restart. The
    // ordering and the blank-password rule live in net_list.h, under test.
    netlist_remember(s.nets, s.net_count, ssid, pass);

    s.use_dhcp = use_dhcp;
    s.static_ip = ip;
    s.static_mask = mask;
    s.static_gw = gw;
    s.static_dns = dns;
    s.scan_interval_s = scan_s;
    s.probe_interval_s = probe_s;
    s.offline_after_s = offline_s;
    s.learning_window_s = learn_s;

    if (!settings_save(s)) {
        api_send(500, "application/json",
                      F("{\"error\":\"could not write settings to flash\"}"));
        return;
    }
    g_settings = s;
    Serial.print(F("[cfg] saved, networks remembered: "));
    Serial.println(g_settings.net_count);
    api_send(200, "application/json", F("{\"status\":\"saved\"}"));
}

// --- discovery / events -------------------------------------------------

static void log_event(EventType type, const Mac& mac, uint32_t ip,
                      const char* text) {
    g_events.add(type, mac, ip, text);
    g_events.stamp_last(now_s());
}

// --- remembered devices -------------------------------------------------------

// Writes this network's record if anything in it changed since the last write.
// Called before every deliberate restart too, like names_flush().
static bool baseline_flush() {
    if (!g_base_dirty || !g_base_loaded) return true;
    if (!baseline_save(g_base)) return false;
    g_base_dirty = false;
    return true;
}

// Something worth keeping changed. `now` writes it straight away, for what
// somebody has just done by hand; otherwise baseline_tick() writes it ten
// seconds later, so a burst of changes costs one write.
static void baseline_changed(bool now) {
    if (!g_base_dirty) g_base_changed_ms = millis();
    g_base_dirty = true;
    if (now && !baseline_flush()) {
        Serial.println(F("[base] could not write the record, will retry"));
    }
}

// The learning window has closed on a network with nothing saved: what it
// learned becomes the list, and from now on the list decides.
static void baseline_learned() {
    const Mac self = wifi_mac();
    for (size_t i = 0; i < g_devices.size(); ++i) {
        const Device& d = g_devices.at(i);
        if (d.status != Status::Known || mac_is_local(d.mac) || mac_equal(d.mac, self)) continue;
        base_known_add(g_base, d.mac);
    }
    g_base.learned = true;
    baseline_changed(true);
    Serial.print(F("[base] learned "));
    Serial.print(g_base.known_count);
    Serial.println(F(" devices; saved for this network"));
}

// Loads the record for the network the board has joined, and writes a changed
// one once it has been quiet for ten seconds. The record belongs to one
// network: should the board ever find itself on another without a restart,
// it is put away and that network's own is loaded.
static void baseline_tick() {
    if (wifi_state() != WifiState::Connected) return;
    const char* ssid = wifi_current_ssid();
    if (ssid == nullptr || ssid[0] == '\0') return;
    if (!g_base_loaded || std::strcmp(g_base.ssid, ssid) != 0) {
        if (g_base_loaded) baseline_flush();
        base_reset(g_base, ssid);
        const bool had = baseline_load(g_base);
        g_base_loaded = true;
        g_base_dirty = false;
        g_guard.clear_all();
        g_claims.clear();
        // With a fixed address there is no lease and no server of our own.
        g_own_dhcp = g_settings.use_dhcp ? lan_dhcp_server() : 0;
        if (had && g_base.learned) {
            Serial.print(F("[base] this network was learned before: "));
            Serial.print(g_base.known_count);
            Serial.println(F(" devices recognised, no learning window"));
        } else {
            Serial.println(F("[base] nothing learned on this network yet"));
        }
    }
    if (g_base_dirty && millis() - g_base_changed_ms >= 10000UL && !baseline_flush()) {
        g_base_changed_ms = millis();
        Serial.println(F("[base] could not write the record, will retry"));
    }
    if (millis() - g_guard_expired_ms >= 60000UL) {
        g_guard_expired_ms = millis();
        g_guard.expire(now_s(), kGuardForgetS);
    }
}

static Status classify_for(const Mac& mac, uint32_t now) {
    const bool randomised = mac_is_local(mac);
    const bool listed = !randomised && base_known_has(g_base, mac);
    return classify_remembered(now, g_first_scan_s, g_settings.learning_window_s,
                               randomised, listed, g_base.learned);
}

// --- the LAN watch ------------------------------------------------------------

static void guard_tell(GuardKind kind, const Mac& mac, const Mac& other,
                       uint32_t ip, uint32_t now, const char* text) {
    if (!g_guard.raise(kind, mac, other, ip, now, kGuardQuietS)) return;
    log_event(guard_kind_event(kind), mac, ip, text);
    char m[18];
    mac_format(mac, m);
    Serial.print(F("[guard] "));
    Serial.print(guard_kind_text(kind));
    Serial.print(' ');
    Serial.print(m);
    Serial.print(F(": "));
    Serial.println(text);
}

// Two devices taking turns at one address are both heard within this long.
static uint32_t claim_window_s() {
    const uint32_t three = g_settings.scan_interval_s * 3;
    return three > 600 ? three : 600;
}

// One entry of the ARP cache. What answers for the router's address is checked
// against the record; every other address, for a second device answering.
static void guard_arp(const ArpHit& hit, uint32_t now) {
    const uint32_t gw = wifi_gateway();
    if (gw != 0 && hit.ip == gw) {
        switch (guard_router_check(g_base, hit.mac)) {
            case RouterVerdict::Learn:
                g_base.router = hit.mac;
                g_base.has_router = true;
                // Written with the rest when the window closes; at once on a
                // network learned by an older firmware, which kept no router.
                if (g_base.learned) baseline_changed(false);
                break;
            case RouterVerdict::Changed: {
                char was[18], text[64];
                mac_format(g_base.router, was);
                snprintf(text, sizeof(text), "router was %s", was);
                guard_tell(GuardKind::RouterChanged, hit.mac, g_base.router, gw, now, text);
                break;
            }
            case RouterVerdict::Same:
                break;
        }
        return;
    }
    Mac rival{};
    if (g_claims.observe(hit.ip, hit.mac, now, claim_window_s(), rival)) {
        char other[18], text[64];
        mac_format(rival, other);
        snprintf(text, sizeof(text), "also answered by %s", other);
        guard_tell(GuardKind::IpConflict, hit.mac, rival, hit.ip, now, text);
    }
}

// A DHCP REQUEST names, in option 54, the server whose offer the client took.
// While learning, every server heard is taken as this network's, the router
// included: recorded, it stays expected once another one is on record too.
// After, one that is not on record is told.
static void guard_dhcp(const DhcpInfo& info, uint32_t now) {
    if (info.msg_type != 3 || info.server_ip == 0) return;
    if (!g_base.learned) {
        base_add_dhcp(g_base, info.server_ip);
        return;
    }
    if (guard_dhcp_expected(g_base, info.server_ip, g_own_dhcp, wifi_gateway())) return;
    char sip[16], text[64];
    ipv4_format(info.server_ip, sip);
    snprintf(text, sizeof(text), "took an address from DHCP server %s", sip);
    guard_tell(GuardKind::DhcpServer, info.client, Mac{}, info.server_ip, now, text);
}

// After a Nearby Wi-Fi scan: every access point just heard with this
// network's name, against the record of its own.
static void guard_aps(uint32_t now) {
    if (wifi_state() != WifiState::Connected || !g_base_loaded) return;
    const char* ssid = wifi_current_ssid();
    if (ssid == nullptr || ssid[0] == '\0') return;
    for (size_t i = 0; i < g_air_aps.size(); ++i) {
        const AirAp& a = g_air_aps.at(i);
        if (!g_air_aps.heard_last(a) || std::strcmp(a.ssid, ssid) != 0) continue;
        char text[64];
        switch (guard_ap_check(g_base, a.bssid, a.auth)) {
            case ApVerdict::Learn:
                // Written when the window closes, or now when this network was
                // learned with no scan heard (Nearby Wi-Fi off at the time).
                if (base_put_ap(g_base, a.bssid, a.auth) && g_base.learned) baseline_changed(false);
                break;
            case ApVerdict::Stronger:
                if (base_put_ap(g_base, a.bssid, a.auth)) baseline_changed(false);
                break;
            case ApVerdict::Unknown:
                snprintf(text, sizeof(text), "unknown access point with your Wi-Fi name: %s",
                         air_auth_text(a.auth));
                guard_tell(GuardKind::RogueAp, a.bssid, Mac{}, 0, now, text);
                break;
            case ApVerdict::Weaker: {
                const BaseAp* was = base_find_ap(g_base, a.bssid);
                snprintf(text, sizeof(text), "now offers %s, was %s", air_auth_text(a.auth),
                         was != nullptr ? air_auth_text(was->auth) : "more");
                guard_tell(GuardKind::WeakAp, a.bssid, Mac{}, 0, now, text);
                break;
            }
            case ApVerdict::Fine:
                break;
        }
    }
}

// Every sighting goes through here, whether the sweep or a DHCP request
// noticed the device, so both log the same events. The DHCP path used to
// refresh the row without checking whether the device had been offline, and
// a phone rejoining is heard on DHCP before the sweep reaches it, so the most
// common return of all was never logged.
static void record_sighting(const Mac& mac, uint32_t ip, uint32_t now,
                            const char* first_text) {
    bool created = false;
    bool returned = false;
    // Status is decided when a row is made, so the list is only looked at then.
    const Status if_new = g_devices.find(mac) != nullptr ? Status::Known
                                                         : classify_for(mac, now);
    g_devices.sight(mac, ip, now, if_new, created, returned);
    if (!created && !returned) return;

    char m[18];
    mac_format(mac, m);
    if (created) {
        // A name learned on an earlier visit, or before a restart.
        const char* known = g_names.get(mac);
        if (known != nullptr) g_devices.set_hostname(mac, known);
        log_event(EventType::DeviceSeen, mac, ip, first_text);
        Serial.print(F("[dev] new device "));
    } else {
        log_event(EventType::DeviceBack, mac, ip, "device returned");
        Serial.print(F("[dev] device back "));
    }
    Serial.println(m);
}

static void merge_hit(const ArpHit& hit, uint32_t now) {
    if (!subnet_contains(derived_subnet(), derived_mask(), hit.ip)) return;
    record_sighting(hit.mac, hit.ip, now,
                    mac_is_local(hit.mac) ? "private device first seen"
                                          : "device first seen");
    ++g_pass_hits;
    guard_arp(hit, now);
}

static void finish_pass() {
    const uint32_t now = now_s();

    // g_pass_hits counts merge operations, not devices. arp_read_cache()
    // returns the whole ARP cache after every batch, so an entry that stays
    // resident is re-merged on each of the ~43 reads in a pass — which is why
    // a 30-device network reported 426 "hits". Harmless (upsert is
    // idempotent) but useless as a coverage figure, so count the devices
    // whose last_seen falls inside this pass instead.
    size_t others = 0;
    g_pass_seen = g_devices.seen_since(g_pass_started_s, wifi_mac(),
                                       wifi_gateway(), others);

    // The window starts at the first pass that actually found something,
    // not at boot: a device that spent ten minutes failing to scan would
    // otherwise flag every device it later discovers as an intruder.
    // "Something" leaves out this board, which records itself at the start
    // of every pass, and the gateway, whose ARP entry stays cached from
    // ordinary traffic even when the sweep reaches nobody, as on a network
    // that keeps wireless clients apart. Counting those two opened the
    // window on the very first pass, whatever it found.
    if (g_first_scan_s == 0 && others > 0) {
        g_first_scan_s = now;
        Serial.println(F("[scan] baseline window opened"));
    }
    Mac was_online[kMaxDevices]{};
    size_t online_count = 0;
    for (size_t i = 0; i < g_devices.size() && online_count < kMaxDevices; ++i) {
        if (g_devices.at(i).online) was_online[online_count++] = g_devices.at(i).mac;
    }
    const size_t went_offline =
        g_devices.mark_offline(now, g_settings.offline_after_s);
    if (went_offline) {
        for (size_t j = 0; j < online_count; ++j) {
            Device* d = g_devices.find(was_online[j]);
            if (d != nullptr && !d->online) {
                log_event(EventType::DeviceOffline, d->mac, d->ip,
                          "device went offline");
            }
        }
    }
    // Recency for the remembered list, once a pass. It only decides what makes
    // room when the list is full, and it lives in RAM.
    for (size_t i = 0; i < g_devices.size(); ++i) {
        const Device& d = g_devices.at(i);
        if (d.online && d.last_seen >= g_pass_started_s) base_known_touch(g_base, d.mac);
    }
    if (!g_base.learned && g_first_scan_s != 0 &&
        !in_learning_window(now, g_first_scan_s, g_settings.learning_window_s)) {
        baseline_learned();
    }
    // No "sweep finished" event, nor "sweep started": two a pass filled the
    // 48-entry history in about 26 minutes, pushing out the device events it
    // is there to keep. Pass counts and timing are on /api/health.
    ++g_passes_done;
    g_last_scan_ms = millis();
    g_last_pass_ms = g_last_scan_ms - g_pass_started_ms;
    g_batch_pending = false;

    Serial.print(F("[scan] pass "));
    Serial.print(g_passes_done);
    Serial.print(F(" done in "));
    Serial.print(g_last_pass_ms);
    Serial.print(F(" ms, seen "));
    Serial.print(g_pass_seen);
    Serial.print(F(" ("));
    Serial.print(g_pass_hits);
    Serial.print(F(" merges)"));
    Serial.print(F(", known devices "));
    Serial.print(g_devices.size());
    if (went_offline) {
        Serial.print(F(", went offline "));
        Serial.print(went_offline);
    }
    Serial.println();
}

static void scan_tick() {
    if (wifi_state() != WifiState::Connected) return;

    // A pending batch is drained before anything else. SweepPlan marks itself
    // inactive as soon as the LAST batch is handed out, not when that batch is
    // read, so testing active() first strands it: the replies are never
    // collected and finish_pass() never runs. That was not a small leak — no
    // pass counter, no offline sweep, no baseline window, and, because
    // g_last_scan_ms is only ever set by finish_pass(), no gap between passes
    // either. The sweep just ran flat out forever.
    if (g_batch_pending) {
        if (millis() - g_batch_sent_ms < kSettleMs) return;
        ArpHit hits[16];
        const size_t n = arp_read_cache(hits, 16);
        const uint32_t now = now_s();
        for (size_t i = 0; i < n; ++i) merge_hit(hits[i], now);
        g_batch_pending = false;
        if (!g_sweep.active()) finish_pass();
        return;
    }

    if (!g_sweep.active()) {
        if (millis() - g_last_scan_ms < g_settings.scan_interval_s * 1000UL) return;
        // Due, but a Nearby scan has the radio. A pass begun now would lose
        // replies to it, so it waits; no Nearby scan starts while it is due,
        // and the one running ends within seconds. See air_plan.h. Somebody
        // turning for a direction holds it off a little longer: air_find.h.
        if (air_busy() || find_holding()) return;
        g_pass_hits = 0;
        if (!g_sweep.begin(derived_subnet(), derived_mask())) {
            g_last_scan_ms = millis();      // nothing sweepable; try again later
            return;
        }
        g_pass_started_ms = millis();
        g_pass_started_s = now_s();

        // A host never ARPs itself, so this board can only ever be absent from
        // its own sweep. Record it explicitly, always as Known: the monitor is
        // not a candidate intruder.
        bool self_created = false;
        g_devices.upsert(wifi_mac(), wifi_local_ip(), g_pass_started_s,
                         Status::Known, self_created);
        if (self_created) g_devices.set_hostname(wifi_mac(), kOtaHostname);
        return;
    }

    uint32_t batch[kBatch];
    const size_t n = g_sweep.next_batch(batch, kBatch);
    if (n == 0) {
        finish_pass();
        return;
    }
    arp_request_batch(batch, n);
    g_batch_sent_ms = millis();
    g_batch_pending = true;
}

static void dhcp_tick() {
    if (wifi_state() != WifiState::Connected) return;

    DhcpInfo info{};
    while (dhcp_capture_poll(info)) {
        ++g_dhcp_packets;
        const uint32_t now = now_s();

        // The most recent DHCP message, kept whole, with or without a
        // hostname. Every field comes from this one packet, blanks included:
        // keeping each field's last non-zero value separately put one
        // device's MAC beside another device's address, lease and name.
        g_dhcp_info_valid = true;
        g_dhcp_client = info.client;
        g_dhcp_ip = info.requested_ip;
        g_dhcp_server = info.server_ip;
        g_dhcp_lease_s = info.lease_s;
        g_dhcp_msg_type = info.msg_type;
        g_dhcp_last_s = now;

        char name[kNameMax];
        const bool named = info.has_hostname && name_clean(info.hostname, name);
        strncpy(g_dhcp_hostname, named ? name : "", sizeof(g_dhcp_hostname) - 1);
        g_dhcp_hostname[sizeof(g_dhcp_hostname) - 1] = '\0';

        // Only a REQUEST or an INFORM says which address the client is using
        // or is about to. A DISCOVER may carry option 50 too, but only as a
        // wish, often the address it had on some other network.
        const bool addressed = info.msg_type == 3 || info.msg_type == 8;
        if (addressed && info.requested_ip != 0 &&
            subnet_contains(derived_subnet(), derived_mask(), info.requested_ip)) {
            record_sighting(info.client, info.requested_ip, now,
                            "device learned from DHCP");
        }
        guard_dhcp(info, now);

        if (named) {
            // Remembered by MAC even when the sweep has not found the device
            // yet, so its row carries the name the moment it appears.
            if (g_names.put(info.client, name)) {
                if (!g_names_dirty) g_names_changed_ms = millis();
                g_names_dirty = true;
            }
            Device* d = g_devices.find(info.client);
            if (d != nullptr && std::strcmp(d->hostname, name) != 0) {
                g_devices.set_hostname(info.client, name);
                log_event(EventType::Hostname, info.client, d->ip, name);
                char ip[16];
                ipv4_format(d->ip, ip);
                Serial.print(F("[dhcp] "));
                Serial.print(name);
                Serial.print(F(" is "));
                Serial.println(ip);
            }
        }
    }
}

// Writes any names not yet on flash. Called before every deliberate restart
// as well as from names_tick(), so a reboot or a firmware update no longer
// throws away the names heard in the 30 seconds before it.
static bool names_flush() {
    if (!g_names_dirty) return true;
    if (!names_save(g_names)) return false;
    g_names_dirty = false;
    return true;
}

// Written 30 s after the first unsaved change rather than on each one: a
// device joining sends DISCOVER and REQUEST back to back, and after a power cut
// every device comes back at once. Counting from the first change, not the
// last, means a steady trickle of names cannot put the write off forever, and
// flash sees at most one write every 30 seconds.
static void names_tick() {
    if (!g_names_dirty || millis() - g_names_changed_ms < 30000UL) return;
    if (names_flush()) {
        Serial.print(F("[names] saved, remembered: "));
        Serial.println(g_names.size());
    } else {
        // The flag used to be cleared before the write, so one failed write
        // meant those names were never written at all. Try again in 30 s.
        g_names_changed_ms = millis();
        Serial.println(F("[names] could not write /names.txt, will retry"));
    }
}

static const char* dhcp_message_text(uint8_t type) {
    switch (type) {
        case 1: return "DISCOVER";
        case 2: return "OFFER";
        case 3: return "REQUEST";
        case 4: return "DECLINE";
        case 5: return "ACK";
        case 6: return "NAK";
        case 7: return "RELEASE";
        case 8: return "INFORM";
        default: return "unknown";
    }
}

static void handle_dhcp() {
    char client[18], ip[16], server[16], lip[16], mask[16], gw[16], dns[16];
    mac_format(g_dhcp_client, client);
    ipv4_format(g_dhcp_ip, ip);
    ipv4_format(g_dhcp_server, server);
    // The "local" block describes THIS board, not the captured client.
    ipv4_format(wifi_local_ip(), lip);
    ipv4_format(wifi_netmask(), mask);
    ipv4_format(wifi_gateway(), gw);
    ipv4_format(wifi_dns(), dns);
    const uint32_t now = now_s();

    // Built with ArduinoJson. This response was assembled by hand until
    // 0.9.6, and both JSON bugs fixed in 0.9.3 were in this very function.
    JsonDocument doc;
    doc["listener"] = dhcp_listener_text(dhcp_capture_state());
    doc["received"] = dhcp_capture_received();
    doc["capture_packets"] = g_dhcp_packets;
    doc["valid"] = g_dhcp_info_valid;
    doc["client_mac"] = client;
    doc["assigned_ip"] = ip;
    doc["server_ip"] = server;
    doc["lease_s"] = g_dhcp_lease_s;
    doc["message_type"] = dhcp_message_text(g_dhcp_msg_type);
    doc["hostname"] = g_dhcp_hostname;
    doc["last_seen_s"] =
        g_dhcp_last_s && now >= g_dhcp_last_s ? now - g_dhcp_last_s : 0;
    JsonObject local = doc["local"].to<JsonObject>();
    local["ip"] = lip;
    local["mask"] = mask;
    local["gateway"] = gw;
    local["dns"] = dns;
    local["mode"] = g_settings.use_dhcp ? "dhcp" : "static";
    local["hostname"] = kOtaHostname;
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static bool probe_due() {
    return wifi_state() == WifiState::Connected &&
           millis() - g_last_probe_ms >= g_settings.probe_interval_s * 1000UL;
}

static void latency_tick() {
    if (!probe_due()) return;
    // A Nearby scan sharing the radio would be measured as gateway latency.
    if (air_busy() || find_holding()) return;
    g_last_probe_ms = millis();

    if (latency_check(wifi_gateway(), g_latency)) {
        Serial.print(F("[latency] gateway "));
        Serial.print(g_latency.rtt_ms);
        Serial.println(F(" ms"));
    } else {
        Serial.println(F("[latency] gateway unavailable"));
    }
}

static void handle_latency() {
    String body;
    body.reserve(220);
    body += F("{\"valid\":");
    body += (g_latency.valid ? F("true") : F("false"));
    body += F(",\"rtt_ms\":");
    body += String(g_latency.rtt_ms);
    body += F(",\"checked_s\":");
    body += String(g_latency.checked_s);
    body += F(",\"age_s\":");
    const uint32_t now = now_s();
    body += String(g_latency.checked_s && now >= g_latency.checked_s
                       ? now - g_latency.checked_s : 0);
    body += F(",\"failures\":");
    body += String(g_latency.failures);
    body += F(",\"method\":\"");
    body += g_latency.method;
    body += F("\"}");
    api_send(200, "application/json", body);
}

// --- Nearby ----------------------------------------------------------------

// Takes in what the Bluetooth task queued, collects a Nearby scan that has
// finished, and starts the one air_plan.h picks, if any.
static void air_tick() {
    BleSighting sighting;
    for (int i = 0; i < 48 && air_ble_poll(sighting); ++i) {
        g_air_ble.sight(sighting, now_s());
        if (sighting.focused && g_find.kind == FindKind::Ble &&
            mac_equal(sighting.addr, g_find.addr)) {
            g_find_trace.add(sighting.at_ms, sighting.rssi);
        }
    }
    if (millis() - g_air_expired_ms >= 30000UL) {
        g_air_expired_ms = millis();
        g_air_ble.expire(now_s(), kAirBleForgetS);
    }

    if (g_air_job == AirJob::Wifi || g_air_job == AirJob::FindWifi) {
        air_wifi_collect(false);
        return;
    }
    if (g_air_job == AirJob::Ble || g_air_job == AirJob::FindBle) {
        if (air_ble_scanning()) {
            if (millis() - g_air_ble_started_ms < g_air_ble_len_ms + kAirBleOverrunMs) return;
            Serial.println(F("[ble] a burst did not end, stopping it"));
            air_ble_stop();
        }
        g_air_job = AirJob::None;
        g_air_burst_end_s = now_s();
        return;
    }
    // Nobody has asked after the device being found for a while: the page was
    // closed or the phone put away. Back to scanning for everything.
    if (g_find.kind != FindKind::None && !air_find_live(g_find, millis())) {
        Serial.println(F("[find] stopped, the page stopped asking"));
        find_stop();
    }
    if (g_updating) return;

    AirInputs in{};
    in.now_ms = millis();
    in.last_watch_ms = g_air_watch_ms;
    in.on_lan = wifi_state() == WifiState::Connected;
    // A phone pairing has the radio too: a Wi-Fi scan taking the radio off
    // channel for a second and a half, or a Bluetooth burst, is no help
    // while the two sides trade the twenty rounds of a code.
    in.radio_busy = sweep_running() || ble_link_pairing_now();
    in.netmon_due = (sweep_due() || probe_due()) && !find_holding();
    in.background_s = g_settings.air_background_s;
    in.wifi = g_air_wifi;
    in.wifi.enabled = g_settings.air_wifi;
    in.ble = g_air_bt;
    in.ble.enabled = g_settings.air_ble && air_ble_ready();
    in.find = g_find_plan;
    in.find.kind = find_radio_on() ? g_find.kind : FindKind::None;

    const AirJob next = air_next(in);
    if (next == AirJob::FindBle) {
        g_find_plan.ever = true;
        g_find_plan.last_ms = millis();
        g_find_plan.failed = !air_ble_focus_burst(g_find.addr, g_find.addr_kind, kAirFindBurstMs);
        if (!g_find_plan.failed) {
            g_air_job = AirJob::FindBle;
            g_air_ble_started_ms = millis();
            g_air_ble_len_ms = kAirFindBurstMs;
        }
    } else if (next == AirJob::FindWifi) {
        g_find_plan.ever = true;
        g_find_plan.last_ms = millis();
        // An access point unheard for a while may have changed channel, as
        // routers do when they restart; one look on every channel finds it.
        const uint32_t unheard_ms = g_find_trace.any()
            ? air_ms_ago(millis(), g_find_trace.newest().at_ms)
            : millis() - g_find.started_ms;
        const bool wide = air_find_wide(unheard_ms, millis() - g_find_wide_ms);
        if (wide) g_find_wide_ms = millis();
        g_find_plan.failed = !air_wifi_begin_target(g_find.addr, wide ? 0 : g_find.channel);
        if (!g_find_plan.failed) g_air_job = AirJob::FindWifi;
    } else if (next == AirJob::Wifi) {
        g_air_wifi.requested = false;
        g_air_wifi.ever = true;
        g_air_wifi.last_ms = millis();
        if (air_wifi_begin()) {
            g_air_job = AirJob::Wifi;
        } else {
            ++g_air_wifi_failures;    // tried again at the next interval
        }
    } else if (next == AirJob::Ble) {
        g_air_bt.requested = false;
        g_air_bt.ever = true;
        g_air_bt.last_ms = millis();
        if (air_ble_burst(kAirBleBurstMs)) {
            g_air_job = AirJob::Ble;
            g_air_ble_started_ms = millis();
            g_air_ble_len_ms = kAirBleBurstMs;
            ++g_air_bursts;
        }
    }
}

static void air_watched() {
    const uint32_t now = millis();
    g_air_watch_ms = now != 0 ? now : 1;     // 0 means "never"
}

// "finding" for the radio the Finder is using, "paused" for the other: while
// a device is being found, the ordinary scans wait.
static const char* air_wifi_state_text() {
    if (!g_settings.air_wifi) return "off";
    if (g_air_job == AirJob::Wifi) return "scanning";
    if (finding()) return g_find.kind == FindKind::Wifi ? "finding" : "paused";
    if (g_air_wifi.requested) return "queued";
    return "idle";
}

static const char* air_ble_state_text() {
    if (!g_settings.air_ble) return "off";
    if (!air_ble_ready()) return "unavailable";
    if (g_air_job == AirJob::Ble) return "listening";
    if (finding()) return g_find.kind == FindKind::Ble ? "finding" : "paused";
    if (g_air_bt.requested) return "queued";
    return "idle";
}

static const char* find_kind_text(FindKind k) {
    return k == FindKind::Wifi ? "wifi" : k == FindKind::Ble ? "ble" : "";
}

// Sends a reply in pieces, so a long one never needs one long allocation.
// /api/nearby with both tables full is near 30 KB, and asking the heap for
// that in one block every three seconds while the Nearby page is open is how
// an ESP32 runs out of memory with plenty free in total.
static void chunk_flush(String& part, bool last) {
    api_piece(part);
    part = "";
    if (last) api_end_pieces();
}

static void chunk_maybe(String& part) {
    if (part.length() >= 1400) chunk_flush(part, false);
}

// Everything the Nearby page draws. Reading it counts as watching, which
// keeps the scans at their quick pace for the next 20 seconds.
static void handle_nearby() {
    air_watched();
    const uint32_t now = now_s();
    uint8_t joined[6] = {0};
    bool have_joined = false;
    if (wifi_state() == WifiState::Connected && WiFi.status() == WL_CONNECTED) {
        const uint8_t* b = WiFi.BSSID();
        if (b != nullptr) {
            std::memcpy(joined, b, sizeof(joined));
            have_joined = true;
        }
    }

    api_begin_pieces(200, "application/json");
    String body;
    body.reserve(1700);
    body += F("{\"version\":\"");
    body += kFirmwareVersion;
    body += F("\",\"on_lan\":");
    body += (wifi_state() == WifiState::Connected ? F("true") : F("false"));
    body += F(",\"sweeping\":");
    body += (sweep_running() ? F("true") : F("false"));
    body += F(",\"background_s\":");
    body += String(g_settings.air_background_s);
    // The device being found, if any, so the other tabs can say why their
    // lists have stopped moving.
    body += F(",\"finding\":");
    if (finding()) {
        char fm[18];
        mac_format(g_find.addr, fm);
        body += F("{\"type\":\"");
        body += find_kind_text(g_find.kind);
        body += F("\",\"addr\":\"");
        body += fm;
        body += F("\",\"name\":\"");
        append_json_escaped(body, g_find_name);
        body += F("\"}");
    } else {
        body += F("null");
    }

    body += F(",\"wifi_scan\":{\"enabled\":");
    body += (g_settings.air_wifi ? F("true") : F("false"));
    body += F(",\"state\":\"");
    body += air_wifi_state_text();
    body += F("\",\"scans\":");
    body += String(g_air_aps.scans());
    body += F(",\"failures\":");
    body += String(g_air_wifi_failures);
    body += F(",\"age_s\":");
    body += (g_air_aps.scans() == 0 || now < g_air_aps.last_scan_s())
                ? String(-1)
                : String(now - g_air_aps.last_scan_s());
    body += F(",\"took_ms\":");
    body += String(g_air_wifi_took_ms);
    body += F("}");

    body += F(",\"ble_scan\":{\"enabled\":");
    body += (g_settings.air_ble ? F("true") : F("false"));
    body += F(",\"state\":\"");
    body += air_ble_state_text();
    body += F("\",\"bursts\":");
    body += String(g_air_bursts);
    body += F(",\"age_s\":");
    body += (g_air_job == AirJob::Ble)
                ? String(0)
                : (g_air_burst_end_s == 0 || now < g_air_burst_end_s)
                      ? String(-1)
                      : String(now - g_air_burst_end_s);
    body += F(",\"dropped\":");
    body += String(air_ble_dropped());
    body += F("}");

    body += F(",\"wifi\":[");
    for (size_t i = 0; i < g_air_aps.size(); ++i) {
        const AirAp& a = g_air_aps.at(i);
        char m[18];
        mac_format(a.bssid, m);
        if (i) body += ',';
        body += F("{\"bssid\":\"");
        body += m;
        body += F("\",\"ssid\":\"");
        append_json_escaped(body, a.ssid);
        body += F("\",\"ch\":");
        body += String(a.channel);
        body += F(",\"rssi\":");
        body += String(a.rssi);
        body += F(",\"security\":\"");
        body += air_auth_text(a.auth);
        body += F("\",\"live\":");
        body += (WifiAirTable<kAirAps>::live(a) ? F("true") : F("false"));
        body += F(",\"joined\":");
        body += ((have_joined && std::memcmp(a.bssid.b, joined, 6) == 0) ? F("true")
                                                                          : F("false"));
        body += F(",\"age_s\":");
        body += String(now >= a.last_seen_s ? now - a.last_seen_s : 0);
        body += F(",\"known_s\":");
        body += String(now >= a.first_seen_s ? now - a.first_seen_s : 0);
        body += '}';
        chunk_maybe(body);
    }

    body += F("],\"ble\":[");
    for (size_t i = 0; i < g_air_ble.size(); ++i) {
        const BleDevice& d = g_air_ble.at(i);
        char m[18];
        mac_format(d.addr, m);
        const char* vendor = d.has_company ? ble_vendor(d.company) : nullptr;
        if (i) body += ',';
        body += F("{\"addr\":\"");
        body += m;
        body += F("\",\"name\":\"");
        append_json_escaped(body, d.name);
        body += F("\",\"vendor\":\"");
        append_json_escaped(body, vendor != nullptr ? vendor : "");
        body += F("\",\"company\":");
        body += (d.has_company ? String(d.company) : String(-1));
        body += F(",\"kind\":\"");
        body += ble_kind_text(d.kind);
        // What sort of device, and the product when its data names one;
        // see ble_type.h for how, and how sure ("sure" 1 to 4).
        body += F("\",\"type\":\"");
        body += ble_type_text(d.type);
        body += F("\",\"sure\":");
        body += String(d.type_strength);
        body += F(",\"model\":\"");
        append_json_escaped(body, d.model != nullptr ? d.model : "");
        body += F("\",\"rssi\":");
        body += String(d.rssi);
        body += F(",\"age_s\":");
        body += String(now >= d.last_seen_s ? now - d.last_seen_s : 0);
        body += F(",\"known_s\":");
        body += String(now >= d.first_seen_s ? now - d.first_seen_s : 0);
        body += F(",\"seen\":");
        body += String(d.sightings);
        body += '}';
        chunk_maybe(body);
    }
    body += F("]}");
    chunk_flush(body, true);
}

// "Scan now": both radios, as soon as netmon's own work leaves the radio free.
// Not checked against Origin like the settings POSTs: it changes nothing, and
// the worst a page elsewhere could do with it is make the board scan sooner.
static void handle_nearby_scan() {
    air_watched();
    const bool wifi = g_settings.air_wifi;
    const bool ble = g_settings.air_ble && air_ble_ready();
    if (wifi) g_air_wifi.requested = true;
    if (ble) g_air_bt.requested = true;
    String body = F("{\"status\":\"queued\",\"wifi\":");
    body += (wifi ? F("true") : F("false"));
    body += F(",\"ble\":");
    body += (ble ? F("true") : F("false"));
    body += '}';
    api_send(200, "application/json", body);
}

static void send_air_config() {
    JsonDocument doc;
    doc["wifi"] = g_settings.air_wifi;
    doc["ble"] = g_settings.air_ble;
    doc["ble_ready"] = air_ble_ready();
    doc["background_s"] = g_settings.air_background_s;
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static void handle_nearby_config_get() { send_air_config(); }

// The Nearby page's switches. Each field is optional and absent means
// unchanged. Applied at once, no restart: a radio switched off simply is not
// scheduled again, and Bluetooth switched on starts its stack now.
static void handle_nearby_config_post() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    Settings s = g_settings;
    if (!doc["wifi"].isNull()) {
        if (!doc["wifi"].is<bool>()) {
            send_config_error("wifi must be true or false");
            return;
        }
        s.air_wifi = doc["wifi"].as<bool>();
    }
    if (!doc["ble"].isNull()) {
        if (!doc["ble"].is<bool>()) {
            send_config_error("ble must be true or false");
            return;
        }
        s.air_ble = doc["ble"].as<bool>();
    }
    if (!doc["background_s"].isNull()) {
        if (!doc["background_s"].is<uint32_t>() ||
            !air_background_valid(doc["background_s"].as<uint32_t>())) {
            send_config_error("the background interval must be 0, or 30 to 3600 seconds");
            return;
        }
        s.air_background_s = doc["background_s"].as<uint32_t>();
    }
    if (!settings_save(s)) {
        api_send(500, "application/json",
                      F("{\"error\":\"could not write settings to flash\"}"));
        return;
    }
    g_settings = s;
    if (!s.air_wifi) g_air_wifi.requested = false;
    if (!s.air_ble) g_air_bt.requested = false;
    if (s.air_ble && !air_ble_ready() && !air_ble_begin()) {
        Serial.println(F("[ble] Bluetooth could not start"));
    }
    send_air_config();
}

static void handle_nearby_page() { send_page(NEARBY_HTML); }

// --- Finder ----------------------------------------------------------------

// Where the Finder stands and, when it is waiting, what for.
static const char* find_state(const char*& why) {
    why = "";
    if (g_find.kind == FindKind::None) return "off";
    if (g_find.kind == FindKind::Wifi && !g_settings.air_wifi) return "off";
    if (g_find.kind == FindKind::Ble && !g_settings.air_ble) return "off";
    if (g_find.kind == FindKind::Ble && !air_ble_ready()) return "unavailable";
    if (g_updating) {
        why = "update";
        return "paused";
    }
    if (g_air_job == AirJob::FindBle || g_air_job == AirJob::FindWifi) return "listening";
    const bool held = find_holding();
    if (sweep_running() || (sweep_due() && !held)) {
        why = "sweep";
        return "paused";
    }
    if (probe_due() && !held) {
        why = "probe";
        return "paused";
    }
    if (g_air_job != AirJob::None) {
        why = "scan";
        return "paused";
    }
    if (g_find_plan.failed) {
        why = "start";
        return "paused";
    }
    return "listening";
}

// The device being found and its readings numbered after `after`, oldest
// first, each as [number, milliseconds ago, dBm]. The readings are raw: the
// page smooths them, and it is the page that knows which way you walked.
static void send_find(uint32_t after) {
    const uint32_t now_ms = millis();
    String body;
    body.reserve(1800);
    if (g_find.kind == FindKind::None) {
        body += F("{\"active\":false,\"seq\":");
        body += String(g_find_trace.last_seq());
        body += '}';
        api_send(200, "application/json", body);
        return;
    }
    char m[18];
    mac_format(g_find.addr, m);
    const char* why = "";
    const char* state = find_state(why);
    body += F("{\"active\":true,\"type\":\"");
    body += find_kind_text(g_find.kind);
    body += F("\",\"addr\":\"");
    body += m;
    body += F("\",\"name\":\"");
    append_json_escaped(body, g_find_name);
    body += '"';
    if (g_find.kind == FindKind::Ble) {
        const BleDevice* d = g_air_ble.get(g_find.addr);
        const char* vendor = d != nullptr && d->has_company ? ble_vendor(d->company) : nullptr;
        body += F(",\"kind\":\"");
        body += ble_kind_text(g_find.addr_kind);
        body += F("\",\"dtype\":\"");
        body += ble_type_text(d != nullptr ? d->type : BleType::Unknown);
        body += F("\",\"model\":\"");
        append_json_escaped(body, d != nullptr && d->model != nullptr ? d->model : "");
        body += F("\",\"vendor\":\"");
        append_json_escaped(body, vendor != nullptr ? vendor : "");
        body += '"';
    } else {
        const AirAp* a = g_air_aps.get(g_find.addr);
        body += F(",\"ch\":");
        body += String(g_find.channel);
        body += F(",\"security\":\"");
        body += (a != nullptr ? air_auth_text(a->auth) : "");
        body += '"';
    }
    body += F(",\"state\":\"");
    body += state;
    body += F("\",\"why\":\"");
    body += why;
    body += F("\",\"for_s\":");
    body += String((now_ms - g_find.started_ms) / 1000);
    body += F(",\"heard_ms\":");
    body += (g_find_trace.any() ? String(air_ms_ago(now_ms, g_find_trace.newest().at_ms))
                                : String(-1));
    body += F(",\"hold_ms\":");
    body += String(finding() ? air_find_held_for(g_find_hold, now_ms) : 0);
    body += F(",\"seq\":");
    body += String(g_find_trace.last_seq());
    body += F(",\"readings\":[");
    FindReading r[64];
    const size_t n = g_find_trace.since(after, r, 64);
    for (size_t i = 0; i < n; ++i) {
        if (i) body += ',';
        body += '[';
        body += String(r[i].seq);
        body += ',';
        body += String(air_ms_ago(now_ms, r[i].at_ms));
        body += ',';
        body += String(r[i].rssi);
        body += ']';
    }
    body += F("]}");
    api_send(200, "application/json", body);
}

static void send_find_error(int code, const char* text) {
    String body = F("{\"error\":\"");
    body += text;
    body += F("\"}");
    api_send(code, "application/json", body);
}


// Asking is what keeps the Finder going; see air_find.h. A Finder nobody has
// asked after for a while has already ended and is not brought back by this:
// the page starts it again with a POST when it is looked at again.
static void handle_find_get() {
    if (g_find.kind != FindKind::None) {
        if (air_find_live(g_find, millis())) {
            g_find.asked_ms = millis();
        } else {
            find_stop();
        }
    }
    const uint32_t after =
        api_has_arg("after") ? strtoul(api_arg("after").c_str(), nullptr, 10) : 0;
    send_find(after);
}

// {"type":"ble"|"wifi","addr":"AA:BB:CC:DD:EE:FF"} starts finding a device,
// {"stop":true} stops. Asking for the device already being found carries on
// with it. Otherwise the device has to be in the Nearby tables, which is where
// the address type a Bluetooth filter needs and the channel a Wi-Fi look needs
// come from, unless it is the device the Finder had when it last stopped: a
// page that went away for a while comes back to that one with this same
// request, and the tables may have forgotten it by then. See air_find.h.
static void handle_find_post() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    if (doc["stop"].is<bool>() && doc["stop"].as<bool>()) {
        if (g_find.kind != FindKind::None) Serial.println(F("[find] stopped from the page"));
        find_stop();
        send_find(g_find_trace.last_seq());
        return;
    }
    const char* type = doc["type"] | "";
    const char* addr = doc["addr"] | "";
    const FindKind kind = std::strcmp(type, "ble") == 0    ? FindKind::Ble
                          : std::strcmp(type, "wifi") == 0 ? FindKind::Wifi
                                                           : FindKind::None;
    if (kind == FindKind::None) {
        send_config_error("type must be wifi or ble");
        return;
    }
    Mac m{};
    if (!mac_parse(addr, m)) {
        send_config_error("addr must be an address like AA:BB:CC:DD:EE:FF");
        return;
    }
    if (kind == FindKind::Ble && !g_settings.air_ble) {
        send_find_error(409, "Bluetooth is switched off on the Nearby page.");
        return;
    }
    if (kind == FindKind::Ble && !air_ble_ready()) {
        send_find_error(409, "Bluetooth could not start on this board.");
        return;
    }
    if (kind == FindKind::Wifi && !g_settings.air_wifi) {
        send_find_error(409, "Wi-Fi scanning is switched off on the Nearby page.");
        return;
    }
    const BleDevice* d = kind == FindKind::Ble ? g_air_ble.get(m) : nullptr;
    const AirAp* a = kind == FindKind::Wifi ? g_air_aps.get(m) : nullptr;
    const FindStart how = air_find_start(
        g_find.kind == kind && mac_equal(g_find.addr, m), d != nullptr || a != nullptr,
        g_find_prev.kind == kind && mac_equal(g_find_prev.addr, m));
    if (how == FindStart::Unknown) {
        send_find_error(404, kind == FindKind::Ble
                                 ? "That device has not been heard in the last five minutes."
                                 : "That network has not been heard since start-up.");
        return;
    }
    if (how != FindStart::Continue) {
        FindTarget t{};
        char name[kSsidMax] = {0};
        if (how == FindStart::Resume) {
            t = g_find_prev;
            std::memcpy(name, g_find_prev_name, sizeof(name));
        } else {
            t.kind = kind;
            t.addr = m;
            if (d != nullptr) {
                t.addr_kind = d->kind;
                std::strncpy(name, d->name, sizeof(name) - 1);
            } else {
                t.channel = a->channel;
                std::strncpy(name, a->ssid, sizeof(name) - 1);
            }
        }
        t.started_ms = millis();
        g_find = t;
        g_find_plan = AirFind{};
        g_find_trace.restart();
        g_find_wide_ms = millis();
        std::memcpy(g_find_name, name, sizeof(g_find_name));
        Serial.print(how == FindStart::Resume ? F("[find] finding again ") : F("[find] finding "));
        Serial.println(addr);
    }
    g_find.asked_ms = millis();
    // "hold_s": a turn about to start, asking for the sweep to wait; 0, the
    // turn is over. See air_find.h for the limits.
    if (doc["hold_s"].is<uint32_t>()) {
        const uint32_t hold_s = doc["hold_s"].as<uint32_t>();
        if (hold_s == 0) {
            air_find_unhold(g_find_hold);
        } else if (air_find_hold(g_find_hold, millis(), hold_s > 60 ? 60000u : hold_s * 1000u)) {
            Serial.print(F("[find] holding the sweep for a turn, s: "));
            Serial.println(hold_s);
        }
    }
    send_find(g_find_trace.last_seq());
}

// --- Map -------------------------------------------------------------------

// The frame of the network map: this board, the access points of its network
// that the Nearby scans have heard, the router and the way out. The devices
// themselves come from /api/devices. Nothing here asks anything of anyone:
// the ISP figures are whatever the Internet page last looked up.
static void handle_map() {
    const uint32_t now = now_s();
    char ip[16], gw[16], cidr[20], m[18];
    ipv4_format(wifi_local_ip(), ip);
    ipv4_format(wifi_gateway(), gw);
    cidr_format(derived_subnet(), derived_mask(), cidr);
    mac_format(wifi_mac(), m);
    const char* state = wifi_state() == WifiState::Connected ? "connected"
                        : wifi_state() == WifiState::SoftAP ? "softap"
                                                            : "connecting";
    const bool joined = wifi_state() == WifiState::Connected && WiFi.status() == WL_CONNECTED;
    uint8_t jb[6] = {0};
    bool have_bssid = false;
    if (joined) {
        const uint8_t* b = WiFi.BSSID();
        if (b != nullptr) {
            std::memcpy(jb, b, sizeof(jb));
            have_bssid = true;
        }
    }

    JsonDocument doc;
    doc["version"] = kFirmwareVersion;
    doc["wifi"] = state;
    doc["ssid"] = wifi_current_ssid();
    doc["ip"] = ip;
    doc["mac"] = m;
    doc["hostname"] = kOtaHostname;
    doc["gateway"] = gw;
    doc["subnet"] = cidr;
    doc["rssi"] = joined ? WiFi.RSSI() : 0;
    doc["channel"] = joined ? WiFi.channel() : 0;
    char jm[18] = "";
    if (have_bssid) {
        Mac jmac{};
        std::memcpy(jmac.b, jb, 6);
        mac_format(jmac, jm);
    }
    doc["bssid"] = jm;
    doc["uptime_s"] = now;
    doc["latency_valid"] = g_latency.valid;
    doc["latency_ms"] = g_latency.rtt_ms;
    JsonObject isp = doc["isp"].to<JsonObject>();
    isp["checked"] = g_isp_ok_s != 0;
    isp["valid"] = g_isp.valid;
    isp["isp"] = g_isp.isp;
    isp["org"] = g_isp.org;
    isp["age_s"] = g_isp_ok_s != 0 && now >= g_isp_ok_s ? now - g_isp_ok_s : 0;
    doc["nearby_wifi"] = g_settings.air_wifi;

    // Other access points with this network's name: mesh nodes and extenders,
    // or something pretending to be one. The Nearby scans are the only way the
    // board hears them; the one it is joined to it always knows.
    JsonArray aps = doc["aps"].to<JsonArray>();
    const char* ssid = wifi_current_ssid();
    bool listed = false;
    if (joined && ssid != nullptr && ssid[0] != '\0') {
        for (size_t i = 0; i < g_air_aps.size(); ++i) {
            const AirAp& a = g_air_aps.at(i);
            if (std::strcmp(a.ssid, ssid) != 0) continue;
            const bool mine = have_bssid && std::memcmp(a.bssid.b, jb, 6) == 0;
            char am[18];
            mac_format(a.bssid, am);
            JsonObject o = aps.add<JsonObject>();
            o["bssid"] = am;
            o["ch"] = mine ? WiFi.channel() : a.channel;
            o["rssi"] = mine ? WiFi.RSSI() : a.rssi;
            o["live"] = mine || WifiAirTable<kAirAps>::live(a);
            o["joined"] = mine;
            o["age_s"] = mine ? 0 : (now >= a.last_seen_s ? now - a.last_seen_s : 0);
            if (mine) listed = true;
        }
    }
    if (have_bssid && !listed) {
        JsonObject o = aps.add<JsonObject>();
        o["bssid"] = jm;
        o["ch"] = WiFi.channel();
        o["rssi"] = WiFi.RSSI();
        o["live"] = true;
        o["joined"] = true;
        o["age_s"] = 0;
    }
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static void handle_events() {
    String body;
    body.reserve(256 + g_events.size() * 180);
    body += '[';
    for (size_t i = 0; i < g_events.size(); ++i) {
        const DeviceEvent& e = g_events.newest(i);
        if (i) body += ',';
        char m[18], ip[16];
        mac_format(e.mac, m);
        ipv4_format(e.ip, ip);
        body += F("{\"at_s\":");
        body += String(e.at_s);
        body += F(",\"type\":\"");
        body += event_type_text(e.type);
        body += F("\",\"mac\":\"");
        body += m;
        body += F("\",\"ip\":\"");
        body += ip;
        body += F("\",\"text\":\"");
        append_json_escaped(body, e.text);
        body += F("\"}");
    }
    body += ']';
    api_send(200, "application/json", body);
}

static void append_json_escaped(String& out, const char* s) {
    for (const char* p = s; p && *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        if (static_cast<unsigned char>(*p) < 0x20) continue;   // drop controls
        out += *p;
    }
}

static void handle_devices() {
    const uint32_t now = now_s();
    const Mac self_mac = wifi_mac();
    String body;
    body.reserve(96 + g_devices.size() * 220);
    body += '[';
    for (size_t i = 0; i < g_devices.size(); ++i) {
        Device& d = g_devices.at(i);
        if (i) body += ',';
        char m[18];
        mac_format(d.mac, m);
        char ip[16];
        ipv4_format(d.ip, ip);
        const char* vendor =
            oui_lookup(mac_oui(d.mac), BUILTIN_OUI, BUILTIN_OUI_COUNT);

        const bool randomised = mac_is_local(d.mac);
        const bool is_self = mac_equal(d.mac, self_mac);

        body += F("{\"mac\":\"");
        body += m;
        body += F("\",\"ip\":\"");
        body += ip;
        body += F("\",\"hostname\":\"");
        append_json_escaped(body, d.hostname);   // filled by DHCP in phase 5
        body += F("\",\"vendor\":\"");
        append_json_escaped(body, vendor ? vendor : "");
        body += F("\",\"status\":\"");
        body += status_text(d.status);
        body += F("\",\"randomised\":");
        body += (randomised ? F("true") : F("false"));
        body += F(",\"self\":");
        body += (is_self ? F("true") : F("false"));
        body += F(",\"online\":");
        body += (d.online ? F("true") : F("false"));
        body += F(",\"last_seen_s\":");
        body += String(now >= d.last_seen ? now - d.last_seen : 0);
        // Uptime is the current unbroken stretch, not time since first sight:
        // a phone seen last Monday and absent since has no uptime to report.
        body += F(",\"up_s\":");
        body += String(d.online && now >= d.online_since ? now - d.online_since
                                                         : 0);
        body += '}';
    }
    body += ']';
    api_send(200, "application/json", body);
}

// --- trust, forget and the LAN watch's answers ---------------------------------

// The body of a POST that names a device, as {"mac": "..."}. Answers the
// request itself and returns false when there is nothing to act on.
static bool posted_mac(Mac& out) {
    if (refuse_other_site()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return false;
    }
    if (!mac_parse(doc["mac"] | "", out)) {
        send_config_error("mac is not a MAC address");
        return false;
    }
    if (mac_equal(out, wifi_mac())) {
        send_config_error("this is the monitor itself");
        return false;
    }
    if (!g_base_loaded) {
        api_send(503, "application/json",
                      F("{\"error\":\"the board is not on a network yet\"}"));
        return false;
    }
    return true;
}

// Marks a device as known on this network, now and after every restart.
static void handle_device_trust() {
    Mac m{};
    if (!posted_mac(m)) return;
    // A private address changes when the device rejoins, so remembering it
    // would not last; and private devices are never flagged in the first place.
    if (mac_is_local(m)) {
        send_config_error("a private address changes when the device rejoins, so "
                          "there is nothing to remember. Private devices are never flagged.");
        return;
    }
    const bool added = base_known_add(g_base, m);
    Device* d = g_devices.find(m);
    const bool was_unknown = d != nullptr && d->status == Status::Unknown;
    if (d != nullptr) d->status = Status::Known;
    if (added) baseline_changed(true);
    if (added || was_unknown) log_event(EventType::Trusted, m, d != nullptr ? d->ip : 0, "marked as known");
    api_send(200, "application/json", F("{\"status\":\"trusted\"}"));
}

// Stops recognising a device: off the list and out of the table. Still on the
// network, it is back at the next sweep, as unknown once this network has been
// learned and as known while a learning window is still open.
static void handle_device_forget() {
    Mac m{};
    if (!posted_mac(m)) return;
    const Device* d = g_devices.find(m);
    const uint32_t ip = d != nullptr ? d->ip : 0;
    const bool listed = base_known_remove(g_base, m);
    const bool removed = g_devices.remove(m);
    if (!listed && !removed) {
        api_send(404, "application/json", F("{\"error\":\"no such device\"}"));
        return;
    }
    if (listed) baseline_changed(true);
    log_event(EventType::Forgotten, m, ip, "no longer recognised");
    api_send(200, "application/json", F("{\"status\":\"forgotten\"}"));
}

// --- Port scanner (0.15) ----------------------------------------------------
//
// POST /api/portscan {"ip":"192.168.2.40"} starts a scan of one device;
// GET /api/portscan?ip=... reports it, with the open ports found so far.
// Only addresses on the board's own network: this looks at your devices,
// it is not for probing the internet. See src/hw/port_scan.h.

static void handle_portscan_post() {
    if (refuse_other_site()) return;
    if (wifi_state() != WifiState::Connected) {
        api_send(503, "application/json",
                      F("{\"error\":\"the board is not on a network yet\"}"));
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    uint32_t ip = 0;
    if (!ipv4_parse(doc["ip"] | "", ip)) {
        send_config_error("ip is not an IPv4 address");
        return;
    }
    const uint32_t net = derived_subnet();
    const uint32_t mask = derived_mask();
    if (!subnet_contains(net, mask, ip) || ip == net || ip == (net | ~mask)) {
        send_config_error("only devices on the board's own network can be scanned");
        return;
    }
    g_portscan.begin(ip);
    api_send(200, "application/json", F("{\"status\":\"started\"}"));
}

// With ?ip= the answer is "idle" unless that address is the one scanned, so a
// page never shows one device's ports under another.
static void handle_portscan_get() {
    const ScanState st = g_portscan.state();
    bool match = st != ScanState::Idle;
    if (match && api_has_arg("ip")) {
        uint32_t want = 0;
        match = ipv4_parse(api_arg("ip").c_str(), want) && want == g_portscan.ip();
    }
    JsonDocument doc;
    doc["state"] = !match ? "idle" : st == ScanState::Scanning ? "scanning" : "done";
    doc["total"] = g_portscan.total();
    if (match) {
        char ip[16];
        ipv4_format(g_portscan.ip(), ip);
        doc["ip"] = ip;
        doc["probed"] = g_portscan.probed();
        doc["elapsed_ms"] = g_portscan.elapsed_ms();
        JsonArray open = doc["open"].to<JsonArray>();
        for (size_t i = 0; i < kScanPortCount; ++i) {
            const PortResult& r = g_portscan.results()[i];
            if (!r.open) continue;
            JsonObject o = open.add<JsonObject>();
            o["port"] = r.port;
            o["name"] = r.name;
        }
    }
    String out;
    serializeJson(doc, out);
    api_send(200, "application/json", out);
}

// What the board keeps for this network and what the LAN watch has noticed.
static void handle_guard() {
    const uint32_t now = now_s();
    JsonDocument doc;
    doc["network"] = g_base_loaded ? g_base.ssid : "";
    doc["learned"] = g_base.learned;
    doc["learning"] = still_learning(now, g_first_scan_s, g_settings.learning_window_s,
                                     g_base.learned);
    doc["known"] = g_base.known_count;
    char m[18] = "";
    if (g_base.has_router) mac_format(g_base.router, m);
    doc["router"] = m;
    char ip[16] = "";
    if (g_own_dhcp != 0) ipv4_format(g_own_dhcp, ip);
    doc["dhcp_own"] = ip;
    JsonArray dhcp = doc["dhcp"].to<JsonArray>();
    for (size_t i = 0; i < g_base.dhcp_count; ++i) {
        ipv4_format(g_base.dhcp[i], ip);
        dhcp.add(ip);
    }
    JsonArray aps = doc["aps"].to<JsonArray>();
    for (size_t i = 0; i < g_base.ap_count; ++i) {
        JsonObject o = aps.add<JsonObject>();
        mac_format(g_base.aps[i].bssid, m);
        o["bssid"] = m;
        o["auth"] = air_auth_rank(g_base.aps[i].auth) == 255 ? "" : air_auth_text(g_base.aps[i].auth);
    }
    // The access point checks run on the Nearby page's Wi-Fi scans.
    doc["wifi_watch"] = g_settings.air_wifi;
    JsonArray alerts = doc["alerts"].to<JsonArray>();
    for (size_t i = 0; i < g_guard.size(); ++i) {
        const GuardEntry& e = g_guard.at(i);
        JsonObject o = alerts.add<JsonObject>();
        o["type"] = guard_kind_text(e.kind);
        mac_format(e.mac, m);
        o["mac"] = m;
        char om[18] = "";
        if (e.kind == GuardKind::RouterChanged || e.kind == GuardKind::IpConflict) mac_format(e.other, om);
        o["other"] = om;
        char eip[16] = "";
        if (e.ip != 0) ipv4_format(e.ip, eip);
        o["ip"] = eip;
        o["first_s"] = e.first_s;
        o["last_s"] = e.last_s;
        o["age_s"] = now >= e.last_s ? now - e.last_s : 0;
    }
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

// Somebody says a change the LAN watch noticed was theirs: a new router, a
// second DHCP server, another access point. That becomes the record and the
// alert goes. An address clash has nothing to record, and is only dismissed.
static void handle_guard_accept() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    GuardKind kind = GuardKind::RouterChanged;
    if (!guard_kind_parse(doc["type"] | "", kind)) {
        send_config_error("type is not one of the LAN watch's alerts");
        return;
    }
    Mac mac{};
    uint32_t ip = 0;
    mac_parse(doc["mac"] | "", mac);
    ipv4_parse(doc["ip"] | "", ip);
    const GuardEntry* found = g_guard.get(kind, mac, ip);
    if (found == nullptr) {
        api_send(404, "application/json", F("{\"error\":\"no such alert\"}"));
        return;
    }
    const GuardEntry e = *found;       // clear() below moves the entries
    const char* text = "dismissed";
    bool changed = false;
    switch (kind) {
        case GuardKind::RouterChanged:
            g_base.router = e.mac;
            g_base.has_router = true;
            changed = true;
            text = "accepted as the router";
            break;
        case GuardKind::DhcpServer:
            changed = base_add_dhcp(g_base, e.ip);
            text = "accepted as a DHCP server";
            break;
        case GuardKind::RogueAp:
        case GuardKind::WeakAp: {
            // The mode it offers now, when the Nearby table still has it; when
            // not, none, and the next scan that hears it records one.
            const AirAp* a = g_air_aps.get(e.mac);
            changed = base_put_ap(g_base, e.mac, a != nullptr ? a->auth : 255);
            text = "accepted as this network's access point";
            break;
        }
        case GuardKind::IpConflict:
            break;
    }
    g_guard.clear(kind, mac, ip);
    if (changed) baseline_changed(true);
    log_event(EventType::Trusted, e.mac, e.ip, text);
    api_send(200, "application/json", F("{\"status\":\"accepted\"}"));
}

// Starts this network over: the list, the router, the DHCP servers and the
// access points are forgotten, and a fresh learning window opens at the next
// sweep. What is on the network now counts as known, as anything seen during
// a window always has.
static void handle_guard_relearn() {
    if (refuse_other_site()) return;
    if (!g_base_loaded) {
        api_send(503, "application/json",
                      F("{\"error\":\"the board is not on a network yet\"}"));
        return;
    }
    char ssid[sizeof(g_base.ssid)];
    std::memcpy(ssid, g_base.ssid, sizeof(ssid));
    base_reset(g_base, ssid);
    g_first_scan_s = 0;
    g_guard.clear_all();
    g_claims.clear();
    for (size_t i = 0; i < g_devices.size(); ++i) {
        Device& d = g_devices.at(i);
        if (!mac_is_local(d.mac)) d.status = Status::Known;
    }
    baseline_changed(true);
    Serial.println(F("[base] learning this network again"));
    api_send(200, "application/json", F("{\"status\":\"learning\"}"));
}

static uint32_t update_max_bytes() {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p != nullptr ? p->size : 0;
}

static void handle_config_get() {
    char ip[16], mask[16], gw[16], dns[16];
    ipv4_format(g_settings.static_ip, ip);
    ipv4_format(g_settings.static_mask, mask);
    ipv4_format(g_settings.static_gw, gw);
    ipv4_format(g_settings.static_dns, dns);
    // The network this form edits is the one the board is on right now, so
    // the name field agrees with "Connected to" underneath it. It used to be
    // whichever network was saved last, even after the board had failed to
    // join it and fallen back to another. In setup mode there is no current
    // network, so it falls back to the front of the list.
    int edit = -1;
    if (wifi_state() == WifiState::Connected) {
        edit = netlist_find(g_settings.nets, g_settings.net_count,
                            wifi_current_ssid());
    }
    if (edit < 0 && g_settings.net_count > 0) edit = 0;
    const NetworkCred* shown = edit >= 0 ? &g_settings.nets[edit] : nullptr;

    String body;
    body.reserve(560);
    body += F("{\"version\":\"");
    body += kFirmwareVersion;
    body += F("\",\"ssid\":\"");
    append_json_escaped(body, shown != nullptr ? shown->ssid : "");
    // The stored password is deliberately not sent. The form leaves the field
    // blank, and blank means "unchanged" on the way back in.
    body += F("\",\"has_password\":");
    body += ((shown != nullptr && shown->pass[0] != '\0') ? F("true")
                                                          : F("false"));
    body += F(",\"networks\":");
    body += String(g_settings.net_count);
    body += F(",\"use_dhcp\":");
    body += (g_settings.use_dhcp ? F("true") : F("false"));
    body += F(",\"ip\":\"");
    body += ip;
    body += F("\",\"mask\":\"");
    body += mask;
    body += F("\",\"gw\":\"");
    body += gw;
    body += F("\",\"dns\":\"");
    body += dns;
    body += F("\",\"scan_interval_s\":");
    body += String(g_settings.scan_interval_s);
    body += F(",\"probe_interval_s\":");
    body += String(g_settings.probe_interval_s);
    body += F(",\"offline_after_s\":");
    body += String(g_settings.offline_after_s);
    body += F(",\"learning_window_s\":");
    body += String(g_settings.learning_window_s);
    // The largest firmware file the updater can take, which the partition
    // scheme decides: the Settings page checks a file against it before
    // spending a megabyte of upload on one that cannot fit.
    body += F(",\"update_max\":");
    body += String(update_max_bytes());

    // What the interface is actually using right now. In DHCP mode the stored
    // static fields are meaningless, so without this the settings page has
    // nothing truthful to show for the address.
    char aip[16], amask[16], agw[16], adns[16];
    ipv4_format(wifi_local_ip(), aip);
    ipv4_format(wifi_netmask(), amask);
    ipv4_format(wifi_gateway(), agw);
    ipv4_format(wifi_dns(), adns);
    body += F(",\"active\":{\"ip\":\"");
    body += aip;
    body += F("\",\"mask\":\"");
    body += amask;
    body += F("\",\"gw\":\"");
    body += agw;
    body += F("\",\"dns\":\"");
    body += adns;
    body += F("\",\"mac\":\"");
    body += WiFi.macAddress();
    body += F("\",\"ssid\":\"");
    append_json_escaped(body, wifi_current_ssid());
    body += F("\",\"rssi\":");
    body += String(WiFi.RSSI());
    body += F(",\"source\":\"");
    body += (wifi_state() == WifiState::SoftAP ? F("softap")
             : g_settings.use_dhcp                ? F("dhcp")
                                                  : F("static"));
    body += F("\"}");
    // From 0.13: the board's Wi-Fi address. "custom" is the one the owner
    // set ("" for none), "applied" whether it is the one in use now: a new
    // one waits for a restart.
    char factory[18], custom[18] = "", inuse[18];
    mac_format(wifi_factory_mac(), factory);
    mac_format(wifi_mac(), inuse);
    if (g_settings.wifi_mac_set) {
        Mac m{};
        std::memcpy(m.b, g_settings.wifi_mac, sizeof(m.b));
        mac_format(m, custom);
    }
    body += F(",\"mac\":{\"active\":\"");
    body += inuse;
    body += F("\",\"factory\":\"");
    body += factory;
    body += F("\",\"custom\":\"");
    body += custom;
    body += F("\",\"applied\":");
    body += (wifi_custom_mac_applied() && g_settings.wifi_mac_set ? F("true") : F("false"));
    body += F("}}");
    api_send(200, "application/json", body);
}

// The remembered networks in the order the board tries them, with how each
// fared at the last start-up. Built with ArduinoJson rather than by hand: the
// two JSON bugs fixed in 0.9.3 were both hand-assembled strings.
static void handle_networks() {
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    const bool joined = wifi_state() == WifiState::Connected;
    for (uint8_t i = 0; i < g_settings.net_count && i < 4; ++i) {
        const NetworkCred& n = g_settings.nets[i];
        uint32_t ms = 0;
        uint8_t reason = 0;
        const JoinResult r = wifi_boot_result(n.ssid, ms, reason);
        JsonObject o = list.add<JsonObject>();
        o["order"] = i + 1;
        o["ssid"] = n.ssid;
        o["has_password"] = n.pass[0] != '\0';
        o["active"] = joined && strcmp(n.ssid, wifi_current_ssid()) == 0;
        o["boot"] = join_result_text(r);
        o["boot_ms"] = ms;
        o["reason"] = reason;
    }
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static void handle_network_forget() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const char* ssid = doc["ssid"] | "";
    if (ssid[0] == '\0') {
        send_config_error("network name is required");
        return;
    }
    // Forgetting the network in use would not drop the connection now, but
    // the next restart would find nothing to join and strand the board in
    // setup mode.
    if (wifi_state() == WifiState::Connected &&
        strcmp(ssid, wifi_current_ssid()) == 0) {
        send_config_error("the board is using this network right now");
        return;
    }
    Settings s = g_settings;
    if (!netlist_forget(s.nets, s.net_count, ssid)) {
        api_send(404, "application/json",
                      F("{\"error\":\"that network is not remembered\"}"));
        return;
    }
    if (!settings_save(s)) {
        api_send(500, "application/json",
                      F("{\"error\":\"could not write settings to flash\"}"));
        return;
    }
    g_settings = s;
    // What the board learned there goes with it.
    baseline_remove(ssid);
    Serial.print(F("[cfg] forgot a network, remembered now: "));
    Serial.println(g_settings.net_count);
    api_send(200, "application/json", F("{\"status\":\"forgotten\"}"));
}

static void handle_isp() {
    const uint32_t now = now_s();
    // "Check again" overrides the six-hour cache but not the two-minute floor.
    // Someone leaning on the button should not turn into a request storm.
    const bool forced = api_has_arg("force");
    if (should_refresh(now, g_isp_try_s, g_isp_ok_s,
                       forced ? 0u : kIspIntervalS, kIspBackoffS)) {
        g_isp_try_s = now;
        if (isp_fetch(g_isp)) g_isp_ok_s = now;
        if (g_isp.date_unix != 0 && clock_set(g_clock, g_isp.date_unix, now_s(), ClockSource::Internet)) {
            Serial.println(F("[clock] set from the provider lookup"));
            clock_learned();
        }
    }

    // The router's own identity comes from our sweep, not from the lookup.
    char gw[16];
    ipv4_format(wifi_gateway(), gw);
    const char* gw_vendor = "";
    char gw_mac[18] = "";
    for (size_t i = 0; i < g_devices.size(); ++i) {
        if (g_devices.at(i).ip != wifi_gateway()) continue;
        mac_format(g_devices.at(i).mac, gw_mac);
        const char* v = oui_lookup(mac_oui(g_devices.at(i).mac), BUILTIN_OUI,
                                   BUILTIN_OUI_COUNT);
        if (v != nullptr) gw_vendor = v;
        break;
    }

    String body;
    body.reserve(640);
    body += F("{\"version\":\"");
    body += kFirmwareVersion;
    body += F("\",\"valid\":");
    body += (g_isp.valid ? F("true") : F("false"));
    body += F(",\"ip\":\"");
    append_json_escaped(body, g_isp.ip);
    body += F("\",\"isp\":\"");
    append_json_escaped(body, g_isp.isp);
    body += F("\",\"org\":\"");
    append_json_escaped(body, g_isp.org);
    body += F("\",\"asn\":\"");
    append_json_escaped(body, g_isp.asn);
    body += F("\",\"city\":\"");
    append_json_escaped(body, g_isp.city);
    body += F("\",\"region\":\"");
    append_json_escaped(body, g_isp.region);
    body += F("\",\"country\":\"");
    append_json_escaped(body, g_isp.country);
    body += F("\",\"timezone\":\"");
    append_json_escaped(body, g_isp.tz);
    body += F("\",\"error\":\"");
    append_json_escaped(body, g_isp.error);
    body += F("\",\"rtt_ms\":");
    body += String(g_isp.rtt_ms);
    body += F(",\"checked_age_s\":");
    body += String(g_isp_ok_s == 0 ? 0 : (now >= g_isp_ok_s ? now - g_isp_ok_s : 0));
    body += F(",\"ever_checked\":");
    body += (g_isp_ok_s != 0 ? F("true") : F("false"));
    body += F(",\"gateway\":\"");
    body += gw;
    body += F("\",\"gateway_mac\":\"");
    body += gw_mac;
    body += F("\",\"gateway_vendor\":\"");
    append_json_escaped(body, gw_vendor);
    body += F("\"}");
    api_send(200, "application/json", body);
}

static void handle_isp_page() { send_page(ISP_HTML); }

static void handle_dashboard() { send_page(DASHBOARD_HTML); }

// Per-upload state. It is reset when an upload starts and again after every
// answer, so a request is only ever judged on its own upload. Before, a POST
// with no file part was answered from the leftovers of the previous one.
static bool g_http_update_started = false;
static bool g_http_update_failed = false;
static bool g_http_update_paused_capture = false;
static size_t g_http_update_bytes = 0;
static char g_http_update_error[64] = "";

static bool update_key_ok() {
    const String key = api_header("X-Netmon-Key");
    return key.length() > 0 && same_text_ct(key.c_str(), kOtaPassword);
}

static void update_fail(const char* why) {
    g_http_update_failed = true;
    strncpy(g_http_update_error, why != nullptr ? why : "unknown error",
            sizeof(g_http_update_error) - 1);
    g_http_update_error[sizeof(g_http_update_error) - 1] = '\0';
}

static void update_reset() {
    if (g_http_update_started && Update.isRunning()) Update.abort();
    if (g_http_update_paused_capture) {
        g_http_update_paused_capture = false;
        if (wifi_state() == WifiState::Connected && !dhcp_capture_begin()) {
            Serial.println(F("[dhcp] capture restart failed after update"));
        }
    }
    g_http_update_started = false;
    g_http_update_failed = false;
    g_http_update_bytes = 0;
    g_http_update_error[0] = '\0';
    g_updating = false;
}

static void handle_update_upload() {
    HTTPUpload& upload = g_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        update_reset();
        g_via = Via::Http;
        if (session_of_request() < 0) {
            Serial.println(F("[ota] HTTP update rejected: not signed in"));
            update_fail("not signed in");
            return;
        }
        if (!update_key_ok()) {
            Serial.println(F("[ota] HTTP update rejected: bad key"));
            update_fail("invalid update key");
            return;
        }
        // The upload has the radio to itself: no Nearby scan starts until it
        // ends, and a Bluetooth burst in progress is cut short.
        g_updating = true;
        air_ble_stop();
        // DHCP capture is paused for the transfer and resumed however it
        // ends, so the upload has the board to itself.
        if (wifi_state() == WifiState::Connected) {
            dhcp_capture_stop();
            g_http_update_paused_capture = true;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.println(F("[ota] HTTP update begin failed"));
            Update.printError(Serial);
            update_fail(Update.errorString());
            return;
        }
        g_http_update_started = true;
        Serial.print(F("[ota] HTTP update started: "));
        Serial.println(upload.filename);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!g_http_update_started || g_http_update_failed) return;
        const size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            Serial.println(F("[ota] HTTP update write failed"));
            Update.printError(Serial);
            update_fail(Update.errorString());
            return;
        }
        g_http_update_bytes += written;
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!g_http_update_started || g_http_update_failed) return;
        if (!Update.end(true)) {
            Serial.println(F("[ota] HTTP update finalize failed"));
            Update.printError(Serial);
            update_fail(Update.errorString());
            return;
        }
        Serial.print(F("[ota] HTTP update complete, bytes: "));
        Serial.println(g_http_update_bytes);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // The client went away mid-transfer, so nobody is waiting for an
        // answer. Put everything back now rather than leave capture paused.
        Serial.println(F("[ota] HTTP update aborted"));
        update_reset();
    }
}

static void handle_update_result() {
    g_via = Via::Http;
    if (session_of_request() < 0) {
        update_reset();
        send_login_required();
        return;
    }
    if (!update_key_ok()) {
        update_reset();
        g_server.send(401, "application/json",
                      F("{\"error\":\"invalid update key\"}"));
        return;
    }
    if (!g_http_update_started && !g_http_update_failed) {
        update_reset();
        g_server.send(400, "application/json",
                      F("{\"error\":\"no firmware file in the request\"}"));
        return;
    }
    if (g_http_update_failed) {
        String body = F("{\"error\":\"firmware update failed: ");
        append_json_escaped(body, g_http_update_error);
        body += F("\"}");
        update_reset();
        g_server.send(500, "application/json", body);
        return;
    }

    g_server.send(200, "application/json",
                  F("{\"status\":\"updated\",\"restarting\":true}"));
    before_restart();
    delay(500);
    ESP.restart();
}

// Lets the web page test the update password before it spends a megabyte of
// upload finding out it was wrong.
static void handle_update_check() {
    if (!update_key_ok()) {
        api_send(401, "application/json",
                      F("{\"error\":\"invalid update key\"}"));
        return;
    }
    api_send(200, "application/json", F("{\"status\":\"ok\"}"));
}

// --- Bluetooth link -----------------------------------------------------------

// The link as it stands: on or off, this board's Bluetooth address, how many
// phones are paired and connected, and the pairing window with its code
// while one is open. Over Wi-Fi the code is only ever shown to somebody on
// the board's own network, which is the point of it.
static void send_ble_status() {
    LinkPairing p{};
    const bool pairing = ble_link_pairing(p);
    JsonDocument doc;
    doc["link"] = kLinkVersion;
    doc["available"] = ble_link_ready();
    doc["enabled"] = g_settings.ble_link;
    doc["on"] = ble_link_enabled();
    doc["name"] = kOtaHostname;
    doc["addr"] = ble_link_address();
    doc["bonds"] = ble_link_bonds();
    doc["max_bonds"] = ble_link_max_bonds();
    doc["connected"] = ble_link_connected();
    doc["secure"] = ble_link_secure();
    doc["pairing"] = pairing;
    doc["code"] = p.code;
    doc["left_s"] = (p.left_ms + 999) / 1000;
    doc["result"] = p.result;
    doc["result_age_s"] = p.result_age_s;
    // From 0.13: why the last attempt in a window went as it did, how many
    // more failures the open window takes, and the last attempt of all, in a
    // window or not, with the stack's own number for it.
    doc["why"] = p.why;
    doc["why_text"] = p.why_text;
    doc["tries_left"] = p.tries_left;
    if (p.last_why[0] != '\0') {
        JsonObject last = doc["last"].to<JsonObject>();
        last["why"] = p.last_why;
        last["text"] = p.last_text;
        last["status"] = p.last_status;
        last["in_window"] = p.last_in_window;
        last["age_s"] = p.last_age_s;
    } else {
        doc["last"] = nullptr;
    }
    // The owner's own pairing code, when there is one (0.13). Only ever sent
    // to somebody signed in, like everything else here.
    doc["own_code"] = g_settings.ble_pin >= 0;
    if (g_settings.ble_pin >= 0) {
        char c[7];
        pair_code_text(static_cast<uint32_t>(g_settings.ble_pin), c);
        doc["own"] = c;
    } else {
        doc["own"] = "";
    }
    doc["served"] = ble_link_served();
    doc["via"] = g_via == Via::Link ? "bluetooth" : "wifi";
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static void handle_ble_get() { send_ble_status(); }

// Six digits, as a string so leading zeros survive, or "" for random codes.
static bool parse_pin(JsonVariantConst v, int32_t& out) {
    if (v.isNull()) return false;
    const char* t = v.is<const char*>() ? v.as<const char*>() : nullptr;
    if (t == nullptr) return false;
    if (t[0] == '\0') {
        out = -1;
        return true;
    }
    if (std::strlen(t) != 6) return false;
    int32_t n = 0;
    for (int i = 0; i < 6; ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
        n = n * 10 + (t[i] - '0');
    }
    out = n;
    return true;
}

// {"enabled":true|false} switches the link on or off, kept across restarts.
// Off, the board stops advertising and lets every phone go; paired phones
// stay paired. From 0.13, {"own":"123456"} sets the owner's own pairing
// code and {"own":""} goes back to a new random one each window. Either
// field may be left out.
static void handle_ble_post() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const bool has_enabled = !doc["enabled"].isNull();
    const bool has_code = !doc["own"].isNull();
    if (!has_enabled && !has_code) {
        send_config_error("enabled must be true or false");
        return;
    }
    if (has_enabled && !doc["enabled"].is<bool>()) {
        send_config_error("enabled must be true or false");
        return;
    }
    int32_t code = g_settings.ble_pin;
    if (has_code && !parse_pin(doc["own"], code)) {
        send_config_error("the pairing code must be six digits, or empty for a random code");
        return;
    }
    Settings s = g_settings;
    if (has_enabled) s.ble_link = doc["enabled"].as<bool>();
    s.ble_pin = code;
    if (!settings_save(s)) {
        api_send(500, "application/json", F("{\"error\":\"could not write settings to flash\"}"));
        return;
    }
    const bool code_changed = s.ble_pin != g_settings.ble_pin;
    g_settings = s;
    if (code_changed) {
        ble_link_set_code(s.ble_pin);
        Serial.println(s.ble_pin >= 0 ? F("[link] pairing now uses the owner's own code")
                                      : F("[link] pairing now uses a new random code each time"));
    }
    if (has_enabled) {
        if (s.ble_link) {
            if (!ble_link_ready()) {
                Serial.println(ble_link_begin(true) ? F("[link] Bluetooth link up")
                                                    : F("[link] Bluetooth link could not start"));
            } else {
                ble_link_enable(true);
            }
        } else {
            ble_link_enable(false);
            ble_link_pair(false);
        }
    }
    send_ble_status();
}

// --- Signing in: the endpoints (0.13) ----------------------------------------------

// Open to anyone: what this is, and whether the asker is signed in. The app
// finds boards on the network by it, and the sign-in page by it knows where
// it stands.
static void handle_auth_get() {
    g_session = session_of_request();
    const bool in = g_session >= 0;
    char id[18];
    mac_format(wifi_factory_mac(), id);
    JsonDocument doc;
    doc["netmon"] = true;
    doc["name"] = kOtaHostname;
    doc["version"] = kFirmwareVersion;
    doc["id"] = id;
    doc["login"] = true;
    doc["signed_in"] = in;
    doc["remember_days"] = kSessionLongS / 86400;
    if (in) {
        doc["own_password"] = g_auth.login.set;
        doc["remembered"] = g_auth.sessions.s[g_session].remember;
        doc["sessions"] = g_auth.sessions.count(now_s(), clock_unix());
    }
    doc["via"] = g_via == Via::Link ? "bluetooth" : "wifi";
    String body;
    serializeJson(doc, body);
    if (g_via == Via::Http) g_server.sendHeader("Cache-Control", "no-store");
    api_send(200, "application/json", body);
}

static bool password_ok(const char* pw) {
    // Both, every time, so how long it takes says nothing about which matched.
    const bool update = same_text_ct(pw, kOtaPassword);
    const bool own = password_matches(g_auth.login, pw);
    return update || own;
}

static void send_throttled(uint32_t wait) {
    String body = F("{\"error\":\"Too many wrong passwords. Try again in ");
    body += String(wait);
    body += F(" seconds.\",\"retry_s\":");
    body += String(wait);
    body += '}';
    api_send(429, "application/json", body);
}

// {"password":"...","remember":true,"unix":1760000000}: a session, as a
// cookie for a browser and as "token" for the app. "unix" is the asker's
// time, which the board takes when it has none, once signed in.
static void handle_login() {
    if (refuse_other_site()) return;
    const uint32_t who = api_client();
    const uint32_t wait = g_throttle.wait_s(who, now_s());
    if (wait > 0) {
        send_throttled(wait);
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const char* pw = doc["password"] | "";
    const bool remember = doc["remember"] | false;
    if (pw[0] == '\0' || !password_ok(pw)) {
        g_throttle.failed(who, now_s());
        Serial.println(F("[auth] wrong password"));
        api_send(401, "application/json", F("{\"error\":\"That password is not right.\",\"wrong\":true}"));
        return;
    }
    g_throttle.succeeded(who);
    if (doc["unix"].is<uint32_t>() &&
        clock_set(g_clock, doc["unix"].as<uint32_t>(), now_s(), ClockSource::Client)) {
        Serial.println(F("[clock] set by a client"));
    }
    char token[kTokenHex + 1];
    auth_new_token(token);
    uint8_t h[32];
    token_hash(token, h);
    g_session = static_cast<int>(g_auth.sessions.add(h, remember, now_s(), clock_unix()));
    clock_learned();
    auth_store();
    Serial.println(remember ? F("[auth] signed in, kept for 30 days") : F("[auth] signed in"));
    if (g_via == Via::Http) {
        set_session_cookie(token, remember);
        g_server.sendHeader("Cache-Control", "no-store");
    }
    JsonDocument out;
    out["status"] = "signed in";
    out["token"] = token;
    out["remember"] = remember;
    out["days"] = remember ? kSessionLongS / 86400 : 0;
    String body;
    serializeJson(out, body);
    api_send(200, "application/json", body);
}

// Ends the asker's session, if it has one, and forgets the cookie.
static void handle_logout() {
    if (refuse_other_site()) return;
    g_session = session_of_request();
    if (g_session >= 0) {
        g_auth.sessions.remove(g_session);
        g_session = -1;
        auth_store();
        Serial.println(F("[auth] signed out"));
    }
    if (g_via == Via::Http) clear_session_cookie();
    api_send(200, "application/json", F("{\"status\":\"signed out\"}"));
}

// Every session ends, the asker's too: a lost phone, a shared browser.
static void handle_signout_all() {
    if (refuse_other_site()) return;
    const size_t n = g_auth.sessions.remove_all_but(-1);
    g_session = -1;
    auth_store();
    Serial.print(F("[auth] signed out everywhere, sessions: "));
    Serial.println(n);
    if (g_via == Via::Http) clear_session_cookie();
    api_send(200, "application/json", F("{\"status\":\"signed out everywhere\"}"));
}

// {"current":"...","new":"..."}: the owner's own login password, from then
// on beside the update password; "new":"" goes back to the update password
// alone. Every other session ends, so a password that leaked stops working
// everywhere it was used.
static void handle_password() {
    if (refuse_other_site()) return;
    const uint32_t who = api_client();
    const uint32_t wait = g_throttle.wait_s(who, now_s());
    if (wait > 0) {
        send_throttled(wait);
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const char* cur = doc["current"] | "";
    const char* pw = doc["new"] | "";
    if (cur[0] == '\0' || !password_ok(cur)) {
        g_throttle.failed(who, now_s());
        api_send(403, "application/json",
                 F("{\"error\":\"The current password is not right.\",\"wrong\":true}"));
        return;
    }
    g_throttle.succeeded(who);
    if (pw[0] != '\0') {
        const PasswordRule r = password_rule(pw);
        if (r != PasswordRule::Ok) {
            send_config_error(password_rule_text(r));
            return;
        }
        if (same_text_ct(pw, kOtaPassword)) {
            send_config_error("That is the update password already; choose a different one.");
            return;
        }
        uint8_t salt[16];
        auth_random(salt, sizeof(salt));
        password_make(g_auth.login, pw, salt, kPasswordRounds);
    } else {
        std::memset(&g_auth.login, 0, sizeof(g_auth.login));
    }
    const size_t ended = g_auth.sessions.remove_all_but(g_session);
    auth_store();
    Serial.print(pw[0] != '\0' ? F("[auth] login password set") : F("[auth] login password removed"));
    Serial.print(F(", other sessions ended: "));
    Serial.println(ended);
    JsonDocument out;
    out["status"] = "changed";
    out["own_password"] = g_auth.login.set;
    out["ended"] = ended;
    String body;
    serializeJson(out, body);
    api_send(200, "application/json", body);
}

// {"mac":"02:1A:2B:3C:4D:5E"} sets the address the board uses on Wi-Fi from
// its next start; {"mac":""} goes back to the chip's own. The page restarts
// the board afterwards. The Bluetooth address stays as it is, so paired
// phones stay paired.
static void handle_mac_post() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    if (!doc["mac"].is<const char*>()) {
        send_config_error("mac must be an address like 02:1A:2B:3C:4D:5E, or empty");
        return;
    }
    const char* text = doc["mac"].as<const char*>();
    Settings s = g_settings;
    if (text[0] == '\0') {
        s.wifi_mac_set = false;
        std::memset(s.wifi_mac, 0, sizeof(s.wifi_mac));
    } else {
        Mac m{};
        const MacRule r = mac_rule(text, wifi_factory_ap_mac(), m);
        if (r != MacRule::Ok) {
            send_config_error(mac_rule_text(r));
            return;
        }
        // The chip's own address is the same as having none set.
        s.wifi_mac_set = !mac_equal(m, wifi_factory_mac());
        std::memcpy(s.wifi_mac, m.b, sizeof(s.wifi_mac));
    }
    if (!settings_save(s)) {
        api_send(500, "application/json", F("{\"error\":\"could not write settings to flash\"}"));
        return;
    }
    g_settings = s;
    Serial.println(s.wifi_mac_set ? F("[cfg] own MAC address saved, used from the next start")
                                  : F("[cfg] back to the chip's own MAC address from the next start"));
    char saved[18] = "";
    if (s.wifi_mac_set) {
        Mac m{};
        std::memcpy(m.b, s.wifi_mac, sizeof(m.b));
        mac_format(m, saved);
    }
    JsonDocument out;
    out["status"] = "saved";
    out["custom"] = saved;
    out["restart"] = true;
    String body;
    serializeJson(out, body);
    api_send(200, "application/json", body);
}

// {} opens a pairing window for two minutes, or keeps the open one going, and
// answers with its code; {"stop":true} closes it.
static void handle_ble_pair() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    const String raw = api_body();
    if (raw.length() > 0 && deserializeJson(doc, raw)) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const bool stop = doc["stop"].is<bool>() && doc["stop"].as<bool>();
    if (!stop) {
        if (!ble_link_ready()) {
            api_send(409, "application/json",
                     F("{\"error\":\"Bluetooth is not running on this board.\"}"));
            return;
        }
        if (!ble_link_enabled()) {
            api_send(409, "application/json",
                     F("{\"error\":\"The Bluetooth link is switched off. Switch it on first.\"}"));
            return;
        }
    }
    ble_link_pair(!stop);
    if (!stop) Serial.println(F("[link] pairing window open for two minutes"));
    send_ble_status();
}

// Forgets every paired phone. Each has to pair again with a new code; the
// one asking, if it asked over Bluetooth, is let go once it has the answer.
static void handle_ble_forget() {
    if (refuse_other_site()) return;
    if (!ble_link_ready()) {
        send_ble_status();
        return;
    }
    if (!ble_link_forget_all()) {
        api_send(500, "application/json", F("{\"error\":\"could not forget the paired phones\"}"));
        return;
    }
    send_ble_status();
}

static void handle_reboot() {
    if (refuse_other_site()) return;
    api_send(200, "application/json", F("{\"status\":\"restarting\"}"));
    before_restart();    // names heard in the last 30 s, and this network's report
    api_settle(250);     // let the answer reach whoever asked before the reset
    ESP.restart();
}

// --- Saved reports -------------------------------------------------------------

static uint32_t clock_unix() { return clock_now(g_clock, now_s()); }

static const char* vendor_of_mac(const Mac& m) {
    return oui_lookup(mac_oui(m), BUILTIN_OUI, BUILTIN_OUI_COUNT);
}

// Saves the report of the network the board is on: its device table, with
// what the last report of this network had and this boot has not seen.
// False, saying why, when there is nothing to save or the flash would not
// take it.
static bool report_save(const char*& why) {
    why = "";
    if (wifi_state() != WifiState::Connected) {
        why = "The board is not on a network, so there is nothing to save.";
        return false;
    }
    if (g_passes_done == 0) {
        why = "No sweep has finished yet. Try again in a minute.";
        return false;
    }
    char cidr[20];
    cidr_format(derived_subnet(), derived_mask(), cidr);
    const char* ssid = wifi_current_ssid();
    const size_t slot = report_slot_for(g_reports, kReportSlots, ssid, cidr);
    const bool same = report_same(g_reports[slot], ssid, cidr);
    const uint32_t up = now_s();
    const uint32_t now_unix = clock_unix();

    // This boot's devices, then room for the last report's: freed on return.
    std::unique_ptr<ReportRow[]> rows(new (std::nothrow) ReportRow[kMaxDevices + kReportRows]);
    if (!rows) {
        why = "Not enough memory to put the report together.";
        return false;
    }
    const Mac self = wifi_mac();
    size_t ncur = 0;
    for (size_t i = 0; i < g_devices.size() && ncur < kMaxDevices; ++i) {
        const Device& d = g_devices.at(i);
        report_row_from(d, up, g_clock, mac_equal(d.mac, self), rows[ncur++]);
    }
    size_t nprev = 0;
    uint32_t gap = 0;
    bool gap_known = false;
    if (same) {
        nprev = report_read_rows(slot, rows.get() + ncur, kReportRows);
        gap_known = report_gap_s(g_reports[slot], up, now_unix, gap);
    }
    const size_t n = report_merge(rows.get(), ncur, nprev, gap_known, gap, kReportRows);
    // Never at the cost of the settings and the learned names, which share
    // the 190 KB file system: a report that would leave it short waits.
    if (reports_free_bytes() < n * 300 + 4096 + kReportReserve) {
        why = "The board's storage is nearly full. Delete a saved report to make room.";
        return false;
    }

    ReportMeta m{};
    std::strncpy(m.ssid, ssid, sizeof(m.ssid) - 1);
    std::strncpy(m.subnet, cidr, sizeof(m.subnet) - 1);
    ipv4_format(wifi_gateway(), m.gateway);
    for (size_t i = 0; i < g_devices.size(); ++i) {
        if (g_devices.at(i).ip == wifi_gateway()) {
            mac_format(g_devices.at(i).mac, m.gateway_mac);
            break;
        }
    }
    ipv4_format(wifi_local_ip(), m.board_ip);
    mac_format(self, m.board_mac);
    std::strncpy(m.version, kFirmwareVersion, sizeof(m.version) - 1);
    m.seq = report_next_seq(g_reports, kReportSlots);
    m.saved_unix = now_unix;
    m.saved_up_s = up;
    m.clock = g_clock.source;
    m.passes = g_passes_done;
    m.learning = in_learning_window(up, g_first_scan_s, g_settings.learning_window_s);
    m.count = n;
    for (size_t i = 0; i < n; ++i) {
        if (rows[i].online) ++m.online;
    }
    if (!report_write(slot, m, rows.get(), n, vendor_of_mac, g_reports[slot])) {
        why = "The flash would not take the report.";
        Serial.println(F("[report] could not write it"));
        return false;
    }
    g_reports[slot].this_boot = true;
    g_reports[slot].saved_up_s = up;
    Serial.print(F("[report] saved "));
    Serial.print(n);
    Serial.print(F(" devices for "));
    Serial.println(ssid);
    return true;
}

// The first report two sweeps after joining a network, then every 15
// minutes, between radio jobs: writing flash holds up both cores for moments.
static void report_tick() {
    if (wifi_state() != WifiState::Connected || g_updating) return;
    if (!g_report_first_done) {
        if (g_passes_done < kReportFirstPasses) return;
    } else if (millis() - g_report_last_ms < kReportEveryMs) {
        return;
    }
    if (sweep_running() || air_busy()) return;
    g_report_first_done = true;
    g_report_last_ms = millis();
    const char* why = "";
    if (!report_save(why)) {
        Serial.print(F("[report] not saved: "));
        Serial.println(why);
    }
}

// Before every restart the board makes on purpose: what is only in memory,
// written down.
static void before_restart() {
    names_flush();
    baseline_flush();
    const char* why = "";
    if (wifi_state() == WifiState::Connected && g_passes_done > 0 && !report_save(why)) {
        Serial.print(F("[report] not saved before the restart: "));
        Serial.println(why);
    }
}

static void send_reports() {
    const uint32_t up = now_s();
    const uint32_t now_unix = clock_unix();
    const bool on = wifi_state() == WifiState::Connected;
    char cidr[20];
    cidr_format(derived_subnet(), derived_mask(), cidr);
    JsonDocument doc;
    doc["max"] = kReportSlots;
    doc["every_s"] = kReportEveryMs / 1000;
    doc["clock"] = clock_known(g_clock);
    doc["now_unix"] = now_unix;
    doc["free_bytes"] = reports_free_bytes();
    if (on) {
        JsonObject net = doc["network"].to<JsonObject>();
        net["ssid"] = wifi_current_ssid();
        net["subnet"] = cidr;
    } else {
        doc["network"] = nullptr;
    }
    JsonArray list = doc["reports"].to<JsonArray>();
    for (size_t i = 0; i < kReportSlots; ++i) {
        const ReportSlot& r = g_reports[i];
        if (!r.used) continue;
        JsonObject o = list.add<JsonObject>();
        o["slot"] = i;
        o["ssid"] = r.ssid;
        o["subnet"] = r.subnet;
        o["gateway"] = r.gateway;
        o["count"] = r.count;
        o["online"] = r.online;
        o["saved_unix"] = r.saved_unix;
        o["age_s"] = report_age_s(r, up, now_unix);
        o["bytes"] = r.bytes;
        o["current"] = on && report_same(r, wifi_current_ssid(), cidr);
    }
    String body;
    serializeJson(doc, body);
    api_send(200, "application/json", body);
}

static void handle_reports() { send_reports(); }

static int report_slot_arg(const String& a) {
    if (a.length() != 1 || a[0] < '0' || a[0] >= static_cast<char>('0' + kReportSlots)) return -1;
    return a[0] - '0';
}

// One saved report, the file as it is: a JSON document. Over the link it is
// read into memory first, so it cannot change under an answer still going out.
static void handle_report_get() {
    const int slot = report_slot_arg(api_arg("slot"));
    if (slot < 0 || !g_reports[slot].used) {
        api_send(404, "application/json", F("{\"error\":\"No report is saved there.\"}"));
        return;
    }
    File f = LittleFS.open(report_path(static_cast<size_t>(slot)), "r");
    if (!f) {
        api_send(404, "application/json", F("{\"error\":\"No report is saved there.\"}"));
        return;
    }
    if (g_via == Via::Http) {
        g_server.streamFile(f, "application/json");
        f.close();
        return;
    }
    ble_link_answer_status(200);
    char buf[1024];
    while (f.available()) {
        const size_t n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
        if (n == 0) break;
        ble_link_answer_add(String(buf, n));
    }
    f.close();
}

static void handle_report_save() {
    if (refuse_other_site()) return;
    const char* why = "";
    if (!report_save(why)) {
        String body = F("{\"error\":\"");
        append_json_escaped(body, why);
        body += F("\"}");
        api_send(409, "application/json", body);
        return;
    }
    send_reports();
}

// {"slot":N}: forgets one network's report.
static void handle_report_delete() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    const int slot = doc["slot"].is<int>() ? doc["slot"].as<int>() : -1;
    if (slot < 0 || slot >= static_cast<int>(kReportSlots) || !g_reports[slot].used) {
        api_send(404, "application/json", F("{\"error\":\"No report is saved there.\"}"));
        return;
    }
    if (!report_remove(static_cast<size_t>(slot))) {
        api_send(500, "application/json", F("{\"error\":\"could not delete the report\"}"));
        return;
    }
    std::memset(&g_reports[slot], 0, sizeof(g_reports[slot]));
    send_reports();
}

// {"unix":seconds}: the time from the app or a page, which dates saved
// reports. Refused when implausible; ignored, though answered, when the board
// already has the time from the provider lookup.
static void handle_clock() {
    if (refuse_other_site()) return;
    JsonDocument doc;
    if (deserializeJson(doc, api_body())) {
        send_config_error("request body is not valid JSON");
        return;
    }
    if (!doc["unix"].is<uint32_t>()) {
        send_config_error("unix must be the time in seconds since 1970");
        return;
    }
    const uint32_t t = doc["unix"].as<uint32_t>();
    if (t < kClockMin || t > kClockMax) {
        send_config_error("that time is not plausible");
        return;
    }
    if (clock_set(g_clock, t, now_s(), ClockSource::Client)) {
        Serial.println(F("[clock] set by a client"));
        clock_learned();
    }
    JsonDocument out;
    out["clock"] = clock_known(g_clock);
    out["unix"] = clock_unix();
    out["source"] = clock_source_text(g_clock.source);
    String body;
    serializeJson(out, body);
    api_send(200, "application/json", body);
}

// --- Routes -------------------------------------------------------------------
//
// Every API endpoint, for the web server and the Bluetooth link alike. The
// firmware upload is not here: it streams a megabyte through the web server's
// upload handler, so it is Wi-Fi only, and the link says so.
static const uint8_t kGet = 1;
static const uint8_t kPost = 2;
static const uint8_t kAny = kGet | kPost;

struct ApiRoute {
    const char* path;
    uint8_t methods;
    void (*handler)();
    bool open;            // answered without a session (0.13): only signing in
};

static const ApiRoute kApiRoutes[] = {
    {"/api/health", kAny, handle_health},
    {"/api/devices", kAny, handle_devices},
    {"/api/latency", kAny, handle_latency},
    {"/api/dhcp", kAny, handle_dhcp},
    {"/api/events", kAny, handle_events},
    {"/api/scan", kAny, handle_scan},
    {"/api/isp", kAny, handle_isp},
    {"/api/nearby", kGet, handle_nearby},
    {"/api/nearby/scan", kPost, handle_nearby_scan},
    {"/api/nearby/config", kGet, handle_nearby_config_get},
    {"/api/nearby/config", kPost, handle_nearby_config_post},
    {"/api/nearby/find", kGet, handle_find_get},
    {"/api/nearby/find", kPost, handle_find_post},
    {"/api/map", kGet, handle_map},
    {"/api/config", kGet, handle_config_get},
    {"/api/config", kPost, handle_config_post},
    {"/api/networks", kGet, handle_networks},
    {"/api/networks/forget", kPost, handle_network_forget},
    {"/api/update/check", kPost, handle_update_check},
    {"/api/reboot", kPost, handle_reboot},
    {"/api/ble", kGet, handle_ble_get},
    {"/api/ble", kPost, handle_ble_post},
    {"/api/ble/pair", kPost, handle_ble_pair},
    {"/api/ble/forget", kPost, handle_ble_forget},
    {"/api/reports", kGet, handle_reports},
    {"/api/reports/get", kGet, handle_report_get},
    {"/api/reports/save", kPost, handle_report_save},
    {"/api/reports/delete", kPost, handle_report_delete},
    {"/api/clock", kPost, handle_clock},
    // Port scanner: probes common TCP ports on one LAN host (0.15).
    {"/api/portscan", kGet,  handle_portscan_get},
    {"/api/portscan", kPost, handle_portscan_post},
    // Recognised devices and the LAN watch (0.14).
    {"/api/devices/trust", kPost, handle_device_trust},
    {"/api/devices/forget", kPost, handle_device_forget},
    {"/api/guard", kGet, handle_guard},
    {"/api/guard/accept", kPost, handle_guard_accept},
    {"/api/guard/relearn", kPost, handle_guard_relearn},
    // Signing in (0.13). Only these three answer anyone; see src/core/auth.h.
    {"/api/auth", kGet, handle_auth_get, true},
    {"/api/login", kPost, handle_login, true},
    {"/api/logout", kPost, handle_logout, true},
    {"/api/auth/password", kPost, handle_password},
    {"/api/auth/signout", kPost, handle_signout_all},
    {"/api/mac", kPost, handle_mac_post},
};

// A route's handler, behind the sign-in, for whichever way the request came.
// (Takes the route's parts rather than the route: the prototype the Arduino
// IDE writes at the top of the sketch comes before struct ApiRoute.)
static void run_route(void (*handler)(), bool open) {
    g_session = -1;
    if (!open && !require_session()) return;
    handler();
}

static const char* link_status_text(uint16_t status) {
    switch (status) {
        case 413: return "{\"error\":\"request too large\"}";
        case 414: return "{\"error\":\"request path or query too long\"}";
        default: return "{\"error\":\"request not understood\"}";
    }
}

// Answers one request that came over the Bluetooth link, with the same
// handler the web server would have used.
static void serve_link(const LinkIncoming& in) {
    g_via = Via::Link;
    g_link_req = &in;
    if (in.status != 0) {
        api_send(in.status, "application/json", link_status_text(in.status));
    } else if (std::strcmp(in.req.path, "/api/update") == 0) {
        api_send(501, "application/json",
                 F("{\"error\":\"Firmware updates go over Wi-Fi, not Bluetooth.\"}"));
    } else {
        const uint8_t method = in.req.post ? kPost : kGet;
        const ApiRoute* route = nullptr;
        for (const ApiRoute& r : kApiRoutes) {
            if ((r.methods & method) && std::strcmp(r.path, in.req.path) == 0) {
                route = &r;
                break;
            }
        }
        if (route != nullptr) {
            run_route(route->handler, route->open);
        } else {
            api_send(404, "text/plain", F("not found"));
        }
    }
    ble_link_answer_end();
    g_link_req = nullptr;
    g_via = Via::Http;
}

// One request off the link per pass of loop(), then whatever the phones
// will take of the answers. While a firmware upload has the board, requests
// wait.
static void link_tick() {
    if (!ble_link_ready()) return;
    ble_link_tick();
    if (!g_updating) {
        LinkIncoming in;
        if (ble_link_next(in)) serve_link(in);
    }
    ble_link_pump();
}

// Setup mode is entered only at start-up, when no remembered network answered
// within its 12 seconds, and nothing ever took the board out of it again.
// After a power cut the board is back in seconds while a router can take a
// minute or two, so the monitor sat in setup mode until somebody power-cycled
// it. Now, when it has networks to go back to and nobody has been joined to
// netmon-setup for three minutes, it restarts and tries them again. Nothing is
// lost by that: in setup mode no sweep has run and nothing has been learned.
static const uint32_t kSetupRetryMs = 180000UL;
static uint32_t g_setup_quiet_since_ms = 0;
static uint32_t g_setup_checked_ms = 0;

static void setup_retry_tick() {
    if (wifi_state() != WifiState::SoftAP || g_settings.net_count == 0) return;
    if (millis() - g_setup_checked_ms < 1000UL) return;
    g_setup_checked_ms = millis();
    if (WiFi.softAPgetStationNum() > 0) {
        g_setup_quiet_since_ms = millis();
        return;
    }
    if (millis() - g_setup_quiet_since_ms < kSetupRetryMs) return;
    Serial.println(F("[wifi] nobody on netmon-setup for 3 minutes, restarting "
                     "to try the saved networks again"));
    names_flush();
    baseline_flush();
    delay(100);
    ESP.restart();
}

// The DHCP listener is paused around a Wi-Fi scan and a firmware upload and
// started again afterwards. When that restart failed, or the first start at
// boot did, it stayed off until the next reboot and hostnames quietly stopped
// arriving. Now it is retried every 30 seconds while the board is on a network.
static uint32_t g_dhcp_retry_ms = 0;

static void dhcp_listener_tick() {
    if (wifi_state() != WifiState::Connected) return;
    if (dhcp_capture_state() == DhcpListener::Listening) return;
    if (millis() - g_dhcp_retry_ms < 30000UL) return;
    g_dhcp_retry_ms = millis();
    if (dhcp_capture_begin()) {
        Serial.println(F("[dhcp] listening again on :67"));
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.print(F("[boot] netmon "));
    Serial.println(kFirmwareVersion);

    settings_load(g_settings);
    Serial.print(F("[boot] remembered networks: "));
    Serial.println(g_settings.net_count);
    names_load(g_names);
    Serial.print(F("[boot] remembered names: "));
    Serial.println(g_names.size());
    const size_t pruned = baseline_prune(g_settings);
    if (pruned) {
        Serial.print(F("[boot] dropped what was learned on networks no longer saved: "));
        Serial.println(pruned);
    }
    reports_load(g_reports);
    // The login password and who is signed in, kept across restarts (0.13).
    if (auth_load(g_auth)) {
        Serial.print(F("[boot] signed-in sessions kept: "));
        Serial.println(g_auth.sessions.count(0, 0));
    }
    ble_link_set_code(g_settings.ble_pin);
    for (const ReportSlot& r : g_reports) {
        if (!r.used) continue;
        Serial.print(F("[boot] saved report: "));
        Serial.println(r.ssid);
    }
    // A board that ran the Wi-Fi + BLE scanner sketch: join the network that
    // sketch was set up on rather than opening netmon-setup.
    if (scanner_import(g_settings)) {
        Serial.println(F("[boot] using the network saved by the scanner sketch"));
    }

    wifi_begin(g_settings);

    ArduinoOTA.setHostname(kOtaHostname);
    ArduinoOTA.setPassword(kOtaPassword);
    // ArduinoOTA restarts the board as soon as the image is written, so any
    // unsaved names, and this network's report, have to be written here or
    // not at all.
    ArduinoOTA.onEnd([]() { before_restart(); });
    ArduinoOTA.onStart([]() {
        g_updating = true;
        air_ble_stop();
    });
    ArduinoOTA.onError([](ota_error_t) { g_updating = false; });
    if (wifi_state() == WifiState::SoftAP) {
        g_setup_quiet_since_ms = millis();
        g_portal = g_dns.start(53, "*", WiFi.softAPIP());
        Serial.println(g_portal ? F("[net] captive portal up on 192.168.4.1")
                                : F("[net] captive portal FAILED to start"));
    }
    ArduinoOTA.begin();
    if (wifi_state() == WifiState::Connected) {
        if (dhcp_capture_begin()) {
            Serial.println(F("[dhcp] listening for DHCP broadcasts on :67"));
        } else {
            Serial.println(F("[dhcp] could not listen on :67"));
        }
    }
    // ArduinoOTA already published the A record for netmon.local. This adds
    // the service advert, so the board also turns up in anything that browses
    // for web servers rather than resolving a name — Fing, Bonjour Browser,
    // Avahi. Without it the name works but nothing lists the device.
    MDNS.addService("http", "tcp", 80);

    // Bluetooth for the Nearby page. Started after Wi-Fi, which is up by now
    // either joined or as netmon-setup. If it will not start, the page says so
    // and shows Wi-Fi alone; nothing else depends on it.
    if (g_settings.air_ble) {
        Serial.println(air_ble_begin()
                           ? F("[ble] listening for Bluetooth devices")
                           : F("[ble] Bluetooth could not start"));
    }

    // The Bluetooth link: the same API, for phones paired with a 6-digit code.
    // Off in settings, the stack is not started for it at all.
    if (g_settings.ble_link) {
        if (ble_link_begin(true)) {
            Serial.println(F("[link] Bluetooth link up, advertising as netmon"));
            // Open a pairing window immediately so a phone can pair without
            // needing access to the board's Wi-Fi first (0.15). The window
            // uses the owner's fixed code if one is set, or a random code
            // shown in Settings. After the window closes the board keeps
            // advertising and already-paired phones reconnect without a window.
            ble_link_pair(true);
            Serial.println(F("[link] pairing window open for 2 minutes"));
        } else {
            Serial.println(F("[link] Bluetooth link could not start"));
        }
    }

    g_server.on("/", handle_dashboard);
    g_server.on("/about", handle_root);
    g_server.on("/events", []() { send_page(EVENTS_HTML); });
    g_server.on("/settings", handle_settings_page);
    g_server.on("/nearby", handle_nearby_page);
    g_server.on("/map", []() { send_page(MAP_HTML); });
    g_server.on("/isp", handle_isp_page);
    // The one page anyone may see.
    g_server.on("/login", []() {
        g_server.sendHeader("Cache-Control", "no-store");
        g_server.send_P(200, "text/html", LOGIN_HTML);
    });
    for (const ApiRoute& r : kApiRoutes) {
        const HTTPMethod m = r.methods == kAny ? HTTP_ANY : r.methods == kGet ? HTTP_GET : HTTP_POST;
        const ApiRoute* route = &r;
        g_server.on(r.path, m, [route]() {
            g_via = Via::Http;
            run_route(route->handler, route->open);
        });
    }
    // In setup mode anything unrecognised is a captive-portal probe, so send
    // it to the page the person actually needs. In normal operation a 404 has
    // to stay a 404, or a mistyped API path would silently return HTML.
    g_server.onNotFound([]() {
        if (wifi_state() != WifiState::SoftAP) {
            g_server.send(404, "text/plain", F("not found"));
            return;
        }
        g_server.sendHeader("Location", "http://192.168.4.1/settings", true);
        g_server.send(302, "text/plain", "");
    });
    // X-Netmon-Key for the update endpoints, Origin for refuse_other_site(),
    // and the session: a browser's cookie, or the app's Authorization.
    const char* header_keys[] = {"X-Netmon-Key", "Origin", "Cookie", "Authorization"};
    g_server.collectHeaders(header_keys, 4);
    g_server.on("/api/update", HTTP_POST, handle_update_result, handle_update_upload);
    g_server.begin();
    Serial.println(F("[boot] http server up on :80"));
}

void loop() {
    ArduinoOTA.handle();
    if (g_portal) g_dns.processNextRequest();
    g_server.handleClient();
    link_tick();
    auth_tick();
    report_tick();
    dhcp_listener_tick();
    baseline_tick();
    dhcp_tick();
    names_tick();
    latency_tick();
    scan_tick();
    air_tick();
    setup_retry_tick();
    g_portscan.tick();
}
