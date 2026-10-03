package com.example.netmon.ui

import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.Format
import com.example.netmon.MainActivity

class InternetScreen(host: MainActivity) : Screen(host) {

    override val parts = listOf(Board.Part.HEALTH, Board.Part.LATENCY)

    private lateinit var publicIp: TextView
    private lateinit var who: TextView
    private lateinit var meta: TextView
    private lateinit var again: TextView
    private lateinit var connCard: LinearLayout
    private lateinit var org: KV
    private lateinit var asn: KV
    private lateinit var place: KV
    private lateinit var tz: KV
    private lateinit var rtt: KV
    private lateinit var router: KV
    private lateinit var routerHw: KV
    private lateinit var routerMac: KV
    private lateinit var gwNow: TextView
    private lateinit var gwNote: TextView
    private lateinit var chart: LatencyChart
    private lateinit var stats: TextView
    private var checking = false
    private var askedOnce = false

    override fun build(): View {
        val c = ctx
        val col = c.column()

        publicIp = col.add(c.label("", 30f, T.TEXT, Fonts.light, numbers = true), top = 12)
        who = col.add(c.label("", 17f, T.TEXT), top = 2)
        meta = col.add(c.label("", 13f, T.TEXT2), top = 4)
        again = col.add(c.button("Check again", Btn.SECONDARY) { check(force = true) }, WRAP, WRAP, top = 14)

        connCard = col.add(c.card(), top = 24)
        connCard.add(c.cardTitle("Connection"))
        org = connCard.addKV("Organisation", top = 10)
        asn = connCard.addKV("Network")
        place = connCard.addKV("Location")
        tz = connCard.addKV("Time zone")
        rtt = connCard.addKV("Lookup round trip")

        val rt = col.add(c.card(), top = 16)
        rt.add(c.cardTitle("Router"))
        router = rt.addKV("Address", top = 10)
        routerHw = rt.addKV("Hardware")
        routerMac = rt.addKV("MAC")

        val gw = col.add(c.card(), top = 16)
        gw.add(c.cardTitle("Gateway latency"))
        gwNow = gw.add(c.label("", 26f, T.TEXT, Fonts.light, numbers = true), top = 6)
        gwNote = gw.add(c.label("", 13f, T.TEXT2), top = 2)
        chart = gw.add(LatencyChart(c, compact = false), top = 14)
        stats = gw.add(c.label("", 13f, T.TEXT2, numbers = true), top = 10)
        gw.add(c.hint("The monitor opens a TCP connection to the router on a timer. The chart is what this phone has " +
            "recorded while it could reach the monitor; red dots are checks the router did not answer."), top = 8)

        col.add(c.hint("The public address and provider come from a lookup service the monitor asks over plain HTTP, " +
            "so treat them as reported rather than proven. The monitor checks every six hours; Check again asks now " +
            "(at most once every two minutes)."), top = 20)
        return c.page(col)
    }

    override fun onShown() {
        if (!askedOnce || Board.isp == null) {
            askedOnce = true
            check(force = false)
        }
    }

    private fun check(force: Boolean) {
        if (checking) return
        checking = true
        again.enabled(false)
        again.text = "Checking"
        Board.run({ it.isp(force) }) { isp, err ->
            checking = false
            again.enabled(true)
            again.text = "Check again"
            if (isp != null) Board.setIsp(isp)
            else if (err != null) {
                who.text = err.message
                who.setTextColor(T.BAD)
            }
        }
    }

    override fun render() {
        if (!::publicIp.isInitialized) return
        val i = Board.isp
        if (i == null) {
            publicIp.text = if (checking) "Checking" else "Not checked yet"
            publicIp.setTextColor(T.TEXT2)
            if (!checking && who.currentTextColor != T.BAD) who.text = ""
            meta.text = ""
            connCard.visibility = View.GONE
        } else if (!i.valid) {
            publicIp.text = "Not known"
            publicIp.setTextColor(T.TEXT2)
            who.text = i.error.ifBlank { "The lookup did not return an answer." }
            who.setTextColor(T.BAD)
            meta.text = ""
            connCard.visibility = View.GONE
        } else {
            publicIp.text = i.ip
            publicIp.setTextColor(T.TEXT)
            who.text = i.isp.ifBlank { "Provider not reported" }
            who.setTextColor(T.TEXT)
            val where = Format.place(i.city, i.region, i.country)
            meta.text = (if (where.isNotEmpty()) "Seen from $where. " else "") +
                (if (i.everChecked) "Checked ${Format.ago(i.checkedAgeS)}." else "")
            connCard.visibility = View.VISIBLE
            org.set(i.org)
            asn.set(i.asn)
            place.set(where)
            tz.set(i.timezone)
            rtt.set("${i.rttMs} ms")
        }
        if (i != null) {
            router.set(i.gateway)
            routerHw.set(Format.shortVendor(i.gatewayVendor).ifBlank { "Not in the registry" })
            routerMac.set(i.gatewayMac.ifBlank { "Not found by the sweep yet" })
        } else {
            val h = Board.health
            router.set(h?.gateway)
            routerHw.set(null)
            routerMac.set(null)
        }

        val h = Board.health
        val l = Board.latency
        when {
            h == null -> { gwNow.text = "-"; gwNote.text = "" }
            h.inSetupMode -> { gwNow.text = "-"; gwNote.text = "Not measured in setup mode" }
            h.latencyValid -> {
                gwNow.text = "${h.latencyMs} ms"
                gwNow.setTextColor(T.TEXT)
                gwNote.text = "Checked ${Format.ago(h.latencyAgeS)}" +
                    (if (l != null && l.failures > 0) ", ${l.failures} unanswered since the monitor started" else "")
            }
            else -> {
                gwNow.text = "No answer"
                gwNow.setTextColor(T.BAD)
                gwNote.text = "The router did not answer the last check"
            }
        }
        chart.set(AppState.latencySnapshot())
        val s = AppState.latencyStats()
        stats.text = if (s == null || s.count < 2) "Readings appear here as the phone collects them."
        else {
            val ok = s.count - s.lost
            if (ok == 0) "The router did not answer any of the last ${s.count} checks."
            else "Lowest ${s.minMs} ms, average ${s.avgMs} ms, highest ${s.maxMs} ms over ${s.count} checks" +
                (if (s.lost > 0) ", ${s.lost} unanswered" else "")
        }
    }
}
