#pragma once
#include <cstdint>

// What the outside world reports back about this connection. Everything here
// is observed from off-network, so it describes the router's uplink rather
// than anything this board can see directly.
struct IspInfo {
    bool valid;
    char ip[46];          // public address, wide enough for IPv6
    char isp[64];
    char org[64];
    char asn[64];
    char city[48];
    char region[48];
    char country[48];
    char tz[48];
    uint32_t rtt_ms;      // time to open the TCP connection
    char error[72];       // why the last attempt failed, when it did
};

// Asks a public lookup service who this connection belongs to. Blocking, with
// a hard deadline: the caller is an HTTP request handler, so the browser is
// already waiting and a stall there is expected rather than surprising.
// Returns false and fills `error` on any failure.
bool isp_fetch(IspInfo& out);
