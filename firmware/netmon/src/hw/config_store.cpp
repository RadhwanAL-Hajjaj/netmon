#include "config_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

// Requires ArduinoJson v7 (JsonDocument without a size template). If the
// Library Manager installs v6, this will not compile — upgrade rather than
// rewriting, v7 is the current line.
#include <ArduinoJson.h>

#include "../core/air_plan.h"

static const char* kPath = "/config.json";

void settings_defaults(Settings& s) {
    memset(&s, 0, sizeof(s));
    s.net_count = 0;
    s.use_dhcp = true;
    s.scan_interval_s = 60;
    s.probe_interval_s = 30;
    s.offline_after_s = 180;
    s.learning_window_s = 600;
    s.subnet_override = 0;
    s.mask_override = 0;
    s.air_wifi = true;
    s.air_ble = true;
    s.air_background_s = 120;
}

bool settings_load(Settings& s) {
    settings_defaults(s);
    if (!LittleFS.begin(true)) {
        Serial.println(F("[cfg] LittleFS mount failed"));
        return false;
    }
    File f = LittleFS.open(kPath, "r");
    if (!f) {
        Serial.println(F("[cfg] no config yet, using defaults"));
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.print(F("[cfg] parse failed: "));
        Serial.println(err.c_str());
        return false;
    }

    JsonArray nets = doc["nets"].as<JsonArray>();
    s.net_count = 0;
    for (JsonObject n : nets) {
        if (s.net_count >= 4) break;
        strncpy(s.nets[s.net_count].ssid, n["ssid"] | "", 32);
        strncpy(s.nets[s.net_count].pass, n["pass"] | "", 64);
        s.nets[s.net_count].ssid[32] = '\0';
        s.nets[s.net_count].pass[64] = '\0';
        if (s.nets[s.net_count].ssid[0] != '\0') ++s.net_count;
    }

    s.use_dhcp = doc["use_dhcp"] | true;
    s.static_ip = doc["static_ip"] | 0u;
    s.static_mask = doc["static_mask"] | 0u;
    s.static_gw = doc["static_gw"] | 0u;
    s.static_dns = doc["static_dns"] | 0u;
    s.scan_interval_s = doc["scan_interval_s"] | 60u;
    s.probe_interval_s = doc["probe_interval_s"] | 30u;
    s.offline_after_s = doc["offline_after_s"] | 180u;
    s.learning_window_s = doc["learning_window_s"] | 600u;
    s.subnet_override = doc["subnet_override"] | 0u;
    s.mask_override = doc["mask_override"] | 0u;
    // Absent from every config.json written before 0.10.0: the defaults.
    s.air_wifi = doc["air_wifi"] | true;
    s.air_ble = doc["air_ble"] | true;
    s.air_background_s = doc["air_background_s"] | 120u;
    if (!air_background_valid(s.air_background_s)) s.air_background_s = 120;
    return true;
}

bool settings_save(const Settings& s) {
    JsonDocument doc;
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (uint8_t i = 0; i < s.net_count && i < 4; ++i) {
        JsonObject n = nets.add<JsonObject>();
        n["ssid"] = s.nets[i].ssid;
        n["pass"] = s.nets[i].pass;
    }
    doc["use_dhcp"] = s.use_dhcp;
    doc["static_ip"] = s.static_ip;
    doc["static_mask"] = s.static_mask;
    doc["static_gw"] = s.static_gw;
    doc["static_dns"] = s.static_dns;
    doc["scan_interval_s"] = s.scan_interval_s;
    doc["probe_interval_s"] = s.probe_interval_s;
    doc["offline_after_s"] = s.offline_after_s;
    doc["learning_window_s"] = s.learning_window_s;
    doc["subnet_override"] = s.subnet_override;
    doc["mask_override"] = s.mask_override;
    doc["air_wifi"] = s.air_wifi;
    doc["air_ble"] = s.air_ble;
    doc["air_background_s"] = s.air_background_s;

    File f = LittleFS.open(kPath, "w");
    if (!f) {
        Serial.println(F("[cfg] open for write failed"));
        return false;
    }
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}
