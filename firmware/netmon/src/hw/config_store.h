#pragma once
#include <cstdint>

struct NetworkCred {
    char ssid[33];
    char pass[65];
};

struct Settings {
    NetworkCred nets[4];
    uint8_t net_count;

    bool use_dhcp;
    uint32_t static_ip;
    uint32_t static_mask;
    uint32_t static_gw;
    uint32_t static_dns;

    uint32_t scan_interval_s;
    uint32_t probe_interval_s;
    uint32_t offline_after_s;
    uint32_t learning_window_s;

    // 0 means "derive from the active interface" — the portable default.
    uint32_t subnet_override;
    uint32_t mask_override;

    // The Nearby page: whether to scan for Wi-Fi networks and listen for
    // Bluetooth devices, and how often while nobody has the page open, in
    // seconds. 0 there means only while somebody is watching. See air_plan.h.
    bool air_wifi;
    bool air_ble;
    uint32_t air_background_s;

    // The Bluetooth link (ble_link.h): the API for paired phones. On unless
    // switched off; pairing still needs the code from the Settings page.
    bool ble_link;
};

void settings_defaults(Settings& s);
bool settings_load(Settings& s);
bool settings_save(const Settings& s);
