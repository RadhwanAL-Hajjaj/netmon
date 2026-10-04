#pragma once
// The radio side of the Bluetooth link (src/core/ble_link.h): a GATT service
// that carries the board's API to paired phones.
//
// The Bluetooth task only copies what arrives and answers the stack's
// questions about pairing. Everything else, the requests themselves included,
// happens in loop(), like the rest of this firmware: ble_link_next() hands
// over one whole request at a time, the handler answers into
// ble_link_answer_*(), and ble_link_pump() sends the answer out as the
// connection takes it.
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

#include "../core/ble_link.h"

// The service and its two characteristics. The last six bytes spell
// "netmon" in ASCII; the rest is random.
#define NETMON_LINK_SERVICE "4e4d0001-9c2b-4d8e-a1f3-6e65746d6f6e"
#define NETMON_LINK_RX "4e4d0002-9c2b-4d8e-a1f3-6e65746d6f6e"   // the app writes requests
#define NETMON_LINK_TX "4e4d0003-9c2b-4d8e-a1f3-6e65746d6f6e"   // the board notifies answers

// Starts the Bluetooth stack if nothing has yet, then the service, the
// pairing rules and, when `on`, advertising. False when the stack would not
// start. Called once; later calls report the first answer.
bool ble_link_begin(bool on);
bool ble_link_ready();

// Switches the link on (advertising, taking connections) or off (no
// advertising, every connection dropped once its answer has gone).
void ble_link_enable(bool on);
bool ble_link_enabled();

// A whole request from a paired phone, waiting to be served. `status` is
// non-zero when it cannot be: 413 too long, 400 unreadable; the handler
// then answers with that and nothing is dispatched.
struct LinkIncoming {
    uint16_t conn;
    uint8_t id;
    uint16_t status;
    LinkRequest req;
};

// The next request to serve, if any. A request waits while the answers
// already queued are large, so a slow phone cannot run the board out of
// memory. The request's body stays valid until the next call.
bool ble_link_next(LinkIncoming& out);

// Building the answer to the request ble_link_next() just handed out. The
// status comes first, then the body in as many pieces as suit the handler.
void ble_link_answer_status(uint16_t status);
void ble_link_answer_add(const String& part);
void ble_link_answer_add(const char* text);
void ble_link_answer_end();

// Sends what the connections will take now. Every loop().
void ble_link_pump();

// Sends everything queued, for up to `ms`: before a restart.
void ble_link_flush(uint32_t ms);

// Connections, security and the pairing window. Every loop().
void ble_link_tick();

// --- what GET /api/ble reports -----------------------------------------------

// Opens the pairing window, or keeps the open one going; see ble_link.h in
// core. Closes it with `open` false.
void ble_link_pair(bool open);
// The window as it stands: whether it is open, its code, time left and how
// the last attempt went.
bool ble_link_pairing(char code[7], uint32_t& left_ms, const char*& result, uint32_t& result_age_s);

// Forgets every paired phone. Connections still open are dropped once their
// answers have gone, the one asking included.
bool ble_link_forget_all();

int ble_link_bonds();
int ble_link_max_bonds();
int ble_link_connected();        // connections open now
int ble_link_secure();           // ... of which paired and encrypted
const char* ble_link_address();  // "D4:E9:F4:A3:B8:AE", or "" before start
uint32_t ble_link_served();      // requests answered since start-up
