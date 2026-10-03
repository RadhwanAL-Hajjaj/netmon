package com.example.netmon.ui

import android.view.View
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.EventHistory
import com.example.netmon.LoggedEvent
import com.example.netmon.MainActivity

class EventsScreen(host: MainActivity) : Screen(host) {

    override val parts = listOf(Board.Part.EVENTS, Board.Part.DEVICES)

    private enum class Filter(val key: String, val title: String) {
        ALL("all", "All"), JOINS("joins", "Joined"), OFFLINE("offline", "Went offline"),
        NAMES("names", "Names"), RESTARTS("restarts", "Restarts");

        fun accepts(e: LoggedEvent): Boolean = when (this) {
            ALL -> true
            JOINS -> e.type == "seen" || e.type == "back"
            OFFLINE -> e.type == "offline"
            NAMES -> e.type == "hostname"
            RESTARTS -> e.type == EventHistory.TYPE_RESTART
        }
    }

    private var filter = Filter.values().firstOrNull { it.key == AppState.prefs.eventFilter } ?: Filter.ALL
    private val chips = HashMap<Filter, TextView>()
    private lateinit var summary: TextView
    private lateinit var list: LinearLayout
    private lateinit var empty: TextView
    private lateinit var more: TextView
    private var limit = PAGE
    private var renderedKey = ""

    override fun build(): View {
        val c = ctx
        val col = c.column()

        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val chipRow = c.row()
        scroller.addView(chipRow, FrameLayout.LayoutParams(WRAP, WRAP))
        for (f in Filter.values()) {
            val chip = c.chip(f.title, f == filter) {
                filter = f
                AppState.prefs.eventFilter = f.key
                limit = PAGE
                for ((k, v) in chips) v.styleChip(k == filter)
                render()
            }
            chips[f] = chip
            chipRow.add(chip, WRAP, WRAP, end = 8)
        }
        col.add(scroller, top = 8)

        val top = col.add(c.row(), top = 10)
        summary = top.add(c.label("", 13f, T.TEXT2), 0, WRAP, weight = 1f)
        top.add(c.button("Clear", Btn.QUIET) { confirmClear() }, WRAP, WRAP)

        list = col.add(c.column())
        empty = col.add(c.label("", 14f, T.TEXT2), top = 24)
        more = col.add(c.button("Show older events", Btn.SECONDARY) {
            limit += PAGE
            render()
        }, WRAP, WRAP, top = 16)

        col.add(c.hint("The monitor keeps only its last 48 events, and forgets them when it restarts. " +
            "This phone copies them whenever the app or a background check reads the board, and keeps up to 600. " +
            "Sweep start and finish entries are left out."), top = 20)
        return c.page(col)
    }

    private fun confirmClear() {
        ctx.dialog("Clear the event history?", ctx.label("This removes the history kept on this phone. The monitor's own list is not affected.", 15f, T.TEXT2),
            "Clear", {
                AppState.clearHistory()
                renderedKey = ""
                render()
            })
    }

    override fun render() {
        if (!::list.isInitialized) return
        val all = AppState.historySnapshot()
        val shown = all.filter { filter.accepts(it) }
        summary.text = when {
            all.isEmpty() -> ""
            filter == Filter.ALL -> "${all.size} events kept on this phone"
            else -> "${shown.size} of ${all.size} events"
        }
        val page = shown.take(limit)
        // Relative times age, so the key includes the minute.
        val key = page.joinToString { it.key } + "|" + filter + "|" + (System.currentTimeMillis() / 60_000) +
            "|" + (Board.devices?.size ?: -1)
        if (key != renderedKey) {
            renderedKey = key
            list.removeAllViews()
            var day = ""
            var box: LinearLayout? = null
            for (e in page) {
                val d = EventRows.day(e.wallMs)
                if (d != day || box == null) {
                    day = d
                    list.add(ctx.label(d, 13f, T.TEXT2, Fonts.medium), top = 18, bottom = 6)
                    box = list.add(ctx.card())
                    box.setPadding(ctx.dp(16), ctx.dp(2), ctx.dp(16), ctx.dp(2))
                } else {
                    box.addDivider()
                }
                box.add(EventRows.build(ctx, e, relative = false))
            }
        }
        more.visibility = if (shown.size > page.size) View.VISIBLE else View.GONE
        empty.visibility = if (page.isEmpty()) View.VISIBLE else View.GONE
        empty.text = when {
            all.isEmpty() && Board.link == Board.Link.LOST -> "No events yet, and the monitor is not answering."
            all.isEmpty() -> "No events yet. The monitor logs devices joining, leaving and naming themselves."
            else -> "No events of this kind yet."
        }
    }

    companion object {
        private const val PAGE = 120
    }
}
