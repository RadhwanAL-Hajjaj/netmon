#pragma once
// Pure logic — no Arduino headers.
//
// What kind of thing a Bluetooth device is, worked out from its advertisement:
// a phone, earbuds, a tracker, a lock. The approach and most of the rules come
// from BlueWatch (github.com/PolarPatch/BlueWatch, MIT licence), reimplemented
// here in a form small enough for this board, with every identifier checked
// against the Bluetooth SIG register (company IDs, 16-bit service UUIDs) or the
// sources BlueWatch cites for the Apple and Microsoft formats.
//
// Several signals can say different things, so each guess carries a strength
// and the strongest wins:
//   4  the maker's own data says exactly what it is: an Apple Find My or
//      AirPods message, Microsoft's Swift Pair device type, a lock maker
//   3  a service it advertises: heart rate, Tile, LE Audio
//   2  its declared appearance, or its name: "Galaxy Buds", "iPhone"
//   1  only who made it, for makers of one kind of thing: Garmin, Sonos
// A device keeps the strongest guess any of its packets has given.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ble_adv.h"

enum class BleType : uint8_t {
    Unknown = 0,
    Phone,
    Tablet,
    Computer,
    Watch,
    Fitness,      // heart-rate straps, bike sensors, scales, health devices
    Audio,        // earbuds, headphones, hearing aids
    Speaker,
    Tv,
    Tracker,      // AirTag, Tile, SmartTag
    Beacon,
    SmartHome,
    Sensor,
    Input,        // keyboards, mice, game controllers
    Glasses,
    Vehicle,
    Lock,
    Printer,
    Flipper,
};

inline const char* ble_type_text(BleType t) {
    switch (t) {
        case BleType::Phone:     return "phone";
        case BleType::Tablet:    return "tablet";
        case BleType::Computer:  return "computer";
        case BleType::Watch:     return "watch";
        case BleType::Fitness:   return "fitness";
        case BleType::Audio:     return "audio";
        case BleType::Speaker:   return "speaker";
        case BleType::Tv:        return "tv";
        case BleType::Tracker:   return "tracker";
        case BleType::Beacon:    return "beacon";
        case BleType::SmartHome: return "smarthome";
        case BleType::Sensor:    return "sensor";
        case BleType::Input:     return "input";
        case BleType::Glasses:   return "glasses";
        case BleType::Vehicle:   return "vehicle";
        case BleType::Lock:      return "lock";
        case BleType::Printer:   return "printer";
        case BleType::Flipper:   return "flipper";
        case BleType::Unknown:   break;
    }
    return "unknown";
}

struct BleGuess {
    BleType type;
    uint8_t strength;     // 0 for no guess at all; see the top of this file
    const char* model;    // a product, when the data names one; else nullptr
};

inline BleGuess ble_guess(BleType t, uint8_t strength, const char* model = nullptr) {
    BleGuess g;
    g.type = t;
    g.strength = strength;
    g.model = model;
    return g;
}

// ---- Apple ----------------------------------------------------------------
//
// Apple's manufacturer data is a chain of Continuity messages, each a type
// byte, a length byte and the body. Walked to the end, because a device often
// sends two at once (Handoff, then Nearby Info) and the useful one need not be
// first. A zero length ends the chain, as it does in Apple's own parsers.

static const uint8_t kAppleIBeacon = 0x02;
static const uint8_t kAppleAirPrint = 0x03;
static const uint8_t kAppleHomeKit = 0x06;
static const uint8_t kAppleProximityPairing = 0x07;
static const uint8_t kAppleFindMy = 0x12;

// The iBeacon proximity UUID Tesla's phone key and key fob broadcast.
static const uint8_t kTeslaBeaconUuid[16] = {0x74, 0x27, 0x8b, 0xda, 0xb6, 0x44, 0x45, 0x20,
                                             0x8f, 0x0c, 0x72, 0x0e, 0xaf, 0x05, 0x99, 0x35};

// Proximity Pairing model codes, big-endian after a 0x01 prefix. Only the six
// two independent decoders agree on; any other code is still AirPods or Beats.
inline const char* apple_audio_model(uint16_t code) {
    switch (code) {
        case 0x0220: return "AirPods (1st generation)";
        case 0x0F20: return "AirPods (2nd generation)";
        case 0x0E20: return "AirPods Pro";
        case 0x0320: return "Powerbeats3";
        case 0x0520: return "BeatsX";
        case 0x0620: return "Beats Solo3";
        default:     return "AirPods or Beats";
    }
}

inline BleGuess ble_guess_apple(const uint8_t* p, size_t n) {
    size_t i = 0;
    while (p != nullptr && i + 2 <= n) {
        const uint8_t type = p[i];
        const uint8_t len = p[i + 1];
        if (len == 0) break;
        const uint8_t* body = p + i + 2;
        const size_t have = n - (i + 2) < len ? n - (i + 2) : len;
        switch (type) {
            case kAppleFindMy:
                // What an AirTag, or any Find My accessory, sends while away
                // from its owner.
                return ble_guess(BleType::Tracker, 4, "Find My tracker");
            case kAppleProximityPairing:
                if (have >= 1 && body[0] == 0x05) {
                    return ble_guess(BleType::Tracker, 4, "AirTag (not set up)");
                }
                if (have >= 3 && body[0] == 0x01) {
                    return ble_guess(BleType::Audio, 4,
                                     apple_audio_model(static_cast<uint16_t>((body[1] << 8) | body[2])));
                }
                break;
            case kAppleAirPrint:
                return ble_guess(BleType::Printer, 4, "AirPrint printer");
            case kAppleHomeKit:
                return ble_guess(BleType::SmartHome, 4, "HomeKit accessory");
            case kAppleIBeacon:
                // 16 bytes of proximity UUID, then major, minor and power.
                if (len >= 21) {
                    if (have >= 16 && std::memcmp(body, kTeslaBeaconUuid, 16) == 0) {
                        return ble_guess(BleType::Vehicle, 4, "Tesla key");
                    }
                    return ble_guess(BleType::Beacon, 4, "iBeacon");
                }
                break;
            default:
                break;
        }
        if (i + 2 + len > n) break;
        i += 2 + len;
    }
    return ble_guess(BleType::Unknown, 0);
}

// ---- Microsoft --------------------------------------------------------------
//
// Swift Pair and the Connected Devices Platform beacon say outright what sent
// them, for Windows' own pairing pop-up: scenario 1, then a device type in the
// low five bits of the next byte.
inline BleGuess ble_guess_microsoft(const uint8_t* p, size_t n) {
    if (p == nullptr || n < 2 || p[0] != 0x01) return ble_guess(BleType::Unknown, 0);
    switch (p[1] & 0x1F) {
        case 1:  return ble_guess(BleType::Input, 4, "Xbox");
        case 6:  return ble_guess(BleType::Phone, 4, "iPhone");
        case 7:  return ble_guess(BleType::Tablet, 4, "iPad");
        case 8:  return ble_guess(BleType::Phone, 4, "Android phone");
        case 9:  return ble_guess(BleType::Computer, 4, "Windows PC");
        case 11: return ble_guess(BleType::Phone, 4, "Windows phone");
        case 12: return ble_guess(BleType::Computer, 4, "Linux PC");
        case 13: return ble_guess(BleType::SmartHome, 4, "Windows IoT device");
        case 14: return ble_guess(BleType::Tv, 4, "Surface Hub");
        case 15: return ble_guess(BleType::Computer, 4, "Windows laptop");
        case 16: return ble_guess(BleType::Tablet, 4, "Windows tablet");
        default: return ble_guess(BleType::Unknown, 0);
    }
}

// ---- Makers of one kind of thing ------------------------------------------

// Company IDs specific enough to name the kind of device on their own.
inline BleGuess ble_guess_company(uint16_t id) {
    switch (id) {
        case 0x0E29: return ble_guess(BleType::Flipper, 4, "Flipper Zero");
        // Smart glasses: Meta (both of its IDs), Even Realities, Vuzix,
        // Luxottica (the eyewear half of Ray-Ban Meta), Snap (Spectacles).
        case 0x01AB: case 0x058E: case 0x10F9: case 0x060C: case 0x0D53: case 0x03C2:
            return ble_guess(BleType::Glasses, 4);
        // Locks: ASSA ABLOY, HID Global, Yale, SALTO, August, Allegion, Tedee,
        // igloohome, Master Lock, Unikey (Kwikset Kevo), dormakaba, Paxton.
        case 0x012E: case 0x0124: case 0x0BDE: case 0x0199: case 0x01D1: case 0x013B:
        case 0x0725: case 0x05BA: case 0x014B: case 0x015E: case 0x0C64: case 0x0196:
            return ble_guess(BleType::Lock, 4);
        // Car makers' phone-as-key and infotainment: Ford, Honda, Hyundai,
        // Toyota, Nissan, Subaru, BMW, Volkswagen, Porsche, Jaguar Land Rover,
        // BYD, Tesla; and Samsara fleet trackers.
        case 0x0723: case 0x0915: case 0x0826: case 0x0977: case 0x0BA6: case 0x0A10:
        case 0x05EB: case 0x011F: case 0x0120: case 0x020B: case 0x0C34: case 0x022B:
        case 0x0B6B:
            return ble_guess(BleType::Vehicle, 4);
        // Chamberlain (garage doors), Hatch Baby.
        case 0x0878: case 0x0434:
            return ble_guess(BleType::SmartHome, 4);
        default:
            return ble_guess(BleType::Unknown, 0);
    }
}

// Makers whose devices are nearly all one kind. Weak: a guess from the maker
// alone, used only when nothing better turned up.
inline BleType ble_vendor_type(uint16_t id) {
    switch (id) {
        case 0x0087: case 0x006B: case 0x009F: case 0x0157:   // Garmin, Polar, Suunto, Huami
            return BleType::Watch;
        case 0x01FC: case 0x03FF: case 0x02B2:                // Wahoo, Withings, Oura
            return BleType::Fitness;
        case 0x009E: case 0x00CC: case 0x012D: case 0x0494:   // Bose, Beats, Sony, Sennheiser
        case 0x0067: case 0x0055: case 0x07C9: case 0x0103:   // GN Hearing, Plantronics, Skullcandy, B&O
        case 0x0057: case 0x065A:                             // Harman, Marshall
            return BleType::Audio;
        case 0x05A7:                                          // Sonos
            return BleType::Speaker;
        case 0x067C: case 0x08C3:                             // Tile, Chipolo
            return BleType::Tracker;
        case 0x0499:                                          // Ruuvi
            return BleType::Sensor;
        case 0x060F: case 0x01DD: case 0x07D0: case 0x0A12:   // Signify, Philips, Tuya, Dyson
            return BleType::SmartHome;
        case 0x01DA: case 0x068E: case 0x0553:                // Logitech, Razer, Nintendo
            return BleType::Input;
        default:
            return BleType::Unknown;
    }
}

// ---- Services, appearance ---------------------------------------------------

inline BleType ble_service_type(uint16_t u) {
    switch (u) {
        case 0x180D: case 0x1814: case 0x1816: case 0x1818: case 0x1826:  // heart rate, running,
        case 0x1810: case 0x1808: case 0x1809: case 0x181D: case 0x181B:  // cycling, fitness machine,
        case 0x1819: case 0x183E:                                         // health, location, activity
            return BleType::Fitness;
        case 0x1812:                                          // HID
            return BleType::Input;
        case 0x181A:                                          // Environmental Sensing
            return BleType::Sensor;
        case 0x184E: case 0x184F: case 0x1850: case 0x1853:   // LE Audio
        case 0x1854: case 0x1855:                             // Hearing Access, Telephony and Media Audio
        case 0xFEBE:                                          // Bose
            return BleType::Audio;
        case 0xFD6F:                                          // Exposure Notification: phones only
            return BleType::Phone;
        case 0xFEAA: case 0xFE9A:                             // Eddystone, Estimote
            return BleType::Beacon;
        case 0xFEED: case 0xFEEC: case 0xFE33: case 0xFD5A:   // Tile, Tile, Chipolo, Samsung SmartTag
            return BleType::Tracker;
        case 0xFD5F:                                          // Meta glasses
            return BleType::Glasses;
        case 0xFEE0:                                          // Huami: Mi Band, Amazfit
            return BleType::Watch;
        case 0xFE95: case 0xFE0F:                             // Xiaomi MiBeacon, Signify (Hue)
            return BleType::SmartHome;
        case 0x3081: case 0x3082: case 0x3083:                // Flipper Zero
            return BleType::Flipper;
        case 0xFBB0:                                          // tyre pressure sensors
            return BleType::Vehicle;
        default:
            return BleType::Unknown;
    }
}

// The Bluetooth SIG's appearance categories (the top ten bits), where the
// category alone settles the kind of device.
inline BleType ble_appearance_type(uint16_t appearance) {
    switch (appearance >> 6) {
        case 1:  return BleType::Phone;
        case 2:  return BleType::Computer;
        case 3:  return BleType::Watch;
        case 5:  return BleType::Tv;          // display
        case 7:  return BleType::Glasses;
        case 8:  case 9:  return BleType::Tracker;   // tag, keyring
        case 12: case 13: case 14: case 16: case 17: case 18:
            return BleType::Fitness;          // thermometer, heart rate, blood pressure,
                                              // glucose, running, cycling
        case 15: case 42: return BleType::Input;     // HID, gaming
        case 21: return BleType::Sensor;
        case 22: case 23: case 24: case 25: case 26: case 27: case 28:
        case 30: case 31: case 32: case 36:
            return BleType::SmartHome;        // lights, fans, heating, access, power,
                                              // blinds, appliances
        case 33: case 34: case 37: case 41: return BleType::Audio;  // audio sink and source,
                                                                    // wearable audio, hearing aid
        case 35: return BleType::Vehicle;
        case 39: case 40: return BleType::Tv;  // AV and display equipment
        default: return BleType::Unknown;
    }
}

// ---- Names ------------------------------------------------------------------

inline char ble_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ble_is_alnum(char c) {
    c = ble_lower(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Whether `name` contains `word` (lower case), case-insensitively. With
// `whole`, only where it stands as a word of its own, so "tv" is found in
// "[TV] Samsung" but not in "TVS Motor", and "lock" not in "Clock".
inline bool ble_name_has(const char* name, size_t n, const char* word, bool whole = false) {
    const size_t w = std::strlen(word);
    if (name == nullptr || w == 0 || n < w) return false;
    for (size_t i = 0; i + w <= n; ++i) {
        size_t k = 0;
        while (k < w && ble_lower(name[i + k]) == word[k]) ++k;
        if (k != w) continue;
        if (!whole) return true;
        const bool left = i == 0 || !ble_is_alnum(name[i - 1]);
        const bool right = i + w == n || !ble_is_alnum(name[i + w]);
        if (left && right) return true;
    }
    return false;
}

inline bool ble_name_any(const char* name, size_t n, const char* const* words, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (ble_name_has(name, n, words[i])) return true;
    }
    return false;
}

inline bool ble_is_hex_lower(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// A Tesla phone key or fob names itself "S", sixteen lower-case hex digits,
// "C": S60845be37d53334eC.
inline bool ble_tesla_key_name(const char* name, size_t n) {
    if (name == nullptr || n != 18 || name[0] != 'S' || name[17] != 'C') return false;
    for (size_t i = 1; i < 17; ++i) {
        if (!ble_is_hex_lower(name[i])) return false;
    }
    return true;
}

// Order matters: earbuds before phones, so "Pixel Buds" and "Galaxy Buds" are
// earbuds and not the phone they share a name with.
inline BleType ble_name_type(const char* name, size_t n) {
    static const char* const audio[] = {"airpods", "buds", "beats", "headphone", "headset",
                                        "earbud", "earphone", "wh-1000", "wf-1000", "jabra",
                                        "soundcore", "quietcomfort", "jbl tune", "jbl live"};
    static const char* const speaker[] = {"speaker", "homepod", "soundbar", "sonos", "soundlink",
                                          "jbl flip", "jbl charge", "jbl go", "jbl clip"};
    static const char* const tracker[] = {"airtag", "smarttag", "chipolo"};
    static const char* const watch[] = {"watch", "mi band", "mi smart band", "amazfit", "fitbit",
                                        "forerunner", "fenix", "galaxy fit"};
    static const char* const tv[] = {"bravia", "chromecast", "fire tv", "firestick", "roku"};
    static const char* const input[] = {"keyboard", "mouse", "mx master", "mx keys", "controller",
                                        "gamepad", "joy-con", "dualsense", "dualshock"};
    static const char* const computer[] = {"macbook", "imac", "mac mini", "mac pro", "thinkpad",
                                           "laptop", "desktop-", "chromebook"};
    static const char* const tablet[] = {"ipad", "galaxy tab", "tablet"};
    static const char* const phone[] = {"iphone", "galaxy s", "galaxy a", "galaxy z", "galaxy note",
                                        "pixel", "redmi", "oneplus", "xperia", "android"};
    static const char* const printer[] = {"printer", "deskjet", "officejet", "laserjet"};
    static const char* const vehicle[] = {"tesla", "model 3", "model y", "model s", "model x",
                                          "tpms"};
    if (name == nullptr || n == 0) return BleType::Unknown;
    if (ble_name_has(name, n, "flipper")) return BleType::Flipper;
    if (ble_name_any(name, n, audio, sizeof(audio) / sizeof(audio[0]))) return BleType::Audio;
    if (ble_name_any(name, n, speaker, sizeof(speaker) / sizeof(speaker[0]))) return BleType::Speaker;
    if (ble_name_any(name, n, tracker, sizeof(tracker) / sizeof(tracker[0])) ||
        ble_name_has(name, n, "tile", true)) {
        return BleType::Tracker;
    }
    if (ble_name_any(name, n, watch, sizeof(watch) / sizeof(watch[0]))) return BleType::Watch;
    if (ble_name_any(name, n, tv, sizeof(tv) / sizeof(tv[0])) || ble_name_has(name, n, "tv", true)) {
        return BleType::Tv;
    }
    if (ble_name_any(name, n, input, sizeof(input) / sizeof(input[0]))) return BleType::Input;
    if (ble_name_any(name, n, computer, sizeof(computer) / sizeof(computer[0]))) return BleType::Computer;
    if (ble_name_any(name, n, tablet, sizeof(tablet) / sizeof(tablet[0]))) return BleType::Tablet;
    if (ble_name_any(name, n, phone, sizeof(phone) / sizeof(phone[0]))) return BleType::Phone;
    if (ble_name_any(name, n, printer, sizeof(printer) / sizeof(printer[0]))) return BleType::Printer;
    if (ble_name_any(name, n, vehicle, sizeof(vehicle) / sizeof(vehicle[0]))) return BleType::Vehicle;
    if (ble_name_has(name, n, "nuki") || ble_name_has(name, n, "smart lock")) return BleType::Lock;
    if (ble_name_has(name, n, "beacon")) return BleType::Beacon;
    return BleType::Unknown;
}

// ---- Everything together ----------------------------------------------------

inline BleGuess ble_classify(const BleAdvInfo& a) {
    if (a.has_company) {
        BleGuess g = ble_guess(BleType::Unknown, 0);
        if (a.company == 0x004C) g = ble_guess_apple(a.mfr, a.mfr_len);
        else if (a.company == 0x0006) g = ble_guess_microsoft(a.mfr, a.mfr_len);
        if (g.strength == 0) g = ble_guess_company(a.company);
        if (g.strength > 0) return g;
    }
    const char* name = reinterpret_cast<const char*>(a.name);
    if (ble_tesla_key_name(name, a.name_len)) return ble_guess(BleType::Vehicle, 4, "Tesla key");
    for (uint8_t i = 0; i < a.uuid_count; ++i) {
        const BleType t = ble_service_type(a.uuid16[i]);
        if (t != BleType::Unknown) return ble_guess(t, 3);
    }
    if (a.has_appearance) {
        const BleType t = ble_appearance_type(a.appearance);
        if (t != BleType::Unknown) return ble_guess(t, 2);
    }
    const BleType byname = ble_name_type(name, a.name_len);
    if (byname != BleType::Unknown) return ble_guess(byname, 2);
    if (a.has_company) {
        const BleType t = ble_vendor_type(a.company);
        if (t != BleType::Unknown) return ble_guess(t, 1);
    }
    return ble_guess(BleType::Unknown, 0);
}
