package com.example.netmon.ui

import android.app.AlertDialog
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.example.netmon.ApiException
import com.example.netmon.Board
import com.example.netmon.Format
import com.example.netmon.MainActivity
import com.example.netmon.Report
import com.example.netmon.ReportDevice
import com.example.netmon.ReportFiles
import com.example.netmon.ReportInfo

/**
 * Settings, Saved reports: the device list of each network the board has
 * been on, kept on the board (firmware 0.12). Read here over Wi-Fi or
 * Bluetooth, looked through on the phone, and exported as CSV or JSON.
 */
class ReportsCard(private val host: MainActivity) {

    private lateinit var list: LinearLayout
    private lateinit var note: TextView
    private lateinit var saveBtn: TextView
    private lateinit var msg: TextView
    private var key = ""
    private var busy = false

    fun build(): View {
        val c = host
        val card = c.card()
        card.add(c.cardTitle("Saved reports"))
        card.add(c.hint("For each network the monitor has been on, up to four, its device list as it last stood: saved " +
            "every 15 minutes, before every restart, and when you ask. Tap one to look through it or export it."), top = 6)
        list = card.add(c.column(), top = 6)
        note = card.add(c.label("", 13f, T.TEXT2), top = 8)
        saveBtn = card.add(c.button("Save this network now", Btn.SECONDARY) { saveNow() }, WRAP, WRAP, top = 12)
        msg = card.add(c.label("", 14f, T.TEXT2), top = 8)
        msg.visibility = View.GONE
        return card
    }

    private fun say(text: String, color: Int = T.TEXT2) {
        msg.text = text
        msg.setTextColor(color)
        msg.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    fun render() {
        if (!::list.isInitialized) return
        val l = Board.reports
        if (l == null) {
            list.removeAllViews()
            key = ""
            note.put(when {
                Board.missing(Board.Part.REPORTS) -> "Saved reports need firmware 0.12 or later on the monitor."
                else -> Board.partErrors[Board.Part.REPORTS] ?: "Reading"
            })
            saveBtn.shown(false)
            return
        }
        val items = ReportFiles.ordered(l.reports)
        val k = items.joinToString("|") { "${it.slot},${it.ssid},${it.subnet},${it.count},${it.online},${it.savedUnix},${it.ageS / 60},${it.current}" }
        if (k != key) {
            key = k
            list.removeAllViews()
            items.forEachIndexed { i, r ->
                if (i > 0) list.addDivider()
                list.add(row(r))
            }
        }
        note.put(buildString {
            if (items.isEmpty()) append("No report saved yet. ")
            append(if (l.networkSsid.isNotEmpty()) "The monitor is on ${l.networkSsid}." else "The monitor is not on a network now.")
            append(" Room for ${l.max}; a new network replaces the one saved longest ago.")
            if (!l.clock) append(" The monitor does not know the time yet, so its reports say how long before the save a device was seen, not when.")
        })
        saveBtn.shown(true)
        saveBtn.enabled(!busy && l.networkSsid.isNotEmpty())
    }

    private fun row(r: ReportInfo): View {
        val c = host
        val row = c.row()
        row.gravity = Gravity.TOP
        row.setPadding(c.dp(2), c.dp(12), c.dp(2), c.dp(12))
        row.background = c.pressable(null, c.dpf(10f))
        row.isClickable = true
        val col = c.column()
        val top = col.add(c.row())
        top.add(c.label(r.ssid, 16f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f)
        if (r.current) {
            val pill = c.label("On it now", 12f, T.OK, Fonts.medium)
            pill.background = rounded(0, c.dpf(10f), c.dp(1), T.OK)
            pill.setPadding(c.dp(8), c.dp(2), c.dp(8), c.dp(2))
            top.add(pill, WRAP, WRAP, start = 8)
        }
        col.add(c.label("${r.subnet}" + if (r.gateway.isNotEmpty()) " · router ${r.gateway}" else "", 13f, T.TEXT2, numbers = true), top = 2)
        col.add(c.label("${r.count} devices, ${r.online} online · ${ReportFiles.savedText(r)}", 13f, T.TEXT2), top = 4)
        row.add(col, 0, WRAP, weight = 1f)
        row.add(c.label("›", 22f, T.TEXT3), WRAP, WRAP, start = 8)
        row.contentDescription = "${r.ssid}, ${r.count} devices. Open the report."
        row.setOnClickListener { open(r) }
        return row
    }

    private fun saveNow() {
        if (busy) return
        busy = true
        say("Saving")
        render()
        Board.run({ it.saveReport() }) { l, err ->
            busy = false
            if (l != null) {
                Board.setReports(l)
                say("Saved.", T.OK)
            } else {
                say(err?.message ?: "The monitor did not save a report.", T.BAD)
            }
            render()
        }
    }

    // --- one report -----------------------------------------------------------------

    private fun open(info: ReportInfo) {
        say(if (Board.onBluetooth) "Reading the report over Bluetooth" else "Reading the report")
        Board.run({ it.report(info.slot) }) { r, err ->
            if (r == null) {
                say(err?.message ?: "Could not read that report.", T.BAD)
                if (err?.status == 404) Board.refresh(Board.Part.REPORTS)
                return@run
            }
            say("")
            show(info, r)
        }
    }

    private enum class Filter(val title: String) { ALL("All"), ONLINE("Online"), UNKNOWN("Not recognised"), KEPT("Kept from before") }

    private fun show(info: ReportInfo, r: Report) {
        val c = host
        val box = c.column()
        box.add(c.label(buildString {
            append(r.subnet)
            if (r.gateway.isNotEmpty()) append(" · router ${r.gateway}")
            if (r.gatewayMac.isNotEmpty()) append(" (${r.gatewayMac})")
        }, 14f, T.TEXT2, numbers = true))
        val dated = if (r.savedUnix > 0) "Saved ${ReportFiles.time(r.savedUnix)}" else "Saved at a time the monitor did not know"
        box.add(c.label("$dated, by firmware ${r.version}.", 14f, T.TEXT2), top = 4)
        val online = r.devices.count { it.device.online }
        val unknown = r.devices.count { it.device.isUnknown }
        val kept = r.devices.count { it.carried }
        box.add(c.label("${r.devices.size} devices: $online online, $unknown not recognised" +
            if (kept > 0) ", $kept kept from before the last restart." else ".", 14f, T.TEXT), top = 6)
        if (r.learning) box.add(c.hint("Saved while the monitor was still learning, so new devices then counted as recognised."), top = 4)

        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val chips = c.row()
        scroller.addView(chips, FrameLayout.LayoutParams(WRAP, WRAP))
        box.add(scroller, top = 12)
        val rows = box.add(c.column(), top = 4)
        val chipViews = HashMap<Filter, TextView>()
        fun fill(f: Filter) {
            for ((k, v) in chipViews) v.styleChip(k == f)
            rows.removeAllViews()
            val shown = r.devices.filter {
                when (f) {
                    Filter.ALL -> true
                    Filter.ONLINE -> it.device.online
                    Filter.UNKNOWN -> it.device.isUnknown
                    Filter.KEPT -> it.carried
                }
            }.sortedWith(Comparator { a, b ->
                when {
                    a.device.online != b.device.online -> if (a.device.online) -1 else 1
                    a.carried != b.carried -> if (a.carried) 1 else -1
                    else -> Format.ipKey(a.device.ip).compareTo(Format.ipKey(b.device.ip))
                }
            })
            if (shown.isEmpty()) rows.add(c.label("None.", 14f, T.TEXT2), top = 10)
            shown.forEachIndexed { i, d ->
                if (i > 0) rows.addDivider()
                rows.add(deviceRow(d))
            }
        }
        for (f in Filter.values()) {
            if (f == Filter.KEPT && kept == 0) continue
            val chip = c.chip(f.title, f == Filter.ALL) { fill(f) }
            chipViews[f] = chip
            chips.add(chip, WRAP, WRAP, end = 8)
        }
        fill(Filter.ALL)

        val del = box.add(c.button("Delete this report", Btn.DANGER) {}, WRAP, WRAP, top = 16)

        val scroll = ScrollView(c)
        scroll.addView(box, FrameLayout.LayoutParams(MATCH, WRAP))
        val wrap = FrameLayout(c)
        wrap.setPadding(c.dp(24), c.dp(8), c.dp(24), c.dp(4))
        wrap.addView(scroll, FrameLayout.LayoutParams(MATCH, WRAP))
        val d = AlertDialog.Builder(c)
            .setTitle(r.ssid)
            .setView(wrap)
            .setPositiveButton("Export CSV") { _, _ -> export(r, csv = true) }
            .setNeutralButton("Export JSON") { _, _ -> export(r, csv = false) }
            .setNegativeButton("Close", null)
            .create()
        del.setOnClickListener {
            host.dialog("Delete the report of ${r.ssid}?",
                host.label("It is gone from the monitor. Export it first to keep a copy. The monitor starts a new report " +
                    "of this network the next time it is on it.", 15f, T.TEXT2),
                "Delete", {
                    d.dismiss()
                    delete(info)
                })
        }
        d.show()
    }

    private fun deviceRow(rd: ReportDevice): View {
        val c = host
        val d = rd.device
        val row = c.row()
        row.gravity = Gravity.TOP
        row.setPadding(0, c.dp(10), 0, c.dp(10))
        val dot = View(c)
        dot.background = rounded(T.status(d.status, d.online), c.dpf(5f))
        row.add(dot, c.dp(10), c.dp(10), top = 6)
        val col = c.column()
        col.add(c.label(Format.deviceName(d), 15f, T.TEXT, Fonts.medium))
        col.add(c.label("${d.ip} · ${d.mac}", 13f, T.TEXT2, numbers = true), top = 2)
        val vendor = Format.vendorLine(d)
        val status = Format.statusWord(d.status)
        col.add(c.label(listOf(status, vendor).filter { it.isNotEmpty() }.joinToString(" · "), 13f,
            if (d.isUnknown) T.BAD else T.TEXT2), top = 2)
        col.add(c.label(ReportFiles.seenText(rd), 12.5f, T.TEXT3), top = 2)
        row.add(col, 0, WRAP, weight = 1f, start = 10)
        return row
    }

    private fun export(r: Report, csv: Boolean) {
        val name = ReportFiles.fileName(r.ssid, r.savedUnix, if (csv) "csv" else "json")
        val bytes = (if (csv) ReportFiles.csv(r) else r.raw).toByteArray(Charsets.UTF_8)
        host.saveFile(name, if (csv) "text/csv" else "application/json", bytes) { ok ->
            if (ok == true) Toast.makeText(host, "Saved $name", Toast.LENGTH_SHORT).show()
            else if (ok == false) Toast.makeText(host, "Could not save the file.", Toast.LENGTH_LONG).show()
        }
    }

    private fun delete(info: ReportInfo) {
        Board.run({ it.deleteReport(info.slot) }) { l, err ->
            if (l != null) {
                Board.setReports(l)
                say("Deleted the report of ${info.ssid}.", T.OK)
            } else {
                say(err?.message ?: "Could not delete that report.", T.BAD)
                if (err?.kind != ApiException.Kind.Unreachable) Board.refresh(Board.Part.REPORTS)
            }
            render()
        }
    }
}
