#pragma once
// A small subset of the IEEE MA-L registry (standards-oui.ieee.org/oui/oui.csv):
// common consumer, camera, NAS and networking vendors. The full 1.1 MB
// registry cannot share 4 MB of flash with OTA.
//
// To add a vendor, insert {0xAABBCCu, "Name"} at its place in prefix order.
//
// MUST stay sorted ascending by prefix: oui_lookup does a binary search.
// test_oui_table_is_sorted guards that.
#include "oui_lookup.h"

static const OuiEntry BUILTIN_OUI[] = {
    {0x00089Bu, "ICP Electronics Inc."},
    {0x000C42u, "Routerboard.com"},
    {0x001132u, "Synology Incorporated"},
    {0x00155Du, "Microsoft Corporation"},
    {0x001788u, "Philips Lighting BV"},
    {0x001A11u, "Google Inc."},
    {0x005056u, "VMware Inc."},
    {0x080027u, "PCS Systemtechnik GmbH"},
    {0x240AC4u, "Espressif Inc."},
    {0x245EBEu, "QNAP Systems Inc."},
    {0x2462ABu, "Espressif Inc."},
    {0x282986u, "APC by Schneider Electric"},
    {0x2CCF67u, "Raspberry Pi (Trading) Ltd"},
    {0x30AEA4u, "Espressif Inc."},
    {0x3C5AB4u, "Google Inc."},
    {0x4022D8u, "Espressif Inc."},
    {0x488F5Au, "Routerboard.com"},
    {0x5091E3u, "TP-Link Corporation Limited"},
    {0x7C9EBDu, "Espressif Inc."},
    {0x84F3EBu, "Espressif Inc."},
    {0x906A94u, "Hangzhou Huacheng Network Technology"},
    {0xA4CF12u, "Espressif Inc."},
    {0xA83162u, "Hangzhou Huacheng Network Technology"},
    {0xAC84C6u, "TP-LINK TECHNOLOGIES CO. LTD."},
    {0xB0E4D5u, "Google Inc."},
    {0xB827EBu, "Raspberry Pi Foundation"},
    {0xBC325Fu, "Zhejiang Dahua Technology Co. Ltd."},
    {0xC44F33u, "Espressif Inc."},
    {0xC82E18u, "Espressif Inc."},
    {0xD05FB8u, "Texas Instruments"},
    {0xD4E9F4u, "Espressif Inc. (observed)"},
    {0xD8B04Cu, "Jinan USR IOT Technology Co. Ltd."},
    {0xD8BFC0u, "Espressif Inc."},
    {0xD8F15Bu, "Espressif Inc."},
    {0xDCA632u, "Raspberry Pi Trading Ltd"},
    {0xE4246Cu, "Zhejiang Dahua Technology Co. Ltd."},
    {0xE45F01u, "Raspberry Pi Trading Ltd"},
    {0xE49C67u, "Apple Inc."},
    {0xECFABCu, "Espressif Inc."},
    {0xF4F5D8u, "Google Inc."},
};

static const size_t BUILTIN_OUI_COUNT =
    sizeof(BUILTIN_OUI) / sizeof(BUILTIN_OUI[0]);
