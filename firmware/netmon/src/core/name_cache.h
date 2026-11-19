#pragma once
// Pure logic — no Arduino headers.
//
// Hostnames, remembered by MAC address. A device only says its name when it
// joins the network, which an always-on camera may do once a month, so a name
// learned once is kept here and written to flash. It survives restarts and
// firmware updates, and goes back on the device's row when the sweep next
// finds it.
//
// The file holds one line per device: twelve hex digits, a tab, the name.
// Names are cleaned of control characters on the way in, so neither separator
// can appear inside one.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mac.h"

static const size_t kNameMax = 64;       // bytes, terminator included

// Drops an incomplete UTF-8 sequence from the end of s[0..n): what is left of
// a multi-byte character after a length limit cut through it.
inline size_t name_utf8_tail(const char* s, size_t n) {
    size_t i = n;
    size_t cont = 0;
    while (i > 0 && cont < 3 &&
           (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) {
        --i;
        ++cont;
    }
    if (i == 0) return cont ? 0 : n;
    const unsigned char lead = static_cast<unsigned char>(s[i - 1]);
    if (lead < 0x80) return cont ? i : n;         // stray continuation bytes
    const size_t need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    return need == cont ? n : i - 1;
}

// Copies `in` to `out`, keeping printable bytes, trimming surrounding spaces
// and cutting at kNameMax - 1 bytes without splitting a character. UTF-8 is
// kept whole, so a name in another script survives. False when nothing is left.
inline bool name_clean(const char* in, char (&out)[kNameMax]) {
    out[0] = '\0';
    if (in == nullptr) return false;
    size_t n = 0;
    for (const char* p = in; *p && n < kNameMax - 1; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == 0x7F) continue;
        if (n == 0 && c == ' ') continue;
        out[n++] = static_cast<char>(c);
    }
    n = name_utf8_tail(out, n);
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = '\0';
    return n > 0;
}

struct NameEntry {
    Mac mac;
    uint32_t used;                 // recency stamp, for eviction
    char name[kNameMax];
};

template <size_t N>
class NameCache {
  public:
    NameCache() : count_(0), clock_(0) { std::memset(items_, 0, sizeof(items_)); }

    size_t size() const { return count_; }
    size_t capacity() const { return N; }
    const NameEntry& at(size_t i) const { return items_[i]; }

    // The stored name for this MAC, or nullptr.
    const char* get(const Mac& m) {
        NameEntry* e = find(m);
        if (e == nullptr) return nullptr;
        e->used = ++clock_;
        return e->name;
    }

    // Stores a name for this MAC. True when that changed what is stored,
    // which is exactly when the file needs writing again. When full, the
    // least recently used entry makes room.
    bool put(const Mac& m, const char* name) {
        char clean[kNameMax];
        if (!name_clean(name, clean)) return false;
        NameEntry* e = find(m);
        if (e != nullptr) {
            e->used = ++clock_;
            if (std::strcmp(e->name, clean) == 0) return false;
            std::memcpy(e->name, clean, sizeof(clean));
            return true;
        }
        e = count_ < N ? &items_[count_++] : least_recent();
        e->mac = m;
        e->used = ++clock_;
        std::memcpy(e->name, clean, sizeof(clean));
        return true;
    }

  private:
    NameEntry* find(const Mac& m) {
        for (size_t i = 0; i < count_; ++i) {
            if (mac_equal(items_[i].mac, m)) return &items_[i];
        }
        return nullptr;
    }
    NameEntry* least_recent() {
        NameEntry* oldest = &items_[0];
        for (size_t i = 1; i < count_; ++i) {
            if (items_[i].used < oldest->used) oldest = &items_[i];
        }
        return oldest;
    }

    NameEntry items_[N];
    size_t count_;
    uint32_t clock_;
};

// One line of the file, "B827EB123456\traspberrypi\n". Returns the length
// written, or 0 when it does not fit in `cap` (terminator included).
inline size_t name_line_format(const Mac& m, const char* name, char* out,
                               size_t cap) {
    static const char* hex = "0123456789ABCDEF";
    if (out == nullptr || name == nullptr) return 0;
    const size_t len = std::strlen(name);
    if (len == 0 || cap < 12 + 1 + len + 2) return 0;
    size_t o = 0;
    for (size_t i = 0; i < 6; ++i) {
        out[o++] = hex[(m.b[i] >> 4) & 0x0F];
        out[o++] = hex[m.b[i] & 0x0F];
    }
    out[o++] = '\t';
    std::memcpy(out + o, name, len);
    o += len;
    out[o++] = '\n';
    out[o] = '\0';
    return o;
}

// Reads one line back, with or without its line ending. Anything malformed is
// refused, so a damaged file loses only the damaged lines.
inline bool name_line_parse(const char* line, Mac& m, char (&name)[kNameMax]) {
    name[0] = '\0';
    if (line == nullptr) return false;
    const char* tab = std::strchr(line, '\t');
    if (tab == nullptr || tab - line != 12) return false;
    char mac_hex[13];
    std::memcpy(mac_hex, line, 12);
    mac_hex[12] = '\0';
    Mac parsed{};
    if (!mac_parse(mac_hex, parsed)) return false;
    char raw[kNameMax + 8];
    size_t n = 0;
    for (const char* p = tab + 1; *p && *p != '\n' && *p != '\r' && n < sizeof(raw) - 1; ++p) {
        raw[n++] = *p;
    }
    raw[n] = '\0';
    if (!name_clean(raw, name)) return false;
    m = parsed;
    return true;
}
