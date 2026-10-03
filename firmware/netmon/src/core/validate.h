#pragma once
// Pure logic — no Arduino headers.
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "cidr.h"

enum class ConfigError : uint8_t {
    None = 0,
    SsidEmpty,
    SsidTooLong,
    PasswordTooLong,
    BadIp,
    BadMask,
    BadGateway,
    GatewayOutsideSubnet,
    IntervalOutOfRange,
};

inline const char* config_error_text(ConfigError e) {
    switch (e) {
        case ConfigError::None: return "ok";
        case ConfigError::SsidEmpty: return "network name is required";
        case ConfigError::SsidTooLong: return "network name is over 32 characters";
        case ConfigError::PasswordTooLong: return "password is over 64 characters";
        case ConfigError::BadIp: return "IP address is not valid";
        case ConfigError::BadMask: return "subnet mask is not valid";
        case ConfigError::BadGateway: return "gateway address is not valid";
        case ConfigError::GatewayOutsideSubnet:
            return "gateway is outside the subnet, the device would be unreachable";
        case ConfigError::IntervalOutOfRange: return "an interval is out of range";
    }
    return "unknown error";
}

inline ConfigError validate_credentials(const char* ssid, const char* pass) {
    if (ssid == nullptr || ssid[0] == '\0') return ConfigError::SsidEmpty;
    if (std::strlen(ssid) > 32) return ConfigError::SsidTooLong;
    if (pass != nullptr && std::strlen(pass) > 64) {
        return ConfigError::PasswordTooLong;
    }
    return ConfigError::None;    // an empty password is a valid open network
}

// Which password to persist for a network, given what the settings form sent
// and what is already stored for that same SSID (nullptr when the SSID is new).
//
// A blank field means "unchanged" so the form can be used to adjust an
// interval without retyping WiFi credentials. It resolves against this SSID's
// own entry, never a neighbouring one, so blank on a network never seen before
// is an open network rather than a borrowed secret.
inline const char* resolve_password(const char* supplied, const char* stored) {
    if (supplied != nullptr && supplied[0] != '\0') return supplied;
    if (stored != nullptr) return stored;
    return "";
}

// Compile-time string equality. netmon.ino uses it to refuse to build with
// the placeholder update password from secrets.example.h.
constexpr bool same_text(const char* a, const char* b) {
    return *a == *b && (*a == '\0' || same_text(a + 1, b + 1));
}

// A netmask is contiguous ones then zeros. 255.0.255.0 is not a netmask.
inline bool mask_is_contiguous(uint32_t mask) {
    const uint32_t inverted = ~mask;
    return (inverted & (inverted + 1)) == 0;
}

inline ConfigError validate_static(const char* ip_s, const char* mask_s,
                                   const char* gw_s) {
    uint32_t ip = 0, mask = 0, gw = 0;
    if (!ipv4_parse(ip_s, ip)) return ConfigError::BadIp;
    if (!ipv4_parse(mask_s, mask) || !mask_is_contiguous(mask) || mask == 0) {
        return ConfigError::BadMask;
    }
    if (!ipv4_parse(gw_s, gw)) return ConfigError::BadGateway;
    if (!subnet_contains(ip & mask, mask, gw)) {
        return ConfigError::GatewayOutsideSubnet;
    }
    return ConfigError::None;
}

inline ConfigError validate_intervals(uint32_t scan, uint32_t probe,
                                      uint32_t offline, uint32_t learning) {
    const bool ok = scan >= 10 && scan <= 3600 && probe >= 10 &&
                    probe <= 3600 && offline >= 30 && offline <= 86400 &&
                    learning <= 86400;
    return ok ? ConfigError::None : ConfigError::IntervalOutOfRange;
}

// Refuse to sweep a subnet that is absent (SoftAP reports a zero mask) or
// so large the walk would never finish. /16 is 65k hosts, already generous
// for a device that sweeps on a timer.
inline bool sweepable(uint32_t subnet, uint32_t mask) {
    (void)subnet;
    if (mask == 0) return false;
    if (!mask_is_contiguous(mask)) return false;
    return mask_to_prefix(mask) >= 16;
}
