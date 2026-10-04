package com.example.netmon

/**
 * How the app reaches the board: over Wi-Fi, over the Bluetooth link, or
 * whichever answers. Plain Kotlin, decided from what Board knows, so the
 * tests can walk through every case.
 */
enum class LinkMode(val key: String, val title: String, val detail: String) {
    AUTO("auto", "Automatic", "Wi-Fi when the monitor answers there, Bluetooth when it does not."),
    WIFI("wifi", "Wi-Fi only", "Never Bluetooth, even when Wi-Fi cannot reach the monitor."),
    BLUETOOTH("bluetooth", "Bluetooth only", "Always Bluetooth, even with the monitor on this phone's Wi-Fi.");

    companion object {
        fun of(key: String?): LinkMode = values().firstOrNull { it.key == key } ?: AUTO
    }
}

/**
 * A route is the base a [NetmonClient] takes: "http://192.168.2.27" for
 * Wi-Fi, "ble://D4:E9:F4:A3:B8:AE" for Bluetooth. A board has at most one of
 * each; [wifi] and [ble] below are those, either of which may be missing.
 */
object RoutePlan {

    /** While on Bluetooth, how often Wi-Fi is tried again in the background. */
    const val WIFI_RECHECK_MS = 30_000L

    /** The route to start on: the one that worked last time, when it is still one of the board's. */
    fun initial(mode: LinkMode, wifi: String?, ble: String?, last: String?): String? = when (mode) {
        LinkMode.WIFI -> wifi ?: ble
        LinkMode.BLUETOOTH -> ble ?: wifi
        LinkMode.AUTO -> if (last != null && (last == wifi || last == ble)) last else wifi ?: ble
    }

    /** After [failed] did not answer: the other route to try, or null to give up. */
    fun fallback(mode: LinkMode, failed: String, wifi: String?, ble: String?): String? {
        if (mode != LinkMode.AUTO) return null
        val other = if (LinkCodec.isBle(failed)) wifi else ble
        return if (other != null && other != failed) other else null
    }

    /** Whether to look for the board over Wi-Fi again, while on Bluetooth. */
    fun recheckWifi(mode: LinkMode, current: String?, wifi: String?, phoneOnLan: Boolean, sinceLastMs: Long): Boolean =
        mode == LinkMode.AUTO && LinkCodec.isBle(current) && wifi != null && phoneOnLan && sinceLastMs >= WIFI_RECHECK_MS

    /**
     * The Wi-Fi route a health reading over Bluetooth points to, when it
     * should replace [kept]: the board joined a network, or the router gave
     * it a new address. An address chosen by hand with a name or a port, as
     * for a VPN or a port forward, is never replaced.
     */
    fun learnedWifi(h: Health, kept: String?): String? {
        if (h.wifi != "connected" || !IPV4.matches(h.ip) || h.ip == "0.0.0.0") return null
        val found = "http://" + h.ip
        if (kept == null) return found
        if (!IPV4.matches(kept.removePrefix("http://"))) return null
        return if (kept != found) found else null
    }

    /**
     * Whether the first reading from an address the app had not used for
     * this board shows it to be the same board: the same Wi-Fi MAC. [before]
     * is the MAC known before, empty when none was; a board too old to say
     * its MAC counts as another board, as every new address did before 0.12.
     */
    fun sameBoard(before: String, h: Health): Boolean =
        before.isNotEmpty() && h.mac.isNotEmpty() && h.mac.equals(before, ignoreCase = true)

    private val IPV4 = Regex("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$")
}
