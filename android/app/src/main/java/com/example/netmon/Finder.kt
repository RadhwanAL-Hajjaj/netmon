package com.example.netmon

import java.util.Locale
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The readings of the device being found, smoothed the way the board's own
 * Finder page does it: the median of three against single deep fades, then
 * an average over about three seconds whatever the rate readings come in at.
 * The board sends every reading raw; the smoothing is the page's job, and
 * here the app's. No Android types, so it runs in plain JVM tests.
 */
class FinderTrack {

    /** t: when heard, phone clock; r: raw dBm; m: median of three; e: smoothed. */
    data class Point(val t: Long, val r: Int, val m: Double, val e: Double)

    private val pts = ArrayList<Point>()
    private var ema: Double? = null
    private var emaT = 0L

    /** When finding started, or started again after a gap: the rate counts from here. */
    var sinceMs = 0L

    val points: List<Point> get() = pts

    fun reset() {
        pts.clear()
        ema = null
        emaT = 0L
        sinceMs = 0L
    }

    fun add(t: Long, r: Int) {
        val a = ArrayList<Int>(3)
        a.add(r)
        var i = pts.size - 1
        while (i >= 0 && a.size < 3) {
            if (t - pts[i].t < 6000) a.add(pts[i].r) else break
            i--
        }
        a.sort()
        val m = if (a.size == 2) (a[0] + a[1]) / 2.0 else a[(a.size - 1) shr 1].toDouble()
        val prev = ema
        val e = if (prev == null || t - emaT > 15000) m
        else prev + (1 - exp(-max(0L, t - emaT) / 3000.0)) * (m - prev)
        ema = e
        emaT = t
        pts.add(Point(t, r, m, e))
        while (pts.isNotEmpty() && t - pts[0].t > 180_000) pts.removeAt(0)
    }

    /** The smoothed signal in dBm, or null before the first reading. */
    fun value(): Int? = ema?.roundToInt()

    fun heardAgo(now: Long): Long = if (pts.isEmpty()) Long.MAX_VALUE else now - pts[pts.size - 1].t

    /** Readings a minute over what has been heard since finding started; null in the first five seconds. */
    fun rate(now: Long): Int? {
        val w = min(60_000L, now - sinceMs)
        if (w < 5000) return null
        var n = 0
        var i = pts.size - 1
        while (i >= 0 && now - pts[i].t <= w) { n++; i-- }
        return (n * 60_000.0 / w).roundToInt()
    }

    /**
     * dB gained over the last 8 seconds or so, by least squares over the
     * filtered readings: a straight line through them shrugs off the odd jumpy
     * one. Null until there are four readings in the last 16 s.
     */
    fun trend(now: Long): Double? {
        var w = 8000L
        var sel: List<Point> = emptyList()
        while (w <= 16000L) {
            sel = pts.filter { now - it.t <= w }
            if (sel.size >= 4) break
            w += 4000L
        }
        if (sel.size < 4) return null
        val n = sel.size.toDouble()
        var sx = 0.0; var sy = 0.0; var sxx = 0.0; var sxy = 0.0
        for (p in sel) {
            val x = (p.t - now) / 1000.0
            val y = p.m
            sx += x; sy += y; sxx += x * x; sxy += x * y
        }
        val dn = n * sxx - sx * sx
        if (dn <= 0) return null
        return (n * sxy - sx * sy) / dn * w / 1000.0
    }
}

/** The Finder's wording and geometry. */
object FinderMath {

    const val TAU = 2 * PI

    /** 30 m is cold (0), 30 cm is hot (1), evenly by the logarithm in between. */
    fun hot(d: Double): Double = max(0.0, min(1.0, (ln(30.0) - ln(d)) / ln(100.0)))

    fun prox(d: Double): String = when {
        d < 0.7 -> "Very close"
        d < 2 -> "Close"
        d < 5 -> "Near"
        d < 15 -> "Some way off"
        else -> "Far off"
    }

    /** "≈ 2.4 m" */
    fun big(m: Double): String =
        "≈ " + (if (m < 10) String.format(Locale.US, "%.1f", m) else m.roundToInt().toString()) + " m"

    /** Close enough to look around rather than walk: within arm's reach, and heard recently. */
    fun found(d: Double?, heardAgoMs: Long): Boolean = d != null && d < 0.7 && heardAgoMs < 10_000

    /** Warmer, colder or steady, from a trend in dB. */
    fun trendWord(tr: Double?): String = when {
        tr == null -> "…"
        tr >= 3 -> "▲ Warmer"
        tr <= -3 -> "▼ Colder"
        else -> "● Steady"
    }

    // --- direction from a turn on the spot -------------------------------------

    /** One reading during a turn: how far round (radians, clockwise from where it started) and dBm. */
    data class TurnPoint(val a: Double, val r: Int)

    /**
     * Where the signal was strongest. a: radians clockwise from where the turn
     * started; contrast: dB between the strongest and weakest part; curve: the
     * smoothed shape, 36 steps; gap: the widest part of the turn that heard
     * nothing.
     */
    data class Direction(
        val a: Double,
        val contrast: Double,
        val n: Int,
        val ok: Boolean,
        val curve: List<Double?>,
        val gap: Double,
    )

    fun direction(pts: List<TurnPoint>): Direction {
        val n = pts.size
        if (n < 5) return Direction(0.0, 0.0, n, false, emptyList(), TAU)
        var best = -1e9
        var worst = 1e9
        var at = 0.0
        val curve = ArrayList<Double?>(36)
        for (k in 0 until 36) {
            val a = k * TAU / 36
            var s = 0.0
            var sw = 0.0
            for (p in pts) {
                val dd = abs(((p.a - a) % TAU + TAU + PI) % TAU - PI)
                if (dd < PI / 3) {
                    val wt = cos(dd * 1.5)
                    s += wt * p.r
                    sw += wt
                }
            }
            val m = if (sw > .3) s / sw else null
            curve.add(m)
            if (m == null) continue
            if (m > best) { best = m; at = a }
            if (m < worst) worst = m
        }
        val contrast = if (best > -1e9) best - worst else 0.0
        val angles = pts.map { norm(it.a) }.sorted()
        var gap = TAU - angles[n - 1] + angles[0]
        for (k in 1 until n) gap = max(gap, angles[k] - angles[k - 1])
        return Direction(at, contrast, n, contrast >= 3.5, curve, gap)
    }

    /** 1 to 12: the hour on a clock face for an angle clockwise from straight ahead. */
    fun clock(a: Double): Int {
        val h = (norm(a) / (PI / 6)).roundToInt() % 12
        return if (h == 0) 12 else h
    }

    private val CLOCK = mapOf(
        12 to "straight ahead", 1 to "a little to the right", 2 to "a little to the right", 3 to "to your right",
        4 to "behind you, on the right", 5 to "behind you, on the right", 6 to "behind you",
        7 to "behind you, on the left", 8 to "behind you, on the left", 9 to "to your left",
        10 to "a little to the left", 11 to "a little to the left",
    )

    fun clockWords(h: Int): String = CLOCK[h] ?: ""

    private fun tail(d: Direction): String =
        if (d.gap > PI / 3) " Part of the turn, about ${(d.gap * 180 / PI).roundToInt()}°, heard nothing, so turn again if this looks wrong."
        else ""

    /** Why a turn gave no answer, or null when it did. */
    fun noAnswer(d: Direction): String? = when {
        d.n < 5 -> "Too few readings during the turn to tell. Turn more slowly, or move a little closer, and try again."
        !d.ok -> "No clear direction: the signal was much the same all the way round. It may be very close, " +
            "or bouncing off walls. Move a few steps and turn again."
        else -> null
    }

    /** After a timed turn: the answer relative to where the turn started, as the web page words it. */
    fun dirText(d: Direction): String {
        noAnswer(d)?.let { return it }
        val h = clock(d.a)
        val words = if (h == 12) "straight ahead of where you started" else clockWords(h)
        return (if (d.contrast < 6) "A faint lead: s" else "S") + "trongest at about $h o’clock, $words (" +
            "${d.contrast.roundToInt()} dB above the weakest). Face that way and walk slowly: it should get warmer." + tail(d)
    }

    /**
     * After a turn the phone followed with its own rotation sensor: the answer
     * relative to where the phone faces now, [rel] radians clockwise.
     */
    fun liveText(d: Direction, rel: Double): String {
        noAnswer(d)?.let { return it }
        val h = clock(rel)
        return (if (d.contrast < 6) "A faint lead: s" else "S") + "trongest at about $h o’clock from where you face now, " +
            "${clockWords(h)} (${d.contrast.roundToInt()} dB above the weakest). The arrow keeps pointing there as you turn: " +
            "face it and walk slowly, and it should get warmer." + tail(d)
    }

    /** Seconds for one timed turn: long enough for about 18 readings at the rate they come in. */
    fun turnSeconds(ratePerMin: Int?): Int =
        if (ratePerMin == null || ratePerMin <= 0) 30 else max(20, min(45, (18.0 * 60 / ratePerMin).roundToInt()))

    // --- the phone's own heading -------------------------------------------

    /**
     * Which way the phone faces, in radians clockwise from the sensor frame's
     * north, from a rotation matrix (row-major, device to world, as
     * SensorManager.getRotationMatrixFromVector gives it). Held anywhere from
     * flat to upright in front of you, the top edge and the back of the phone
     * both lean the way you face, so their sum, flattened, is that way.
     * Null when the phone is held so that neither says anything.
     */
    fun headingOf(r: FloatArray): Double? {
        if (r.size < 9) return null
        val east = (r[1] - r[2]).toDouble()
        val north = (r[4] - r[5]).toDouble()
        if (hypot(east, north) < 0.2) return null
        return norm(atan2(east, north))
    }

    /** An angle in [0, 2π). */
    fun norm(a: Double): Double {
        val x = a % TAU
        return if (x < 0) x + TAU else x
    }

    /** The signed difference a − b, in (−π, π]. */
    fun diff(a: Double, b: Double): Double {
        var d = (a - b) % TAU
        if (d <= -PI) d += TAU
        if (d > PI) d -= TAU
        return d
    }
}

/**
 * Which way the phone pointed, over time, while it turned. Kept unwrapped (a
 * turn and a half is 3π, not π), so a reading heard during the turn can be
 * placed at the angle the phone had then, however late its reply arrives.
 */
class HeadingLog {
    private val times = ArrayList<Long>()
    private val angles = ArrayList<Double>()
    private var last: Double? = null
    var turned = 0.0
        private set

    fun clear() {
        times.clear(); angles.clear(); last = null; turned = 0.0
    }

    /** Adds the heading [h] (radians, 0 to 2π) seen at [t]. Returns how far round it has turned since the start. */
    fun add(t: Long, h: Double): Double {
        val prev = last
        turned = if (prev == null) 0.0 else turned + FinderMath.diff(h, prev)
        last = h
        if (times.isEmpty() || t - times[times.size - 1] >= 30) {
            times.add(t)
            angles.add(turned)
        } else {
            angles[angles.size - 1] = turned
        }
        return turned
    }

    fun isEmpty() = times.isEmpty()

    /** How far round it had turned at [t]: interpolated, and held at either end. */
    fun at(t: Long): Double? {
        if (times.isEmpty()) return null
        if (t <= times[0]) return angles[0]
        val n = times.size
        if (t >= times[n - 1]) return angles[n - 1]
        var lo = 0
        var hi = n - 1
        while (hi - lo > 1) {
            val mid = (lo + hi) ushr 1
            if (times[mid] <= t) lo = mid else hi = mid
        }
        val f = (t - times[lo]).toDouble() / (times[hi] - times[lo]).coerceAtLeast(1)
        return angles[lo] + (angles[hi] - angles[lo]) * f
    }
}
