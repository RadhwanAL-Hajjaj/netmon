#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// Saved reports: for each network the board has been on, its device list as
// it last stood, kept in flash so it can be looked at after the board has
// moved on, from the Settings page or from the app over Wi-Fi or Bluetooth.
//
// One report per network, the latest only, for up to four networks, as many
// as the board remembers. A network is its name and its subnet. A fifth
// network takes the place of the one saved longest ago.
//
// A report is saved two sweeps after the board joins a network, then every
// 15 minutes, before every restart the board makes on purpose (saving
// settings, a firmware update, Restart) and when asked. Devices in the last
// report of the same network that have not been seen since the board started
// are kept, marked as carried over, so a power cut does not leave a report
// with only what was seen since.
//
// The board has no clock of its own. It learns the time from the Date header
// of the provider lookup it already makes, or from the app or a page, which
// send their own clock. Until then reports say how long before the save each
// device was seen, which is always known, and leave dates out.
//
// On flash a report is the JSON the API serves, laid out a line at a time so
// it can be read back with little memory: the header on the first line, which
// ends with "devices":[, then one device per line, then ]}.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "classify.h"
#include "device_table.h"
#include "mac.h"
#include "name_cache.h"

static const size_t kReportSlots = 4;
static const size_t kReportRows = 80;            // devices kept per report
static const uint32_t kReportEveryMs = 900000;   // 15 minutes
static const uint32_t kReportFirstPasses = 2;    // sweeps before the first save
static const size_t kReportSsidMax = 33;
static const size_t kReportSubnetMax = 20;
static const size_t kReportLineMax = 640;        // longest line written
static const uint8_t kReportFormat = 1;

// --- The clock -------------------------------------------------------------

enum class ClockSource : uint8_t { None = 0, Client = 1, Internet = 2 };

struct Clock {
    uint32_t boot_unix;    // the time at uptime 0; 0 while unknown
    ClockSource source;
};

// 2024-01-01 to 2100. Anything outside is a phone set wrong, or garbage.
static const uint32_t kClockMin = 1704067200u;
static const uint32_t kClockMax = 4102444800u;

inline bool clock_known(const Clock& c) { return c.boot_unix != 0; }

inline uint32_t clock_now(const Clock& c, uint32_t up_s) {
    return clock_known(c) ? c.boot_unix + up_s : 0;
}

// Sets the clock to `unix`, read at uptime `up_s`. The provider lookup's
// Date header outranks the clock of a phone or browser, which can be wrong;
// between two of the same rank the later wins. False when refused.
inline bool clock_set(Clock& c, uint32_t unix, uint32_t up_s, ClockSource src) {
    if (src == ClockSource::None) return false;
    if (unix < kClockMin || unix > kClockMax || unix < up_s) return false;
    if (static_cast<uint8_t>(src) < static_cast<uint8_t>(c.source)) return false;
    c.boot_unix = unix - up_s;
    c.source = src;
    return true;
}

inline const char* clock_source_text(ClockSource s) {
    switch (s) {
        case ClockSource::Internet: return "internet";
        case ClockSource::Client: return "client";
        case ClockSource::None: break;
    }
    return "none";
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar.
// Howard Hinnant's days_from_civil.
inline int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
    y -= m <= 2 ? 1 : 0;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline bool date_digits(const char* s, int n, uint32_t& out) {
    out = 0;
    for (int i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + static_cast<uint32_t>(s[i] - '0');
    }
    return true;
}

// "Sun, 04 Oct 2026 18:01:02 GMT", the one form an HTTP server sends in a
// Date header (RFC 9110's IMF-fixdate).
inline bool http_date_parse(const char* s, uint32_t& out) {
    out = 0;
    if (s == nullptr || std::strlen(s) < 29) return false;
    if (s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' ||
        s[19] != ':' || s[22] != ':' || s[25] != ' ' || std::strncmp(s + 26, "GMT", 3) != 0) {
        return false;
    }
    static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    uint32_t mon = 0;
    for (uint32_t i = 0; i < 12; ++i) {
        if (std::strncmp(s + 8, kMonths + i * 3, 3) == 0) mon = i + 1;
    }
    uint32_t day, year, hh, mm, ss;
    if (mon == 0 || !date_digits(s + 5, 2, day) || !date_digits(s + 12, 4, year) ||
        !date_digits(s + 17, 2, hh) || !date_digits(s + 20, 2, mm) || !date_digits(s + 23, 2, ss)) {
        return false;
    }
    if (day < 1 || day > 31 || year < 1970 || year > 2105 || hh > 23 || mm > 59 || ss > 60) return false;
    const int32_t days = days_from_civil(static_cast<int32_t>(year), mon, day);
    if (days < 0) return false;
    const uint64_t t = static_cast<uint64_t>(days) * 86400u + hh * 3600u + mm * 60u + ss;
    if (t > 0xFFFFFFFFu) return false;
    out = static_cast<uint32_t>(t);
    return true;
}

inline char report_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// The Date header in a block of response headers, `len` bytes long.
inline bool http_date_header(const char* headers, size_t len, uint32_t& out) {
    out = 0;
    if (headers == nullptr) return false;
    size_t i = 0;
    while (i < len) {
        // At the start of a line.
        if (len - i > 5 && report_lower(headers[i]) == 'd' && report_lower(headers[i + 1]) == 'a' &&
            report_lower(headers[i + 2]) == 't' && report_lower(headers[i + 3]) == 'e' &&
            headers[i + 4] == ':') {
            size_t v = i + 5;
            while (v < len && (headers[v] == ' ' || headers[v] == '\t')) ++v;
            char buf[40];
            size_t n = 0;
            while (v + n < len && headers[v + n] != '\r' && headers[v + n] != '\n' && n < sizeof(buf) - 1) {
                buf[n] = headers[v + n];
                ++n;
            }
            buf[n] = '\0';
            return http_date_parse(buf, out);
        }
        const void* nl = std::memchr(headers + i, '\n', len - i);
        if (nl == nullptr) break;
        i = static_cast<size_t>(static_cast<const char*>(nl) - headers) + 1;
    }
    return false;
}

// --- Which slot --------------------------------------------------------------

// What the board knows about one saved report without reading all of it.
struct ReportSlot {
    bool used;
    char ssid[kReportSsidMax];
    char subnet[kReportSubnetMax];
    char gateway[16];
    uint32_t seq;          // save order across every slot: higher is newer
    uint32_t saved_unix;   // 0: the board had no clock then
    uint32_t count;
    uint32_t online;
    uint32_t bytes;
    bool this_boot;        // saved since the board started...
    uint32_t saved_up_s;   // ...at this uptime
};

inline bool report_same(const ReportSlot& s, const char* ssid, const char* subnet) {
    return s.used && ssid != nullptr && subnet != nullptr && std::strcmp(s.ssid, ssid) == 0 &&
           std::strcmp(s.subnet, subnet) == 0;
}

// Where the report of the network named `ssid` with `subnet` goes: its own
// slot, an empty one, or the one saved longest ago.
inline size_t report_slot_for(const ReportSlot* slots, size_t n, const char* ssid, const char* subnet) {
    for (size_t i = 0; i < n; ++i) {
        if (report_same(slots[i], ssid, subnet)) return i;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!slots[i].used) return i;
    }
    size_t oldest = 0;
    for (size_t i = 1; i < n; ++i) {
        if (slots[i].seq < slots[oldest].seq) oldest = i;
    }
    return oldest;
}

inline uint32_t report_next_seq(const ReportSlot* slots, size_t n) {
    uint32_t top = 0;
    for (size_t i = 0; i < n; ++i) {
        if (slots[i].used && slots[i].seq > top) top = slots[i].seq;
    }
    return top + 1;
}

// How long ago a report was saved, or -1 when that cannot be told: saved
// before a restart, with no clock then or now.
inline int64_t report_age_s(const ReportSlot& s, uint32_t up_s, uint32_t now_unix) {
    if (!s.used) return -1;
    if (s.this_boot) return up_s >= s.saved_up_s ? up_s - s.saved_up_s : 0;
    if (s.saved_unix != 0 && now_unix != 0) return now_unix >= s.saved_unix ? now_unix - s.saved_unix : 0;
    return -1;
}

// The time between the previous report of a network and one saved now, to
// age the devices carried over from it. False when it cannot be told.
inline bool report_gap_s(const ReportSlot& prev, uint32_t up_s, uint32_t now_unix, uint32_t& gap) {
    gap = 0;
    if (!prev.used) return false;
    if (prev.this_boot) {
        gap = up_s >= prev.saved_up_s ? up_s - prev.saved_up_s : 0;
        return true;
    }
    if (prev.saved_unix != 0 && now_unix >= prev.saved_unix) {
        gap = now_unix - prev.saved_unix;
        return true;
    }
    return false;
}

// --- The devices -------------------------------------------------------------

struct ReportRow {
    Mac mac;
    uint32_t ip;
    Status status;
    bool online;
    bool self;
    bool carried;          // from an earlier report; not seen since the board started
    uint32_t up_s;
    uint32_t ago_s;        // last seen this long before the save (at least, when carried
                           // over a gap that could not be told)
    uint32_t seen_unix;    // when last seen; 0 when not known
    uint32_t first_unix;   // when first seen on this network, as far as the board knows
    char hostname[kNameMax];
};

// A device in the board's table as a row, saved at uptime `up_s`.
inline void report_row_from(const Device& d, uint32_t up_s, const Clock& clk, bool self, ReportRow& r) {
    std::memset(&r, 0, sizeof(r));
    r.mac = d.mac;
    r.ip = d.ip;
    r.status = d.status;
    r.online = d.online;
    r.self = self;
    r.carried = false;
    r.up_s = d.online && up_s >= d.online_since ? up_s - d.online_since : 0;
    r.ago_s = up_s >= d.last_seen ? up_s - d.last_seen : 0;
    r.seen_unix = clock_known(clk) ? clk.boot_unix + d.last_seen : 0;
    r.first_unix = clock_known(clk) ? clk.boot_unix + d.first_seen : 0;
    std::strncpy(r.hostname, d.hostname, sizeof(r.hostname) - 1);
}

inline bool report_more_recent(const ReportRow& a, const ReportRow& b) {
    if (a.ago_s != b.ago_s) return a.ago_s < b.ago_s;
    return a.seen_unix > b.seen_unix;
}

// Puts this boot's rows and the last report's together, in place.
//
// rows[0..ncur) are the devices seen since the board started; rows[ncur..
// ncur+nprev) come from the network's previous report, saved `gap_s` seconds
// ago when `gap_known`. A device in both keeps this boot's row, with the
// earlier first sighting and, if it has none now, the name it had. The others
// are carried over: offline, aged by the gap, the most recently seen first.
// Everything past `cap` rows is dropped. Returns how many rows are left.
inline size_t report_merge(ReportRow* rows, size_t ncur, size_t nprev, bool gap_known, uint32_t gap_s,
                           size_t cap) {
    size_t kept = ncur;
    for (size_t i = ncur; i < ncur + nprev; ++i) {
        ReportRow p = rows[i];
        bool dup = false;
        for (size_t j = 0; j < kept; ++j) {
            ReportRow& c = rows[j];
            if (!mac_equal(c.mac, p.mac)) continue;
            dup = true;
            if (j < ncur) {
                if (p.first_unix != 0 && (c.first_unix == 0 || p.first_unix < c.first_unix)) {
                    c.first_unix = p.first_unix;
                }
                if (c.hostname[0] == '\0' && p.hostname[0] != '\0') {
                    std::memcpy(c.hostname, p.hostname, sizeof(c.hostname));
                }
            }
            break;
        }
        if (dup) continue;
        p.carried = true;
        p.online = false;
        p.up_s = 0;
        p.self = false;
        if (gap_known) p.ago_s = p.ago_s > 0xFFFFFFFFu - gap_s ? 0xFFFFFFFFu : p.ago_s + gap_s;
        // Insertion into the carried rows, most recent first.
        size_t k = kept;
        while (k > ncur && report_more_recent(p, rows[k - 1])) {
            rows[k] = rows[k - 1];
            --k;
        }
        rows[k] = p;
        ++kept;
    }
    return kept < cap ? kept : cap;
}

// --- On flash ------------------------------------------------------------------

// What the first line of a report says about it.
struct ReportMeta {
    char ssid[kReportSsidMax];
    char subnet[kReportSubnetMax];
    char gateway[16];
    char gateway_mac[18];
    char board_ip[16];
    char board_mac[18];
    char version[32];
    uint32_t seq;
    uint32_t saved_unix;
    uint32_t saved_up_s;
    ClockSource clock;
    uint32_t passes;
    bool learning;
    uint32_t count;
    uint32_t online;
};

// Appends `s` to out[len..cap) as JSON string content: quote and backslash
// escaped, control characters dropped, UTF-8 kept. False when out of room.
inline bool report_put(char* out, size_t cap, size_t& len, const char* s) {
    for (const char* p = s; p != nullptr && *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == 0x7F) continue;
        const bool esc = c == '"' || c == '\\';
        if (len + (esc ? 2 : 1) >= cap) return false;
        if (esc) out[len++] = '\\';
        out[len++] = static_cast<char>(c);
    }
    out[len] = '\0';
    return true;
}

// Appends text as it is: field names, numbers.
inline bool report_raw(char* out, size_t cap, size_t& len, const char* s) {
    const size_t n = std::strlen(s);
    if (len + n >= cap) return false;
    std::memcpy(out + len, s, n + 1);
    len += n;
    return true;
}

inline bool report_num(char* out, size_t cap, size_t& len, uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof(b), "%lu", static_cast<unsigned long>(v));
    return report_raw(out, cap, len, b);
}

inline bool report_str_field(char* out, size_t cap, size_t& len, const char* name, const char* v) {
    return report_raw(out, cap, len, name) && report_raw(out, cap, len, "\"") &&
           report_put(out, cap, len, v) && report_raw(out, cap, len, "\"");
}

// The first line, through "devices":[ and the newline after it.
inline size_t report_header_line(const ReportMeta& m, char* out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    char fmt[24];
    std::snprintf(fmt, sizeof(fmt), "{\"report\":%u,", static_cast<unsigned>(kReportFormat));
    const bool ok = report_raw(out, cap, n, fmt) &&
        report_str_field(out, cap, n, "\"ssid\":", m.ssid) &&
        report_str_field(out, cap, n, ",\"subnet\":", m.subnet) &&
        report_str_field(out, cap, n, ",\"gateway\":", m.gateway) &&
        report_str_field(out, cap, n, ",\"gateway_mac\":", m.gateway_mac) &&
        report_str_field(out, cap, n, ",\"board_ip\":", m.board_ip) &&
        report_str_field(out, cap, n, ",\"board_mac\":", m.board_mac) &&
        report_str_field(out, cap, n, ",\"version\":", m.version) &&
        report_raw(out, cap, n, ",\"seq\":") && report_num(out, cap, n, m.seq) &&
        report_raw(out, cap, n, ",\"saved_unix\":") && report_num(out, cap, n, m.saved_unix) &&
        report_raw(out, cap, n, ",\"saved_up_s\":") && report_num(out, cap, n, m.saved_up_s) &&
        report_str_field(out, cap, n, ",\"clock\":", clock_source_text(m.clock)) &&
        report_raw(out, cap, n, ",\"passes\":") && report_num(out, cap, n, m.passes) &&
        report_raw(out, cap, n, m.learning ? ",\"learning\":true" : ",\"learning\":false") &&
        report_raw(out, cap, n, ",\"count\":") && report_num(out, cap, n, m.count) &&
        report_raw(out, cap, n, ",\"online\":") && report_num(out, cap, n, m.online) &&
        report_raw(out, cap, n, ",\"devices\":[\n");
    return ok ? n : 0;
}

// One device's line, with "," before the newline unless it is the last.
// The fields are those of GET /api/devices, last_seen_s counted back from the
// save, then the report's own.
inline size_t report_row_line(const ReportRow& r, const char* vendor, bool last, char* out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    char mac[18], ip[16];
    mac_format(r.mac, mac);
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", static_cast<unsigned>(r.ip >> 24 & 255),
                  static_cast<unsigned>(r.ip >> 16 & 255), static_cast<unsigned>(r.ip >> 8 & 255),
                  static_cast<unsigned>(r.ip & 255));
    const bool ok = report_str_field(out, cap, n, "{\"mac\":", mac) &&
        report_str_field(out, cap, n, ",\"ip\":", ip) &&
        report_str_field(out, cap, n, ",\"hostname\":", r.hostname) &&
        report_str_field(out, cap, n, ",\"vendor\":", vendor != nullptr ? vendor : "") &&
        report_str_field(out, cap, n, ",\"status\":", status_text(r.status)) &&
        report_raw(out, cap, n, mac_is_local(r.mac) ? ",\"randomised\":true" : ",\"randomised\":false") &&
        report_raw(out, cap, n, r.self ? ",\"self\":true" : ",\"self\":false") &&
        report_raw(out, cap, n, r.online ? ",\"online\":true" : ",\"online\":false") &&
        report_raw(out, cap, n, ",\"up_s\":") && report_num(out, cap, n, r.up_s) &&
        report_raw(out, cap, n, ",\"last_seen_s\":") && report_num(out, cap, n, r.ago_s) &&
        report_raw(out, cap, n, ",\"seen_unix\":") && report_num(out, cap, n, r.seen_unix) &&
        report_raw(out, cap, n, ",\"first_unix\":") && report_num(out, cap, n, r.first_unix) &&
        report_raw(out, cap, n, r.carried ? ",\"carried\":true}" : ",\"carried\":false}") &&
        report_raw(out, cap, n, last ? "\n" : ",\n");
    return ok ? n : 0;
}

static const char kReportTail[] = "]}\n";

inline Status report_status_of(const char* s) {
    if (s != nullptr && std::strcmp(s, "known") == 0) return Status::Known;
    if (s != nullptr && std::strcmp(s, "private") == 0) return Status::Private;
    return Status::Unknown;
}

inline ClockSource report_clock_of(const char* s) {
    if (s != nullptr && std::strcmp(s, "internet") == 0) return ClockSource::Internet;
    if (s != nullptr && std::strcmp(s, "client") == 0) return ClockSource::Client;
    return ClockSource::None;
}
