#pragma once
// Pure logic — no Arduino headers.
//
// Reads a Bluetooth LE advertisement: the raw bytes a device broadcasts,
// advertising data followed by its scan response when there was one. The
// radio side hands these over untouched (BleSighting) and they are taken
// apart here, in loop(), the same way dhcp_parse.h takes apart a DHCP packet,
// so that every rule about what a device is can be tested on a PC.
//
// The format is a run of fields, each a length byte, a type byte and
// length - 1 bytes of data. Only the fields the Nearby page uses are read.
#include <cstddef>
#include <cstdint>
#include <cstring>

// Legacy advertising: 31 bytes of advertising data and 31 of scan response.
static const size_t kBlePayloadMax = 62;

// Field types, from the Bluetooth SIG's assigned numbers.
static const uint8_t kAdUuid16Some = 0x02;
static const uint8_t kAdUuid16All = 0x03;
static const uint8_t kAdNameShort = 0x08;
static const uint8_t kAdNameFull = 0x09;
static const uint8_t kAdTxPower = 0x0A;
static const uint8_t kAdServiceData16 = 0x16;
static const uint8_t kAdAppearance = 0x19;
static const uint8_t kAdManufacturer = 0xFF;

static const size_t kAdMaxUuids = 6;

struct BleAdvInfo {
    // The name as sent, not yet cleaned; points into the payload.
    const uint8_t* name;
    uint8_t name_len;
    bool name_full;             // the complete name rather than a shortened one

    // Manufacturer data: the company identifier, then whatever follows it.
    bool has_company;
    uint16_t company;
    const uint8_t* mfr;         // the bytes after the company identifier
    uint8_t mfr_len;

    // 16-bit service UUIDs, from the UUID lists and from service data.
    uint16_t uuid16[kAdMaxUuids];
    uint8_t uuid_count;

    bool has_appearance;
    uint16_t appearance;

    bool has_tx_power;
    int8_t tx_power;
};

inline uint16_t ble_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline void ble_adv_add_uuid(BleAdvInfo& a, uint16_t u) {
    for (uint8_t i = 0; i < a.uuid_count; ++i) {
        if (a.uuid16[i] == u) return;
    }
    if (a.uuid_count < kAdMaxUuids) a.uuid16[a.uuid_count++] = u;
}

// Fills `out` from `n` bytes at `p`. A zero length byte is skipped as padding
// rather than taken as the end: some devices pad their advertising data to 31
// bytes with zeros, and the scan response comes after the padding. A field
// that runs past the end stops the reading; everything before it is kept.
// Returns false only when nothing could be read at all.
inline bool ble_adv_parse(const uint8_t* p, size_t n, BleAdvInfo& out) {
    std::memset(&out, 0, sizeof(out));
    if (p == nullptr || n == 0) return false;
    if (n > kBlePayloadMax) n = kBlePayloadMax;
    bool any = false;
    size_t i = 0;
    while (i < n) {
        const uint8_t len = p[i];
        if (len == 0) {
            ++i;
            continue;
        }
        if (i + 1 + len > n) break;
        const uint8_t type = p[i + 1];
        const uint8_t* d = p + i + 2;
        const uint8_t dlen = static_cast<uint8_t>(len - 1);
        any = true;
        switch (type) {
            case kAdUuid16Some:
            case kAdUuid16All:
                for (uint8_t k = 0; k + 1 < dlen; k += 2) ble_adv_add_uuid(out, ble_le16(d + k));
                break;
            case kAdNameFull:
            case kAdNameShort:
                // The complete name wins over a shortened one, wherever each is.
                if (dlen > 0 && (out.name == nullptr || (type == kAdNameFull && !out.name_full))) {
                    out.name = d;
                    out.name_len = dlen;
                    out.name_full = type == kAdNameFull;
                }
                break;
            case kAdTxPower:
                if (dlen >= 1) {
                    out.has_tx_power = true;
                    out.tx_power = static_cast<int8_t>(d[0]);
                }
                break;
            case kAdServiceData16:
                if (dlen >= 2) ble_adv_add_uuid(out, ble_le16(d));
                break;
            case kAdAppearance:
                if (dlen >= 2) {
                    out.has_appearance = true;
                    out.appearance = ble_le16(d);
                }
                break;
            case kAdManufacturer:
                // The first manufacturer field is the one kept; a second one
                // is rare, and the first is what identifies the maker.
                if (dlen >= 2 && !out.has_company) {
                    out.has_company = true;
                    out.company = ble_le16(d);
                    out.mfr = d + 2;
                    out.mfr_len = static_cast<uint8_t>(dlen - 2);
                }
                break;
            default:
                break;
        }
        i += 1 + len;
    }
    return any;
}
