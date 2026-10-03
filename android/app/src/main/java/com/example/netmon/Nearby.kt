package com.example.netmon

import java.util.Locale
import kotlin.math.PI
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

/**
 * Signal strength one metre from the board, and how fast it falls off: 2 in
 * open air, 3 or more through walls. The defaults are the web page's.
 */
data class Calibration(
    val wifiAt1m: Double = -45.0,
    val wifiFalloff: Double = 2.7,
    val bleAt1m: Double = -59.0,
    val bleFalloff: Double = 2.2,
) {
    companion object {
        fun atOneMetreOk(v: Double) = v in -100.0..-10.0
        fun falloffOk(v: Double) = v in 1.5..6.0
    }
}

/**
 * The Nearby page's wording and arithmetic, kept in step with the board's own
 * page (pages.h, NEARBY_HTML) so the app and the browser say the same thing
 * about the same device. No Android types: it runs in plain JVM tests.
 */
object Air {

    /** What the board's classifier calls a device, in words. */
    val KIND: Map<String, String> = mapOf(
        "phone" to "Phone", "tablet" to "Tablet", "computer" to "Computer", "watch" to "Watch",
        "fitness" to "Fitness", "audio" to "Headphones", "speaker" to "Speaker", "tv" to "TV",
        "tracker" to "Tracker", "beacon" to "Beacon", "smarthome" to "Smart home", "sensor" to "Sensor",
        "input" to "Input device", "glasses" to "Smart glasses", "vehicle" to "Vehicle", "lock" to "Lock",
        "printer" to "Printer", "flipper" to "Flipper Zero",
    )

    private val GROUP = mapOf(
        "phone" to "p", "tablet" to "p", "computer" to "p", "watch" to "p", "fitness" to "p",
        "audio" to "p", "glasses" to "p", "tracker" to "t", "flipper" to "t",
    )

    /** The four colour groups, in legend order. */
    val GROUPS = listOf("p", "t", "h", "u")
    val GROUP_NAME = mapOf("p" to "personal", "t" to "trackers", "h" to "home and things", "u" to "not identified")

    /** p personal, t trackers, h home and things, u not identified. */
    fun group(type: String): String =
        if (type.isNotEmpty() && type != "unknown") GROUP[type] ?: "h" else "u"

    fun group(d: NearbyBle): String = group(d.type)

    fun kindWord(type: String): String? = KIND[type]

    // --- distance ------------------------------------------------------------

    /** Metres from signal strength alone, 0.1 to 200: a rough guide, and the screen says so. */
    fun metres(rssi: Int, ble: Boolean, cal: Calibration): Double {
        val p = if (ble) cal.bleAt1m else cal.wifiAt1m
        val n = if (ble) cal.bleFalloff else cal.wifiFalloff
        val m = 10.0.pow((p - rssi) / (10 * n))
        return max(0.1, min(200.0, m))
    }

    /** "~3.2 m", "~14 m". */
    fun distance(m: Double): String =
        "~" + (if (m < 10) String.format(Locale.US, "%.1f", m) else Math.round(m).toString()) + " m"

    // --- names ---------------------------------------------------------------

    /**
     * Where a dot sits on a radar. Not measured: a bearing made up from the
     * address, fixed per device so a dot can be followed. The same arithmetic
     * as the web page, so a dot sits in the same place in both.
     */
    fun bearing(key: String): Double {
        var h = 0L
        for (ch in key) h = (h * 31 + ch.code) and 0xFFFFFFFFL
        return (h % 3600) / 3600.0 * 2 * PI
    }

    fun maskAddr(a: String): String = if (a.length > 5) a.substring(0, 5) + ":XX:XX:XX:XX" else a

    fun maskName(n: String): String = if (n.length > 2) n.substring(0, 2) + "*".repeat(n.length - 2) else n

    fun apName(a: NearbyAp, masked: Boolean = false): String =
        if (a.ssid.isNotEmpty()) (if (masked) maskName(a.ssid) else a.ssid) else "hidden network"

    /** Its own name, else the product it names, the maker, the kind, or nothing better. */
    fun bleName(d: NearbyBle, masked: Boolean = false): String = when {
        d.name.isNotEmpty() -> if (masked) maskName(d.name) else d.name
        d.model.isNotEmpty() -> d.model
        d.vendor.isNotEmpty() -> d.vendor + " device"
        KIND[d.type] != null -> KIND.getValue(d.type)
        else -> "unnamed device"
    }

    /** True when the name shown is the device's own, not one made up for it. */
    fun hasOwnName(d: NearbyBle) = d.name.isNotEmpty()

    // --- search --------------------------------------------------------------

    private fun words(q: String) = q.lowercase(Locale.ROOT).split(' ').filter { it.isNotEmpty() }

    fun matches(a: NearbyAp, q: String): Boolean {
        val w = words(q)
        if (w.isEmpty()) return true
        val hay = listOf(a.ssid, if (a.ssid.isEmpty()) "hidden" else "", a.bssid, a.bssid.replace(":", ""),
            a.security, "ch${a.ch}", "channel ${a.ch}").joinToString(" ").lowercase(Locale.ROOT)
        return w.all { hay.contains(it) }
    }

    fun matches(d: NearbyBle, q: String): Boolean {
        val w = words(q)
        if (w.isEmpty()) return true
        val hay = listOf(d.name, d.model, d.vendor, KIND[d.type] ?: "", d.addr, d.addr.replace(":", ""),
            d.kind, GROUP_NAME[group(d)] ?: "").joinToString(" ").lowercase(Locale.ROOT)
        return w.all { hay.contains(it) }
    }

    // --- what the board is doing ---------------------------------------------

    /** "12 Wi-Fi networks and 9 Bluetooth devices nearby". */
    fun countLine(n: Nearby): String {
        val parts = ArrayList<String>()
        val live = n.wifi.count { it.live }
        if (n.wifiScan.enabled) parts.add("$live Wi-Fi network" + if (live == 1) "" else "s")
        if (n.bleScan.enabled) parts.add("${n.ble.size} Bluetooth device" + if (n.ble.size == 1) "" else "s")
        return if (parts.isEmpty()) "Scanning is off" else parts.joinToString(" and ") + " nearby"
    }

    data class State(val text: String, val bad: Boolean)

    /** What each radio is doing, as the web page words it. */
    fun stateLine(n: Nearby, masked: Boolean = false): State {
        val w = n.wifiScan
        val b = n.bleScan
        val p = ArrayList<String>()
        var bad = false
        val f = n.finding
        if (f != null) {
            val who = when {
                f.name.isNotEmpty() -> if (masked) maskName(f.name) else f.name
                f.type == "ble" -> "a Bluetooth device"
                else -> "a hidden network"
            }
            p.add("Finding $who. The other scans wait until that stops, so these lists stand still")
        } else {
            if (w.enabled) p.add(when {
                w.state == "scanning" -> "Scanning Wi-Fi now"
                w.state == "queued" -> "Wi-Fi scan queued"
                w.ageS < 0 -> "Wi-Fi not scanned yet"
                else -> "Wi-Fi scanned " + Format.ago(w.ageS)
            })
            if (b.enabled) {
                if (b.state == "unavailable") {
                    p.add("Bluetooth could not start")
                    bad = true
                } else p.add(when {
                    b.state == "listening" -> "listening for Bluetooth now"
                    b.state == "queued" -> "Bluetooth queued"
                    b.ageS < 0 -> "Bluetooth not listened for yet"
                    else -> "Bluetooth listened " + Format.ago(b.ageS)
                })
            }
            if (n.sweeping && (w.state == "queued" || b.state == "queued")) p.add("waiting for the network sweep to finish")
        }
        if (!n.onLan) p.add("setup mode: scanning only while this screen is open")
        val text = p.joinToString(" · ")
        return State(text.replaceFirstChar { it.uppercase() }, bad)
    }

    /** The background intervals the board's own page offers, in seconds; 0 is off. */
    val BACKGROUND = listOf(0, 60, 120, 300, 900, 3600)

    fun backgroundWord(s: Int): String = when (s) {
        0 -> "Off"
        60 -> "1 min"
        3600 -> "1 hour"
        else -> if (s % 60 == 0) "${s / 60} min" else "$s s"
    }

    /** Radar scales in metres; 0 means plain signal strength. */
    val SCALES = listOf(0, 5, 10, 20, 30, 50, 100)

    fun scaleWord(m: Int) = if (m == 0) "Signal" else "$m m"
}

/**
 * Which way each device's signal is going, from the last few readings this
 * app has made: the web page's trend arrows.
 */
class TrendMemory(private val keep: Int = 6) {

    private val hist = HashMap<String, ArrayList<Int>>()

    /** One reading of every device in a reply. Devices no longer listed are forgotten. */
    fun remember(readings: Map<String, Int>) {
        val gone = hist.keys.filter { it !in readings }
        for (k in gone) hist.remove(k)
        for ((k, r) in readings) {
            val h = hist.getOrPut(k) { ArrayList() }
            h.add(r)
            while (h.size > keep) h.removeAt(0)
        }
    }

    /** 1 stronger, -1 weaker, 0 steady or not enough readings yet. */
    fun trend(key: String): Int {
        val h = hist[key] ?: return 0
        if (h.size < 4) return 0
        val n = h.size
        val d = (h[n - 1] + h[n - 2]) / 2.0 - (h[0] + h[1]) / 2.0
        return if (d >= 4) 1 else if (d <= -4) -1 else 0
    }

    fun arrow(key: String): String = when (trend(key)) {
        1 -> " ▲"
        -1 -> " ▼"
        else -> ""
    }

    fun clear() = hist.clear()
}

/**
 * Arrivals and departures while the Nearby screen is open, worked out by
 * comparing one reading of the board's tables with the next, as the web
 * page's live log does. The first [learnMs] only learn what is already there:
 * a screen opened after a quiet spell would otherwise log everything the first
 * quick scans find. Nothing is logged while the board is finding a device,
 * since the lists stand still then.
 */
class LiveLog(private val learnMs: Long = 20_000, private val max: Int = 80, private val batchOver: Int = 12) {

    data class Entry(
        val atMs: Long,
        val wifi: Boolean,
        val key: String,
        val name: String,       // unmasked: see shown()
        val group: String,      // "w" for Wi-Fi, else the Bluetooth colour group
        val arrived: Boolean,
        val rssi: Int,
        val batch: Int = 0,     // more than batchOver arrived at once: one line for all of them
        val own: Boolean = false,   // the name is the device's own (its SSID or advertised name)
    ) {
        /**
         * The name as the log shows it. Masking applies when shown, as on the
         * web page, so turning it on hides names logged before as well.
         */
        fun shown(masked: Boolean): String = if (masked && own) Air.maskName(name) else name
    }

    /** What one reading changed, for the radars to mark: keys that arrived, and the entries for those that left. */
    data class Changes(val arrived: List<String>, val left: List<Entry>)

    private val hereW = HashMap<String, NearbyAp>()
    private val hereB = HashMap<String, NearbyBle>()
    private var openedMs = 0L
    private val list = ArrayList<Entry>()   // newest first

    val entries: List<Entry> get() = list

    /** Starts learning again, as when the screen is opened after being away. */
    fun relearn(nowMs: Long) {
        hereW.clear()
        hereB.clear()
        openedMs = nowMs
    }

    fun clear() = list.clear()

    /**
     * Folds in one reading. [showBle] says which Bluetooth devices are shown
     * at all (hidden groups and private addresses are not logged).
     */
    fun track(nowMs: Long, n: Nearby, showBle: (NearbyBle) -> Boolean): Changes {
        if (openedMs == 0L) openedMs = nowMs
        if (n.finding != null) return Changes(emptyList(), emptyList())
        val curW = LinkedHashMap<String, NearbyAp>()
        for (a in n.wifi) if (a.live) curW[a.bssid] = a
        val curB = LinkedHashMap<String, NearbyBle>()
        for (d in n.ble) if (d.ageS <= 60) curB[d.addr] = d

        val addW = curW.values.filter { it.bssid !in hereW }
        val goneW = hereW.values.filter { it.bssid !in curW }
        val addB = curB.values.filter { it.addr !in hereB }
        val goneB = hereB.values.filter { it.addr !in curB }
        hereW.clear(); hereW.putAll(curW)
        hereB.clear(); hereB.putAll(curB)

        if (nowMs - openedMs <= learnMs) return Changes(emptyList(), emptyList())
        val arrived = ArrayList<String>()
        val left = ArrayList<Entry>()
        val aB = addB.filter(showBle)
        val gB = goneB.filter(showBle)

        if (addW.size > batchOver) list.add(0, Entry(nowMs, true, "", "", "w", true, 0, addW.size))
        else for (a in addW) {
            list.add(0, Entry(nowMs, true, a.bssid, Air.apName(a), "w", true, a.rssi, own = a.ssid.isNotEmpty()))
            arrived.add(a.bssid)
        }
        if (aB.size > batchOver) list.add(0, Entry(nowMs, false, "", "", "u", true, 0, aB.size))
        else for (d in aB) {
            list.add(0, Entry(nowMs, false, d.addr, Air.bleName(d), Air.group(d), true, d.rssi, own = Air.hasOwnName(d)))
            arrived.add(d.addr)
        }
        for (a in goneW) {
            val e = Entry(nowMs, true, a.bssid, Air.apName(a), "w", false, a.rssi, own = a.ssid.isNotEmpty())
            list.add(0, e)
            left.add(e)
        }
        for (d in gB) {
            val e = Entry(nowMs, false, d.addr, Air.bleName(d), Air.group(d), false, d.rssi, own = Air.hasOwnName(d))
            list.add(0, e)
            left.add(e)
        }
        while (list.size > max) list.removeAt(list.size - 1)
        return Changes(arrived, left)
    }
}
