#include "report_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

#include "../core/cidr.h"

namespace {

String temp_path(size_t i) {
    String p = F("/report");
    p += String(static_cast<unsigned>(i));
    p += F(".tmp");
    return p;
}

// One line into `buf`, without its newline. A line longer than the buffer is
// cut short and the rest of it skipped. -1 at the end of the file.
int read_line(File& f, char* buf, size_t cap) {
    if (!f.available()) return -1;
    size_t n = 0;
    while (f.available()) {
        const int c = f.read();
        if (c < 0 || c == '\n') break;
        if (n + 1 < cap) buf[n++] = static_cast<char>(c);
    }
    buf[n] = '\0';
    return static_cast<int>(n);
}

void copy_str(char* dst, size_t cap, JsonVariantConst v) {
    const char* s = v.is<const char*>() ? v.as<const char*>() : "";
    if (s == nullptr) s = "";
    std::strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
}

// The first line, closed off so it parses on its own.
bool read_header(File& f, ReportSlot& s) {
    char line[kReportLineMax + 4];
    const int n = read_line(f, line, kReportLineMax);
    if (n <= 0) return false;
    std::strcat(line, "]}");
    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;
    if ((doc["report"] | 0) != kReportFormat) return false;
    std::memset(&s, 0, sizeof(s));
    copy_str(s.ssid, sizeof(s.ssid), doc["ssid"]);
    copy_str(s.subnet, sizeof(s.subnet), doc["subnet"]);
    copy_str(s.gateway, sizeof(s.gateway), doc["gateway"]);
    s.seq = doc["seq"] | 0u;
    s.saved_unix = doc["saved_unix"] | 0u;
    s.count = doc["count"] | 0u;
    s.online = doc["online"] | 0u;
    s.used = s.ssid[0] != '\0' && s.subnet[0] != '\0';
    return s.used;
}

}  // namespace

String report_path(size_t i) {
    String p = F("/report");
    p += String(static_cast<unsigned>(i));
    p += F(".json");
    return p;
}

void reports_load(ReportSlot (&slots)[kReportSlots]) {
    for (size_t i = 0; i < kReportSlots; ++i) {
        std::memset(&slots[i], 0, sizeof(slots[i]));
        // A power cut between writing and renaming leaves only the new file.
        if (!LittleFS.exists(report_path(i)) && LittleFS.exists(temp_path(i))) {
            LittleFS.rename(temp_path(i), report_path(i));
        }
        File f = LittleFS.open(report_path(i), "r");
        if (!f) continue;
        ReportSlot s{};
        if (read_header(f, s)) {
            s.bytes = f.size();
            slots[i] = s;
        }
        f.close();
    }
}

size_t report_read_rows(size_t i, ReportRow* rows, size_t cap) {
    File f = LittleFS.open(report_path(i), "r");
    if (!f) return 0;
    char line[kReportLineMax];
    if (read_line(f, line, sizeof(line)) <= 0) {
        f.close();
        return 0;
    }
    size_t n = 0;
    JsonDocument doc;
    while (n < cap) {
        const int len = read_line(f, line, sizeof(line));
        if (len < 0) break;
        if (len == 0 || line[0] != '{') continue;
        if (line[len - 1] == ',') line[len - 1] = '\0';
        if (deserializeJson(doc, line)) continue;
        ReportRow& r = rows[n];
        std::memset(&r, 0, sizeof(r));
        if (!mac_parse(doc["mac"] | "", r.mac)) continue;
        ipv4_parse(doc["ip"] | "", r.ip);
        copy_str(r.hostname, sizeof(r.hostname), doc["hostname"]);
        r.status = report_status_of(doc["status"] | "");
        r.online = doc["online"] | false;
        r.self = doc["self"] | false;
        r.carried = doc["carried"] | false;
        r.up_s = doc["up_s"] | 0u;
        r.ago_s = doc["last_seen_s"] | 0u;
        r.seen_unix = doc["seen_unix"] | 0u;
        r.first_unix = doc["first_unix"] | 0u;
        ++n;
    }
    f.close();
    return n;
}

bool report_write(size_t i, const ReportMeta& m, const ReportRow* rows, size_t n,
                  const char* (*vendor_of)(const Mac&), ReportSlot& slot) {
    const String tmp = temp_path(i);
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    char line[kReportLineMax];
    size_t len = report_header_line(m, line, sizeof(line));
    bool ok = len > 0 && f.write(reinterpret_cast<const uint8_t*>(line), len) == len;
    size_t bytes = len;
    for (size_t k = 0; k < n && ok; ++k) {
        const char* vendor = vendor_of != nullptr ? vendor_of(rows[k].mac) : nullptr;
        len = report_row_line(rows[k], vendor, k + 1 == n, line, sizeof(line));
        ok = len > 0 && f.write(reinterpret_cast<const uint8_t*>(line), len) == len;
        bytes += len;
    }
    const size_t tail = sizeof(kReportTail) - 1;
    ok = ok && f.write(reinterpret_cast<const uint8_t*>(kReportTail), tail) == tail;
    bytes += tail;
    f.close();
    if (!ok) {
        LittleFS.remove(tmp);
        return false;
    }
    if (!LittleFS.rename(tmp, report_path(i))) {
        // Some file systems will not rename over an existing file.
        LittleFS.remove(report_path(i));
        if (!LittleFS.rename(tmp, report_path(i))) {
            LittleFS.remove(tmp);
            return false;
        }
    }
    std::memset(&slot, 0, sizeof(slot));
    slot.used = true;
    std::strncpy(slot.ssid, m.ssid, sizeof(slot.ssid) - 1);
    std::strncpy(slot.subnet, m.subnet, sizeof(slot.subnet) - 1);
    std::strncpy(slot.gateway, m.gateway, sizeof(slot.gateway) - 1);
    slot.seq = m.seq;
    slot.saved_unix = m.saved_unix;
    slot.count = m.count;
    slot.online = m.online;
    slot.bytes = bytes;
    return true;
}

bool report_remove(size_t i) {
    LittleFS.remove(temp_path(i));
    return !LittleFS.exists(report_path(i)) || LittleFS.remove(report_path(i));
}

size_t reports_free_bytes() {
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    return total > used ? total - used : 0;
}
