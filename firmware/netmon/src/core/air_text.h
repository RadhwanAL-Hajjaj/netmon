#pragma once
// Pure logic — no Arduino headers.
//
// Names heard over the air: Wi-Fi network names and Bluetooth device names.
// Anyone in radio range picks them, control characters and broken UTF-8
// included, so they are cleaned the way DHCP hostnames are (name_cache.h)
// before they are kept: control bytes dropped, surrounding spaces trimmed, and
// a length limit never left in the middle of a multi-byte character.
#include <cstddef>

#include "name_cache.h"

// Copies at most `in_len` bytes of `in`, stopping early at a NUL, into `out`,
// which holds `cap` bytes including the terminator. False when nothing
// printable is left, and then `out` is empty.
inline bool air_text_clean(const char* in, size_t in_len, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    if (in == nullptr) return false;
    size_t n = 0;
    for (size_t i = 0; i < in_len && in[i] != '\0' && n + 1 < cap; ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x20 || c == 0x7F) continue;
        if (n == 0 && c == ' ') continue;
        out[n++] = static_cast<char>(c);
    }
    n = name_utf8_tail(out, n);
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = '\0';
    return n > 0;
}
