package com.example.netmon

// What the board's HTTP API returns, one class per endpoint. Field names follow
// the firmware (netmon.ino); anything an older firmware leaves out gets a
// neutral default in Parse.kt rather than failing the whole screen.

/** GET /api/health */
data class Health(
    val version: String,
    val wifi: String,              // "connected", "softap" or "connecting"
    val ssid: String,
    val ip: String,
    val gateway: String,
    val subnet: String,
    val rssi: Int,
    val uptimeS: Long,
    val freeHeap: Long,
    val sweepable: Boolean,
    val devices: Int,
    val scanPasses: Long,
    val scanRemaining: Long,
    val lastPassMs: Long,
    val passSeen: Int,
    val arpCache: Int,
    val latencyValid: Boolean,
    val latencyMs: Long,
    val latencyAgeS: Long,
    val dhcpPackets: Long,
    val events: Int,
    val baselineOpen: Boolean,
    val baselineAnchored: Boolean,
    val baselineClosesInS: Long,
    val namesKnown: Int,
) {
    val inSetupMode: Boolean get() = wifi == "softap"
}

/** One row of GET /api/devices */
data class Device(
    val mac: String,
    val ip: String,
    val hostname: String,
    val vendor: String,
    val status: String,            // "known", "private" or "unknown"
    val randomised: Boolean,
    val self: Boolean,
    val online: Boolean,
    val lastSeenS: Long,
    val upS: Long,
) {
    val isUnknown: Boolean get() = status == "unknown"
    val isPrivate: Boolean get() = status == "private"
}

/** One row of GET /api/events, newest first. at_s counts seconds since the board started. */
data class BoardEvent(
    val atS: Long,
    val type: String,              // seen, back, offline, hostname, scan, scan_done
    val mac: String,
    val ip: String,
    val text: String,
)

/** GET /api/latency */
data class Latency(
    val valid: Boolean,
    val rttMs: Long,
    val checkedS: Long,
    val ageS: Long,
    val failures: Long,
    val method: String,
)

/** GET /api/isp */
data class Isp(
    val valid: Boolean,
    val ip: String,
    val isp: String,
    val org: String,
    val asn: String,
    val city: String,
    val region: String,
    val country: String,
    val timezone: String,
    val error: String,
    val rttMs: Long,
    val checkedAgeS: Long,
    val everChecked: Boolean,
    val gateway: String,
    val gatewayMac: String,
    val gatewayVendor: String,
)

/** The "active" block of GET /api/config: what the interface is using right now. */
data class ActiveLink(
    val ip: String,
    val mask: String,
    val gw: String,
    val dns: String,
    val mac: String,
    val ssid: String,
    val rssi: Int,
    val source: String,            // "dhcp", "static" or "softap"
)

/** GET /api/config. The stored password is never sent, only whether there is one. */
data class BoardConfig(
    val version: String,
    val ssid: String,
    val hasPassword: Boolean,
    val networks: Int,
    val useDhcp: Boolean,
    val ip: String,
    val mask: String,
    val gw: String,
    val dns: String,
    val scanIntervalS: Int,
    val probeIntervalS: Int,
    val offlineAfterS: Int,
    val learningWindowS: Int,
    val active: ActiveLink?,
)

/** One row of GET /api/networks, in the order the board tries them at start-up. */
data class SavedNetwork(
    val order: Int,
    val ssid: String,
    val hasPassword: Boolean,
    val active: Boolean,
    val boot: String,              // joined, not_found, failed, refused, no_answer, not_tried
    val bootMs: Long,
    val reason: Int,
)

/** The "local" block of GET /api/dhcp: this board's own addressing. */
data class DhcpLocal(
    val ip: String,
    val mask: String,
    val gateway: String,
    val dns: String,
    val mode: String,
    val hostname: String,
)

/** GET /api/dhcp: the last DHCP message the board overheard, plus listener health. */
data class DhcpStatus(
    val listener: String,          // listening, paused, failed, off (empty on firmware before 0.9.6)
    val received: Long,
    val capturePackets: Long,
    val valid: Boolean,
    val clientMac: String,
    val assignedIp: String,
    val serverIp: String,
    val leaseS: Long,
    val messageType: String,
    val hostname: String,
    val lastSeenS: Long,
    val local: DhcpLocal?,
)

/** One row of GET /api/scan */
data class WifiNetwork(val ssid: String, val rssi: Int)

/**
 * The body of POST /api/config. The firmware treats a missing use_dhcp as
 * true and a blank password as "keep the stored one", so both are always sent.
 */
data class ConfigUpdate(
    val ssid: String,
    val pass: String,
    val useDhcp: Boolean,
    val ip: String,
    val mask: String,
    val gw: String,
    val dns: String,
    val scanIntervalS: Int,
    val probeIntervalS: Int,
    val offlineAfterS: Int,
    val learningWindowS: Int,
)
