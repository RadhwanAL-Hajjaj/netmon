package com.example.netmon.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.View
import com.example.netmon.Device
import com.example.netmon.Format
import com.example.netmon.LatencySample
import java.util.Locale

/** Four bars for Wi-Fi signal. */
class SignalBars(context: Context) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rect = RectF()
    var bars = 0
        set(v) { field = v.coerceIn(0, 4); contentDescription = "Signal ${field} of 4"; invalidate() }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(context.dp(18), context.dp(14))
    }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat()
        val h = height.toFloat()
        val gap = context.dpf(2f)
        val bw = (w - gap * 3) / 4
        val r = context.dpf(1f)
        for (i in 0 until 4) {
            val bh = h * (i + 1) / 4f
            val left = i * (bw + gap)
            rect.set(left, h - bh, left + bw, h)
            paint.color = if (i < bars) T.TEXT else T.EDGE
            canvas.drawRoundRect(rect, r, r, paint)
        }
    }
}

/**
 * The network at a glance: one mark per device, in the colour of its status,
 * problems first. Thirty devices is thirty marks; an unrecognised one is a red
 * mark at the very start of the line.
 */
class DeviceStrip(context: Context) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rect = RectF()
    private var colors = IntArray(0)

    fun set(devices: List<Device>) {
        fun rank(d: Device) = when {
            !d.online -> 4
            d.isUnknown -> 0
            d.isPrivate -> 1
            else -> 2
        }
        val sorted = devices.sortedWith(Comparator { a, b ->
            val r = rank(a) - rank(b)
            if (r != 0) r else Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip))
        })
        colors = IntArray(sorted.size) { T.status(sorted[it].status, sorted[it].online) }
        val online = devices.count { it.online }
        contentDescription = "$online of ${devices.size} devices online"
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), context.dp(30))
    }

    override fun onDraw(canvas: Canvas) {
        val n = colors.size
        if (n == 0) {
            paint.color = T.EDGE
            rect.set(0f, height / 2f - 1, width.toFloat(), height / 2f + 1)
            canvas.drawRect(rect, paint)
            return
        }
        // Up to 64 devices: marks shrink to fit, but never below 2dp.
        val gap = context.dpf(if (n > 40) 2f else 4f)
        val max = context.dpf(10f)
        val mw = ((width - gap * (n - 1)) / n).coerceIn(context.dpf(2f), max)
        val r = mw / 2f
        var x = 0f
        for (c in colors) {
            paint.color = c
            rect.set(x, 0f, x + mw, height.toFloat())
            canvas.drawRoundRect(rect, r, r, paint)
            x += mw + gap
        }
    }
}

/**
 * Gateway round trips over time. Each probe is a point on the line; a probe
 * the router did not answer is a red mark on the floor, so a gap never looks
 * like a fast answer.
 */
class LatencyChart(context: Context, private val compact: Boolean) : View(context) {
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = context.dpf(2f)
        color = T.ACCENT
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL; color = 0x22FFB020 }
    private val grid = Paint().apply { color = T.EDGE; strokeWidth = 1f }
    private val lost = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = T.BAD }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = T.TEXT3
        textSize = context.dpf(11f)
        typeface = Fonts.regular
        fontFeatureSettings = "tnum"
    }
    private val path = Path()
    private val area = Path()
    private var samples: List<LatencySample> = emptyList()

    fun set(s: List<LatencySample>) {
        samples = if (s.size > 120) s.subList(s.size - 120, s.size) else s
        val ok = samples.filter { it.rttMs >= 0 }
        contentDescription = if (ok.isEmpty()) "No gateway readings yet"
        else "Gateway round trip, last ${ok.last().rttMs} milliseconds"
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), context.dp(if (compact) 56 else 150))
    }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat()
        val h = height.toFloat()
        val labelW = if (compact) 0f else text.measureText("000 ms") + context.dpf(8f)
        val top = context.dpf(6f)
        val floor = h - context.dpf(if (compact) 4f else 10f)
        val left = labelW
        val right = w - context.dpf(2f)

        val ok = samples.filter { it.rttMs >= 0 }
        if (samples.size < 2) {
            canvas.drawLine(left, floor, right, floor, grid)
            if (!compact) canvas.drawText("Collecting readings", left, floor - context.dpf(8f), text)
            return
        }
        var maxMs = 0L
        for (s in ok) if (s.rttMs > maxMs) maxMs = s.rttMs
        val scale = niceCeil(maxOf(maxMs, 5L))
        canvas.drawLine(left, floor, right, floor, grid)
        canvas.drawLine(left, top, right, top, grid)
        if (!compact) {
            canvas.drawText(String.format(Locale.US, "%d ms", scale), 0f, top + text.textSize * 0.35f, text)
            canvas.drawText("0", 0f, floor, text)
        }

        val t0 = samples.first().wallMs
        val span = (samples.last().wallMs - t0).coerceAtLeast(1L).toFloat()
        fun x(s: LatencySample) = left + (right - left) * ((s.wallMs - t0) / span)
        fun y(ms: Long) = floor - (floor - top) * (ms.toFloat() / scale)

        path.reset()
        area.reset()
        var started = false
        var firstX = 0f
        var lastX = 0f
        for (s in samples) {
            if (s.rttMs < 0) continue
            val px = x(s)
            val py = y(s.rttMs)
            if (!started) {
                path.moveTo(px, py); area.moveTo(px, floor); area.lineTo(px, py)
                firstX = px; started = true
            } else {
                path.lineTo(px, py); area.lineTo(px, py)
            }
            lastX = px
        }
        if (started) {
            area.lineTo(lastX, floor)
            area.lineTo(firstX, floor)
            area.close()
            canvas.drawPath(area, fill)
            canvas.drawPath(path, line)
        }
        val mark = context.dpf(3f)
        for (s in samples) {
            if (s.rttMs >= 0) continue
            val px = x(s)
            canvas.drawCircle(px, floor, mark, lost)
        }
    }

    private fun niceCeil(v: Long): Long {
        val steps = longArrayOf(5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000)
        for (s in steps) if (v <= s) return s
        return ((v + 999) / 1000) * 1000
    }
}
