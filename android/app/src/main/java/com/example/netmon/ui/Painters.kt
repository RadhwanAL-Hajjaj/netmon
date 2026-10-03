package com.example.netmon.ui

import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import com.example.netmon.FinderMath
import com.example.netmon.MapAp
import com.example.netmon.MapInfo
import com.example.netmon.NetMap
import java.util.Locale
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin

// The drawing behind the radars, the Finder and the map. Painters use only
// android.graphics and take sizes in pixels with the screen density, so the
// views stay thin and the drawing itself can be rendered and checked off the
// phone. Each follows the board's own page (pages.h) closely.

private fun deg(rad: Double) = (rad * 180 / PI).toFloat()

/** One dot on a radar. a: bearing in radians; r: 0 at the centre to 1 at the rim. */
class RadarDot(
    val key: String,
    val a: Double,
    val r: Double,
    val out: Boolean,       // beyond the scale: drawn small, on the rim
    val on: Boolean,        // matches the search
    val joined: Boolean,    // the access point the board is joined to
    val open: Boolean,      // an open network: a ring, not a dot
    val color: Int,
    val fade: Double,       // a Bluetooth device not heard for a while fades
    val rssi: Int,
    val text: String,
    val sub: String,
)

/** Where a device left: a fading dashed ring. */
class RadarGhost(val a: Double, val r: Double, val color: Int, val atMs: Long)

/** Draws text with its top, not its baseline, at y. */
private fun Canvas.textTop(s: String, x: Float, top: Float, p: Paint) = drawText(s, x, top - p.ascent(), p)

private fun clipText(p: Paint, s: String, max: Float): String {
    if (p.measureText(s) <= max) return s
    var t = s
    while (t.length > 1 && p.measureText("$t…") > max) t = t.substring(0, t.length - 1)
    return "$t…"
}

class RadarPainter(private val density: Float) {
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create("sans-serif", Typeface.NORMAL) }
    private val oval = RectF()
    private val dash = DashPathEffect(floatArrayOf(3 * density, 3 * density), 0f)

    private class Box(val x: Float, val y: Float, val w: Float, val h: Float)

    /** The scope itself: face, rings, cross and, when [sweep] is given, the beam. */
    fun scope(c: Canvas, cx: Float, cy: Float, r: Float, sweep: Double?) {
        val d = density
        fill.color = Pal.SCOPE
        fill.alpha = 255
        c.drawCircle(cx, cy, r, fill)
        stroke.color = Pal.RING
        stroke.strokeWidth = max(1f, d * 0.75f)
        stroke.pathEffect = null
        for (i in 1..4) c.drawCircle(cx, cy, r * i / 4, stroke)
        c.drawLine(cx - r, cy, cx + r, cy, stroke)
        c.drawLine(cx, cy - r, cx, cy + r, stroke)
        if (sweep == null) return
        oval.set(cx - r, cy - r, cx + r, cy + r)
        fill.color = Pal.BEAM
        for (i in 0 until 20) {
            fill.alpha = (255 * .14 * (1 - i / 20.0).pow(1.3)).roundToInt()
            c.drawArc(oval, deg(sweep - (i + 1) * .032), deg(.032) + 0.3f, true, fill)
        }
        fill.alpha = 255
        stroke.color = Pal.BEAM
        stroke.alpha = (255 * .75).roundToInt()
        stroke.strokeWidth = 1.5f * d
        c.drawLine(cx, cy, cx + r * cos(sweep).toFloat(), cy + r * sin(sweep).toFloat(), stroke)
        stroke.alpha = 255
    }

    /**
     * One radar of [size] pixels. scaleM: metres at the rim, or 0 for signal
     * strength; sweep: the beam's angle, or null when motion is off.
     */
    fun draw(
        c: Canvas, size: Float, dots: List<RadarDot>, ghosts: List<RadarGhost>, born: Map<String, Long>,
        scaleM: Int, selected: String?, now: Long, sweep: Double?,
    ) {
        val d = density
        val ctr = size / 2
        val big = ctr - 12 * d
        scope(c, ctr, ctr, big, sweep)

        text.textSize = 10 * d
        text.color = T.TEXT3
        text.textAlign = Paint.Align.LEFT
        val placed = ArrayList<Box>()
        for (i in 1..4) {
            val rl = if (scaleM > 0) {
                val v = scaleM * i / 4.0
                (if (v % 1.0 != 0.0) String.format(Locale.US, "%.1f", v) else v.toInt().toString()) + " m"
            } else "${-20 - 20 * i} dBm"
            val y = ctr - big * i / 4 + 2 * d
            c.textTop(rl, ctr + 3 * d, y, text)
            placed.add(Box(ctr + d, y - d, text.measureText(rl) + 4 * d, 12 * d))
        }

        class Drawn(val p: RadarDot, val x: Float, val y: Float)
        val drawn = ArrayList<Drawn>()
        for (p in dots) {
            val px = ctr + (p.r * big * cos(p.a)).toFloat()
            val py = ctr + (p.r * big * sin(p.a)).toFloat()
            var rad = 4.2f * d
            var glow = 1.0
            if (sweep != null) {
                val behind = ((sweep - p.a + 4 * PI) % (2 * PI))
                glow = .4 + .6 * exp(-behind / 2.4)
                if (behind < .5) rad += (1.6 * (1 - behind / .5)).toFloat() * d
            }
            val alpha = if (p.on) p.fade * glow else .1
            fill.color = p.color
            stroke.color = p.color
            stroke.pathEffect = null
            val b = born[p.key]
            if (b != null && now - b < 6000 && p.on) {
                val ph = ((now - b) % 1500) / 1500.0
                stroke.alpha = (255 * .85 * (1 - ph)).roundToInt()
                stroke.strokeWidth = 2 * d
                c.drawCircle(px, py, rad + 4 * d + (22 * ph).toFloat() * d, stroke)
            }
            val a255 = (255 * alpha).roundToInt().coerceIn(0, 255)
            fill.alpha = a255
            stroke.alpha = a255
            stroke.strokeWidth = 1.6f * d
            val dr = if (p.out) 3 * d else rad
            if (p.open) c.drawCircle(px, py, dr, stroke) else c.drawCircle(px, py, dr, fill)
            if (p.out) {
                stroke.strokeWidth = d
                c.drawCircle(px, py, 5.5f * d, stroke)
            }
            if (p.joined) {
                stroke.strokeWidth = 1.5f * d
                c.drawCircle(px, py, 8 * d, stroke)
            }
            if (selected == p.key) {
                stroke.color = T.TEXT
                stroke.alpha = 255
                stroke.strokeWidth = 2 * d
                c.drawCircle(px, py, 10 * d, stroke)
            }
            if (p.on) drawn.add(Drawn(p, px, py))
        }
        fill.alpha = 255
        stroke.alpha = 255

        for (g in ghosts) {
            val ga = now - g.atMs
            if (ga > 4000 || ga < 0) continue
            val gx = ctr + (g.r * big * cos(g.a)).toFloat()
            val gy = ctr + (g.r * big * sin(g.a)).toFloat()
            stroke.color = g.color
            stroke.alpha = (255 * .7 * (1 - ga / 4000.0)).roundToInt()
            stroke.strokeWidth = 1.4f * d
            stroke.pathEffect = dash
            c.drawCircle(gx, gy, (6 + 10 * ga / 4000.0).toFloat() * d, stroke)
        }
        stroke.pathEffect = null
        stroke.alpha = 255

        // Direct labels for the strongest, the selected one first, never overlapping.
        val maxLabels = if (size / d < 300) 4 else 6
        val order = drawn.sortedWith(Comparator { a, b ->
            val sa = if (a.p.key == selected) 1 else 0
            val sb = if (b.p.key == selected) 1 else 0
            if (sa != sb) sb - sa else b.p.rssi - a.p.rssi
        })
        var shown = 0
        for (dd in order) {
            if (shown >= maxLabels) break
            text.textSize = 10 * d
            val tw = min(text.measureText(dd.p.text), 96 * d)
            var bx = dd.x + 8 * d
            val by = dd.y - 8 * d
            val bw = max(tw, 46 * d)
            val bh = 21 * d
            if (bx + bw > size - 2 * d) bx = dd.x - 8 * d - bw
            if (by < 2 * d || by + bh > size - 2 * d) continue
            if (placed.any { bx < it.x + it.w + 3 * d && bx + bw + 3 * d > it.x && by < it.y + it.h + d && by + bh + d > it.y }) continue
            placed.add(Box(bx, by, bw, bh))
            shown++
            text.color = T.TEXT
            c.textTop(clipText(text, dd.p.text, 96 * d), bx, by, text)
            text.textSize = 9 * d
            text.color = T.TEXT3
            c.textTop(dd.p.sub, bx, by + 11 * d, text)
        }
        fill.color = T.TEXT
        c.drawCircle(ctr, ctr, 2.5f * d, fill)
    }
}

/**
 * What the Finder's radar shows. Angles are radians clockwise from where a
 * turn started; [rot] turns all of them for display, so that with the phone's
 * own compass the picture stays fixed to the room as you turn (0 without it).
 */
class FinderPic(
    val dist: Double?,              // metres, when heard recently
    val heat: Int,
    val turned: Double?,            // radians turned so far, while turning
    val rot: Double,
    val pts: List<FinderMath.TurnPoint>,
    val curve: List<Double?>?,      // the smoothed shape after a turn
    val arrow: Double?,             // where the signal was strongest, after a good turn
    val arrowFade: Double,
    val found: Boolean,
    val topLabel: String?,
    val beam: Boolean,
)

class FinderPainter(private val density: Float) {
    private val radar = RadarPainter(density)
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create("sans-serif", Typeface.NORMAL) }
    private val oval = RectF()
    private val path = Path()
    private val dash = DashPathEffect(floatArrayOf(6 * density, 5 * density), 0f)

    private val scales = doubleArrayOf(1.0, 2.0, 4.0, 8.0, 20.0, 40.0, 100.0)

    fun draw(c: Canvas, size: Float, pic: FinderPic, now: Long) {
        val d = density
        val ctr = size / 2
        val big = ctr - 16 * d
        val tau = 2 * PI
        val dist = pic.dist
        var sc = 10.0
        if (dist != null) {
            sc = 100.0
            for (s in scales) if (s >= dist * 1.6) { sc = s; break }
        }
        radar.scope(c, ctr, ctr, big, if (pic.turned != null || !pic.beam) null else now / 4200.0 * tau % tau)
        val turned = pic.turned
        if (turned != null) {
            oval.set(ctr - big, ctr - big, ctr + big, ctr + big)
            fill.color = Pal.BEAM
            fill.alpha = (255 * .13).roundToInt()
            val start = pic.rot - PI / 2
            val sw = max(-tau, min(tau, turned))
            if (sw >= 0) c.drawArc(oval, deg(start), deg(sw), true, fill)
            else c.drawArc(oval, deg(start + sw), deg(-sw), true, fill)
            fill.alpha = 255
        }
        text.textSize = 10 * d
        text.color = T.TEXT3
        text.textAlign = Paint.Align.LEFT
        for (i in 1..4) {
            val v = sc * i / 4
            val s = if (abs(v - v.roundToInt()) < 1e-9) v.roundToInt().toString()
            else String.format(Locale.US, "%.2f", v).trimEnd('0').trimEnd('.')
            c.textTop("$s m", ctr + 3 * d, ctr - big * i / 4 + 2 * d, text)
        }

        // What the turn heard: each reading at the angle the phone faced, farther
        // out the stronger it was; after the turn, the smoothed shape of it.
        val pts = pic.pts
        if (pts.isNotEmpty()) {
            var lo = 1e9
            var hi = -1e9
            for (p in pts) { lo = min(lo, p.r.toDouble()); hi = max(hi, p.r.toDouble()) }
            val span = max(6.0, hi - lo)
            fun rad(v: Double) = (big * (.22 + .72 * (v - lo) / span)).toFloat()
            val curve = pic.curve
            if (turned == null && curve != null) {
                path.reset()
                var started = false
                curve.forEachIndexed { k, v ->
                    if (v == null) return@forEachIndexed
                    val a = k * tau / 36 + pic.rot - PI / 2
                    val rr = rad(v)
                    val x = ctr + rr * cos(a).toFloat()
                    val y = ctr + rr * sin(a).toFloat()
                    if (!started) { path.moveTo(x, y); started = true } else path.lineTo(x, y)
                }
                path.close()
                fill.color = pic.heat
                fill.alpha = (255 * .1).roundToInt()
                c.drawPath(path, fill)
                stroke.color = pic.heat
                stroke.alpha = (255 * .45).roundToInt()
                stroke.strokeWidth = 1.2f * d
                stroke.pathEffect = null
                c.drawPath(path, stroke)
            }
            fill.color = pic.heat
            fill.alpha = (255 * .85).roundToInt()
            for (p in pts) {
                val rr = rad(p.r.toDouble())
                val a = p.a + pic.rot - PI / 2
                c.drawCircle(ctr + rr * cos(a).toFloat(), ctr + rr * sin(a).toFloat(), 2.6f * d, fill)
            }
            fill.alpha = 255
            stroke.alpha = 255
        }

        if (dist != null) {
            val rr = (min(dist / sc, 1.0) * big).toFloat()
            val lo2 = (min(dist / 1.5 / sc, 1.0) * big).toFloat()
            val hi2 = (min(dist * 1.5 / sc, 1.0) * big).toFloat()
            // The band: how far it could be, given how rough a distance from signal is.
            stroke.pathEffect = null
            stroke.color = pic.heat
            stroke.alpha = (255 * (if (pic.arrow != null) .11 else .17)).roundToInt()
            stroke.strokeWidth = max(1f, hi2 - lo2)
            stroke.strokeCap = Paint.Cap.BUTT
            c.drawCircle(ctr, ctr, (lo2 + hi2) / 2, stroke)
            stroke.strokeCap = Paint.Cap.ROUND
            stroke.alpha = 255
            stroke.strokeWidth = 2.4f * d
            stroke.pathEffect = dash
            c.drawCircle(ctr, ctr, rr, stroke)
            stroke.pathEffect = null
            val arrow = pic.arrow
            if (arrow != null && turned == null) {
                val a = arrow + pic.rot - PI / 2
                val ca = cos(a).toFloat()
                val sa = sin(a).toFloat()
                val tip = max(rr, 30 * d)
                val alpha = (255 * pic.arrowFade).roundToInt().coerceIn(0, 255)
                stroke.color = pic.heat
                stroke.alpha = alpha
                stroke.strokeWidth = 3 * d
                c.drawLine(ctr, ctr, ctr + (tip - 14 * d) * ca, ctr + (tip - 14 * d) * sa, stroke)
                fill.color = pic.heat
                fill.alpha = alpha
                path.reset()
                path.moveTo(ctr + (tip - 4 * d) * ca, ctr + (tip - 4 * d) * sa)
                path.lineTo(ctr + (tip - 16 * d) * ca - 6 * d * sa, ctr + (tip - 16 * d) * sa + 6 * d * ca)
                path.lineTo(ctr + (tip - 16 * d) * ca + 6 * d * sa, ctr + (tip - 16 * d) * sa - 6 * d * ca)
                path.close()
                c.drawPath(path, fill)
                c.drawCircle(ctr + tip * ca, ctr + tip * sa, 6.5f * d, fill)
                fill.alpha = 255
                stroke.alpha = 255
            }
        }
        if (turned != null) {
            val ha = pic.rot + turned - PI / 2
            stroke.color = T.TEXT
            stroke.strokeWidth = 2.5f * d
            val ex = ctr + big * cos(ha).toFloat()
            val ey = ctr + big * sin(ha).toFloat()
            c.drawLine(ctr, ctr, ex, ey, stroke)
            fill.color = T.TEXT
            c.drawCircle(ex, ey, 5.5f * d, fill)
        }
        val top = pic.topLabel
        if (top != null) {
            text.textSize = 10 * d
            text.color = T.TEXT3
            text.textAlign = Paint.Align.CENTER
            c.drawText(top, ctr, ctr - big - 4 * d, text)
            text.textAlign = Paint.Align.LEFT
        }
        if (pic.found) {
            val ph = (now % 1200) / 1200.0
            stroke.color = pic.heat
            stroke.alpha = (255 * .55 * (1 - ph)).roundToInt()
            stroke.strokeWidth = 3 * d
            c.drawCircle(ctr, ctr, (8 + 34 * ph).toFloat() * d, stroke)
            stroke.alpha = 255
        }
        fill.color = T.TEXT
        fill.alpha = 255
        c.drawCircle(ctr, ctr, 3.6f * d, fill)
    }
}

/** The last minute of signal: each reading a dot, the smoothed figure a line. */
class TracePainter(private val density: Float) {
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeJoin = Paint.Join.ROUND; strokeCap = Paint.Cap.ROUND
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create("sans-serif", Typeface.NORMAL) }
    private val path = Path()

    /** t: when heard; r: raw dBm; e: smoothed dBm. */
    fun draw(c: Canvas, w: Float, h: Float, t: LongArray, r: IntArray, e: DoubleArray, now: Long) {
        val d = density
        val lo = -100.0
        val hi = -30.0
        fun x(at: Long) = w - (now - at) / 60000f * w
        fun y(v: Double) = (4 * d + (h - 8 * d) * (1 - (max(lo, min(hi, v)) - lo) / (hi - lo))).toFloat()
        stroke.color = Pal.RING
        stroke.strokeWidth = max(1f, d * .75f)
        text.textSize = 9 * d
        text.color = T.TEXT3
        var g = -40
        while (g >= -90) {
            val gy = y(g.toDouble())
            c.drawLine(0f, gy, w, gy, stroke)
            c.drawText(g.toString(), 2 * d, gy - d, text)
            g -= 25
        }
        fill.color = T.TEXT3
        fill.alpha = (255 * .6).roundToInt()
        val n = t.size
        for (i in 0 until n) {
            if (now - t[i] > 60000) continue
            c.drawCircle(x(t[i]), y(r[i].toDouble()), 2 * d, fill)
        }
        fill.alpha = 255
        path.reset()
        var prev = Long.MIN_VALUE
        var any = false
        for (i in 0 until n) {
            if (now - t[i] > 60000) continue
            val px = x(t[i])
            val py = y(e[i])
            if (!any || t[i] - prev > 15000) path.moveTo(px, py) else path.lineTo(px, py)
            prev = t[i]
            any = true
        }
        if (any) {
            stroke.color = T.TEXT
            stroke.strokeWidth = 1.8f * d
            c.drawPath(path, stroke)
        }
    }
}

/** A cold-to-hot bar with a mark for how close the device is; no mark when it has not been heard. */
class HeatBarPainter(private val density: Float) {
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val rect = RectF()

    fun draw(c: Canvas, w: Float, h: Float, v: Double?) {
        val d = density
        val bh = 9 * d
        val top = (h - bh) / 2
        val rad = bh / 2
        val segs = 160
        val x0 = rad
        val x1 = w - rad
        fill.color = Pal.heat(0.0)
        c.drawCircle(x0, top + rad, rad, fill)
        fill.color = Pal.heat(1.0)
        c.drawCircle(x1, top + rad, rad, fill)
        for (i in 0 until segs) {
            fill.color = Pal.heat((i + .5) / segs)
            rect.set(x0 + (x1 - x0) * i / segs, top, x0 + (x1 - x0) * (i + 1) / segs + .5f, top + bh)
            c.drawRect(rect, fill)
        }
        if (v == null) return
        val mx = x0 + (x1 - x0) * max(0.0, min(1.0, v)).toFloat()
        rect.set(mx - 2 * d, 0f, mx + 2 * d, h)
        fill.color = T.TEXT
        c.drawRoundRect(rect, 2 * d, 2 * d, fill)
        stroke.color = T.SURFACE
        stroke.strokeWidth = 2 * d
        c.drawRoundRect(rect, 2 * d, 2 * d, stroke)
    }
}

/** What is selected on the map. */
sealed class MapSel {
    class Group(val key: String) : MapSel()
    class Device(val mac: String) : MapSel()
    class Ap(val bssid: String) : MapSel()
    object Me : MapSel()
    object Router : MapSel()
    object Internet : MapSel()
}

class MapPainter(private val density: Float) {
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND
    }
    private val t1 = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL); textSize = 11.5f; color = T.TEXT
    }
    private val t2 = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create("sans-serif", Typeface.NORMAL); textSize = 10f; color = T.TEXT2
    }
    private val halo = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 3f; color = Pal.SCOPE; strokeJoin = Paint.Join.ROUND
    }
    private val rect = RectF()
    private val path = Path()
    private val dash = DashPathEffect(floatArrayOf(3f, 3f), 0f)
    private val dash2 = DashPathEffect(floatArrayOf(2f, 2f), 0f)

    private fun label(c: Canvas, s: String, x: Double, y: Double, p: Paint, align: Paint.Align = Paint.Align.CENTER) {
        p.textAlign = align
        halo.textSize = p.textSize
        halo.typeface = p.typeface
        halo.textAlign = align
        c.drawText(s, x.toFloat(), y.toFloat(), halo)
        c.drawText(s, x.toFloat(), y.toFloat(), p)
    }

    private fun glyphRouter(c: Canvas, x: Double, y: Double) {
        val fx = x.toFloat()
        val fy = y.toFloat()
        rect.set(fx - 9, fy + 1, fx + 9, fy + 8)
        c.drawRect(rect, stroke)
        c.drawLine(fx - 5, fy + 1, fx - 5, fy - 5, stroke)
        c.drawLine(fx + 5, fy + 1, fx + 5, fy - 5, stroke)
        c.drawLine(fx - 5, fy + 4.5f, fx - 4, fy + 4.5f, stroke)
        c.drawLine(fx - 1, fy + 4.5f, fx, fy + 4.5f, stroke)
    }

    private fun glyphGlobe(c: Canvas, x: Double, y: Double, s: Float) {
        val fx = x.toFloat()
        val fy = y.toFloat()
        c.drawCircle(fx, fy, 9 * s, stroke)
        c.drawLine(fx - 9 * s, fy, fx + 9 * s, fy, stroke)
        path.reset()
        path.moveTo(fx, fy - 9 * s)
        path.cubicTo(fx - 4.5f * s, fy - 5 * s, fx - 4.5f * s, fy + 5 * s, fx, fy + 9 * s)
        path.moveTo(fx, fy - 9 * s)
        path.cubicTo(fx + 4.5f * s, fy - 5 * s, fx + 4.5f * s, fy + 5 * s, fx, fy + 9 * s)
        c.drawPath(path, stroke)
    }

    private fun glyphWifi(c: Canvas, x: Double, y: Double, s: Float) {
        val fx = x.toFloat()
        val fy = y.toFloat() + 5.2f * s
        for (r in floatArrayOf(8.5f * s, 4.8f * s)) {
            rect.set(fx - r, fy - r, fx + r, fy + r)
            c.drawArc(rect, 225f, 90f, false, stroke)
        }
        c.drawCircle(fx, fy, .9f * s, fill)
    }

    /**
     * Draws [plan] at [px] pixels per map unit. Sizes inside are in map units,
     * the web page's pixels, so the drawing scales as one.
     */
    fun draw(c: Canvas, plan: NetMap.Plan, info: MapInfo, sel: MapSel?, px: Float) {
        val box = plan.box
        c.save()
        c.scale(px, px)
        c.translate((-box[0]).toFloat(), (-box[1]).toFloat())
        val hub = NetMap.HUB.toFloat()
        val iw = NetMap.IW.toFloat()
        val ih = NetMap.IH.toFloat()
        val iy = plan.iy.toFloat()

        // The way out.
        stroke.pathEffect = null
        stroke.color = if (sel is MapSel.Internet) T.ACCENT else Pal.LINE
        stroke.strokeWidth = 2.2f
        c.drawLine(0f, -hub, 0f, iy + ih / 2, stroke)
        for (g in plan.groups) {
            val on = sel is MapSel.Group && sel.key == g.key
            stroke.color = if (on) T.ACCENT else Pal.LINE
            stroke.strokeWidth = if (g.wifi) 2.4f else min(5.0, 1.2 + kotlin.math.sqrt(g.items.size.toDouble()) * .7).toFloat()
            c.drawLine(0f, 0f, g.x.toFloat(), g.y.toFloat(), stroke)
        }

        // The internet.
        rect.set(-iw / 2, iy - ih / 2, iw / 2, iy + ih / 2)
        fill.color = T.SURFACE
        c.drawRoundRect(rect, 12f, 12f, fill)
        stroke.color = if (sel is MapSel.Internet) T.ACCENT else T.TEXT
        stroke.strokeWidth = if (sel is MapSel.Internet) 2.6f else 1.6f
        c.drawRoundRect(rect, 12f, 12f, stroke)
        stroke.color = T.TEXT
        stroke.strokeWidth = 1.5f
        glyphGlobe(c, -iw / 2.0 + 20, iy.toDouble(), .8f)
        label(c, "Internet", -iw / 2.0 + 36, iy - 2.0, t1, Paint.Align.LEFT)
        val isp = info.isp
        val who = if (isp.checked && isp.valid) com.example.netmon.Format.shortVendor(isp.isp.ifEmpty { isp.org })
        else if (info.fromBoard) "not looked up yet" else "provider not known"
        label(c, NetMap.clip(who, 18), -iw / 2.0 + 36, iy + 11.0, t2, Paint.Align.LEFT)

        // The groups.
        for (g in plan.groups) {
            val on = sel is MapSel.Group && sel.key == g.key
            fill.color = T.SURFACE
            c.drawCircle(g.x.toFloat(), g.y.toFloat(), g.r.toFloat(), fill)
            stroke.color = if (on) T.ACCENT else T.EDGE
            stroke.strokeWidth = if (on) 2f else 1.2f
            c.drawCircle(g.x.toFloat(), g.y.toFloat(), g.r.toFloat(), stroke)
            g.lines.forEachIndexed { k, s -> label(c, s, g.x, g.y + g.r + 14 + 13 * k, t1) }
            val n = if (g.wifi) {
                val k = g.aps.size
                "$k access point" + if (k == 1) "" else "s"
            } else "${g.items.size} device" + if (g.items.size == 1) "" else "s"
            label(c, n, g.x, g.y + g.r + 14 + 13 * g.lines.size, t2)
        }
        for (g in plan.groups) {
            var joined: NetMap.Item? = null
            var me: NetMap.Item? = null
            for (it in g.items) {
                val x = it.x.toFloat()
                val y = it.y.toFloat()
                val ap = it.ap
                when {
                    ap != null -> {
                        if (ap.joined) joined = it
                        fill.color = T.SURFACE
                        c.drawCircle(x, y, 10f, fill)
                        stroke.color = if (ap.live) Pal.WIFI else T.TEXT3
                        stroke.strokeWidth = 1.8f
                        stroke.pathEffect = if (ap.live) null else dash2
                        c.drawCircle(x, y, 10f, stroke)
                        stroke.pathEffect = null
                        stroke.color = T.TEXT
                        stroke.strokeWidth = 1.5f
                        fill.color = T.TEXT
                        glyphWifi(c, it.x, it.y, .85f)
                    }
                    it.me -> {
                        me = it
                        fill.color = Pal.ME
                        c.drawCircle(x, y, (NetMap.DOT + .5).toFloat(), fill)
                        stroke.color = Pal.SCOPE
                        stroke.strokeWidth = 1.6f
                        c.drawCircle(x, y, 2.6f, stroke)
                    }
                    else -> {
                        val d = it.device ?: continue
                        val r = NetMap.DOT.toFloat()
                        if (!d.online) {
                            fill.color = T.SURFACE
                            fill.alpha = 180
                            c.drawCircle(x, y, r, fill)
                            fill.alpha = 255
                            stroke.color = T.TEXT3
                            stroke.strokeWidth = 1.6f
                            c.drawCircle(x, y, r, stroke)
                        } else {
                            fill.color = T.status(d.status, true)
                            c.drawCircle(x, y, r, fill)
                        }
                    }
                }
            }
            val j = joined
            val m = me
            if (g.wifi && j != null && m != null) {
                stroke.color = T.TEXT3
                stroke.strokeWidth = 1f
                stroke.pathEffect = dash
                c.drawLine(j.x.toFloat(), j.y.toFloat(), m.x.toFloat(), m.y.toFloat(), stroke)
                stroke.pathEffect = null
            }
        }

        // The router.
        fill.color = T.SURFACE
        c.drawCircle(0f, 0f, hub, fill)
        stroke.color = if (sel is MapSel.Router) T.ACCENT else T.TEXT
        stroke.strokeWidth = if (sel is MapSel.Router) 2.6f else 1.6f
        c.drawCircle(0f, 0f, hub, stroke)
        stroke.color = T.TEXT
        stroke.strokeWidth = 1.5f
        glyphRouter(c, 0.0, 0.0)
        label(c, "Router", 0.0, NetMap.HUB + 14, t1)
        label(c, info.gateway, 0.0, NetMap.HUB + 26, t2)

        // What is selected.
        val s = locate(plan, sel)
        if (s != null) {
            stroke.color = T.ACCENT
            stroke.strokeWidth = 2f
            c.drawCircle(s.x.toFloat(), s.y.toFloat(), if (s.ap != null) 14f else 11f, stroke)
            val d = s.device
            if (s.ap == null && !s.me && d != null) {
                label(c, NetMap.clip(com.example.netmon.Format.deviceName(d), 24), s.x, s.y - 15, t1)
            }
        }
        c.restore()
    }

    fun locate(plan: NetMap.Plan, sel: MapSel?): NetMap.Item? {
        if (sel == null) return null
        for (g in plan.groups) for (it in g.items) {
            when (sel) {
                is MapSel.Device -> if (it.ap == null && !it.me && it.device?.mac == sel.mac) return it
                is MapSel.Ap -> if (it.ap?.bssid == sel.bssid) return it
                is MapSel.Me -> if (it.me) return it
                else -> return null
            }
        }
        return null
    }
}

/** The access points of the board's own network, joined one first, for lists. */
fun sortedAps(aps: List<MapAp>): List<MapAp> = aps.sortedWith(Comparator { a, b ->
    if (a.joined != b.joined) (if (a.joined) -1 else 1) else b.rssi - a.rssi
})
