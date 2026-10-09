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
    /** The board's Wi-Fi MAC, from firmware 0.12: tells the board apart however it is reached. */
    val mac: String = "",
    /** Whether the board's Bluetooth link is on (firmware 0.12). */
    val bleLink: Boolean = false,
    /** The board knows no time yet (firmware 0.12); false for older boards, which never ask. */
    val clockUnset: Boolean = false,
    /** The address the board uses on Wi-Fi now (0.13): [mac] unless the owner set another. */
    val wifiMac: String = "",
    // Firmware 0.14: whether it keeps recognised devices on the board (and so
    // takes Trust and Forget), whether this network's list has been learned,
    // how many devices are on it, and what the LAN watch is reporting.
    val lanWatch: Boolean = false,
    val baselineSaved: Boolean = false,
    val knownSaved: Int = 0,
    val alerts: Int = 0,
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
    val type: String,              // seen, back, offline, hostname, scan, scan_done; from 0.14
                                   // also trusted, forgotten and the LAN watch's: see Format.WATCH
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
    /** The largest firmware file the board's updater takes (0.10 and later); 0 when not reported. */
    val updateMax: Long = 0,
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

// --- Nearby (firmware 0.10 and later) -------------------------------------------

/** An access point the board's Nearby scans have heard. live: heard in one of the last two scans. */
data class NearbyAp(
    val bssid: String,
    val ssid: String,              // empty for a hidden network
    val ch: Int,
    val rssi: Int,
    val security: String,          // "WPA2", "Open", ...
    val live: Boolean,
    val joined: Boolean,           // the access point the board itself is joined to
    val ageS: Long,
    val knownS: Long,
)

/** A Bluetooth LE device the board has heard advertising. */
data class NearbyBle(
    val addr: String,
    val name: String,
    val vendor: String,
    val company: Int,              // Bluetooth SIG company id, -1 when none was advertised
    val kind: String,              // how it addresses itself: public, static or private
    val type: String,              // phone, tracker, audio, ... or unknown
    val sure: Int,                 // 1 to 4: how strong the evidence for the type is
    val model: String,             // a product the advertisement names, such as "AirPods Pro"
    val rssi: Int,
    val ageS: Long,
    val knownS: Long,
    val seen: Long,
)

/** How one radio's Nearby scanning stands. count: completed Wi-Fi scans, or Bluetooth bursts. */
data class AirScan(
    val enabled: Boolean,
    val state: String,             // off, idle, queued, scanning/listening, finding, paused, unavailable
    val count: Long,
    val failures: Long,
    val ageS: Long,                // -1 when it has not run yet
    val tookMs: Long,
    val dropped: Long,
)

/** The device the board's Finder is listening for, as /api/nearby reports it. */
data class Finding(val type: String, val addr: String, val name: String)

/** GET /api/nearby */
data class Nearby(
    val version: String,
    val onLan: Boolean,
    val sweeping: Boolean,
    val backgroundS: Int,
    val finding: Finding?,
    val wifiScan: AirScan,
    val bleScan: AirScan,
    val wifi: List<NearbyAp>,
    val ble: List<NearbyBle>,
)

/** GET and POST /api/nearby/config: the board's Nearby switches. */
data class NearbyConfig(
    val wifi: Boolean,
    val ble: Boolean,
    val bleReady: Boolean,
    val backgroundS: Int,
)

/** GET /api/ble (firmware 0.12): the board's Bluetooth link and its pairing window. */
data class BleStatus(
    val link: Int,                 // the link's version
    val available: Boolean,        // the stack is running
    val enabled: Boolean,          // the setting
    val on: Boolean,               // advertising and taking connections
    val name: String,
    val addr: String,              // the board's Bluetooth address
    val bonds: Int,                // paired phones
    val maxBonds: Int,
    val connected: Int,
    val secure: Int,               // of those connected, paired and encrypted
    val pairing: Boolean,          // a pairing window is open
    val code: String,              // its six digits, while open
    val leftS: Int,
    val result: String,            // "paired", "failed" or "" for the last attempt in a window
    val resultAgeS: Long,
    val via: String,               // "wifi" or "bluetooth": how this reading was asked for
    // From firmware 0.13: why the last attempt in a window went as it did, the
    // failed tries the open window still takes, the last attempt of all, and
    // whether the owner set a pairing code of their own.
    val why: String = "",          // "wrong_code", "cancelled", "dropped", ...
    val whyText: String = "",
    val triesLeft: Int = 3,
    val last: PairAttempt? = null,
    val ownCode: Boolean = false,
    val codeKnown: Boolean = false,  // the board says which kind of code it uses (0.13)
)

/** How a pairing attempt ended, as the board saw it (firmware 0.13). */
data class PairAttempt(
    val why: String,
    val text: String,
    val status: Int,               // the Bluetooth stack's own number for it
    val inWindow: Boolean,
    val ageS: Long,
)

/**
 * What a board says about itself to anyone (GET /api/auth, firmware 0.13), or
 * from /api/health before 0.13. [id] is the chip's own Wi-Fi MAC, which tells
 * the board apart however it is reached and whatever address it uses.
 */
data class BoardId(
    val version: String,
    val id: String,
    val name: String,
    val login: Boolean,            // the board asks for a password (0.13 and later)
    val signedIn: Boolean,         // ... and the asker has a session
)

/** GET /api/auth for a signed-in asker. */
data class AuthInfo(
    val board: BoardId,
    val ownPassword: Boolean,      // the owner set a login password; the update password works too
    val remembered: Boolean,       // this session lasts 30 days
    val sessions: Int,
    val rememberDays: Int,
)

/** POST /api/login: the session to send with every request from now on. */
data class LoginReply(val token: String, val days: Int)

/** GET /api/reports (firmware 0.12): the board's saved report of each network it has been on. */
data class ReportList(
    val max: Int,
    val everyS: Int,
    val clock: Boolean,
    val nowUnix: Long,
    val freeBytes: Long,
    val networkSsid: String,       // the network the board is on now; "" when none
    val networkSubnet: String,
    val reports: List<ReportInfo>,
)

/** One entry of GET /api/reports. */
data class ReportInfo(
    val slot: Int,
    val ssid: String,
    val subnet: String,
    val gateway: String,
    val count: Int,
    val online: Int,
    val savedUnix: Long,           // 0: the board had no clock then
    val ageS: Long,                // -1: cannot be told
    val bytes: Long,
    val current: Boolean,          // the network the board is on now
)

/** GET /api/reports/get?slot=N: one network's device list as it last stood. */
data class Report(
    val ssid: String,
    val subnet: String,
    val gateway: String,
    val gatewayMac: String,
    val boardIp: String,
    val boardMac: String,
    val version: String,
    val seq: Long,
    val savedUnix: Long,
    val savedUpS: Long,
    val clock: String,             // "internet", "client" or "none": where the board's time came from
    val passes: Long,
    val learning: Boolean,
    val devices: List<ReportDevice>,
    /** The report as the board sent it, for exporting unchanged. */
    val raw: String,
)

/** A device in a saved report: the row GET /api/devices has, last_seen_s counted back from the save. */
data class ReportDevice(
    val device: Device,
    val seenUnix: Long,            // when last seen, 0 when not known
    val firstUnix: Long,           // when first seen on that network, as far as the board knows
    val carried: Boolean,          // kept from an earlier report: not seen since the board last started
) {
    /** When last seen: the board's date, else worked out from the save, else 0. */
    fun seenAt(r: Report): Long = when {
        seenUnix > 0 -> seenUnix
        r.savedUnix > 0 -> r.savedUnix - device.lastSeenS
        else -> 0
    }
}

/** One raw reading of the device being found: its number, how long before the reply it was heard, dBm. */
data class FindReading(val seq: Long, val msAgo: Long, val rssi: Int)

/** GET and POST /api/nearby/find (firmware 0.11 and later). */
data class FindStatus(
    val active: Boolean,
    val type: String,              // "ble" or "wifi"
    val addr: String,
    val name: String,
    val kind: String,              // Bluetooth: public, static or private
    val dtype: String,             // Bluetooth: what sort of device
    val model: String,
    val vendor: String,
    val ch: Int,                   // Wi-Fi: the channel it is looked for on
    val security: String,
    val state: String,             // listening, paused, off, unavailable
    val why: String,               // when paused: sweep, probe, scan, update, start
    val forS: Long,
    val heardMs: Long,             // -1 when nothing has been heard yet
    val holdMs: Long,              // what is left of a hold on the sweep, for a turn
    val seq: Long,
    val readings: List<FindReading>,
)

// --- Map (firmware 0.11 and later) ----------------------------------------------

/** One access point carrying the board's own network name, as GET /api/map lists them. */
data class MapAp(
    val bssid: String,
    val ch: Int,
    val rssi: Int,
    val live: Boolean,
    val joined: Boolean,
    val ageS: Long,
)

/** The provider lookup the Internet page last made, as GET /api/map carries it. */
data class MapIsp(
    val checked: Boolean,
    val valid: Boolean,
    val isp: String,
    val org: String,
    val ageS: Long,
)

/** GET /api/map: the frame of the network map. The devices come from /api/devices. */
data class MapInfo(
    val version: String,
    val wifi: String,              // connected, softap or connecting
    val ssid: String,
    val ip: String,
    val mac: String,
    val hostname: String,
    val gateway: String,
    val subnet: String,
    val rssi: Int,
    val channel: Int,
    val bssid: String,
    val uptimeS: Long,
    val latencyValid: Boolean,
    val latencyMs: Long,
    val isp: MapIsp,
    val nearbyWifi: Boolean,
    val aps: List<MapAp>,
    /** False when this was put together from /api/health, for firmware before 0.11. */
    val fromBoard: Boolean = true,
)

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
