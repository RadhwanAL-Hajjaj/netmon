package com.example.netmon

import java.util.Locale

/** Words and numbers for the screens. No Android types, so it runs in plain JVM tests. */
object Format {

    /** Uptime-style span: 45s, 12m, 3h 20m, 2d 4h. Same steps as the web dashboard. */
    fun duration(seconds: Long): String {
        val s = if (seconds < 0) 0 else seconds
        return when {
            s < 60 -> "${s}s"
            s < 3600 -> "${s / 60}m"
            s < 86400 -> "${s / 3600}h ${(s % 3600) / 60}m"
            else -> "${s / 86400}d ${(s % 86400) / 3600}h"
        }
    }

    /** How long ago: just now, 40 s ago, 5 min ago, 3 h ago, 2 days ago. */
    fun ago(seconds: Long): String {
        val s = if (seconds < 0) 0 else seconds
        return when {
            s < 5 -> "just now"
            s < 60 -> "$s s ago"
            s < 3600 -> "${s / 60} min ago"
            s < 86400 -> "${s / 3600} h ago"
            s < 2 * 86400 -> "yesterday"
            else -> "${s / 86400} days ago"
        }
    }

    private val legalWords = Regex(
        "\\b(corporation|corp|company|technologies|technology|electronics|international|" +
            "holdings|limited|ltd|inc|co|llc|gmbh|plc|pte|bv|nv|srl|spa)\\b\\.?",
        RegexOption.IGNORE_CASE,
    )

    /**
     * Registry names carry legal boilerplate that costs width and says
     * nothing: "Espressif Inc." -> "Espressif". Display only.
     */
    fun shortVendor(vendor: String): String {
        if (vendor.isBlank()) return ""
        val x = legalWords.replace(vendor.replace(',', ' '), " ")
            .replace(Regex("\\s+"), " ")
            .trim { it == ' ' || it == '.' }
        return x.ifEmpty { vendor.trim() }
    }

    /** What to call a device: its own name, else its maker, else what kind of address it has. */
    fun deviceName(d: Device): String = when {
        d.hostname.isNotBlank() -> d.hostname
        d.vendor.isNotBlank() -> shortVendor(d.vendor)
        d.randomised -> "Private device"
        else -> "Unnamed device"
    }

    /** The maker line under a name. */
    fun vendorLine(d: Device): String = when {
        d.vendor.isNotBlank() -> shortVendor(d.vendor)
        d.randomised -> "Randomised address"
        else -> "Maker not in the registry"
    }

    fun statusWord(status: String): String = when (status) {
        "known" -> "Known"
        "private" -> "Private"
        "unknown" -> "Unrecognised"
        else -> status.replaceFirstChar { it.uppercase() }
    }

    /** The LAN watch's event types, from firmware 0.14: worth a notification, drawn as alerts. */
    val WATCH = setOf("router_changed", "ip_conflict", "dhcp_server", "rogue_ap", "weak_ap")

    fun statusExplained(d: Device): String = when (d.status) {
        "known" -> "Seen while the monitor was learning this network, or marked as known, so it is treated as part of it."
        "private" -> "Uses a randomised (private) address, which phones change from network to network. " +
            "The monitor does not judge these by the learning window."
        "unknown" -> "First seen after the monitor finished learning this network. If it is yours, trust it; " +
            "if you do not recognise it, check your router."
        else -> ""
    }

    /** Numeric sort key for a dotted IPv4 address; anything else sorts last. */
    fun ipKey(ip: String): Long {
        val p = ip.split('.')
        if (p.size != 4) return Long.MAX_VALUE
        var v = 0L
        for (part in p) {
            val n = part.toIntOrNull() ?: return Long.MAX_VALUE
            if (n < 0 || n > 255) return Long.MAX_VALUE
            v = v * 256 + n
        }
        return v
    }

    /** 0 to 4 bars. The board reports 0 dBm when it has no station link (setup mode). */
    fun signalBars(rssi: Int): Int = when {
        rssi >= 0 -> 0
        rssi >= -55 -> 4
        rssi >= -67 -> 3
        rssi >= -75 -> 2
        else -> 1
    }

    fun signalWord(rssi: Int): String = when (signalBars(rssi)) {
        4 -> "Excellent"
        3 -> "Good"
        2 -> "Fair"
        1 -> "Weak"
        else -> "No signal"
    }

    // The Wi-Fi stack's own words for why a join failed, keyed by its code —
    // the same table as the web settings page, so both say the same thing.
    private val joinReasons = mapOf(
        2 to "authentication timed out",
        14 to "key check failed",
        15 to "handshake timed out",
        23 to "802.1X sign-in failed",
        200 to "lost the access point",
        201 to "not found",
        202 to "authentication failed",
        203 to "association refused",
        204 to "handshake timed out",
        205 to "connection failed",
        210 to "no compatible security",
        211 to "security weaker than WPA2",
        212 to "signal too weak",
    )

    fun joinReason(code: Int): String =
        if (code == 0) "" else "${joinReasons[code] ?: "reason"} (code $code)"

    enum class Tone { Good, Warn, Bad, Quiet }

    data class JoinText(val label: String, val detail: String, val tone: Tone)

    /** How a remembered network fared at the last start-up. */
    fun joinText(n: SavedNetwork): JoinText {
        val s = String.format(Locale.US, "%.1f s", n.bootMs / 1000.0)
        val r = joinReason(n.reason)
        return when (n.boot) {
            "joined" -> JoinText("Joined", "in $s", Tone.Good)
            "not_found" -> JoinText("Not in range", "waited $s", Tone.Bad)
            "refused" -> JoinText("Refused", "check the password" + if (r.isNotEmpty()) "; $r" else "", Tone.Bad)
            "failed" -> JoinText("Could not join", "in range; " + r.ifEmpty { "connection failed" }, Tone.Warn)
            "no_answer" -> JoinText("Did not join", "waited $s", Tone.Warn)
            else -> JoinText("Not tried", "an earlier one joined", Tone.Quiet)
        }
    }

    /**
     * Start-up time lost to networks tried before the one that answered, as
     * the web page words it. Empty when nothing was lost.
     */
    fun historyNote(list: List<SavedNetwork>): String {
        if (list.isEmpty()) return "No networks remembered yet."
        val active = list.indexOfFirst { it.active }
        var lost = 0L
        val slow = ArrayList<String>()
        list.forEachIndexed { i, n ->
            if ((active < 0 || i < active) && n.boot != "joined" && n.boot != "not_tried") {
                lost += n.bootMs
                slow.add(n.ssid)
            }
        }
        return when {
            active < 0 && slow.isNotEmpty() -> "None of these answered at start-up, so the board opened netmon-setup."
            lost > 0 && active >= 0 -> slow.joinToString(", ") + (if (slow.size > 1) " were" else " was") +
                " tried first and cost " + String.format(Locale.US, "%.1f", lost / 1000.0) +
                " s at start-up. Saving settings moves " + list[active].ssid + " to the top."
            else -> ""
        }
    }

    fun listenerText(s: String): String = when (s) {
        "listening" -> "Listening on port 67"
        "paused" -> "Paused for a Wi-Fi scan or an update"
        "failed" -> "Not listening: port 67 could not be opened"
        "off" -> "Not listening in setup mode"
        "" -> "Not reported by this firmware"
        else -> s
    }

    /** City and region are often the same word; say it once. */
    fun place(city: String, region: String, country: String): String {
        val out = ArrayList<String>()
        for (v in listOf(city, region, country)) if (v.isNotBlank() && v !in out) out.add(v)
        return out.joinToString(", ")
    }

    fun fileSize(bytes: Long): String =
        if (bytes >= 1_048_576) String.format(Locale.US, "%.2f MB", bytes / 1_048_576.0)
        else "${(bytes + 512) / 1024} KB"

    fun heap(bytes: Long): String = "${bytes / 1024} KB"

    fun sweepLine(h: Health): String =
        if (h.lastPassMs > 0) String.format(Locale.US, "Swept in %.1f s and reached %d", h.lastPassMs / 1000.0, h.passSeen)
        else "The first sweep has not finished yet"

    /** A title for each event type, including the one this app adds itself. */
    fun eventTitle(type: String): String = when (type) {
        "seen" -> "New device"
        "back" -> "Came back"
        "offline" -> "Went offline"
        "hostname" -> "Name learned"
        "scan" -> "Sweep started"
        "scan_done" -> "Sweep finished"
        "trusted" -> "Marked as known"
        "forgotten" -> "Forgotten"
        "router_changed" -> "Router changed"
        "ip_conflict" -> "Address clash"
        "dhcp_server" -> "Unexpected DHCP server"
        "rogue_ap" -> "Unknown access point"
        "weak_ap" -> "Weaker Wi-Fi security"
        EventHistory.TYPE_RESTART -> "Monitor restarted"
        else -> type.replaceFirstChar { it.uppercase() }
    }
}
