package com.example.netmon

/** One gateway probe: when it ran, and its round trip, or -1 when the gateway did not answer. */
data class LatencySample(val wallMs: Long, val rttMs: Long)

/**
 * The board keeps only its latest gateway probe. The app keeps a rolling
 * record of them so the Internet screen can draw a trend. A probe is recorded
 * once, however many times it is read: two readings of the same probe agree
 * on when it ran to within a few seconds.
 */
class LatencyLog(private val capacity: Int = 240) {

    private val samples = ArrayList<LatencySample>()   // oldest first

    fun all(): List<LatencySample> = ArrayList(samples)

    fun size() = samples.size

    /**
     * Records the probe described by a /api/health reading taken at
     * [nowWallMs]. Returns true when it was a probe not seen before.
     */
    fun record(nowWallMs: Long, valid: Boolean, rttMs: Long, ageS: Long): Boolean {
        val at = nowWallMs - ageS * 1000
        val last = samples.lastOrNull()
        if (last != null && kotlin.math.abs(last.wallMs - at) < SAME_PROBE_MS) return false
        if (last != null && at < last.wallMs) return false
        samples.add(LatencySample(at, if (valid) rttMs else -1))
        while (samples.size > capacity) samples.removeAt(0)
        return true
    }

    data class Stats(val count: Int, val minMs: Long, val avgMs: Long, val maxMs: Long, val lost: Int)

    fun stats(): Stats? {
        if (samples.isEmpty()) return null
        val ok = samples.filter { it.rttMs >= 0 }
        val lost = samples.size - ok.size
        if (ok.isEmpty()) return Stats(samples.size, 0, 0, 0, lost)
        var min = Long.MAX_VALUE
        var max = 0L
        var sum = 0L
        for (s in ok) {
            if (s.rttMs < min) min = s.rttMs
            if (s.rttMs > max) max = s.rttMs
            sum += s.rttMs
        }
        return Stats(samples.size, min, sum / ok.size, max, lost)
    }

    /** Compact text form for SharedPreferences: "wall:rtt;wall:rtt". */
    fun encode(): String = samples.joinToString(";") { "${it.wallMs}:${it.rttMs}" }

    fun decode(text: String?) {
        samples.clear()
        if (text.isNullOrEmpty()) return
        for (part in text.split(';')) {
            val i = part.indexOf(':')
            if (i <= 0) continue
            val w = part.substring(0, i).toLongOrNull() ?: continue
            val r = part.substring(i + 1).toLongOrNull() ?: continue
            samples.add(LatencySample(w, r))
        }
        samples.sortBy { it.wallMs }
        while (samples.size > capacity) samples.removeAt(0)
    }

    fun clear() = samples.clear()

    companion object {
        const val SAME_PROBE_MS = 5000L
    }
}
