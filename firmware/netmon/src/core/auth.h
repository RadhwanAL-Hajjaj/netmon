#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// Signing in to the board. Every page and every API call needs a session,
// over Wi-Fi and over the Bluetooth link alike; only the sign-in page, the
// sign-in itself and a short "what is this board" answer do not.
//
// The password is the update password from secrets.h until the owner sets a
// login password of their own on the Settings page. The update password keeps
// working after that, as the way back in: whoever has it can reflash the
// board anyway, so it opens nothing they could not open already.
//
// A session is a random 128-bit token. The browser keeps it in a cookie, the
// app in its settings, and the board keeps only its SHA-256, so a copy of the
// board's flash holds no token anybody could use. "Remember me" (the web) and
// "Save password" (the app) ask for a session that lasts 30 days and outlives
// restarts; otherwise a session ends after 12 hours unused.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "sha256.h"

static const size_t kTokenBytes = 16;
static const size_t kTokenHex = 2 * kTokenBytes;
static const size_t kSessionSlots = 8;

static const uint32_t kSessionIdleS = 12u * 3600u;        // a session not kept: 12 hours unused
static const uint32_t kSessionShortMaxS = 7u * 86400u;     // ... and a week at most, however busy
static const uint32_t kSessionLongS = 30u * 86400u;        // "Remember me": 30 days

struct Session {
    bool used;
    bool remember;
    bool earlier_boot;       // made before the board last started
    uint8_t hash[32];        // SHA-256 of the token
    uint32_t created_up;     // uptime seconds when made, this boot
    uint32_t used_up;        // uptime seconds when last used, this boot
    uint32_t created_unix;   // 0 when the board did not know the time
};

// A token is 32 lower-case hex digits; anything else is not one of ours and
// is not even hashed.
inline bool token_well_formed(const char* t) {
    if (t == nullptr || std::strlen(t) != kTokenHex) return false;
    for (size_t i = 0; i < kTokenHex; ++i) {
        const char c = t[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

inline void token_hash(const char* t, uint8_t out[32]) { sha256(t, kTokenHex, out); }

// Whether a session may still be used at `now_up` (uptime seconds) and
// `now_unix` (0 when the board does not know the time).
inline bool session_alive(const Session& s, uint32_t now_up, uint32_t now_unix) {
    if (!s.used) return false;
    const uint32_t life = s.remember ? kSessionLongS : kSessionShortMaxS;
    if (s.created_unix != 0 && now_unix != 0) {
        // A clock that went backwards past the session's start says nothing
        // about its age; the uptime test below still holds it to account.
        if (now_unix >= s.created_unix && now_unix - s.created_unix >= life) return false;
    }
    if (!s.earlier_boot && now_up - s.created_up >= life) return false;
    if (!s.remember && now_up - s.used_up >= kSessionIdleS) return false;
    return true;
}

template <size_t N>
struct SessionTable {
    Session s[N];

    void clear() { std::memset(s, 0, sizeof(s)); }

    size_t count(uint32_t now_up, uint32_t now_unix) const {
        size_t n = 0;
        for (size_t i = 0; i < N; ++i) {
            if (session_alive(s[i], now_up, now_unix)) ++n;
        }
        return n;
    }

    // The slot holding this token's hash, or -1. Every slot is compared, in
    // constant time each, so the answer takes as long for a stranger's token.
    int find(const uint8_t hash[32], uint32_t now_up, uint32_t now_unix) const {
        int found = -1;
        for (size_t i = 0; i < N; ++i) {
            const bool same = same_bytes(s[i].hash, hash, 32);
            if (same && session_alive(s[i], now_up, now_unix)) found = static_cast<int>(i);
        }
        return found;
    }

    // Where a new session goes: an empty or dead slot, else the one used
    // longest ago, sessions not kept going before kept ones.
    size_t pick(uint32_t now_up, uint32_t now_unix) const {
        for (size_t i = 0; i < N; ++i) {
            if (!session_alive(s[i], now_up, now_unix)) return i;
        }
        size_t best = N;
        for (int pass = 0; pass < 2 && best == N; ++pass) {
            const bool want_remember = pass == 1;
            uint32_t oldest_age = 0;
            for (size_t i = 0; i < N; ++i) {
                if (s[i].remember != want_remember) continue;
                // From an earlier boot counts as used at boot: the oldest of all.
                const uint32_t age = s[i].earlier_boot ? now_up + 1 : now_up - s[i].used_up;
                if (best == N || age > oldest_age) {
                    best = i;
                    oldest_age = age;
                }
            }
        }
        return best == N ? 0 : best;
    }

    size_t add(const uint8_t hash[32], bool remember, uint32_t now_up, uint32_t now_unix) {
        const size_t i = pick(now_up, now_unix);
        Session& n = s[i];
        std::memset(&n, 0, sizeof(n));
        n.used = true;
        n.remember = remember;
        std::memcpy(n.hash, hash, 32);
        n.created_up = now_up;
        n.used_up = now_up;
        n.created_unix = now_unix;
        return i;
    }

    void touch(int i, uint32_t now_up) {
        if (i >= 0 && static_cast<size_t>(i) < N) s[i].used_up = now_up;
    }

    void remove(int i) {
        if (i >= 0 && static_cast<size_t>(i) < N) std::memset(&s[i], 0, sizeof(s[i]));
    }

    // Every session but `keep` (-1 for none), as when the password changes.
    size_t remove_all_but(int keep) {
        size_t n = 0;
        for (size_t i = 0; i < N; ++i) {
            if (static_cast<int>(i) == keep || !s[i].used) continue;
            std::memset(&s[i], 0, sizeof(s[i]));
            ++n;
        }
        return n;
    }

    // Empties dead slots. True when anything went, so the caller knows the
    // copy on flash is out of date.
    bool purge(uint32_t now_up, uint32_t now_unix) {
        bool any = false;
        for (size_t i = 0; i < N; ++i) {
            if (s[i].used && !session_alive(s[i], now_up, now_unix)) {
                std::memset(&s[i], 0, sizeof(s[i]));
                any = true;
            }
        }
        return any;
    }

    // The board has just learned the time: sessions made this boot without
    // it get the date they were made, so their 30 days count from then.
    bool date_undated(uint32_t now_up, uint32_t now_unix) {
        if (now_unix == 0) return false;
        bool any = false;
        for (size_t i = 0; i < N; ++i) {
            Session& x = s[i];
            if (!x.used || x.created_unix != 0 || x.earlier_boot) continue;
            const uint32_t age = now_up - x.created_up;
            if (now_unix > age) {
                x.created_unix = now_unix - age;
                any = true;
            }
        }
        return any;
    }
};

// --- Reading the token from a request -----------------------------------------

// The value of cookie `name` in a Cookie header ("a=1; nm_s=abc; b=2"), into
// out. False when it is not there or will not fit.
inline bool cookie_value(const char* header, const char* name, char* out, size_t cap) {
    if (cap > 0) out[0] = '\0';
    if (header == nullptr || name == nullptr) return false;
    const size_t nlen = std::strlen(name);
    const char* p = header;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        const char* end = std::strchr(p, ';');
        if (end == nullptr) end = p + std::strlen(p);
        const char* eq = static_cast<const char*>(std::memchr(p, '=', static_cast<size_t>(end - p)));
        if (eq != nullptr && static_cast<size_t>(eq - p) == nlen && std::memcmp(p, name, nlen) == 0) {
            const char* v = eq + 1;
            const char* ve = end;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
            const size_t len = static_cast<size_t>(ve - v);
            if (len + 1 > cap) return false;
            std::memcpy(out, v, len);
            out[len] = '\0';
            return true;
        }
        p = end;
    }
    return false;
}

// The token in "Authorization: Bearer <token>", into out.
inline bool bearer_value(const char* header, char* out, size_t cap) {
    if (cap > 0) out[0] = '\0';
    if (header == nullptr) return false;
    const char* p = header;
    while (*p == ' ') ++p;
    static const char kScheme[] = "bearer";
    for (size_t i = 0; i < sizeof(kScheme) - 1; ++i) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kScheme[i]) return false;
    }
    p += sizeof(kScheme) - 1;
    if (*p != ' ') return false;
    while (*p == ' ') ++p;
    const char* e = p + std::strlen(p);
    while (e > p && e[-1] == ' ') --e;
    const size_t len = static_cast<size_t>(e - p);
    if (len == 0 || len + 1 > cap) return false;
    std::memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

// --- The password --------------------------------------------------------------

static const size_t kPasswordMin = 8;
static const size_t kPasswordMax = 64;
static const uint32_t kPasswordRounds = 4096;

enum class PasswordRule : uint8_t { Ok, TooShort, TooLong, Control };

// Any characters but control ones, counted in bytes: 8 to 64.
inline PasswordRule password_rule(const char* pw) {
    if (pw == nullptr) return PasswordRule::TooShort;
    const size_t n = std::strlen(pw);
    if (n < kPasswordMin) return PasswordRule::TooShort;
    if (n > kPasswordMax) return PasswordRule::TooLong;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(pw[i]);
        if (c < 0x20 || c == 0x7f) return PasswordRule::Control;
    }
    return PasswordRule::Ok;
}

inline const char* password_rule_text(PasswordRule r) {
    switch (r) {
        case PasswordRule::TooShort: return "The password needs at least 8 characters.";
        case PasswordRule::TooLong: return "The password can be at most 64 characters.";
        case PasswordRule::Control: return "The password cannot contain control characters.";
        case PasswordRule::Ok: break;
    }
    return "";
}

struct PasswordHash {
    bool set;
    uint8_t salt[16];
    uint32_t rounds;
    uint8_t hash[32];
};

inline void password_make(PasswordHash& out, const char* pw, const uint8_t salt[16], uint32_t rounds) {
    std::memset(&out, 0, sizeof(out));
    out.set = true;
    std::memcpy(out.salt, salt, 16);
    out.rounds = rounds;
    pbkdf2_sha256(reinterpret_cast<const uint8_t*>(pw), std::strlen(pw), out.salt, sizeof(out.salt),
                  out.rounds, out.hash, sizeof(out.hash));
}

inline bool password_matches(const PasswordHash& ph, const char* pw) {
    if (!ph.set || pw == nullptr || ph.rounds == 0 || ph.rounds > 1000000) return false;
    uint8_t h[32];
    pbkdf2_sha256(reinterpret_cast<const uint8_t*>(pw), std::strlen(pw), ph.salt, sizeof(ph.salt), ph.rounds,
                  h, sizeof(h));
    return same_bytes(h, ph.hash, sizeof(h));
}

// --- Guessing --------------------------------------------------------------------
//
// Five wrong passwords from one place are free; after that each costs a wait,
// 30 seconds doubling to 15 minutes, until a right one. A place is an IPv4
// address, or a Bluetooth connection. And whatever the places, more than 30
// wrong passwords in ten minutes stop every sign-in for a minute, so somebody
// changing address cannot go faster. Nothing here is kept across a restart.

static const uint8_t kLoginFree = 5;
static const uint32_t kLoginWaitS = 30;
static const uint32_t kLoginWaitMaxS = 900;
static const uint32_t kLoginForgetS = 3600;      // a place quiet this long starts afresh
static const uint32_t kLoginGlobalSpanS = 600;
static const uint32_t kLoginGlobalMax = 30;
static const uint32_t kLoginGlobalWaitS = 60;

template <size_t N>
struct LoginThrottle {
    struct Entry {
        bool used;
        uint32_t client;
        uint8_t fails;
        uint32_t last_up;
        uint32_t until_up;
    };
    Entry e[N];
    uint32_t global_since;
    uint32_t global_fails;
    uint32_t global_until;

    void clear() { std::memset(this, 0, sizeof(*this)); }

    Entry* find(uint32_t client) {
        for (size_t i = 0; i < N; ++i) {
            if (e[i].used && e[i].client == client) return &e[i];
        }
        return nullptr;
    }

    // Seconds `client` has to wait before trying again; 0 when it may try now.
    uint32_t wait_s(uint32_t client, uint32_t now_up) {
        uint32_t w = 0;
        if (global_until > now_up) w = global_until - now_up;
        Entry* x = find(client);
        if (x != nullptr) {
            if (now_up - x->last_up >= kLoginForgetS) {
                x->used = false;
            } else if (x->until_up > now_up && x->until_up - now_up > w) {
                w = x->until_up - now_up;
            }
        }
        return w;
    }

    void failed(uint32_t client, uint32_t now_up) {
        if (now_up - global_since >= kLoginGlobalSpanS || global_fails == 0) {
            global_since = now_up;
            global_fails = 0;
        }
        if (++global_fails > kLoginGlobalMax) {
            global_until = now_up + kLoginGlobalWaitS;
            global_since = now_up;
            global_fails = 0;
        }
        Entry* x = find(client);
        if (x == nullptr) {
            // A free entry, else the one quiet longest.
            size_t pick = 0;
            uint32_t oldest = 0;
            for (size_t i = 0; i < N; ++i) {
                if (!e[i].used) {
                    pick = i;
                    break;
                }
                const uint32_t age = now_up - e[i].last_up;
                if (age >= oldest) {
                    oldest = age;
                    pick = i;
                }
            }
            x = &e[pick];
            std::memset(x, 0, sizeof(*x));
            x->used = true;
            x->client = client;
        }
        if (x->fails < 255) ++x->fails;
        x->last_up = now_up;
        if (x->fails > kLoginFree) {
            uint32_t w = kLoginWaitS;
            for (uint8_t i = kLoginFree + 1; i < x->fails && w < kLoginWaitMaxS; ++i) w *= 2;
            if (w > kLoginWaitMaxS) w = kLoginWaitMaxS;
            x->until_up = now_up + w;
        }
    }

    void succeeded(uint32_t client) {
        Entry* x = find(client);
        if (x != nullptr) x->used = false;
    }
};

// A "next" address after signing in: a path on this board, nothing that
// could send the browser somewhere else.
inline bool next_path_ok(const char* p) {
    if (p == nullptr || p[0] != '/' || p[1] == '/' || p[1] == '\\') return false;
    for (const char* c = p; *c != '\0'; ++c) {
        const unsigned char u = static_cast<unsigned char>(*c);
        if (u < 0x20 || u == 0x7f || *c == '\\') return false;
    }
    return std::strlen(p) < 200;
}
