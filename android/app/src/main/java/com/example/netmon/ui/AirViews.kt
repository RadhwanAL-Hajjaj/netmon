package com.example.netmon.ui

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.view.MotionEvent
import android.view.View
import com.example.netmon.MapInfo
import com.example.netmon.NetMap
import com.example.netmon.Device
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

// Thin views around the painters: size, touch and animation. The drawing
// itself is in Painters.kt.

/** Whether to animate at all: Android's "Remove animations" switches the beams off. */
fun motionOn(): Boolean = ValueAnimator.areAnimatorsEnabled()

/** A square radar of Wi-Fi networks or Bluetooth devices. Tap a dot to pick it. */
class RadarView(context: Context) : View(context) {
    private val painter = RadarPainter(context.resources.displayMetrics.density)
    var dots: List<RadarDot> = emptyList()
        set(v) { field = v; invalidate() }
    val born = HashMap<String, Long>()
    val ghosts = ArrayList<RadarGhost>()
    var scaleM = 30
        set(v) { field = v; invalidate() }
    var selected: String? = null
        set(v) { field = v; invalidate() }
    var onPick: ((String?) -> Unit)? = null
    private var downX = 0f
    private var downY = 0f

    init {
        isClickable = true
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = min(MeasureSpec.getSize(widthMeasureSpec), context.dp(420))
        setMeasuredDimension(w, w)
    }

    override fun onDraw(canvas: Canvas) {
        val now = System.currentTimeMillis()
        val anim = motionOn()
        val sweep = if (anim) now / 4200.0 * 2 * Math.PI % (2 * Math.PI) else null
        ghosts.removeAll { now - it.atMs > 4000 }
        val stale = born.entries.filter { now - it.value > 6000 }.map { it.key }
        for (k in stale) born.remove(k)
        canvas.translate(((width - height) / 2).toFloat().coerceAtLeast(0f), 0f)
        painter.draw(canvas, min(width, height).toFloat(), dots, ghosts, born, scaleM, selected, now, sweep)
        if (anim && isShown) postInvalidateOnAnimation()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; downY = e.y; return true }
            MotionEvent.ACTION_UP -> {
                if (Math.hypot((e.x - downX).toDouble(), (e.y - downY).toDouble()) > context.dpf(12f)) return true
                performClick()
                onPick?.invoke(nearest(e.x, e.y))
                return true
            }
        }
        return super.onTouchEvent(e)
    }

    override fun performClick(): Boolean = super.performClick()

    /** The dot nearest a tap, within reach, among those the search shows. */
    private fun nearest(x: Float, y: Float): String? {
        val size = min(width, height).toFloat()
        val ox = ((width - height) / 2).toFloat().coerceAtLeast(0f)
        val c = size / 2
        val r = c - context.dpf(12f)
        var best: String? = null
        var bd = context.dpf(24f).let { it * it }
        for (p in dots) {
            if (!p.on) continue
            val px = ox + c + (p.r * r * cos(p.a)).toFloat()
            val py = c + (p.r * r * sin(p.a)).toFloat()
            val d = (px - x) * (px - x) + (py - y) * (py - y)
            if (d < bd) { bd = d; best = p.key }
        }
        return best
    }
}

/** The Finder's radar: you in the middle, the distance as a ring, the direction after a turn. */
class FinderRadarView(context: Context) : View(context) {
    private val painter = FinderPainter(context.resources.displayMetrics.density)
    var pic: FinderPic? = null
        set(v) { field = v; invalidate() }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = min(MeasureSpec.getSize(widthMeasureSpec), context.dp(340))
        setMeasuredDimension(w, w)
    }

    override fun onDraw(canvas: Canvas) {
        val p = pic ?: return
        painter.draw(canvas, min(width, height).toFloat(), p, System.currentTimeMillis())
        if ((p.beam || p.found) && motionOn() && isShown) postInvalidateOnAnimation()
    }
}

/** The last minute of signal from the device being found. */
class TraceView(context: Context) : View(context) {
    private val painter = TracePainter(context.resources.displayMetrics.density)
    private var t = LongArray(0)
    private var r = IntArray(0)
    private var e = DoubleArray(0)

    fun set(times: LongArray, raw: IntArray, smooth: DoubleArray) {
        t = times; r = raw; e = smooth
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), context.dp(84))
    }

    override fun onDraw(canvas: Canvas) {
        painter.draw(canvas, width.toFloat(), height.toFloat(), t, r, e, System.currentTimeMillis())
    }
}

/** Far to here, with a mark for how close the device is. */
class HeatBarView(context: Context) : View(context) {
    private val painter = HeatBarPainter(context.resources.displayMetrics.density)
    var value: Double? = null
        set(v) { field = v; invalidate() }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), context.dp(20))
    }

    override fun onDraw(canvas: Canvas) = painter.draw(canvas, width.toFloat(), height.toFloat(), value)
}

/**
 * The network map. Lays itself out for its width, as tall as that needs, and
 * reports taps on dots, groups, the router and the internet.
 */
class MapView(context: Context) : View(context) {
    private val density = context.resources.displayMetrics.density
    private val painter = MapPainter(density)
    private var info: MapInfo? = null
    private var devices: List<Device> = emptyList()
    private var byStatus = false
    private var offline = false
    var plan: NetMap.Plan? = null
        private set
    private var planWidth = -1
    var sel: MapSel? = null
        set(v) { field = v; invalidate() }
    var onTap: ((NetMap.Hit?) -> Unit)? = null
    private var downX = 0f
    private var downY = 0f

    init {
        isClickable = true
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
    }

    fun set(info: MapInfo?, devices: List<Device>, byStatus: Boolean, offline: Boolean) {
        this.info = info
        this.devices = devices
        this.byStatus = byStatus
        this.offline = offline
        planWidth = -1
        // Planned now, not at the next layout: the panel under the map reads
        // the plan straight after this (a group's devices, whether the
        // selection is still there).
        if (width > 0) replan(width)
        requestLayout()
        invalidate()
    }

    private fun replan(widthPx: Int) {
        val i = info
        plan = if (i == null || widthPx <= 0) null else NetMap.plan(i, devices, byStatus, offline, widthPx / density.toDouble())
        planWidth = widthPx
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        if (w != planWidth) replan(w)
        val p = plan
        val h = if (p == null) context.dp(120) else (p.box[3] * p.scale * density).toInt() + 1
        setMeasuredDimension(w, h)
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Pal.SCOPE)
        val i = info ?: return
        if (width != planWidth) replan(width)
        val p = plan
        if (p == null) {
            val t = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
            t.color = T.TEXT2
            t.textSize = 13 * density
            t.textAlign = android.graphics.Paint.Align.CENTER
            val msg = if (i.wifi == "softap") "In setup mode: no network to map yet." else "Joining the network…"
            canvas.drawText(msg, width / 2f, height / 2f, t)
            return
        }
        painter.draw(canvas, p, i, sel, (p.scale * density).toFloat())
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; downY = e.y; return true }
            MotionEvent.ACTION_UP -> {
                if (Math.hypot((e.x - downX).toDouble(), (e.y - downY).toDouble()) > context.dpf(12f)) return true
                performClick()
                val p = plan ?: return true
                val k = p.scale * density
                val x = p.box[0] + e.x / k
                val y = p.box[1] + e.y / k
                // Dots are small and packed, so a tap goes to the nearest within reach.
                onTap?.invoke(NetMap.hit(p, x, y, 22 / p.scale))
                return true
            }
        }
        return super.onTouchEvent(e)
    }

    override fun performClick(): Boolean = super.performClick()
}
