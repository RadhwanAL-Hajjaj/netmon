package com.example.netmon.ui

import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.AbsoluteSizeSpan
import android.text.style.ForegroundColorSpan
import android.text.style.TypefaceSpan
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import com.example.netmon.AlertMode
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.Format
import com.example.netmon.MainActivity
import com.example.netmon.NetmonClient

class OverviewScreen(host: MainActivity) : Screen(host) {

    override val parts = listOf(Board.Part.HEALTH, Board.Part.DEVICES, Board.Part.EVENTS)

    private lateinit var count: TextView
    private lateinit var verdict: TextView
    private lateinit var strip: DeviceStrip
    private lateinit var legend: TextView

    private lateinit var lostCard: LinearLayout
    private lateinit var lostText: TextView
    private lateinit var promptCard: LinearLayout

    private lateinit var fw: KV
    private lateinit var up: KV
    private lateinit var wifi: KV
    private lateinit var addr: KV
    private lateinit var sweep: KV
    private lateinit var names: KV
    private lateinit var heap: KV

    private lateinit var gwValue: TextView
    private lateinit var gwNote: TextView
    private lateinit var gwChart: LatencyChart

    private lateinit var recent: LinearLayout
    private lateinit var recentEmpty: TextView
    private var recentKey = ""

    override fun build(): View {
        val c = ctx
        val col = c.column()

        // The one bold element: how many devices, and whether any are strangers.
        count = col.add(c.label("", 17f, T.TEXT2, numbers = true), top = 12)
        verdict = col.add(c.label("", 16f, T.TEXT2, Fonts.medium), top = 2)
        strip = col.add(DeviceStrip(c), top = 18)
        legend = col.add(c.label("", 13f, T.TEXT2, numbers = true), top = 10)

        lostCard = col.add(c.card(), top = 24)
        lostCard.add(c.cardTitle("Can't reach the monitor"))
        lostText = lostCard.add(c.label("", 14f, T.TEXT2), top = 6)
        lostCard.add(c.button("Find my monitor", Btn.SECONDARY) { host.showConnect() }, WRAP, WRAP, top = 14)

        promptCard = col.add(c.card(), top = 24)
        promptCard.add(c.cardTitle("Know when a stranger joins"))
        promptCard.add(c.label("Get a notification when the monitor sees a device it does not recognise.", 14f, T.TEXT2), top = 6)
        val pr = promptCard.add(c.row(), top = 14)
        pr.add(c.button("Turn on", Btn.PRIMARY) { host.enableAlerts(AlertMode.UNRECOGNISED) }, WRAP, WRAP)
        pr.add(c.button("Not now", Btn.QUIET) {
            AppState.prefs.alertPromptDone = true
            render()
        }, WRAP, WRAP, start = 8)

        val board = col.add(c.card(), top = 24)
        board.add(c.cardTitle("This monitor"))
        fw = board.addKV("Firmware", top = 10)
        up = board.addKV("Running for")
        wifi = board.addKV("Wi-Fi")
        addr = board.addKV("Address")
        sweep = board.addKV("Last sweep")
        names = board.addKV("Names learned")
        heap = board.addKV("Free memory")

        val gw = col.add(c.card(), top = 16)
        gw.add(c.cardTitle("Gateway"))
        gwValue = gw.add(c.label("", 26f, T.TEXT, Fonts.light, numbers = true), top = 6)
        gwNote = gw.add(c.label("", 13f, T.TEXT2), top = 2)
        gwChart = gw.add(LatencyChart(c, compact = true), top = 12)

        val act = col.add(c.card(), top = 16)
        act.add(c.cardTitle("Recent activity"))
        recentEmpty = act.add(c.hint("Devices joining, leaving and naming themselves show up here."), top = 8)
        recent = act.add(c.column(), top = 2)
        act.add(c.button("See all events", Btn.QUIET) { host.showTab(MainActivity.TAB_EVENTS) }, WRAP, WRAP, top = 4)

        return c.page(col)
    }

    override fun render() {
        if (!::count.isInitialized) return
        val h = Board.health
        val devices = Board.devices
        val link = Board.link

        // Headline
        if (devices != null) {
            val online = devices.count { it.online }
            val sb = SpannableStringBuilder()
            val n = online.toString()
            sb.append(n)
            sb.setSpan(AbsoluteSizeSpan(44, true), 0, n.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.setSpan(ForegroundColorSpan(T.TEXT), 0, n.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.setSpan(TypefaceSpan("sans-serif-light"), 0, n.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.append(if (online == devices.size) {
                if (online == 1) "  device online" else "  devices online"
            } else "  of ${devices.size} devices online")
            count.text = sb
            strip.set(devices)
            strip.visibility = View.VISIBLE
            legend.text = legendText(devices)
            legend.visibility = View.VISIBLE
        } else {
            count.text = if (link == Board.Link.LOST) "No readings" else "Reading the monitor"
            strip.visibility = View.GONE
            legend.visibility = View.GONE
        }

        val unknown = devices?.count { it.isUnknown && it.online } ?: 0
        when {
            h == null && link == Board.Link.LOST -> setVerdict("The monitor is not answering.", T.BAD)
            h == null -> setVerdict("", T.TEXT2)
            h.inSetupMode -> setVerdict("Setup mode: the monitor could not join any Wi-Fi it knows. Pick a network in Settings.", T.WARN)
            !h.sweepable -> setVerdict("Not scanning: the monitor has no usable subnet.", T.WARN)
            unknown == 1 -> setVerdict("1 device is not recognised.", T.BAD)
            unknown > 1 -> setVerdict("$unknown devices are not recognised.", T.BAD)
            h.baselineOpen -> {
                val min = maxOf(1L, (h.baselineClosesInS + 30) / 60)
                setVerdict("Still learning. Everything seen so far counts as known for $min more minute${if (min == 1L) "" else "s"}.", T.TEXT2)
            }
            devices != null -> setVerdict("Everything here is recognised.", T.TEXT2)
            else -> setVerdict("", T.TEXT2)
        }

        // Unreachable
        val base = Board.base
        lostCard.visibility = if (link == Board.Link.LOST) View.VISIBLE else View.GONE
        if (base != null) {
            lostText.text = if (com.example.netmon.LinkCodec.isBle(base)) {
                com.example.netmon.Board.lastProblem ?: ("No answer over Bluetooth. Make sure the monitor is " +
                    "switched on and within about 10 metres, or join its Wi-Fi.")
            } else {
                "No answer from ${NetmonClient.display(base)}. Make sure this phone is on the same Wi-Fi as the monitor" +
                    (if (com.example.netmon.Board.bleBase == null) ", or pair it in Settings, Bluetooth, to reach it from further away." else ".") +
                    " If the monitor lost its Wi-Fi, it opens a network called netmon-setup."
            }
        }

        val prefs = AppState.prefs
        promptCard.visibility = if (prefs.alertMode == AlertMode.OFF && !prefs.alertPromptDone && h != null) View.VISIBLE else View.GONE

        // This monitor
        if (h != null) {
            fw.set(h.version)
            up.set(Format.duration(h.uptimeS))
            wifi.set(if (h.inSetupMode) "Setup network ${h.ssid}" else "${h.ssid}, ${h.rssi} dBm, ${Format.signalWord(h.rssi).lowercase()}")
            addr.set(if (h.subnet.isNotBlank()) "${h.ip} on ${h.subnet}" else h.ip)
            sweep.set(Format.sweepLine(h))
            names.set(h.namesKnown.toString())
            heap.set(Format.heap(h.freeHeap))
        }

        // Gateway
        if (h != null && !h.inSetupMode) {
            if (h.latencyValid) {
                gwValue.text = "${h.latencyMs} ms"
                gwValue.setTextColor(T.TEXT)
                gwNote.text = "Router at ${h.gateway} answered ${Format.ago(h.latencyAgeS)}"
            } else {
                gwValue.text = "No answer"
                gwValue.setTextColor(T.BAD)
                gwNote.text = "The router at ${h.gateway} did not answer the last check"
            }
        } else {
            gwValue.text = "-"
            gwNote.text = if (h?.inSetupMode == true) "Not measured in setup mode" else ""
        }
        gwChart.set(AppState.latencySnapshot())

        // Recent activity
        val list = AppState.historySnapshot().take(5)
        val key = list.joinToString { it.key } + "|" + (System.currentTimeMillis() / 30_000)
        if (key != recentKey) {
            recentKey = key
            recent.removeAllViews()
            list.forEachIndexed { i, e ->
                if (i > 0) recent.addDivider()
                recent.add(EventRows.build(ctx, e, relative = true))
            }
            recentEmpty.visibility = if (list.isEmpty()) View.VISIBLE else View.GONE
        }
    }

    private fun setVerdict(text: String, color: Int) {
        verdict.text = text
        verdict.setTextColor(color)
        verdict.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun legendText(devices: List<com.example.netmon.Device>): CharSequence {
        val on = devices.filter { it.online }
        val parts = listOf(
            Triple(on.count { it.status == "known" }, "known", T.OK),
            Triple(on.count { it.isPrivate }, "private", T.PRIVATE),
            Triple(on.count { it.isUnknown }, "unrecognised", T.BAD),
            Triple(devices.count { !it.online }, "offline", T.OFFLINE),
        )
        val sb = SpannableStringBuilder()
        for ((n, word, color) in parts) {
            if (n == 0) continue
            if (sb.isNotEmpty()) sb.append("     ")
            val s = sb.length
            sb.append("●")
            sb.setSpan(ForegroundColorSpan(color), s, s + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.append(" $n $word")
        }
        return sb
    }
}
