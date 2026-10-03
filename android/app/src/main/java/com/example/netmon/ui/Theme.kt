package com.example.netmon.ui

import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

// Colours only, no Android types, so the drawing code can also be checked on
// a plain JVM.

/**
 * The look, in one place. Pure black canvas, charcoal surfaces, a white-to-grey
 * text ladder and one amber accent for whatever can be pressed. Status colours
 * match the board's own web pages, except "private", which moves off amber so
 * it never reads as something to tap.
 */
object T {
    const val BG = 0xFF000000.toInt()
    const val SURFACE = 0xFF141414.toInt()
    const val RAISED = 0xFF1F1F1F.toInt()
    const val EDGE = 0xFF2A2A2A.toInt()
    const val TEXT = 0xFFF2F2F2.toInt()
    const val TEXT2 = 0xFFA6A6A6.toInt()
    const val TEXT3 = 0xFF6E6E6E.toInt()
    const val ACCENT = 0xFFFFB020.toInt()
    const val ACCENT_TINT = 0xFF2B2110.toInt()
    const val ON_ACCENT = 0xFF1A1200.toInt()
    const val OK = 0xFF4FD18B.toInt()
    const val BAD = 0xFFF0605F.toInt()
    const val WARN = 0xFFF0924A.toInt()
    const val PRIVATE = 0xFF8AB4F8.toInt()
    const val OFFLINE = 0xFF555555.toInt()

    fun status(status: String, online: Boolean): Int = when {
        !online -> OFFLINE
        status == "unknown" -> BAD
        status == "private" -> PRIVATE
        else -> OK
    }
}

/**
 * The radars, the Finder and the map, in the dark colours of the board's own
 * Nearby and Map pages, so a device is the same colour in the app and in the
 * browser.
 */
object Pal {
    const val SCOPE = 0xFF111920.toInt()     // a radar's face, the map's ground
    const val RING = 0xFF2B3947.toInt()
    const val LINE = 0xFF34475A.toInt()      // the map's links
    const val BEAM = T.OK
    const val WIFI = 0xFF5BC8D6.toInt()
    const val PERSONAL = 0xFF3987E5.toInt()  // phones, computers, watches, earbuds
    const val TRACKERS = 0xFFD95926.toInt()  // AirTags, Tiles, Flipper Zero
    const val THINGS = 0xFF199E70.toInt()    // home and things
    const val UNKNOWN = 0xFF7D8A96.toInt()   // not identified
    const val ME = T.TEXT                    // this board, on the map

    /** Bluetooth kind group (see Air.group) to colour. */
    fun group(g: String): Int = when (g) {
        "p" -> PERSONAL
        "t" -> TRACKERS
        "h" -> THINGS
        else -> UNKNOWN
    }

    // Cold to hot, the Finder's colour for how close a device is.
    private val HEAT = intArrayOf(
        0xFF5B9BE6.toInt(), 0xFF2BB5C4.toInt(), 0xFF43C27A.toInt(),
        0xFFE0BB38.toInt(), 0xFFF08447.toInt(), 0xFFF2585C.toInt(),
    )

    /**
     * 0 is cold (far), 1 is hot (here). Between two stops by hue rather than by
     * red, green and blue, which would turn green and gold into olive: the same
     * blend as the web page.
     */
    fun heat(v: Double): Int {
        val x = max(0.0, min(1.0, if (v.isNaN()) 0.0 else v)) * 5
        val i = min(4, floor(x).toInt())
        val f = x - i
        val a = hsl(HEAT[i])
        val b = hsl(HEAT[i + 1])
        val dh = ((b[0] - a[0] + 540) % 360) - 180
        val h = ((a[0] + dh * f) % 360 + 360) % 360
        return fromHsl(h, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f)
    }

    fun withAlpha(color: Int, alpha: Double): Int {
        val a = (max(0.0, min(1.0, alpha)) * 255 + 0.5).toInt()
        return (color and 0x00FFFFFF) or (a shl 24)
    }

    internal fun hsl(c: Int): DoubleArray {
        val r = ((c shr 16) and 255) / 255.0
        val g = ((c shr 8) and 255) / 255.0
        val b = (c and 255) / 255.0
        val mx = max(r, max(g, b))
        val mn = min(r, min(g, b))
        val l = (mx + mn) / 2
        val d = mx - mn
        var h = 0.0
        var s = 0.0
        if (d > 0) {
            s = d / (1 - abs(2 * l - 1))
            h = when (mx) {
                r -> ((g - b) / d + 6) % 6
                g -> (b - r) / d + 2
                else -> (r - g) / d + 4
            }
        }
        return doubleArrayOf(h * 60, s, l)
    }

    internal fun fromHsl(h: Double, s: Double, l: Double): Int {
        val c = (1 - abs(2 * l - 1)) * s
        val hp = h / 60
        val x = c * (1 - abs(hp % 2 - 1))
        val (r1, g1, b1) = when {
            hp < 1 -> Triple(c, x, 0.0)
            hp < 2 -> Triple(x, c, 0.0)
            hp < 3 -> Triple(0.0, c, x)
            hp < 4 -> Triple(0.0, x, c)
            hp < 5 -> Triple(x, 0.0, c)
            else -> Triple(c, 0.0, x)
        }
        val m = l - c / 2
        fun ch(v: Double) = max(0, min(255, ((v + m) * 255 + 0.5).toInt()))
        return (0xFF shl 24) or (ch(r1) shl 16) or (ch(g1) shl 8) or ch(b1)
    }
}
