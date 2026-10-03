package com.example.netmon

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException

/** One entry of the on-phone history, with a real clock time. */
data class LoggedEvent(
    val wallMs: Long,
    val type: String,
    val mac: String,
    val ip: String,
    val text: String,
    /** Identity used to recognise the same board event on the next reading. */
    val key: String,
)

/**
 * The board keeps its last 48 events in RAM, stamped in seconds since it
 * started, and forgets them all when it restarts. Two ARP sweeps a minute
 * take two of those 48 slots, so a device event scrolls off in under half an
 * hour.
 *
 * This keeps a longer history on the phone. Each reading of the board's list
 * has its "seconds since start" turned into clock time; sweep entries are
 * skipped and anything not seen before is added. A restart of the board is
 * noticed from its uptime and recorded as an entry of its own.
 *
 * Not thread-safe: callers synchronise on [lock].
 */
class EventHistory(private val file: File?, private val capacity: Int = 600) {

    val lock = Any()

    private val items = ArrayList<LoggedEvent>()   // newest first
    private val keys = HashSet<String>()

    /** The board's start time on the phone's clock, for the board's current run. */
    var bootWallMs = 0L
        private set
    private var lastUptimeS = -1L
    private var lastMergeWallMs = 0L
    /** Events from before a "Clear" stay cleared, even while the board still lists them. */
    private var clearedAtMs = 0L

    /** Name last seen for each MAC, so old entries still read as a device. */
    private val names = LinkedHashMap<String, String>()

    fun all(): List<LoggedEvent> = ArrayList(items)

    fun size(): Int = items.size

    fun nameFor(mac: String): String? = names[mac]

    /** Returns true when anything changed and a save is worth doing. */
    fun rememberNames(devices: List<Device>): Boolean {
        var changed = false
        for (d in devices) {
            val n = when {
                d.hostname.isNotBlank() -> d.hostname
                d.vendor.isNotBlank() -> Format.shortVendor(d.vendor)
                else -> continue
            }
            if (names[d.mac] != n) {
                names.remove(d.mac)
                names[d.mac] = n
                changed = true
            }
        }
        while (names.size > 400) names.remove(names.keys.first())
        return changed
    }

    /**
     * Folds in one reading of /api/events taken at [nowWallMs], when the
     * board reported [uptimeS]. Returns how many entries were new.
     */
    fun merge(nowWallMs: Long, uptimeS: Long, events: List<BoardEvent>): Int {
        val boot = nowWallMs - uptimeS * 1000
        var added = 0
        if (bootWallMs == 0L) {
            bootWallMs = boot
        } else {
            // Uptime going backwards is a restart. So is a start time that has
            // moved by more than clock drift could explain, which catches a
            // restart that happened while the app was not looking.
            val elapsed = (nowWallMs - lastMergeWallMs).coerceAtLeast(0)
            val allowance = 30_000 + elapsed / 1000
            val moved = kotlin.math.abs(boot - bootWallMs) > allowance
            if (uptimeS < lastUptimeS || moved) {
                bootWallMs = boot
                val restart = LoggedEvent(boot, TYPE_RESTART, "", "", "", "restart|${boot / 1000}")
                if (restart.wallMs > clearedAtMs && insert(restart)) added++
            }
        }
        lastUptimeS = uptimeS
        lastMergeWallMs = nowWallMs

        for (e in events) {
            if (e.type == "scan" || e.type == "scan_done") continue
            // Stamped later than the uptime read beside it: a torn reading.
            if (e.atS > uptimeS + 5) continue
            val key = "${bootWallMs / 1000}|${e.atS}|${e.type}|${e.mac}|${e.text}"
            val ev = LoggedEvent(bootWallMs + e.atS * 1000, e.type, e.mac, e.ip, e.text, key)
            if (ev.wallMs <= clearedAtMs) continue
            if (insert(ev)) added++
        }
        return added
    }

    fun clear(nowMs: Long = System.currentTimeMillis()) {
        items.clear()
        keys.clear()
        clearedAtMs = nowMs
    }

    private fun insert(ev: LoggedEvent): Boolean {
        if (!keys.add(ev.key)) return false
        // Newest first. Entries arrive roughly in order, so search from the top.
        var i = 0
        while (i < items.size && items[i].wallMs > ev.wallMs) i++
        items.add(i, ev)
        while (items.size > capacity) {
            val gone = items.removeAt(items.size - 1)
            keys.remove(gone.key)
        }
        return true
    }

    // --- persistence -------------------------------------------------------

    fun load() {
        val f = file ?: return
        if (!f.exists()) return
        try {
            val o = JSONObject(f.readText(Charsets.UTF_8))
            val loaded = ArrayList<LoggedEvent>()
            val a = o.optJSONArray("events") ?: JSONArray()
            for (i in 0 until a.length()) {
                val e = a.optJSONObject(i) ?: continue
                loaded.add(LoggedEvent(e.long("t"), e.str("type"), e.str("mac"), e.str("ip"), e.str("text"), e.str("k")))
            }
            items.clear()
            keys.clear()
            for (e in loaded) {
                items.add(e)
                keys.add(e.key)
            }
            bootWallMs = o.long("boot_wall_ms")
            lastUptimeS = o.long("last_uptime_s", -1)
            lastMergeWallMs = o.long("last_merge_ms")
            clearedAtMs = o.long("cleared_at_ms")
            names.clear()
            val n = o.optJSONObject("names")
            if (n != null) {
                val it: Iterator<*> = n.keys()
                while (it.hasNext()) {
                    val mac = it.next() as? String ?: continue
                    names[mac] = n.str(mac)
                }
            }
        } catch (e: JSONException) {
            // A damaged file costs the history, not the app.
        } catch (e: IOException) {
        }
    }

    fun save() {
        val f = file ?: return
        val a = JSONArray()
        for (e in items) {
            val o = JSONObject()
            o.put("t", e.wallMs)
            o.put("type", e.type)
            o.put("mac", e.mac)
            o.put("ip", e.ip)
            o.put("text", e.text)
            o.put("k", e.key)
            a.put(o)
        }
        val n = JSONObject()
        for ((mac, name) in names) n.put(mac, name)
        val root = JSONObject()
        root.put("boot_wall_ms", bootWallMs)
        root.put("last_uptime_s", lastUptimeS)
        root.put("last_merge_ms", lastMergeWallMs)
        root.put("cleared_at_ms", clearedAtMs)
        root.put("events", a)
        root.put("names", n)
        try {
            val tmp = File(f.parentFile, f.name + ".tmp")
            tmp.writeText(root.toString(), Charsets.UTF_8)
            if (!tmp.renameTo(f)) {
                f.delete()
                tmp.renameTo(f)
            }
        } catch (e: IOException) {
            // Best effort: the history is a convenience, never worth a crash.
        }
    }

    companion object {
        const val TYPE_RESTART = "restart"
    }
}
