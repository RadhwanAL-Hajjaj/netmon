#pragma once
// Pure logic — no Arduino headers, no dynamic allocation.
//
// The Bluetooth link: the board's HTTP API, carried over Bluetooth LE, so the
// Android app can reach the board without being on its Wi-Fi.
//
// The board offers one GATT service with two characteristics. The app writes
// requests to one of them and the board answers with notifications on the
// other. A request or an answer is usually longer than one ATT value, so both
// travel as numbered frames:
//
//   byte 0   flags: 0x01 the first frame of a message, 0x02 the last
//            (one frame on its own carries both). The other bits are 0.
//   byte 1   the request's number, chosen by the app; every frame of the
//            answer carries the same number back
//   bytes 2- payload
//
// The payloads of a request's frames, put together, are a cut-down HTTP
// request: a line with the method and target, header lines, a blank line and
// the body.
//
//   POST /api/nearby/find\n
//   \n
//   {"stop":true}
//
// The payloads of an answer's frames, put together, are the HTTP status as
// two bytes, least significant first, then the body, exactly as the same
// request over Wi-Fi would have had it. The app turns that back into what an
// HTTP reply would have given it, so every screen works the same either way.
//
// Who may use the link is the Bluetooth stack's business, not this file's:
// the request characteristic takes writes only over a link that was paired
// with the 6-digit code. See hw/ble_link.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>

// The link's version, in the advertisement and in GET /api/ble. A change to
// the frames above that an older app would misread gets a new number.
static const uint8_t kLinkVersion = 1;

static const uint8_t kLinkFirst = 0x01;
static const uint8_t kLinkLast = 0x02;
static const size_t kLinkHeader = 2;     // flags, number
static const size_t kLinkStatus = 2;     // the status at the start of an answer

// The longest request the board takes. The largest the app sends is the Wi-Fi
// settings, a few hundred bytes.
static const size_t kLinkRequestMax = 2048;

// Frames of a request, put back together. One per connection: a phone sends
// one request at a time and waits for its answer.
template <size_t N>
class LinkAssembler {
  public:
    enum class Step : uint8_t {
        Ignored,    // not a frame, or one for a message this has given up on
        More,       // taken, more to come
        Done,       // a whole request: data(), size(), id()
        TooLarge,   // a whole request, but longer than N: answer 413
    };

    LinkAssembler() { reset(); }

    void reset() {
        open_ = false;
        overflow_ = false;
        len_ = 0;
        id_ = 0;
        buf_[0] = '\0';
    }

    Step feed(const uint8_t* frame, size_t len) {
        if (frame == nullptr || len < kLinkHeader) return Step::Ignored;
        const uint8_t flags = frame[0];
        if (flags & ~(kLinkFirst | kLinkLast)) return Step::Ignored;
        const uint8_t id = frame[1];
        if (flags & kLinkFirst) {
            // A first frame always starts afresh: whatever was half received
            // belonged to a request the phone has given up on.
            open_ = true;
            overflow_ = false;
            len_ = 0;
            id_ = id;
        } else if (!open_ || id != id_) {
            // The middle of a message that was never started, or of one that
            // a newer request replaced.
            return Step::Ignored;
        }
        const size_t n = len - kLinkHeader;
        if (!overflow_) {
            if (len_ + n > N) {
                overflow_ = true;
                len_ = 0;
            } else {
                std::memcpy(buf_ + len_, frame + kLinkHeader, n);
                len_ += n;
            }
        }
        buf_[overflow_ ? 0 : len_] = '\0';
        if (!(flags & kLinkLast)) return Step::More;
        open_ = false;
        return overflow_ ? Step::TooLarge : Step::Done;
    }

    uint8_t id() const { return id_; }
    // NUL-terminated after size() bytes, for parsing; the body itself may
    // hold anything.
    char* data() { return buf_; }
    size_t size() const { return len_; }
    bool busy() const { return open_; }

  private:
    char buf_[N + 1];
    size_t len_;
    uint8_t id_;
    bool open_;
    bool overflow_;
};

// A request, read out of the text the frames carried.
static const size_t kLinkPathMax = 48;
static const size_t kLinkQueryMax = 160;
static const size_t kLinkKeyMax = 80;
static const size_t kLinkAuthMax = 72;

struct LinkRequest {
    bool post;                        // false: GET
    char path[kLinkPathMax];          // "/api/nearby/find"
    char query[kLinkQueryMax];        // "after=12", undecoded, no '?'
    char key[kLinkKeyMax];            // X-Netmon-Key, if the request had one
    char auth[kLinkAuthMax];          // Authorization ("Bearer <token>"), from 0.13
    const char* body;                 // inside the assembler's buffer
    size_t body_len;
};

enum class LinkParse : uint8_t { Ok, BadMethod, BadTarget, TooLong };

inline bool link_name_is(const char* s, size_t n, const char* name) {
    if (std::strlen(name) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// Copies s[0..n) into out when it fits, with its terminating NUL.
inline bool link_copy(char* out, size_t cap, const char* s, size_t n) {
    if (n + 1 > cap) return false;
    std::memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

// Reads `text` (len bytes, NUL-terminated after them) into `out`. The body
// points into `text`. Lines may end in "\n" or "\r\n".
inline LinkParse link_parse(const char* text, size_t len, LinkRequest& out) {
    std::memset(&out, 0, sizeof(out));
    out.body = text + len;
    const char* end = text + len;

    // The request line: METHOD SP TARGET, an optional " HTTP/1.1" ignored.
    const char* eol = static_cast<const char*>(std::memchr(text, '\n', len));
    const char* line_end = eol != nullptr ? eol : end;
    const char* sp = static_cast<const char*>(std::memchr(text, ' ', static_cast<size_t>(line_end - text)));
    if (sp == nullptr) return LinkParse::BadMethod;
    const size_t mlen = static_cast<size_t>(sp - text);
    if (mlen == 3 && std::memcmp(text, "GET", 3) == 0) {
        out.post = false;
    } else if (mlen == 4 && std::memcmp(text, "POST", 4) == 0) {
        out.post = true;
    } else {
        return LinkParse::BadMethod;
    }
    const char* target = sp + 1;
    const char* tend = line_end;
    if (tend > target && tend[-1] == '\r') --tend;
    const char* sp2 = static_cast<const char*>(std::memchr(target, ' ', static_cast<size_t>(tend - target)));
    if (sp2 != nullptr) tend = sp2;
    if (tend == target || target[0] != '/') return LinkParse::BadTarget;
    const char* q = static_cast<const char*>(std::memchr(target, '?', static_cast<size_t>(tend - target)));
    const char* pend = q != nullptr ? q : tend;
    for (const char* p = target; p < pend; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c <= ' ' || c >= 0x7f) return LinkParse::BadTarget;
    }
    if (!link_copy(out.path, sizeof(out.path), target, static_cast<size_t>(pend - target))) {
        return LinkParse::TooLong;
    }
    if (q != nullptr && !link_copy(out.query, sizeof(out.query), q + 1, static_cast<size_t>(tend - q - 1))) {
        return LinkParse::TooLong;
    }

    // Header lines up to a blank one. Only the update password and, from
    // 0.13, the session (Authorization) matter here; anything else is skipped.
    const char* p = eol != nullptr ? eol + 1 : end;
    while (p < end) {
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
        const char* le = nl != nullptr ? nl : end;
        const char* lt = le;
        if (lt > p && lt[-1] == '\r') --lt;
        if (lt == p) {                       // the blank line: the body follows
            p = nl != nullptr ? nl + 1 : end;
            out.body = p;
            out.body_len = static_cast<size_t>(end - p);
            return LinkParse::Ok;
        }
        const char* colon = static_cast<const char*>(std::memchr(p, ':', static_cast<size_t>(lt - p)));
        if (colon != nullptr) {
            const size_t nlen = static_cast<size_t>(colon - p);
            const bool is_key = link_name_is(p, nlen, "X-Netmon-Key");
            const bool is_auth = link_name_is(p, nlen, "Authorization");
            if (is_key || is_auth) {
                const char* v = colon + 1;
                while (v < lt && (*v == ' ' || *v == '\t')) ++v;
                char* dst = is_key ? out.key : out.auth;
                const size_t cap = is_key ? sizeof(out.key) : sizeof(out.auth);
                if (!link_copy(dst, cap, v, static_cast<size_t>(lt - v))) return LinkParse::TooLong;
            }
        }
        p = nl != nullptr ? nl + 1 : end;
    }
    // No blank line: a request without a body.
    out.body = end;
    out.body_len = 0;
    return LinkParse::Ok;
}

inline int link_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The value of `name` in a query such as "after=12&force", percent-decoded,
// into out. True when the name is there, value or not: "force" alone counts.
// A value too long for `cap` comes back cut short.
inline bool link_arg(const char* query, const char* name, char* out, size_t cap) {
    if (cap > 0) out[0] = '\0';
    if (query == nullptr || name == nullptr || name[0] == '\0') return false;
    const size_t nlen = std::strlen(name);
    const char* p = query;
    while (*p != '\0') {
        const char* amp = std::strchr(p, '&');
        const char* pe = amp != nullptr ? amp : p + std::strlen(p);
        const char* eq = static_cast<const char*>(std::memchr(p, '=', static_cast<size_t>(pe - p)));
        const char* ke = eq != nullptr ? eq : pe;
        if (static_cast<size_t>(ke - p) == nlen && std::memcmp(p, name, nlen) == 0) {
            size_t o = 0;
            if (eq != nullptr) {
                for (const char* v = eq + 1; v < pe && o + 1 < cap; ++v) {
                    char c = *v;
                    if (c == '+') {
                        c = ' ';
                    } else if (c == '%' && v + 2 < pe && link_hex(v[1]) >= 0 && link_hex(v[2]) >= 0) {
                        c = static_cast<char>(link_hex(v[1]) * 16 + link_hex(v[2]));
                        v += 2;
                    }
                    out[o++] = c;
                }
            }
            if (cap > 0) out[o] = '\0';
            return true;
        }
        if (amp == nullptr) break;
        p = amp + 1;
    }
    return false;
}

// The next frame of an answer, cut from its body as it stands so far.
//
// `chunks` is the body in pieces, as the handler produced it: anything with
// count(), data(i) and size(i). The frame starts `offset` bytes into piece
// `chunk`; `first` says whether any frame has gone out yet, which decides
// whether this one carries the status; `complete` says the handler has
// finished, so no more pieces are coming. `cap` is the largest value the
// connection takes in one notification: its ATT MTU less 3.
//
// Nothing is consumed: the caller sends the frame, and only once the stack
// has taken it moves on to where LinkCut says the next one starts. A frame
// the stack refused is simply cut again later.
struct LinkCut {
    size_t len;        // bytes written to out; 0 means nothing to send yet
    size_t chunk;      // where the next frame starts
    size_t offset;
    bool last;         // this frame ends the answer
};

template <typename Chunks>
LinkCut link_cut(const Chunks& chunks, size_t chunk, size_t offset, bool first, bool complete,
                 uint8_t id, uint16_t status, uint8_t* out, size_t cap) {
    LinkCut cut{0, chunk, offset, false};
    const size_t head = kLinkHeader + (first ? kLinkStatus : 0);
    if (out == nullptr || cap < head + 1) return cut;
    size_t pos = head;
    size_t ci = chunk;
    size_t off = offset;
    while (pos < cap && ci < chunks.count()) {
        const size_t sz = chunks.size(ci);
        if (off >= sz) {
            ++ci;
            off = 0;
            continue;
        }
        size_t take = sz - off;
        if (take > cap - pos) take = cap - pos;
        std::memcpy(out + pos, chunks.data(ci) + off, take);
        pos += take;
        off += take;
    }
    // Skip past empty or used-up pieces, so "everything sent" is easy to see.
    while (ci < chunks.count() && off >= chunks.size(ci)) {
        ++ci;
        off = 0;
    }
    const bool all = ci >= chunks.count();
    const bool last = complete && all;
    // Nothing new and not finished: wait rather than send an empty frame.
    if (pos == head && !first && !last) return cut;
    out[0] = static_cast<uint8_t>((first ? kLinkFirst : 0) | (last ? kLinkLast : 0));
    out[1] = id;
    if (first) {
        out[2] = static_cast<uint8_t>(status & 0xff);
        out[3] = static_cast<uint8_t>(status >> 8);
    }
    cut.len = pos;
    cut.chunk = ci;
    cut.offset = off;
    cut.last = last;
    return cut;
}

// --- Pairing ---------------------------------------------------------------
//
// A phone pairs with a 6-digit code. The board has no screen, so the code is
// shown on the Settings page, or in the app over Wi-Fi: whoever is signed in
// there opens a pairing window and is given the code, a new random one each
// time unless the owner has set a code of their own. Outside a window every
// pairing attempt is given a code nobody was shown, so it fails.
//
// Only the phone starts pairing. Until 0.13 the board also asked every phone
// that connected to pair at once; Android answered with a "Pair with
// netmon?" prompt of its own, often only as a notification, while the app
// asked for pairing too, and the two attempts tripped over each other: the
// link dropped part-way, after the person had typed the code.
//
// A window takes up to three failed attempts, so a mistyped code or a
// dropped connection does not need a new window. A phone that pairs closes
// it. An attempt that never got as far as the code (the connection dropped,
// or nobody answered in time) does not count against it.

static const uint32_t kPairWindowMs = 120000;
static const uint8_t kPairTries = 3;

struct PairWindow {
    bool open;
    uint32_t opened_ms;
    uint32_t code;          // 0 to 999999
    uint8_t failures;       // attempts in this window that counted against it
    uint8_t result;         // of the last attempt in a window: PairResult
    uint8_t why;            // ... and why: PairWhy
    uint32_t result_ms;
};

enum class PairResult : uint8_t { None = 0, Paired = 1, Failed = 2 };

// How an attempt ended, from what the Bluetooth stack reported.
enum class PairWhy : uint8_t {
    None = 0,
    Paired,        // with the code
    WrongCode,     // the codes on the two sides did not match
    Cancelled,     // the person cancelled, or the phone gave up asking for the code
    TimedOut,      // nobody went on within 30 seconds
    Dropped,       // the connection went before pairing finished
    Refused,       // the phone and the board could not agree how to pair
    NoCode,        // the phone paired without any code, which is refused
    TooMany,       // the phone says there were too many attempts just now
    Other,
};

inline bool pair_live(const PairWindow& w, uint32_t now_ms) {
    return w.open && now_ms - w.opened_ms < kPairWindowMs;
}

inline uint32_t pair_left_ms(const PairWindow& w, uint32_t now_ms) {
    return pair_live(w, now_ms) ? kPairWindowMs - (now_ms - w.opened_ms) : 0;
}

// Opens a window, or keeps the one open going for another two minutes with
// the same code, so a page and the app asking together agree on it. `fixed`
// is the owner's own code, or -1 for a random one each window.
inline void pair_open(PairWindow& w, uint32_t now_ms, uint32_t random, int32_t fixed = -1) {
    if (!pair_live(w, now_ms)) {
        w.code = fixed >= 0 ? static_cast<uint32_t>(fixed) % 1000000u : random % 1000000u;
        w.failures = 0;
    } else if (fixed >= 0) {
        w.code = static_cast<uint32_t>(fixed) % 1000000u;
    }
    w.open = true;
    w.opened_ms = now_ms;
    w.result = static_cast<uint8_t>(PairResult::None);
    w.why = static_cast<uint8_t>(PairWhy::None);
}

inline void pair_close(PairWindow& w) { w.open = false; }

// Whether a failed attempt says anything about the code.
inline bool pair_counts(PairWhy why) { return why != PairWhy::Dropped && why != PairWhy::TimedOut; }

// An attempt in a window has finished: a phone that paired closes it, and a
// failure that counts brings it a try closer to closing.
inline void pair_done(PairWindow& w, uint32_t now_ms, bool ok, PairWhy why = PairWhy::Other) {
    w.result = static_cast<uint8_t>(ok ? PairResult::Paired : PairResult::Failed);
    w.why = static_cast<uint8_t>(ok ? PairWhy::Paired : why);
    w.result_ms = now_ms;
    if (ok) {
        w.open = false;
        return;
    }
    if (pair_counts(why) && ++w.failures >= kPairTries) w.open = false;
}

inline uint8_t pair_tries_left(const PairWindow& w) {
    return w.failures >= kPairTries ? 0 : static_cast<uint8_t>(kPairTries - w.failures);
}

// The Bluetooth stack's status numbers (NimBLE's ble_hs.h), as they arrive
// with the end of an attempt: the stack's own errors, the controller's from
// 0x200, and pairing errors from 0x400 (found by the board) and 0x500 (sent
// by the phone), each base plus the Security Manager's reason.
static const int kNimNotConn = 7;
static const int kNimTimeout = 13;
static const int kNimHciBase = 0x200;
static const int kNimSmUsBase = 0x400;
static const int kNimSmPeerBase = 0x500;

inline PairWhy pair_why_of(int status, bool encrypted, bool authenticated) {
    if (status == 0) {
        if (!encrypted) return PairWhy::Other;
        return authenticated ? PairWhy::Paired : PairWhy::NoCode;
    }
    if (status == kNimTimeout) return PairWhy::TimedOut;
    if (status == kNimNotConn) return PairWhy::Dropped;
    int sm = -1;
    if (status > kNimSmUsBase && status < kNimSmUsBase + 0x100) sm = status - kNimSmUsBase;
    if (status > kNimSmPeerBase && status < kNimSmPeerBase + 0x100) sm = status - kNimSmPeerBase;
    switch (sm) {
        case -1: break;
        case 0x04:            // confirm value failed
        case 0x0b:            // DHKey check failed
            return PairWhy::WrongCode;
        case 0x01:            // passkey entry failed
            return PairWhy::Cancelled;
        case 0x09:            // repeated attempts
            return PairWhy::TooMany;
        case 0x03:            // authentication requirements
        case 0x05:            // pairing not supported
        case 0x06:            // encryption key size
        case 0x07:            // command not supported
            return PairWhy::Refused;
        default:
            return PairWhy::Other;
    }
    if (status > kNimHciBase && status < kNimHciBase + 0x100) {
        switch (status - kNimHciBase) {
            case 0x08:        // connection timeout
            case 0x13:        // the phone ended the connection
            case 0x16:        // the board ended it
            case 0x22:        // link layer response timeout
            case 0x3e:        // failed to be established
                return PairWhy::Dropped;
            default:
                break;
        }
    }
    return PairWhy::Other;
}

// For the pages and the app: a key, and words for a person.
inline const char* pair_why_key(uint8_t w) {
    switch (static_cast<PairWhy>(w)) {
        case PairWhy::Paired: return "paired";
        case PairWhy::WrongCode: return "wrong_code";
        case PairWhy::Cancelled: return "cancelled";
        case PairWhy::TimedOut: return "timed_out";
        case PairWhy::Dropped: return "dropped";
        case PairWhy::Refused: return "refused";
        case PairWhy::NoCode: return "no_code";
        case PairWhy::TooMany: return "too_many";
        case PairWhy::Other: return "failed";
        case PairWhy::None: break;
    }
    return "";
}

inline const char* pair_why_text(uint8_t w) {
    switch (static_cast<PairWhy>(w)) {
        case PairWhy::Paired: return "Paired.";
        case PairWhy::WrongCode: return "The code did not match.";
        case PairWhy::Cancelled: return "The phone stopped asking for the code.";
        case PairWhy::TimedOut: return "Nobody entered the code in time.";
        case PairWhy::Dropped: return "The Bluetooth connection dropped before pairing finished.";
        case PairWhy::Refused: return "The phone and the board could not agree how to pair.";
        case PairWhy::NoCode: return "The phone paired without the code, so the board refused it.";
        case PairWhy::TooMany: return "The phone saw too many attempts. Wait a minute, then try again.";
        case PairWhy::Other: return "Pairing failed.";
        case PairWhy::None: break;
    }
    return "";
}

// The passkey the board gives the stack for a pairing attempt now: the
// window's code while one is open, otherwise one nobody was shown.
inline uint32_t pair_passkey(const PairWindow& w, uint32_t now_ms, uint32_t random) {
    return pair_live(w, now_ms) ? w.code : random % 1000000u;
}

// "042517": six digits, leading zeros kept, as the phone asks for them.
inline void pair_code_text(uint32_t code, char out[7]) {
    code %= 1000000u;
    for (int i = 5; i >= 0; --i) {
        out[i] = static_cast<char>('0' + code % 10);
        code /= 10;
    }
    out[6] = '\0';
}

inline const char* pair_result_text(uint8_t r) {
    switch (static_cast<PairResult>(r)) {
        case PairResult::Paired: return "paired";
        case PairResult::Failed: return "failed";
        case PairResult::None: break;
    }
    return "";
}

// A connection that has not proved it was paired is let go after this long:
// otherwise anyone in range could hold the board's few connections open.
// Somebody typing the code during a window gets longer.
static const uint32_t kLinkUnpairedMs = 20000;
static const uint32_t kLinkPairingMs = 90000;

inline bool link_drop_unpaired(bool secure, uint32_t connected_ms, uint32_t now_ms, bool window_live) {
    if (secure) return false;
    return now_ms - connected_ms > (window_live ? kLinkPairingMs : kLinkUnpairedMs);
}
