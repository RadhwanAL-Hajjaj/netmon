#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// SHA-256 (FIPS 180-4), HMAC-SHA-256 (RFC 2104) and PBKDF2-HMAC-SHA-256
// (RFC 8018), for the login: a password is kept only as a salted PBKDF2
// hash, and a session token only as its SHA-256. Written plainly rather than
// fast; a sign-in hashes once, and everything else hashes 32 bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>

struct Sha256 {
    uint32_t h[8];
    uint64_t bytes;      // message length so far
    uint8_t buf[64];
    size_t used;         // bytes waiting in buf
};

inline uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline void sha256_block(Sha256& s, const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
               (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3];
    uint32_t e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + k[i] + w[i];
        const uint32_t S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s.h[0] += a;
    s.h[1] += b;
    s.h[2] += c;
    s.h[3] += d;
    s.h[4] += e;
    s.h[5] += f;
    s.h[6] += g;
    s.h[7] += h;
}

inline void sha256_init(Sha256& s) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(s.h, iv, sizeof(iv));
    s.bytes = 0;
    s.used = 0;
}

inline void sha256_update(Sha256& s, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    s.bytes += len;
    while (len > 0) {
        if (s.used == 0 && len >= 64) {
            sha256_block(s, p);
            p += 64;
            len -= 64;
            continue;
        }
        size_t take = 64 - s.used;
        if (take > len) take = len;
        std::memcpy(s.buf + s.used, p, take);
        s.used += take;
        p += take;
        len -= take;
        if (s.used == 64) {
            sha256_block(s, s.buf);
            s.used = 0;
        }
    }
}

inline void sha256_final(Sha256& s, uint8_t out[32]) {
    const uint64_t bits = s.bytes * 8;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    sha256_update(s, &one, 1);
    while (s.used != 56) sha256_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<uint8_t>(s.h[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(s.h[i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(s.h[i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(s.h[i]);
    }
}

inline void sha256(const void* data, size_t len, uint8_t out[32]) {
    Sha256 s;
    sha256_init(s);
    sha256_update(s, data, len);
    sha256_final(s, out);
}

// HMAC-SHA-256 with the key already absorbed into both halves, so PBKDF2's
// thousands of rounds each cost two blocks rather than four.
struct HmacSha256 {
    Sha256 inner;
    Sha256 outer;
};

inline void hmac_sha256_init(HmacSha256& m, const uint8_t* key, size_t key_len) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        sha256(key, key_len, k);
    } else if (key_len > 0) {
        std::memcpy(k, key, key_len);
    }
    uint8_t pad[64];
    for (int i = 0; i < 64; ++i) pad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
    sha256_init(m.inner);
    sha256_update(m.inner, pad, 64);
    for (int i = 0; i < 64; ++i) pad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    sha256_init(m.outer);
    sha256_update(m.outer, pad, 64);
}

// One MAC of `msg` with the key set up in `m`, which is left as it was.
inline void hmac_sha256_run(const HmacSha256& m, const uint8_t* msg, size_t len, uint8_t out[32]) {
    Sha256 s = m.inner;
    sha256_update(s, msg, len);
    uint8_t inner[32];
    sha256_final(s, inner);
    s = m.outer;
    sha256_update(s, inner, sizeof(inner));
    sha256_final(s, out);
}

inline void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t len, uint8_t out[32]) {
    HmacSha256 m;
    hmac_sha256_init(m, key, key_len);
    hmac_sha256_run(m, msg, len, out);
}

// PBKDF2-HMAC-SHA-256: `out_len` bytes derived from the password and salt.
// The salt can be at most 60 bytes, which is far more than anything here uses.
inline bool pbkdf2_sha256(const uint8_t* pw, size_t pw_len, const uint8_t* salt, size_t salt_len,
                          uint32_t iterations, uint8_t* out, size_t out_len) {
    if (salt_len > 60 || iterations == 0) return false;
    HmacSha256 m;
    hmac_sha256_init(m, pw, pw_len);
    uint8_t block_in[64];
    std::memcpy(block_in, salt, salt_len);
    for (uint32_t blk = 1; out_len > 0; ++blk) {
        block_in[salt_len] = static_cast<uint8_t>(blk >> 24);
        block_in[salt_len + 1] = static_cast<uint8_t>(blk >> 16);
        block_in[salt_len + 2] = static_cast<uint8_t>(blk >> 8);
        block_in[salt_len + 3] = static_cast<uint8_t>(blk);
        uint8_t u[32], t[32];
        hmac_sha256_run(m, block_in, salt_len + 4, u);
        std::memcpy(t, u, sizeof(t));
        for (uint32_t i = 1; i < iterations; ++i) {
            hmac_sha256_run(m, u, sizeof(u), u);
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        const size_t take = out_len < 32 ? out_len : 32;
        std::memcpy(out, t, take);
        out += take;
        out_len -= take;
    }
    return true;
}

// Compares in time that does not depend on where the first difference is.
inline bool same_bytes(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

// The same for two NUL-terminated strings; how long the expected one is may
// leak, which for a password the firmware was built with tells nobody much.
inline bool same_text_ct(const char* given, const char* expected) {
    if (given == nullptr || expected == nullptr) return false;
    const size_t n = std::strlen(expected);
    const size_t g = std::strlen(given);
    uint8_t diff = static_cast<uint8_t>(g != n);
    for (size_t i = 0; i < n; ++i) {
        const char c = i < g ? given[i] : 0;
        diff = static_cast<uint8_t>(diff | (c ^ expected[i]));
    }
    return diff == 0;
}

// Lower-case hex, NUL-terminated: `out` holds 2 * n + 1.
inline void hex_encode(const uint8_t* in, size_t n, char* out) {
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = '\0';
}

inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Exactly 2 * n hex digits, either case, into n bytes.
inline bool hex_decode(const char* in, uint8_t* out, size_t n) {
    if (in == nullptr || std::strlen(in) != 2 * n) return false;
    for (size_t i = 0; i < n; ++i) {
        const int hi = hex_digit(in[2 * i]);
        const int lo = hex_digit(in[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return true;
}
