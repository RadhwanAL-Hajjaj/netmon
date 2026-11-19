#include "scanner_import.h"

#include <Arduino.h>
#include <nvs.h>

#include <cstring>

#include "../core/net_list.h"
#include "../core/validate.h"

// NVS directly rather than Preferences: Preferences logs an error for a
// namespace that does not exist, which is every board that never ran the
// scanner, on every start-up in setup mode. Opened read-only first for the
// same reason: opening for writing would create the namespace.
static bool read_str(nvs_handle_t h, const char* key, char* out, size_t cap) {
    size_t len = cap;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static void forget_scanner_network() {
    nvs_handle_t h;
    if (nvs_open("scanner", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "pass");
    nvs_commit(h);
    nvs_close(h);
}

bool scanner_import(Settings& s) {
    if (s.net_count != 0) return false;
    nvs_handle_t h;
    if (nvs_open("scanner", NVS_READONLY, &h) != ESP_OK) return false;
    char ssid[sizeof(NetworkCred::ssid)] = {0};
    char pass[sizeof(NetworkCred::pass)] = {0};
    const bool have = read_str(h, "ssid", ssid, sizeof(ssid)) && ssid[0] != '\0';
    if (have) read_str(h, "pass", pass, sizeof(pass));
    nvs_close(h);
    if (!have) return false;

    const bool usable = validate_credentials(ssid, pass) == ConfigError::None;
    bool imported = false;
    if (usable) {
        Settings next = s;
        netlist_remember(next.nets, next.net_count, ssid, pass);
        if (settings_save(next)) {
            s = next;
            imported = true;
            Serial.print(F("[cfg] took the scanner sketch's network: "));
            Serial.println(ssid);
        }
    }
    // Erased once it is safely in config.json, or when it can never be used.
    // A failed write leaves it for the next start-up to try again.
    if (imported || !usable) forget_scanner_network();
    std::memset(pass, 0, sizeof(pass));
    return imported;
}
