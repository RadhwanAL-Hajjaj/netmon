#include "baseline_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

// LittleFS is mounted by settings_load(), which runs first at boot.
static const char* kTemp = "/base.tmp";

// Reads straight into the caller's record, which costs no second 1.4 KB copy,
// and empties it again when the file turns out to hold another network (two
// names sharing a hash) or nothing at all.
static bool load_from(const char* path, Baseline& b) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    char want[sizeof(b.ssid)];
    std::memcpy(want, b.ssid, sizeof(want));
    base_reset(b, nullptr);
    char line[kBaseLineMax];
    while (f.available()) {
        const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        base_read_line(b, line);
    }
    f.close();
    base_read_done(b);
    if (std::strcmp(b.ssid, want) == 0) return true;
    base_reset(b, want);
    return false;
}

bool baseline_load(Baseline& b) {
    char path[24];
    base_file_name(b.ssid, path);
    if (load_from(path, b)) return true;
    // A power cut between the two steps of a save leaves only the new file.
    return load_from(kTemp, b);
}

bool baseline_save(const Baseline& b) {
    char path[24];
    base_file_name(b.ssid, path);
    File f = LittleFS.open(kTemp, "w");
    if (!f) return false;
    const bool ok = base_write(b, [&f](const char* line, size_t n) {
        return f.write(reinterpret_cast<const uint8_t*>(line), n) == n;
    });
    f.close();
    if (!ok) {
        LittleFS.remove(kTemp);
        return false;
    }
    if (LittleFS.rename(kTemp, path)) return true;
    // Some filesystems will not rename over an existing file.
    LittleFS.remove(path);
    return LittleFS.rename(kTemp, path);
}

bool baseline_remove(const char* ssid) {
    char path[24];
    base_file_name(ssid, path);
    return LittleFS.exists(path) && LittleFS.remove(path);
}

size_t baseline_prune(const Settings& s) {
    char keep[4][24];
    const uint8_t n = s.net_count < 4 ? s.net_count : 4;
    for (uint8_t i = 0; i < n; ++i) base_file_name(s.nets[i].ssid, keep[i]);

    // Collected first and removed after: removing while a directory is being
    // walked is not something every LittleFS version promises to survive.
    char doomed[8][24];
    size_t count = 0;
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) return 0;
    for (File f = root.openNextFile(); f && count < 8; f = root.openNextFile()) {
        // name() is the bare file name on current cores, the full path on old
        // ones. Either way the part after the last slash is what to compare.
        const char* name = f.name();
        const char* slash = std::strrchr(name, '/');
        const char* bare = slash != nullptr ? slash + 1 : name;
        f.close();
        if (std::strncmp(bare, "base-", 5) != 0 || std::strlen(bare) != 17) continue;
        char path[24];
        path[0] = '/';
        std::strncpy(path + 1, bare, sizeof(path) - 2);
        path[sizeof(path) - 1] = '\0';
        bool wanted = false;
        for (uint8_t i = 0; i < n; ++i) {
            if (std::strcmp(keep[i], path) == 0) wanted = true;
        }
        if (!wanted) std::memcpy(doomed[count++], path, sizeof(path));
    }
    root.close();
    size_t removed = 0;
    for (size_t i = 0; i < count; ++i) {
        if (LittleFS.remove(doomed[i])) ++removed;
    }
    return removed;
}
