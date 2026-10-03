package com.example.netmon.ui

import android.app.AlertDialog
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

object Fonts {
    val regular: Typeface = Typeface.create("sans-serif", Typeface.NORMAL)
    val medium: Typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    val light: Typeface = Typeface.create("sans-serif-light", Typeface.NORMAL)
}

const val MATCH = ViewGroup.LayoutParams.MATCH_PARENT
const val WRAP = ViewGroup.LayoutParams.WRAP_CONTENT

fun Context.dp(v: Int): Int = (v * resources.displayMetrics.density + 0.5f).toInt()
fun Context.dpf(v: Float): Float = v * resources.displayMetrics.density

fun Context.label(
    text: CharSequence = "",
    size: Float = 15f,
    color: Int = T.TEXT,
    font: Typeface = Fonts.regular,
    numbers: Boolean = false,
): TextView = TextView(this).apply {
    this.text = text
    setTextSize(TypedValue.COMPLEX_UNIT_SP, size)
    setTextColor(color)
    typeface = font
    // Tabular figures keep addresses and counts aligned without a monospace face.
    if (numbers) fontFeatureSettings = "tnum"
    setLineSpacing(0f, 1.12f)
}

fun Context.column(): LinearLayout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }

fun Context.row(): LinearLayout = LinearLayout(this).apply {
    orientation = LinearLayout.HORIZONTAL
    gravity = Gravity.CENTER_VERTICAL
}

/** Adds a child to a LinearLayout. Sizes are pixels (or MATCH / WRAP); margins are dp. */
fun <V : View> LinearLayout.add(
    v: V,
    w: Int = MATCH,
    h: Int = WRAP,
    weight: Float = 0f,
    top: Int = 0,
    bottom: Int = 0,
    start: Int = 0,
    end: Int = 0,
): V {
    val lp = LinearLayout.LayoutParams(w, h, weight)
    val c = context
    lp.setMargins(c.dp(start), c.dp(top), c.dp(end), c.dp(bottom))
    addView(v, lp)
    return v
}

fun <V : View> FrameLayout.addFull(v: V): V {
    addView(v, FrameLayout.LayoutParams(MATCH, MATCH))
    return v
}

fun rounded(color: Int, radius: Float, stroke: Int = 0, strokeColor: Int = 0): GradientDrawable =
    GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        cornerRadius = radius
        setColor(color)
        if (stroke > 0) setStroke(stroke, strokeColor)
    }

/** A touch ripple clipped to the shape of [base]. */
fun Context.pressable(base: Drawable?, radius: Float): Drawable =
    RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), base, rounded(0xFFFFFFFF.toInt(), radius))

fun Context.card(): LinearLayout = column().apply {
    background = rounded(T.SURFACE, dpf(20f))
    setPadding(dp(16), dp(16), dp(16), dp(16))
}

fun Context.cardTitle(text: String): TextView = label(text, 17f, T.TEXT, Fonts.medium)

fun Context.hint(text: CharSequence = ""): TextView = label(text, 13f, T.TEXT3)

fun Context.divider(): View = View(this).apply { setBackgroundColor(T.EDGE) }

fun LinearLayout.addDivider(top: Int = 0, bottom: Int = 0): View =
    add(context.divider(), MATCH, maxOf(1, context.dp(1) / 2), top = top, bottom = bottom)

enum class Btn { PRIMARY, SECONDARY, QUIET, DANGER }

private object ButtonRole : View.AccessibilityDelegate() {
    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(host, info)
        info.className = Button::class.java.name
    }
}

fun Context.button(text: String, kind: Btn = Btn.SECONDARY, onClick: (View) -> Unit): TextView {
    val r = dpf(12f)
    val (bg, fg) = when (kind) {
        Btn.PRIMARY -> rounded(T.ACCENT, r) to T.ON_ACCENT
        Btn.SECONDARY -> rounded(T.RAISED, r, dp(1), T.EDGE) to T.TEXT
        Btn.QUIET -> rounded(0, r) to T.ACCENT
        Btn.DANGER -> rounded(T.RAISED, r, dp(1), T.EDGE) to T.BAD
    }
    return label(text, 15f, fg, Fonts.medium).apply {
        gravity = Gravity.CENTER
        minHeight = dp(46)
        minWidth = dp(64)
        setPadding(dp(18), dp(10), dp(18), dp(10))
        background = pressable(bg, r)
        isClickable = true
        isFocusable = true
        setAccessibilityDelegate(ButtonRole)
        setOnClickListener(onClick)
    }
}

fun View.enabled(on: Boolean) {
    isEnabled = on
    alpha = if (on) 1f else 0.4f
}

/** A pill that can be selected. Colour and weight both change, so it never relies on colour alone. */
fun Context.chip(text: String, selected: Boolean, onClick: (View) -> Unit): TextView =
    label(text, 14f).apply {
        gravity = Gravity.CENTER
        minHeight = dp(36)
        setPadding(dp(14), dp(6), dp(14), dp(6))
        isClickable = true
        isFocusable = true
        setAccessibilityDelegate(ButtonRole)
        setOnClickListener(onClick)
        styleChip(selected)
    }

fun TextView.styleChip(selected: Boolean) {
    val c = context
    val r = c.dpf(18f)
    background = c.pressable(
        if (selected) rounded(T.ACCENT_TINT, r, c.dp(1), T.ACCENT) else rounded(T.RAISED, r),
        r,
    )
    setTextColor(if (selected) T.ACCENT else T.TEXT2)
    typeface = if (selected) Fonts.medium else Fonts.regular
    isSelected = selected
}

fun Context.input(hint: String, type: Int = InputType.TYPE_CLASS_TEXT): EditText = EditText(this).apply {
    this.hint = hint
    setHintTextColor(T.TEXT3)
    setTextColor(T.TEXT)
    setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
    // No setSingleLine(): it swaps the transformation method and would show a
    // password in clear. A text input type without the multi-line flag is
    // already single-line.
    inputType = type
    maxLines = 1
    minHeight = dp(50)
    setPadding(dp(14), dp(12), dp(14), dp(12))
    val r = dpf(12f)
    background = StateListDrawable().apply {
        addState(intArrayOf(android.R.attr.state_focused), rounded(T.RAISED, r, dp(2), T.ACCENT))
        addState(intArrayOf(), rounded(T.RAISED, r, dp(1), T.EDGE))
    }
}

fun Context.fieldLabel(text: String): TextView = label(text, 13f, T.TEXT2)

/** A key and value on one line; the value side is kept to update in place. */
class KV(context: Context, key: String) {
    val view: LinearLayout = context.row()
    private val k: TextView = context.label(key, 14f, T.TEXT2)
    val value: TextView = context.label("", 14f, T.TEXT, numbers = true)

    init {
        view.gravity = Gravity.TOP
        view.minimumHeight = context.dp(30)
        view.add(k, 0, WRAP, weight = 0.9f)
        view.add(value, 0, WRAP, weight = 1.3f, start = 12)
        value.gravity = Gravity.END
        value.setTextIsSelectable(true)
    }

    fun set(text: CharSequence?, color: Int = T.TEXT) {
        value.text = if (text.isNullOrEmpty()) "-" else text
        value.setTextColor(color)
    }

    fun show(on: Boolean) { view.visibility = if (on) View.VISIBLE else View.GONE }
}

fun LinearLayout.addKV(key: String, top: Int = 6): KV {
    val kv = KV(context, key)
    add(kv.view, top = top)
    return kv
}

/** The scrolling page every screen sits in. */
fun Context.page(content: LinearLayout): ScrollView = ScrollView(this).apply {
    isFillViewport = true
    isVerticalScrollBarEnabled = false
    // The page takes focus first, so arriving on a screen never opens the keyboard.
    content.isFocusableInTouchMode = true
    content.descendantFocusability = ViewGroup.FOCUS_BEFORE_DESCENDANTS
    content.setPadding(dp(16), dp(4), dp(16), dp(28))
    addView(content, FrameLayout.LayoutParams(MATCH, WRAP))
}

fun Context.dialog(title: String?, body: View?, positive: String?, onPositive: (() -> Unit)?, negative: String? = "Cancel"): AlertDialog? {
    // A reply can arrive after the screen that asked for it has closed; a
    // dialog shown then would throw BadTokenException.
    if (this is android.app.Activity && (isFinishing || isDestroyed)) return null
    val b = AlertDialog.Builder(this)
    if (title != null) b.setTitle(title)
    if (body != null) {
        val wrap = FrameLayout(this)
        wrap.setPadding(dp(24), dp(8), dp(24), dp(4))
        wrap.addView(body, FrameLayout.LayoutParams(MATCH, WRAP))
        b.setView(wrap)
    }
    if (positive != null) b.setPositiveButton(positive) { _, _ -> onPositive?.invoke() }
    if (negative != null) b.setNegativeButton(negative, null)
    val d = b.create()
    d.show()
    return d
}

/** Centres a view across its LinearLayout parent; call after adding it. */
fun <V : View> V.centred(): V {
    (layoutParams as? LinearLayout.LayoutParams)?.gravity = Gravity.CENTER_HORIZONTAL
    return this
}

/** Sets text only when it differs, so a screen refreshed every few seconds does not relayout for nothing. */
fun TextView.put(s: CharSequence) {
    if (text.toString() != s.toString()) text = s
}

fun TextView.put(s: CharSequence, color: Int) {
    put(s)
    if (currentTextColor != color) setTextColor(color)
}

fun View.shown(on: Boolean) {
    val v = if (on) View.VISIBLE else View.GONE
    if (visibility != v) visibility = v
}

/** A label and a switch on one line, in the app's colours. */
fun Context.switchRow(text: String, checked: Boolean, onChange: (Boolean) -> Unit): Pair<LinearLayout, android.widget.Switch> {
    val r = row()
    r.minimumHeight = dp(44)
    r.add(label(text, 15f, T.TEXT), 0, WRAP, weight = 1f)
    val sw = android.widget.Switch(this)
    sw.isChecked = checked
    sw.thumbTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(T.ACCENT, T.TEXT2))
    sw.trackTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(0x88FFB020.toInt(), T.EDGE))
    sw.contentDescription = text
    sw.setOnCheckedChangeListener { _, on -> onChange(on) }
    r.add(sw, WRAP, WRAP, start = 8)
    return r to sw
}

/**
 * Rows kept by key and moved, not rebuilt. A list that refreshes every few
 * seconds and re-sorts as signals move would otherwise be torn down under a
 * finger: a tap that straddled a refresh could miss, or land on the row that
 * had just moved there. Each row carries its own divider, shown from the
 * second row on.
 */
class KeyedRows<H : KeyedRows.Holder>(private val box: LinearLayout, private val create: () -> H) {

    abstract class Holder(context: Context) {
        val view: LinearLayout = context.column()
        val divider: View = context.divider()
        init {
            view.add(divider, MATCH, maxOf(1, context.dp(1) / 2))
        }
    }

    private val held = HashMap<String, H>()

    fun <X> show(items: List<X>, keyOf: (X) -> String, bind: (H, X) -> Unit) {
        val keep = HashSet<String>()
        for (x in items) {
            val k = keyOf(x)
            if (!keep.add(k)) continue
            val h = held.getOrPut(k) { create() }
            bind(h, x)
            h.divider.shown(keep.size > 1)
            val at = keep.size - 1
            if (box.getChildAt(at) !== h.view) {
                (h.view.parent as? ViewGroup)?.removeView(h.view)
                box.addView(h.view, at, LinearLayout.LayoutParams(MATCH, WRAP))
            }
        }
        while (box.childCount > keep.size) box.removeViewAt(box.childCount - 1)
        held.keys.retainAll(keep)
    }

    fun clear() {
        box.removeAllViews()
        held.clear()
    }
}
