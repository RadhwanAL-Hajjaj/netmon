#pragma once
// Pure logic — no Arduino headers.
//
// Who made a Bluetooth device, from the company identifier at the front of the
// manufacturer data it advertises. Most devices around a house never say their
// name, so without this the Bluetooth list is a column of addresses. The
// identifiers are the Bluetooth SIG's assigned numbers, checked against the
// full register; the comment on each row is the register's own name for it.
// Like the MAC vendor table, this is the subset worth its flash: the makers of
// phones, watches, earbuds, trackers and the chips inside the rest.
//
// MUST stay sorted ascending by id: ble_vendor() does a binary search, and
// test_ble_vendor_table_is_sorted guards that.
#include <cstddef>
#include <cstdint>

struct BleVendor {
    uint16_t id;
    const char* name;
};

static const BleVendor BLE_VENDORS[] = {
    {0x0006u, "Microsoft"},              // Microsoft
    {0x0008u, "Motorola"},               // Motorola
    {0x000Du, "Texas Instruments"},      // Texas Instruments Inc.
    {0x000Fu, "Broadcom"},               // Broadcom Corporation
    {0x001Du, "Qualcomm"},               // Qualcomm
    {0x0046u, "MediaTek"},               // MediaTek, Inc.
    {0x004Cu, "Apple"},                  // Apple, Inc.
    {0x0055u, "Plantronics"},            // Plantronics, Inc.
    {0x0057u, "Harman"},                 // Harman International Industries, Inc.
    {0x0059u, "Nordic Semiconductor"},   // Nordic Semiconductor ASA
    {0x005Du, "Realtek"},                // Realtek Semiconductor Corporation
    {0x0067u, "GN Hearing"},             // GN Hearing
    {0x006Bu, "Polar"},                  // Polar Electro OY
    {0x0075u, "Samsung"},                // Samsung Electronics Co. Ltd.
    {0x0087u, "Garmin"},                 // Garmin International, Inc.
    {0x009Eu, "Bose"},                   // Bose Corporation
    {0x009Fu, "Suunto"},                 // Suunto Oy
    {0x00C4u, "LG"},                     // LG Electronics
    {0x00CCu, "Beats"},                  // Beats Electronics
    {0x00CDu, "Microchip"},              // Microchip Technology Inc.
    {0x00E0u, "Google"},                 // Google
    {0x0103u, "Bang & Olufsen"},         // Bang & Olufsen A/S
    {0x012Du, "Sony"},                   // Sony Corporation
    {0x0157u, "Huami (Amazfit)"},        // Anhui Huami Information Technology Co., Ltd.
    {0x0171u, "Amazon"},                 // Amazon.com Services LLC
    {0x018Eu, "Google"},                 // Google LLC
    {0x01ABu, "Meta"},                   // Meta Platforms, Inc.
    {0x01DAu, "Logitech"},               // Logitech International SA
    {0x01DDu, "Philips"},                // Koninklijke Philips N.V.
    {0x01FCu, "Wahoo"},                  // Wahoo Fitness, LLC
    {0x022Bu, "Tesla"},                  // Tesla, Inc.
    {0x027Du, "Huawei"},                 // HUAWEI Technologies Co., Ltd.
    {0x02B2u, "Oura"},                   // Oura Health Oy
    {0x02C5u, "Lenovo"},                 // Lenovo (Singapore) Pte Ltd.
    {0x02E5u, "Espressif"},              // Espressif Systems (Shanghai) Co., Ltd.
    {0x02FFu, "Silicon Labs"},           // Silicon Laboratories
    {0x038Fu, "Xiaomi"},                 // Xiaomi Inc.
    {0x03FFu, "Withings"},               // Withings
    {0x0494u, "Sennheiser"},             // SENNHEISER electronic GmbH & Co. KG
    {0x0499u, "Ruuvi"},                  // Ruuvi Innovations Ltd.
    {0x0553u, "Nintendo"},               // Nintendo Co., Ltd.
    {0x058Eu, "Meta"},                   // Meta Platforms Technologies, LLC
    {0x05A7u, "Sonos"},                  // Sonos Inc
    {0x060Fu, "Signify (Philips Hue)"},  // Signify Netherlands B.V.
    {0x065Au, "Marshall"},               // Marshall Group AB
    {0x067Cu, "Tile"},                   // Tile, Inc.
    {0x068Eu, "Razer"},                  // Razer Inc.
    {0x072Fu, "OnePlus"},                // OnePlus Electronics (Shenzhen) Co., Ltd.
    {0x079Au, "OPPO"},                   // GuangDong Oppo Mobile Telecommunications Corp., Ltd.
    {0x07C9u, "Skullcandy"},             // Skullcandy, Inc.
    {0x07D0u, "Tuya"},                   // Hangzhou Tuya Information Technology Co., Ltd
    {0x0822u, "Adafruit"},               // adafruit industries
    {0x0837u, "vivo"},                   // vivo Mobile Communication Co., Ltd.
    {0x08A4u, "realme"},                 // Realme Chongqing Mobile Telecommunications Corp., Ltd.
    {0x08C3u, "Chipolo"},                // CHIPOLO d.o.o.
    {0x09C6u, "Honor"},                  // Honor Device Co., Ltd.
    {0x0A12u, "Dyson"},                  // Dyson Technology Limited
    {0x0CC2u, "Anker"},                  // Anker Innovations Limited
    {0x0CCBu, "Nothing"},                // NOTHING TECHNOLOGY LIMITED
};

static const size_t BLE_VENDOR_COUNT = sizeof(BLE_VENDORS) / sizeof(BLE_VENDORS[0]);

inline const char* ble_vendor(uint16_t id, const BleVendor* table = BLE_VENDORS,
                              size_t n = BLE_VENDOR_COUNT) {
    if (table == nullptr) return nullptr;
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (table[mid].id == id) return table[mid].name;
        if (table[mid].id < id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return nullptr;
}
