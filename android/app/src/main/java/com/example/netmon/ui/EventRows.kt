package com.example.netmon.ui

import android.content.Context
import android.view.Gravity
import android.widget.LinearLayout
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.EventHistory
import com.example.netmon.Format
import com.example.netmon.LoggedEvent
import java.text.DateFormat
import java.util.Calendar
import java.util.Date

/** How one history entry is drawn, shared by the Overview and Events screens. */
object EventRows {

    fun color(type: String): Int = when (type) {
        "back" -> T.OK
        "offline" -> T.WARN
        "hostname" -> T.PRIVATE
        EventHistory.TYPE_RESTART -> T.WARN
        else -> T.TEXT
    }

    /** The device an entry is about, by the best name available now. */
    fun subject(e: LoggedEvent): String {
        if (e.type == EventHistory.TYPE_RESTART) return "Event list started over"
        if (e.type == "hostname" && e.text.isNotBlank()) return "Now called ${e.text}"
        val live = Board.devices?.firstOrNull { it.mac == e.mac }
        if (live != null) return Format.deviceName(live)
        return AppState.nameFor(e.mac) ?: e.mac.ifEmpty { "Unknown device" }
    }

    fun detail(e: LoggedEvent): String = when {
        e.type == EventHistory.TYPE_RESTART -> "Power cut, update or restart. The board forgets its event list when it starts."
        e.ip.isNotBlank() && e.mac.isNotBlank() -> "${e.ip}   ${e.mac}"
        else -> e.mac
    }

    private val timeFormat: DateFormat get() = DateFormat.getTimeInstance(DateFormat.SHORT)

    fun clock(ms: Long): String = timeFormat.format(Date(ms))

    /** "Today", "Yesterday" or a date, for grouping. */
    fun day(ms: Long, now: Long = System.currentTimeMillis()): String {
        val c = Calendar.getInstance().apply { timeInMillis = now }
        val e = Calendar.getInstance().apply { timeInMillis = ms }
        val sameYear = c.get(Calendar.YEAR) == e.get(Calendar.YEAR)
        val dc = c.get(Calendar.DAY_OF_YEAR)
        val de = e.get(Calendar.DAY_OF_YEAR)
        return when {
            sameYear && dc == de -> "Today"
            sameYear && dc - de == 1 -> "Yesterday"
            else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(ms))
        }
    }

    /** A two-line row: what happened and to whom, with the time at the end. */
    fun build(context: Context, e: LoggedEvent, relative: Boolean): LinearLayout {
        val r = context.row()
        r.gravity = Gravity.TOP
        r.setPadding(0, context.dp(10), 0, context.dp(10))
        val left = context.column()
        val head = context.label(Format.eventTitle(e.type), 14f, color(e.type), Fonts.medium)
        val who = context.label(subject(e), 15f, T.TEXT)
        val info = context.label(detail(e), 12.5f, T.TEXT3, numbers = true)
        left.add(head)
        left.add(who, top = 2)
        if (info.text.isNotEmpty()) left.add(info, top = 2)
        r.add(left, 0, WRAP, weight = 1f)
        val secs = (System.currentTimeMillis() - e.wallMs) / 1000
        val time = context.label(if (relative) Format.ago(secs) else clock(e.wallMs), 13f, T.TEXT2, numbers = true)
        time.gravity = Gravity.END
        r.add(time, WRAP, WRAP, start = 12)
        r.contentDescription = "${Format.eventTitle(e.type)}, ${subject(e)}, ${time.text}"
        return r
    }
}
