#include "ble_link.h"

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstring>
#include <vector>

#include <NimBLEDevice.h>
#include "nimble/nimble/host/include/host/ble_store.h"
#include "nimble/porting/nimble/include/os/os_mbuf.h"

#include "air_scan.h"

// Pairing, as the stack sees it: the board can show a 6-digit code (on the
// Settings page, or the app, over Wi-Fi) and cannot type one, so a phone
// pairs by typing the code the board gives. The pairing is bonded (both ends
// keep the keys, so it happens once per phone), protected against a man in
// the middle by that code, and uses LE Secure Connections where the phone
// has them.
//
// The request characteristic takes writes only over a link encrypted with
// keys from such a pairing (WRITE_AUTHEN): a phone that has not paired, or
// paired some other way, is refused by the stack before anything reaches
// this file. Answers go only to the connection that asked.
//
// The board keeps three pairings. A pairing made without the code is let go
// at once, and room for a new one, by forgetting the phone paired longest
// ago, is made only for a pairing with the code while a window is open: a
// stranger in range cannot push a paired phone out.

namespace {

const size_t kMaxConns = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
// An ATT MTU of 247 fills one 251-byte link-layer packet exactly, with data
// length extension, which every phone in years has: the most the radio
// carries per packet with no splitting.
const uint16_t kMtu = 247;
const size_t kFrameMax = kMtu - 3;

// Room for a few requests' frames while loop() is held up by a slow request
// (a Wi-Fi scan, the provider lookup): a request is one to five frames.
const UBaseType_t kFrameQueue = 16;

// The stack's buffers are few (12). Notifications leave this many free, for
// the replies to the phone's own writes and for keeping the link up.
const int kKeepFree = 4;

// While this much is queued for sending, further requests wait.
const size_t kQueuedMax = 16384;

// Frames sent to one connection per pump, before the others get a turn.
const int kFramesPerTurn = 6;

// How often loop() looks at the stack's own list of connections.
const uint32_t kPollMs = 250;

struct Frame {
    uint16_t conn;
    uint16_t len;
    uint8_t data[kFrameMax];
};

// What else the Bluetooth task tells loop(): a connection's encryption
// settled (a pairing finished, or a paired phone came back), or it went.
enum class NoteKind : uint8_t { Auth, Gone };

struct Note {
    NoteKind kind;
    uint16_t conn;
    bool ok;
};

QueueHandle_t g_frames = nullptr;
QueueHandle_t g_notes = nullptr;
std::atomic<uint32_t> g_frames_dropped{0};

// The pairing window, read by the Bluetooth task when a phone asks to pair.
portMUX_TYPE g_pair_mux = portMUX_INITIALIZER_UNLOCKED;
PairWindow g_window{};
std::atomic<bool> g_attempt{false};          // a phone is pairing now
std::atomic<bool> g_attempt_in_window{false};

struct Answer {
    bool active = false;      // handed out, not yet fully sent
    bool has_status = false;
    bool complete = false;    // the handler has finished
    bool started = false;     // the first frame has gone
    uint8_t id = 0;
    uint16_t status = 0;
    std::vector<String> parts;
    size_t chunk = 0;
    size_t offset = 0;
    size_t queued = 0;
};

struct Conn {
    bool used = false;
    uint16_t handle = 0;
    uint32_t since_ms = 0;
    bool secure = false;
    bool dropping = false;    // disconnect asked for
    bool drop_after = false;  // disconnect once the answer has gone
    bool waiting = false;     // rx holds a request not handed out yet
    uint16_t waiting_status = 0;
    LinkAssembler<kLinkRequestMax> rx;
    Answer tx;
};

Conn g_conns[kMaxConns];
Conn* g_serving = nullptr;
size_t g_turn = 0;

NimBLEServer* g_srv = nullptr;
NimBLECharacteristic* g_rx = nullptr;
NimBLECharacteristic* g_tx = nullptr;
bool g_tried = false;
bool g_ready = false;
bool g_on = false;
bool g_adv_pairing = false;   // what the advertisement says now
char g_addr[18] = "";
uint32_t g_served = 0;
uint32_t g_polled_ms = 0;

void answer_clear(Answer& a) {
    a.active = false;
    a.has_status = false;
    a.complete = false;
    a.started = false;
    a.id = 0;
    a.status = 0;
    std::vector<String>().swap(a.parts);    // the memory too, not just the size
    a.chunk = 0;
    a.offset = 0;
    a.queued = 0;
}

void conn_clear(Conn& c) {
    if (g_serving == &c) g_serving = nullptr;
    c.used = false;
    c.handle = 0;
    c.since_ms = 0;
    c.secure = false;
    c.dropping = false;
    c.drop_after = false;
    c.waiting = false;
    c.waiting_status = 0;
    c.rx.reset();
    answer_clear(c.tx);
}

Conn* conn_find(uint16_t handle) {
    for (auto& c : g_conns) {
        if (c.used && c.handle == handle) return &c;
    }
    return nullptr;
}

Conn* conn_add(uint16_t handle) {
    for (auto& c : g_conns) {
        if (!c.used) {
            conn_clear(c);
            c.used = true;
            c.handle = handle;
            c.since_ms = millis();
            return &c;
        }
    }
    return nullptr;
}

size_t queued_total() {
    size_t n = 0;
    for (const auto& c : g_conns) {
        if (c.used) n += c.tx.queued;
    }
    return n;
}

void disconnect(Conn& c) {
    if (c.dropping || g_srv == nullptr) return;
    c.dropping = true;
    g_srv->disconnect(c.handle);
}

// The advertisement: the service, so the app can look for it, and a flag
// saying whether a pairing window is open, so it can say "ready to pair". The
// name goes in the scan response, where there is room for it.
void set_advert(bool pairing) {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData ad;
    ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    ad.addServiceUUID(NimBLEUUID(NETMON_LINK_SERVICE));
    // 0xFFFF is the company identifier for testing and private use. "NM",
    // the link's version, then the flags.
    const uint8_t md[6] = {0xFF, 0xFF, 'N', 'M', kLinkVersion, static_cast<uint8_t>(pairing ? 1 : 0)};
    ad.setManufacturerData(md, sizeof(md));
    NimBLEAdvertisementData sr;
    sr.setName("netmon");
    adv->enableScanResponse(true);
    adv->setAdvertisementData(ad);
    adv->setScanResponseData(sr);
    g_adv_pairing = pairing;
}

void advertise(bool on) {
    if (!g_ready) return;
    if (on) {
        if (g_srv->getConnectedCount() < kMaxConns) NimBLEDevice::startAdvertising();
    } else {
        NimBLEDevice::stopAdvertising();
    }
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        // Asked for encryption at once. A paired phone encrypts with the keys
        // it kept, before its first request; anyone else is asked to pair,
        // which fails without the code.
        NimBLEDevice::startSecurity(info.getConnHandle());
        s->setDataLen(info.getConnHandle(), 251);
        // Room for another phone: keep advertising.
        if (g_on && s->getConnectedCount() < kMaxConns) NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
        // loop() also checks the stack's own list (poll_conns()), in case
        // this note is lost to a full queue.
        Note n{NoteKind::Gone, info.getConnHandle(), false};
        if (g_notes != nullptr) xQueueSend(g_notes, &n, 0);
    }

    // The stack wants the code a pairing phone has to type: the window's,
    // or one nobody was shown.
    uint32_t onPassKeyDisplay() override {
        portENTER_CRITICAL(&g_pair_mux);
        const uint32_t now = millis();
        const bool live = pair_live(g_window, now);
        const uint32_t code = pair_passkey(g_window, now, esp_random());
        portEXIT_CRITICAL(&g_pair_mux);
        g_attempt_in_window = live;
        g_attempt = true;
        return code;
    }

    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Note n{NoteKind::Auth, info.getConnHandle(), info.isEncrypted() && info.isAuthenticated()};
        if (g_notes != nullptr) xQueueSend(g_notes, &n, 0);
    }
};

class RequestCallbacks : public NimBLECharacteristicCallbacks {
    // Runs in the Bluetooth task: copies the frame for loop() and returns.
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        // The stack has already refused writes over a link not paired with
        // the code; checked again here all the same.
        if (!info.isEncrypted() || !info.isAuthenticated() || g_frames == nullptr) return;
        const NimBLEAttValue& v = c->getValue();
        Frame f;
        f.conn = info.getConnHandle();
        f.len = static_cast<uint16_t>(v.size() < kFrameMax ? v.size() : kFrameMax);
        std::memcpy(f.data, v.data(), f.len);
        if (xQueueSend(g_frames, &f, 0) != pdTRUE) g_frames_dropped++;
    }
};

bool window_live() {
    portENTER_CRITICAL(&g_pair_mux);
    const bool live = pair_live(g_window, millis());
    portEXIT_CRITICAL(&g_pair_mux);
    return live;
}

class StoreCallbacks : public NimBLEDeviceCallbacks {
    // Runs in the Bluetooth task when the store of pairings is full.
    int onStoreStatus(struct ble_store_status_event* event, void* arg) override {
        if (event->event_code == BLE_STORE_EVENT_OVERFLOW) {
            const int t = event->overflow.obj_type;
            if (t == BLE_STORE_OBJ_TYPE_OUR_SEC || t == BLE_STORE_OBJ_TYPE_PEER_SEC) {
                if (!event->overflow.value->sec.authenticated || !window_live()) return BLE_HS_ESTORE_CAP;
            } else if (t == BLE_STORE_OBJ_TYPE_PEER_ADDR && !window_live()) {
                return BLE_HS_ESTORE_CAP;
            }
        }
        return ble_store_util_status_rr(event, arg);
    }
};

ServerCallbacks g_server_cb;
RequestCallbacks g_request_cb;
StoreCallbacks g_store_cb;

// Brings the list of connections in line with the stack's: new ones added,
// gone ones forgotten, and each one's security as it stands.
void poll_conns() {
    if (g_srv == nullptr) return;
    for (auto& c : g_conns) {
        if (c.used && ble_gap_conn_find(c.handle, nullptr) != 0) conn_clear(c);
    }
    for (uint16_t h : g_srv->getPeerDevices()) {
        Conn* c = conn_find(h);
        if (c == nullptr) c = conn_add(h);
        if (c == nullptr) continue;
        NimBLEConnInfo info = g_srv->getPeerInfoByHandle(h);
        c->secure = info.isEncrypted() && info.isAuthenticated();
    }
}

// Frames the Bluetooth task queued, into each connection's request.
void take_frames() {
    if (g_frames == nullptr) return;
    Frame f;
    for (int i = 0; i < 24 && xQueueReceive(g_frames, &f, 0) == pdTRUE; ++i) {
        Conn* c = conn_find(f.conn);
        if (c == nullptr) {
            poll_conns();
            c = conn_find(f.conn);
            if (c == nullptr) continue;
        }
        c->secure = true;     // the write got past the stack's check
        if (f.len >= 1 && (f.data[0] & kLinkFirst)) {
            // A new request: whatever came before it, waiting or half sent,
            // is one the phone has given up on.
            c->waiting = false;
            if (c->tx.active && g_serving != c) answer_clear(c->tx);
        }
        switch (c->rx.feed(f.data, f.len)) {
            case LinkAssembler<kLinkRequestMax>::Step::Done:
                c->waiting = true;
                c->waiting_status = 0;
                break;
            case LinkAssembler<kLinkRequestMax>::Step::TooLarge:
                c->waiting = true;
                c->waiting_status = 413;
                break;
            default:
                break;
        }
    }
}

// Connections gone, and the outcomes of pairing attempts and of encryption
// with kept keys.
void take_notes() {
    if (g_notes == nullptr) return;
    Note n;
    while (xQueueReceive(g_notes, &n, 0) == pdTRUE) {
        Conn* c = conn_find(n.conn);
        if (n.kind == NoteKind::Gone) {
            if (c != nullptr) conn_clear(*c);
            if (g_attempt.exchange(false)) {
                // Gone in the middle of pairing: the window stays open for
                // another try with the same code.
                g_attempt_in_window = false;
            }
            continue;
        }
        if (c != nullptr) c->secure = n.ok;
        if (!n.ok && c != nullptr) {
            // Encrypted without the code: a pairing that skipped it ("just
            // works"). Its keys are no use here and must not take a place
            // among the three, so they go, and so does the connection.
            NimBLEConnInfo info = g_srv->getPeerInfoByHandle(n.conn);
            if (info.isEncrypted() && !info.isAuthenticated()) {
                if (info.isBonded()) NimBLEDevice::deleteBond(info.getIdAddress());
                Serial.println(F("[link] refused a pairing made without the code"));
                disconnect(*c);
            }
        }
        if (!g_attempt.exchange(false)) continue;    // kept keys, not a pairing
        const bool in_window = g_attempt_in_window.exchange(false);
        Serial.print(F("[link] pairing "));
        Serial.println(n.ok ? F("succeeded") : F("failed"));
        if (!in_window) continue;   // somebody else's attempt, outside a window
        portENTER_CRITICAL(&g_pair_mux);
        pair_done(g_window, millis(), n.ok);
        portEXIT_CRITICAL(&g_pair_mux);
    }
}

struct Parts {
    const std::vector<String>& v;
    size_t count() const { return v.size(); }
    const uint8_t* data(size_t i) const { return reinterpret_cast<const uint8_t*>(v[i].c_str()); }
    size_t size(size_t i) const { return v[i].length(); }
};

// Sends one connection what it will take of its answer now. False once the
// stack has no more room, which holds for every connection alike.
bool pump_conn(Conn& c) {
    Answer& a = c.tx;
    if (!a.active || !a.has_status || c.dropping) return true;
    // Asked afresh: an answer only ever goes to a link paired with the code.
    // A connection handle the stack has given to somebody new since the
    // request came in gets nothing.
    NimBLEConnInfo info = g_srv->getPeerInfoByHandle(c.handle);
    if (!info.isEncrypted() || !info.isAuthenticated()) {
        answer_clear(a);
        return true;
    }
    const uint16_t mtu = g_srv->getPeerMTU(c.handle);
    size_t cap = mtu > 3 ? mtu - 3 : 20;
    if (cap > kFrameMax) cap = kFrameMax;
    uint8_t buf[kFrameMax];
    for (int n = 0; n < kFramesPerTurn; ++n) {
        if (os_msys_num_free() < kKeepFree) return false;
        const LinkCut cut = link_cut(Parts{a.parts}, a.chunk, a.offset, !a.started, a.complete,
                                     a.id, a.status, buf, cap);
        if (cut.len == 0) return true;               // waiting for the handler
        if (!g_tx->notify(buf, cut.len, c.handle)) return false;
        // Pieces fully sent are freed now, not when the answer ends.
        for (size_t i = a.chunk; i < cut.chunk && i < a.parts.size(); ++i) {
            a.queued -= a.parts[i].length() <= a.queued ? a.parts[i].length() : a.queued;
            a.parts[i] = String();
        }
        a.chunk = cut.chunk;
        a.offset = cut.offset;
        a.started = true;
        if (cut.last) {
            answer_clear(a);
            if (c.drop_after) disconnect(c);
            return true;
        }
    }
    return true;
}

void update_advert() {
    portENTER_CRITICAL(&g_pair_mux);
    const bool live = pair_live(g_window, millis());
    portEXIT_CRITICAL(&g_pair_mux);
    if (live != g_adv_pairing) set_advert(live);
}

}  // namespace

bool ble_link_begin(bool on) {
    if (g_tried) return g_ready;
    g_tried = true;
    g_frames = xQueueCreate(kFrameQueue, sizeof(Frame));
    g_notes = xQueueCreate(12, sizeof(Note));
    if (g_frames == nullptr || g_notes == nullptr) return false;
    if (!ble_stack_begin()) return false;

    NimBLEDevice::setMTU(kMtu);
    NimBLEDevice::setDeviceCallbacks(&g_store_cb);
    NimBLEDevice::setSecurityAuth(true, true, true);       // bond, MITM, Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    g_srv = NimBLEDevice::createServer();
    g_srv->setCallbacks(&g_server_cb, false);
    g_srv->advertiseOnDisconnect(on);
    NimBLEService* svc = g_srv->createService(NETMON_LINK_SERVICE);
    g_rx = svc->createCharacteristic(NETMON_LINK_RX,
                                     NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
                                         NIMBLE_PROPERTY::WRITE_AUTHEN,
                                     kFrameMax);
    g_rx->setCallbacks(&g_request_cb);
    g_tx = svc->createCharacteristic(NETMON_LINK_TX, NIMBLE_PROPERTY::NOTIFY, kFrameMax);
    g_srv->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    // 300 to 500 ms: a phone looking for the board finds it within a second
    // or two, and the radio Wi-Fi shares is hardly troubled.
    adv->setMinInterval(480);
    adv->setMaxInterval(800);
    set_advert(false);

    std::string a = NimBLEDevice::getAddress().toString();
    for (size_t i = 0; i < a.size() && i < sizeof(g_addr) - 1; ++i) {
        const char ch = a[i];
        g_addr[i] = (ch >= 'a' && ch <= 'f') ? static_cast<char>(ch - 'a' + 'A') : ch;
        g_addr[i + 1] = '\0';
    }

    g_ready = true;
    g_on = on;
    if (on) advertise(true);
    return true;
}

bool ble_link_ready() { return g_ready; }

void ble_link_enable(bool on) {
    if (!g_ready) return;
    g_on = on;
    g_srv->advertiseOnDisconnect(on);
    advertise(on);
    if (!on) {
        for (auto& c : g_conns) {
            if (!c.used) continue;
            if (c.tx.active) {
                c.drop_after = true;
            } else {
                disconnect(c);
            }
        }
    }
}

bool ble_link_enabled() { return g_ready && g_on; }

bool ble_link_next(LinkIncoming& out) {
    if (!g_ready) return false;
    // Frames first: those a connection sent before it went still belong to
    // it, and are dropped with it.
    take_frames();
    take_notes();
    // Switched off: what is still connected is being let go, not served.
    if (!g_on) return false;
    if (queued_total() > kQueuedMax) return false;
    for (size_t k = 0; k < kMaxConns; ++k) {
        const size_t i = (g_turn + k) % kMaxConns;
        Conn& c = g_conns[i];
        if (!c.used || !c.waiting || c.tx.active || c.dropping) continue;
        g_turn = (i + 1) % kMaxConns;
        c.waiting = false;
        std::memset(&out, 0, sizeof(out));
        out.conn = c.handle;
        out.id = c.rx.id();
        out.status = c.waiting_status;
        if (out.status == 0) {
            const LinkParse p = link_parse(c.rx.data(), c.rx.size(), out.req);
            if (p == LinkParse::TooLong) out.status = 414;
            else if (p != LinkParse::Ok) out.status = 400;
        }
        answer_clear(c.tx);
        c.tx.active = true;
        c.tx.id = out.id;
        g_serving = &c;
        return true;
    }
    return false;
}

void ble_link_answer_status(uint16_t status) {
    if (g_serving == nullptr) return;
    g_serving->tx.status = status;
    g_serving->tx.has_status = true;
}

void ble_link_answer_add(const String& part) {
    if (g_serving == nullptr || part.length() == 0) return;
    g_serving->tx.parts.push_back(part);
    g_serving->tx.queued += part.length();
}

void ble_link_answer_add(const char* text) {
    if (text == nullptr || text[0] == '\0') return;
    ble_link_answer_add(String(text));
}

void ble_link_answer_end() {
    if (g_serving == nullptr) return;
    Answer& a = g_serving->tx;
    if (!a.has_status) {
        // A handler that never answered: say so rather than leave the phone
        // waiting for its timeout.
        a.status = 500;
        a.has_status = true;
        a.parts.clear();
        a.parts.push_back(String(F("{\"error\":\"the board did not answer that request\"}")));
        a.queued = a.parts.back().length();
    }
    a.complete = true;
    g_serving = nullptr;
    ++g_served;
}

void ble_link_pump() {
    if (!g_ready) return;
    for (size_t k = 0; k < kMaxConns; ++k) {
        Conn& c = g_conns[(g_turn + k) % kMaxConns];
        if (c.used && !pump_conn(c)) break;
    }
}

void ble_link_flush(uint32_t ms) {
    if (!g_ready) return;
    const uint32_t from = millis();
    while (millis() - from < ms) {
        bool busy = false;
        for (const auto& c : g_conns) {
            if (c.used && c.tx.active && !c.dropping) busy = true;
        }
        if (!busy) return;
        ble_link_pump();
        delay(5);
    }
}

void ble_link_tick() {
    if (!g_ready) return;
    // Connections gone first, so one whose handle the stack has already
    // given to a new connection is not mistaken for it below.
    take_notes();
    const uint32_t now = millis();
    if (now - g_polled_ms >= kPollMs) {
        g_polled_ms = now;
        poll_conns();
        portENTER_CRITICAL(&g_pair_mux);
        const bool live = pair_live(g_window, now);
        portEXIT_CRITICAL(&g_pair_mux);
        for (auto& c : g_conns) {
            if (c.used && link_drop_unpaired(c.secure, c.since_ms, now, live)) {
                Serial.println(F("[link] dropping a connection that never paired"));
                disconnect(c);
            }
            if (c.used && !g_on && !c.tx.active) disconnect(c);
        }
        update_advert();
    }
}

void ble_link_pair(bool open) {
    if (!g_ready) return;
    portENTER_CRITICAL(&g_pair_mux);
    if (open) {
        pair_open(g_window, millis(), esp_random());
    } else {
        pair_close(g_window);
    }
    portEXIT_CRITICAL(&g_pair_mux);
    update_advert();
}

bool ble_link_pairing(char code[7], uint32_t& left_ms, const char*& result, uint32_t& result_age_s) {
    portENTER_CRITICAL(&g_pair_mux);
    const uint32_t now = millis();
    const PairWindow w = g_window;
    portEXIT_CRITICAL(&g_pair_mux);
    const bool live = pair_live(w, now);
    left_ms = pair_left_ms(w, now);
    if (live) {
        pair_code_text(w.code, code);
    } else {
        code[0] = '\0';
    }
    result = pair_result_text(w.result);
    result_age_s = w.result != 0 ? (now - w.result_ms) / 1000 : 0;
    return live;
}

bool ble_link_forget_all() {
    if (!g_ready) return false;
    const bool ok = NimBLEDevice::deleteAllBonds();
    for (auto& c : g_conns) {
        if (!c.used) continue;
        if (c.tx.active) {
            c.drop_after = true;
        } else {
            disconnect(c);
        }
    }
    Serial.println(ok ? F("[link] forgot every paired phone") : F("[link] could not forget the paired phones"));
    return ok;
}

int ble_link_bonds() { return g_ready ? NimBLEDevice::getNumBonds() : 0; }

int ble_link_max_bonds() { return CONFIG_BT_NIMBLE_MAX_BONDS; }

int ble_link_connected() {
    int n = 0;
    for (const auto& c : g_conns) {
        if (c.used) ++n;
    }
    return n;
}

int ble_link_secure() {
    int n = 0;
    for (const auto& c : g_conns) {
        if (c.used && c.secure) ++n;
    }
    return n;
}

const char* ble_link_address() { return g_addr; }

uint32_t ble_link_served() { return g_served; }
