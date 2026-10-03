// A stand-in for the parts of android.graphics the painters use, drawn with
// java.awt, so the app's own drawing code can be rendered to PNG off the phone.
package android.graphics

import java.awt.BasicStroke
import java.awt.Font
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.geom.AffineTransform
import java.awt.geom.Arc2D
import java.awt.geom.Ellipse2D
import java.awt.geom.GeneralPath
import java.awt.geom.Line2D
import java.awt.geom.Rectangle2D
import java.awt.geom.RoundRectangle2D
import java.awt.image.BufferedImage

open class PathEffect
class DashPathEffect(val intervals: FloatArray, val phase: Float) : PathEffect()

class Typeface private constructor(val family: String) {
    companion object {
        const val NORMAL = 0
        @JvmStatic fun create(family: String, style: Int): Typeface = Typeface(family)
    }
}

class RectF {
    @JvmField var left = 0f
    @JvmField var top = 0f
    @JvmField var right = 0f
    @JvmField var bottom = 0f
    fun set(l: Float, t: Float, r: Float, b: Float) { left = l; top = t; right = r; bottom = b }
}

class Path {
    val gp = GeneralPath()
    fun reset() = gp.reset()
    fun moveTo(x: Float, y: Float) = gp.moveTo(x, y)
    fun lineTo(x: Float, y: Float) = gp.lineTo(x, y)
    fun cubicTo(x1: Float, y1: Float, x2: Float, y2: Float, x3: Float, y3: Float) = gp.curveTo(x1, y1, x2, y2, x3, y3)
    fun close() = gp.closePath()
}

class Paint(flags: Int = 0) {
    enum class Style { FILL, STROKE }
    enum class Cap { BUTT, ROUND, SQUARE }
    enum class Join { MITER, ROUND, BEVEL }
    enum class Align { LEFT, CENTER, RIGHT }
    companion object { const val ANTI_ALIAS_FLAG = 1 }

    var style = Style.FILL
    var color: Int = 0xFF000000.toInt()
    var alpha: Int
        get() = (color ushr 24) and 255
        set(v) { color = (color and 0x00FFFFFF) or ((v and 255) shl 24) }
    var strokeWidth = 0f
    var strokeCap = Cap.BUTT
    var strokeJoin = Join.MITER
    var textSize = 12f
    var typeface: Typeface? = null
    var textAlign = Align.LEFT
    var pathEffect: PathEffect? = null
    var fontFeatureSettings: String? = null

    fun font(): Font {
        val fam = typeface?.family ?: "sans-serif"
        val st = if (fam.contains("medium")) Font.BOLD else Font.PLAIN
        return Font("DejaVu Sans", st, 1).deriveFont(textSize * 0.94f)
    }

    private val scratch = BufferedImage(1, 1, BufferedImage.TYPE_INT_ARGB).createGraphics()
    fun measureText(s: String): Float = scratch.getFontMetrics(font()).getStringBounds(s, scratch).width.toFloat()
    fun ascent(): Float = -scratch.getFontMetrics(font()).ascent.toFloat()
}

class Canvas(private val g: Graphics2D, private val w: Int, private val h: Int) {
    private val stack = ArrayList<AffineTransform>()

    init {
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON)
        g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON)
        g.setRenderingHint(RenderingHints.KEY_STROKE_CONTROL, RenderingHints.VALUE_STROKE_PURE)
    }

    private fun use(p: Paint) {
        val c = p.color
        g.color = java.awt.Color((c shr 16) and 255, (c shr 8) and 255, c and 255, (c ushr 24) and 255)
        val cap = when (p.strokeCap) { Paint.Cap.ROUND -> BasicStroke.CAP_ROUND; Paint.Cap.SQUARE -> BasicStroke.CAP_SQUARE; else -> BasicStroke.CAP_BUTT }
        val join = when (p.strokeJoin) { Paint.Join.ROUND -> BasicStroke.JOIN_ROUND; Paint.Join.BEVEL -> BasicStroke.JOIN_BEVEL; else -> BasicStroke.JOIN_MITER }
        val d = (p.pathEffect as? DashPathEffect)
        g.stroke = if (d != null) BasicStroke(maxOf(p.strokeWidth, 1f), cap, join, 10f, d.intervals, d.phase)
        else BasicStroke(maxOf(p.strokeWidth, if (p.strokeWidth == 0f) 1f else p.strokeWidth), cap, join)
    }

    private fun paint(s: java.awt.Shape, p: Paint) {
        use(p)
        if (p.style == Paint.Style.FILL) g.fill(s) else g.draw(s)
    }

    fun save(): Int { stack.add(g.transform); return stack.size }
    fun restore() { g.transform = stack.removeAt(stack.size - 1) }
    fun scale(sx: Float, sy: Float) = g.scale(sx.toDouble(), sy.toDouble())
    fun translate(dx: Float, dy: Float) = g.translate(dx.toDouble(), dy.toDouble())
    fun drawColor(c: Int) {
        val t = g.transform
        g.transform = AffineTransform()
        g.color = java.awt.Color((c shr 16) and 255, (c shr 8) and 255, c and 255, (c ushr 24) and 255)
        g.fillRect(0, 0, w, h)
        g.transform = t
    }
    fun drawCircle(cx: Float, cy: Float, r: Float, p: Paint) = paint(Ellipse2D.Float(cx - r, cy - r, 2 * r, 2 * r), p)
    fun drawLine(x0: Float, y0: Float, x1: Float, y1: Float, p: Paint) { use(p); g.draw(Line2D.Float(x0, y0, x1, y1)) }
    fun drawRect(r: RectF, p: Paint) = paint(Rectangle2D.Float(r.left, r.top, r.right - r.left, r.bottom - r.top), p)
    fun drawRoundRect(r: RectF, rx: Float, ry: Float, p: Paint) =
        paint(RoundRectangle2D.Float(r.left, r.top, r.right - r.left, r.bottom - r.top, 2 * rx, 2 * ry), p)
    fun drawArc(r: RectF, start: Float, sweep: Float, useCenter: Boolean, p: Paint) =
        paint(Arc2D.Float(r.left, r.top, r.right - r.left, r.bottom - r.top, -start, -sweep, if (useCenter) Arc2D.PIE else Arc2D.OPEN), p)
    fun drawPath(path: Path, p: Paint) = paint(path.gp, p)
    fun drawText(s: String, x: Float, y: Float, p: Paint) {
        use(p)
        g.font = p.font()
        val w = g.fontMetrics.stringWidth(s).toFloat()
        val dx = when (p.textAlign) { Paint.Align.CENTER -> -w / 2; Paint.Align.RIGHT -> -w; else -> 0f }
        if (p.style == Paint.Style.STROKE) {
            val gv = g.font.createGlyphVector(g.fontRenderContext, s)
            val outline = gv.getOutline(x + dx, y)
            g.draw(outline)
        } else g.drawString(s, x + dx, y)
    }
}
