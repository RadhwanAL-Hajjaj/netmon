package com.example.netmon.ui

import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.ViewOutlineProvider
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.Device
import com.example.netmon.Format
import com.example.netmon.MainActivity
import com.example.netmon.MapAp
import com.example.netmon.MapInfo
import com.example.netmon.NetMap

/**
 * The network drawn around the router, as on the board's own Map page:
 * devices grouped by what they are, or by whether they are recognised, and
 * the access points that carry your network's name. Tap anything for what
 * the board knows about it.
 */
class MapPane(private val host: MainActivity) {

    private val ctx get() = host
    private val prefs get() = AppState.prefs
    private var byStatus = prefs.mapByStatus
    private var offline = prefs.mapOffline
    private var sel: MapSel? = null

    private lateinit var root: LinearLayout
    private lateinit var summary: TextView
    private lateinit var state: TextView
    private lateinit var kindChip: TextView
    private lateinit var statusChip: TextView
    private lateinit var offSwitch: android.widget.Switch
    private lateinit var offCount: TextView
    private lateinit var map: MapView
    private lateinit var info: LinearLayout
    private lateinit var olderNote: TextView
    private var infoKey = ""

    fun build(): View {
        val c = ctx
        root = c.column()
        summary = root.add(c.label("", 17f, T.TEXT, Fonts.medium, numbers = true), top = 12)
        state = root.add(c.label("", 13f, T.TEXT2), top = 2)

        root.add(c.fieldLabel("Group by"), top = 12)
        val gr = root.add(c.row(), top = 6)
        kindChip = gr.add(c.chip("What they are", !byStatus) { setBy(false) }, WRAP, WRAP)
        statusChip = gr.add(c.chip("Recognised or not", byStatus) { setBy(true) }, WRAP, WRAP, start = 8)
        val (or, sw) = c.switchRow("Show offline devices", offline) { on ->
            offline = on
            prefs.mapOffline = on
            render()
        }
        offSwitch = sw
        offCount = c.label("", 13f, T.TEXT3, numbers = true)
        or.addView(offCount, 1, LinearLayout.LayoutParams(WRAP, WRAP))
        root.add(or, top = 4)

        val frame = FrameLayout(c)
        frame.background = rounded(Pal.SCOPE, c.dpf(20f))
        frame.outlineProvider = ViewOutlineProvider.BACKGROUND
        frame.clipToOutline = true
        map = MapView(c)
        map.contentDescription = "Map of the local network: the router in the middle, devices grouped around it. " +
            "Tap a group, a dot or the router; the details appear below the map."
        map.onTap = { hit -> choose(hit) }
        frame.addView(map, FrameLayout.LayoutParams(MATCH, WRAP))
        root.add(frame, top = 10)

        val legend = root.add(c.label(legendText(), 12f, T.TEXT3), top = 8)
        legend.gravity = Gravity.CENTER

        info = root.add(c.card(), top = 12)
        olderNote = root.add(c.hint("This monitor's firmware is older than 0.11, so the map is drawn from its health " +
            "report: the access points of your network and your provider are not on it. Update the firmware to see them."), top = 10)
        olderNote.visibility = View.GONE
        root.add(c.hint("The board sees the network from where it sits: who answers on it, and what the router and the devices " +
            "say about themselves. It cannot see the cables, or which access point a device is joined to, so every device " +
            "hangs off the router here. Groups are guesses from names and makers. Access points come from the Nearby Wi-Fi " +
            "scans: other access points with your network's name are mesh nodes or extenders, or something pretending to be one."),
            top = 12)
        return root
    }

    private fun legendText(): CharSequence {
        val sb = android.text.SpannableStringBuilder()
        fun mark(sym: String, color: Int, text: String) {
            if (sb.isNotEmpty()) sb.append("    ")
            val s = sb.length
            sb.append(sym)
            sb.setSpan(android.text.style.ForegroundColorSpan(color), s, s + sym.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.append(" ").append(text)
        }
        mark("●", T.OK, "known")
        mark("●", T.PRIVATE, "private")
        mark("●", T.BAD, "unrecognised")
        mark("○", T.TEXT3, "offline")
        mark("●", Pal.ME, "this board")
        mark("◎", Pal.WIFI, "access point")
        return sb
    }

    private fun setBy(status: Boolean) {
        if (byStatus == status) return
        byStatus = status
        prefs.mapByStatus = status
        kindChip.styleChip(!status)
        statusChip.styleChip(status)
        sel = null
        render()
    }

    private fun choose(hit: NetMap.Hit?) {
        sel = when (hit) {
            null -> null
            is NetMap.Hit.Router -> MapSel.Router
            is NetMap.Hit.Internet -> MapSel.Internet
            is NetMap.Hit.InGroup -> MapSel.Group(hit.group.key)
            is NetMap.Hit.Dot -> {
                val it = hit.item
                val ap = it.ap
                val d = it.device
                when {
                    ap != null -> MapSel.Ap(ap.bssid)
                    it.me -> MapSel.Me
                    d != null -> MapSel.Device(d.mac)
                    else -> null
                }
            }
        }
        render()
    }

    /**
     * The map's frame: the board's own, or for firmware before 0.11 (which
     * answers 404) one made from its health report. In setup mode there is
     * nothing to map, and the health report says so.
     */
    private fun frame(): MapInfo? {
        Board.map?.let { return it }
        val h = Board.health ?: return null
        if (Board.missing(Board.Part.MAP) || h.inSetupMode) return NetMap.fromHealth(h, Board.devices)
        return null
    }

    fun render() {
        if (!::root.isInitialized) return
        val m = frame()
        val devs = Board.devices ?: emptyList()
        olderNote.shown(m != null && !m.fromBoard && Board.missing(Board.Part.MAP))
        val online = devs.count { it.online }
        val offn = devs.size - online
        offCount.put(if (offn > 0) "$offn  " else "")
        if (m == null) {
            summary.put(if (Board.link == Board.Link.LOST) "Cannot reach the monitor" else "Drawing the map")
            state.put(Board.partErrors[Board.Part.MAP] ?: "", T.BAD)
            map.set(null, emptyList(), byStatus, offline)
            renderInfo(null, devs)
            return
        }
        val bad = devs.count { it.isUnknown && it.online }
        summary.put(if (m.wifi != "connected") "No network yet" else "$online of ${devs.size} devices online")
        state.put(when {
            m.wifi != "connected" -> if (m.wifi == "softap") "The board is in setup mode." else "Joining the network."
            bad == 1 -> "One device online is not recognised."
            bad > 1 -> "$bad devices online are not recognised."
            else -> (if (m.ssid.isNotEmpty()) "“${m.ssid}” · " else "") + m.subnet + " · router " + m.gateway
        }, if (bad > 0) T.BAD else T.TEXT2)
        map.set(m, devs, byStatus, offline)
        // A selection that is no longer on the map (gone offline and hidden, say) is dropped.
        val p = map.plan
        if (p != null && sel != null && !present(p, sel!!)) sel = null
        map.sel = sel
        renderInfo(m, devs)
    }

    private fun present(p: NetMap.Plan, s: MapSel): Boolean = when (s) {
        is MapSel.Group -> p.groups.any { it.key == s.key }
        is MapSel.Device -> p.items.any { it.device?.mac == s.mac && !it.me && it.ap == null }
        is MapSel.Ap -> p.items.any { it.ap?.bssid == s.bssid }
        else -> true
    }

    // --- the panel under the map ------------------------------------------------

    private fun stat(status: String): String = when (status) {
        "known" -> "Recognised: first seen while the board was learning the network after it started"
        "private" -> "Private address: the device makes up its own address, as phones and laptops do for privacy, " +
            "so the board cannot tell whether it has seen it before"
        "unknown" -> "Not recognised: first seen after the board finished learning the network. Worth a look if you do not know what it is"
        else -> status
    }

    private fun renderInfo(m: MapInfo?, devs: List<Device>) {
        val s = sel
        val key = buildString {
            append(s?.javaClass?.simpleName).append('|')
            when (s) {
                is MapSel.Group -> append(s.key)
                is MapSel.Device -> append(s.mac)
                is MapSel.Ap -> append(s.bssid)
                else -> {}
            }
            append('|').append(m?.hashCode()).append('|')
            append(devs.joinToString(",") { "${it.mac}${it.online}${it.ip}${it.hostname}${it.status}${it.upS / 60}${it.lastSeenS / 60}" })
            append('|').append(byStatus).append(offline)
        }
        if (key == infoKey) return
        infoKey = key
        info.removeAllViews()
        val c = ctx
        if (m == null) {
            info.add(c.label("Reading the network…", 14f, T.TEXT2))
            return
        }
        fun title(t: String, sub: String = "") {
            val r = info.add(c.row())
            r.gravity = Gravity.BOTTOM
            val tv = r.add(c.label(t, 17f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f)
            tv.maxLines = 2
            tv.ellipsize = TextUtils.TruncateAt.END
            if (sub.isNotEmpty()) r.add(c.label(sub, 13f, T.TEXT3, numbers = true), WRAP, WRAP, start = 8)
        }
        fun line(t: String, color: Int = T.TEXT, top: Int = 6) {
            info.add(c.label(t, 14f, color, numbers = true), top = top)
        }
        val gwDev = devs.firstOrNull { it.ip == m.gateway }
        when (s) {
            null -> {
                title("Your network", m.subnet)
                val online = devs.count { it.online }
                val offn = devs.size - online
                line("$online of ${devs.size} devices online" + (if (offn > 0 && !offline) ", $offn offline not shown" else "") +
                    ". Tap a group, a dot or the router for details.", T.TEXT2)
                val bad = devs.count { it.isUnknown }
                if (bad > 0) line((if (bad == 1) "One device is" else "$bad devices are") + " not recognised: the red dots.", T.BAD)
            }
            is MapSel.Internet -> {
                title("Internet")
                val i = m.isp
                line(if (i.checked && i.valid) "Through ${i.isp.ifEmpty { i.org }}" +
                    (if (i.org.isNotEmpty() && i.org != i.isp) " (${i.org})" else "") + ", as of ${Format.ago(i.ageS)}."
                else "Not looked up yet. The Internet tab asks who your connection belongs to when you open it.", T.TEXT2)
                info.add(c.button("Open the Internet tab", Btn.QUIET) { host.showTab(MainActivity.TAB_INTERNET) }, WRAP, WRAP, top = 6)
            }
            is MapSel.Router -> {
                title("Router", m.gateway)
                line(if (gwDev != null) "${Format.shortVendor(gwDev.vendor).ifEmpty { "Maker not in the board’s list" }} · ${gwDev.mac}"
                else "Not answered the sweep yet.")
                line((if (m.latencyValid) "Answers this board in ${m.latencyMs} ms." else "No round trip measured yet.") +
                    " Everything on the map reaches the internet through it.", T.TEXT2)
                if (gwDev != null) info.add(c.button("Details", Btn.QUIET) { host.showDevice(gwDev.mac) }, WRAP, WRAP, top = 6)
            }
            is MapSel.Me -> {
                title("netmon", "this board")
                line("${m.ip} · ${m.mac}")
                line((if (m.ssid.isNotEmpty()) "Joined to “${m.ssid}”" + (if (m.channel > 0) " on channel ${m.channel}" else "") +
                    " at ${m.rssi} dBm. " else "") + "Firmware ${m.version}, up ${Format.duration(m.uptimeS)}.", T.TEXT2)
            }
            is MapSel.Ap -> {
                val a = m.aps.firstOrNull { it.bssid == s.bssid }
                if (a == null) { sel = null; infoKey = ""; renderInfo(m, devs); return }
                title("Access point", a.bssid)
                line("“${m.ssid}” on channel ${a.ch}, ${a.rssi} dBm" + if (a.joined) ". This board is joined to it."
                else ", heard ${if (a.ageS < 5) "just now" else Format.ago(a.ageS)}" + (if (a.live) "." else ", and not in range now."))
                if (!a.joined) line("Another access point with your network’s name: a mesh node or an extender, or something pretending to be one.", T.TEXT2)
                info.add(c.button("Find it with the board", Btn.QUIET) { host.openFinder("wifi", a.bssid) }, WRAP, WRAP, top = 6)
            }
            is MapSel.Device -> {
                val d = devs.firstOrNull { it.mac == s.mac }
                if (d == null) { sel = null; infoKey = ""; renderInfo(m, devs); return }
                title(Format.deviceName(d), NetMap.title(NetMap.kindOf(d)))
                line(listOf(d.ip, d.mac, d.vendor).filter { it.isNotEmpty() }.joinToString(" · "))
                line(stat(d.status) + ". " + if (d.online) "Online" + (if (d.upS > 0) " for ${Format.duration(d.upS)}" else "") + "."
                else "Offline, last seen ${Format.ago(d.lastSeenS)}.", T.status(d.status, d.online).let { if (d.isUnknown) it else T.TEXT2 })
                info.add(c.button("Details", Btn.QUIET) { host.showDevice(d.mac) }, WRAP, WRAP, top = 6)
            }
            is MapSel.Group -> {
                val g = map.plan?.groups?.firstOrNull { it.key == s.key }
                if (g == null) { sel = null; infoKey = ""; renderInfo(m, devs); return }
                if (g.wifi) {
                    val aps = sortedAps(g.aps)
                    title(g.title, "${aps.size} access point" + if (aps.size == 1) "" else "s")
                    if (!m.nearbyWifi && m.fromBoard) line("Wi-Fi scanning is off on the Nearby tab, so only the access point " +
                        "this board is joined to is known.", T.TEXT2)
                    val box = info.add(c.column(), top = 6)
                    aps.forEachIndexed { i, a ->
                        if (i > 0) box.addDivider()
                        box.add(apRow(a))
                    }
                } else {
                    title(g.title, g.items.size.toString())
                    val box = info.add(c.column(), top = 6)
                    g.items.forEachIndexed { i, it ->
                        val d = it.device ?: return@forEachIndexed
                        if (i > 0) box.addDivider()
                        box.add(deviceRow(d))
                    }
                }
            }
        }
    }

    private fun deviceRow(d: Device): View {
        val c = ctx
        val r = c.row()
        r.setPadding(0, c.dp(10), 0, c.dp(10))
        r.background = c.pressable(null, c.dpf(10f))
        r.isClickable = true
        r.setOnClickListener { sel = MapSel.Device(d.mac); render() }
        val mark = View(c)
        mark.background = rounded(T.status(d.status, d.online), c.dpf(5f))
        r.add(mark, c.dp(10), c.dp(10))
        val mid = c.column()
        val n = mid.add(c.label(Format.deviceName(d), 15f, if (d.online) T.TEXT else T.TEXT2, Fonts.medium))
        n.maxLines = 1
        n.ellipsize = TextUtils.TruncateAt.END
        val s = mid.add(c.label(listOf(d.ip, Format.shortVendor(d.vendor)).filter { it.isNotEmpty() }.joinToString(" · "), 12.5f, T.TEXT3, numbers = true), top = 2)
        s.maxLines = 1
        s.ellipsize = TextUtils.TruncateAt.END
        r.add(mid, 0, WRAP, weight = 1f, start = 12)
        r.add(c.label(if (d.online) (if (d.upS > 0) "up ${Format.duration(d.upS)}" else "online") else "seen ${Format.ago(d.lastSeenS)}",
            12.5f, T.TEXT3, numbers = true), WRAP, WRAP, start = 8)
        r.contentDescription = "${Format.deviceName(d)}, ${d.ip}, " + if (d.online) "online" else "offline"
        return r
    }

    private fun apRow(a: MapAp): View {
        val c = ctx
        val r = c.row()
        r.setPadding(0, c.dp(10), 0, c.dp(10))
        r.background = c.pressable(null, c.dpf(10f))
        r.isClickable = true
        r.setOnClickListener { sel = MapSel.Ap(a.bssid); render() }
        r.add(c.label("◎", 14f, if (a.live) Pal.WIFI else T.TEXT3), WRAP, WRAP)
        val mid = c.column()
        mid.add(c.label(if (a.joined) "This board joins here" else "Another access point", 15f, T.TEXT, Fonts.medium))
        mid.add(c.label("${a.bssid} · channel ${a.ch}", 12.5f, T.TEXT3, numbers = true), top = 2)
        r.add(mid, 0, WRAP, weight = 1f, start = 12)
        r.add(c.label("${a.rssi} dBm", 12.5f, T.TEXT3, numbers = true), WRAP, WRAP, start = 8)
        return r
    }

    fun onBack(): Boolean {
        if (sel != null) {
            sel = null
            render()
            return true
        }
        return false
    }
}
