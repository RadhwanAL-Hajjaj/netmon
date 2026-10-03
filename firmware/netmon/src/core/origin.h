#pragma once
// Pure logic — no Arduino headers.
//
// Whether a POST that changes something may go ahead, judged by its Origin.
//
// A browser attaches Origin to a POST, naming the site whose page sent it, and
// a page on any site can post to this board: a plain form, or fetch in no-cors
// mode, needs nobody's permission to send, only to read the answer. So a
// request that changes settings or restarts the board is refused when its
// Origin names some other site. Clients that are not browsers (the Android
// app, curl) send no Origin at all and are let through.
//
// This stops drive-by web pages. It does not stop a device on the LAN talking
// to the board directly; that takes a key on these requests.
#include <cstddef>

inline char origin_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// `origin` is the Origin header, empty or nullptr when there was none; `host`
// is the Host header, which carries the port when it is not 80, exactly as
// the browser puts it in Origin. Scheme and host compare without case.
inline bool origin_allowed(const char* origin, const char* host) {
    if (origin == nullptr || origin[0] == '\0') return true;
    if (host == nullptr || host[0] == '\0') return false;
    static const char kScheme[] = "http://";
    size_t i = 0;
    for (; kScheme[i] != '\0'; ++i) {
        if (origin_lower(origin[i]) != kScheme[i]) return false;
    }
    const char* o = origin + i;
    const char* h = host;
    while (*o != '\0' && *h != '\0') {
        if (origin_lower(*o) != origin_lower(*h)) return false;
        ++o;
        ++h;
    }
    return *o == '\0' && *h == '\0';
}
