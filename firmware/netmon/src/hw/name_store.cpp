#include "name_store.h"

#include <Arduino.h>
#include <LittleFS.h>

// LittleFS is mounted by settings_load(), which runs first at boot.
static const char* kPath = "/names.txt";
static const char* kTemp = "/names.tmp";

static bool load_from(const char* path, Names& names) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    char line[96];
    while (f.available()) {
        const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        Mac m{};
        char name[kNameMax];
        if (name_line_parse(line, m, name)) names.put(m, name);
    }
    f.close();
    return true;
}

bool names_load(Names& names) {
    if (load_from(kPath, names)) return true;
    // A power cut between the two steps of a save leaves only the new file.
    return load_from(kTemp, names);
}

bool names_save(const Names& names) {
    // Written beside the old list and renamed over it, so a power cut
    // mid-write leaves the previous list rather than half of a new one.
    File f = LittleFS.open(kTemp, "w");
    if (!f) return false;
    char line[96];
    bool ok = true;
    for (size_t i = 0; i < names.size() && ok; ++i) {
        const NameEntry& e = names.at(i);
        const size_t n = name_line_format(e.mac, e.name, line, sizeof(line));
        ok = n > 0 && f.write(reinterpret_cast<const uint8_t*>(line), n) == n;
    }
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
