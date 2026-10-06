#include "auth_store.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_random.h>
#include <cstring>

namespace {

const char* kPath = "/auth.json";
const char* kTemp = "/auth.tmp";

bool load_from(const char* path, AuthState& a) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    JsonObjectConst pw = doc["login"];
    if (!pw.isNull()) {
        const char* salt = pw["salt"] | "";
        const char* hash = pw["hash"] | "";
        const uint32_t rounds = pw["rounds"] | 0u;
        PasswordHash ph{};
        if (hex_decode(salt, ph.salt, sizeof(ph.salt)) && hex_decode(hash, ph.hash, sizeof(ph.hash)) &&
            rounds > 0 && rounds <= 1000000) {
            ph.set = true;
            ph.rounds = rounds;
            a.login = ph;
        }
    }
    size_t i = 0;
    for (JsonObjectConst s : doc["sessions"].as<JsonArrayConst>()) {
        if (i >= kAuthSessions) break;
        Session x{};
        if (!hex_decode(s["h"] | "", x.hash, sizeof(x.hash))) continue;
        x.used = true;
        x.remember = s["r"] | false;
        x.created_unix = s["c"] | 0u;
        x.earlier_boot = true;
        a.sessions.s[i++] = x;
    }
    return true;
}

}  // namespace

bool auth_load(AuthState& a) {
    std::memset(&a, 0, sizeof(a));
    if (load_from(kPath, a)) return true;
    std::memset(&a, 0, sizeof(a));
    // A power cut between the two steps of a save leaves only the new file.
    if (load_from(kTemp, a)) return true;
    std::memset(&a, 0, sizeof(a));
    return false;
}

bool auth_save(const AuthState& a) {
    JsonDocument doc;
    if (a.login.set) {
        char salt[2 * sizeof(a.login.salt) + 1];
        char hash[2 * sizeof(a.login.hash) + 1];
        hex_encode(a.login.salt, sizeof(a.login.salt), salt);
        hex_encode(a.login.hash, sizeof(a.login.hash), hash);
        JsonObject pw = doc["login"].to<JsonObject>();
        pw["salt"] = salt;
        pw["rounds"] = a.login.rounds;
        pw["hash"] = hash;
    }
    JsonArray list = doc["sessions"].to<JsonArray>();
    for (size_t i = 0; i < kAuthSessions; ++i) {
        const Session& s = a.sessions.s[i];
        if (!s.used) continue;
        char h[2 * sizeof(s.hash) + 1];
        hex_encode(s.hash, sizeof(s.hash), h);
        JsonObject o = list.add<JsonObject>();
        o["h"] = h;
        o["r"] = s.remember;
        o["c"] = s.created_unix;
    }
    File f = LittleFS.open(kTemp, "w");
    if (!f) return false;
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    if (!ok) {
        LittleFS.remove(kTemp);
        return false;
    }
    if (LittleFS.rename(kTemp, kPath)) return true;
    // Some filesystems will not rename over an existing file.
    LittleFS.remove(kPath);
    return LittleFS.rename(kTemp, kPath);
}

void auth_random(uint8_t* out, size_t n) { esp_fill_random(out, n); }

void auth_new_token(char out[kTokenHex + 1]) {
    uint8_t b[kTokenBytes];
    auth_random(b, sizeof(b));
    hex_encode(b, sizeof(b), out);
}
