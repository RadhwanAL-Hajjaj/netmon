package com.example.netmon.ui

import android.text.Editable
import android.text.InputType
import android.text.TextUtils
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.Switch
import android.widget.TextView
import com.example.netmon.Air
import com.example.netmon.ApiException
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.Calibration
import com.example.netmon.Format
import com.example.netmon.LiveLog
import com.example.netmon.MainActivity
import com.example.netmon.Nearby
import com.example.netmon.NearbyAp
import com.example.netmon.NearbyBle
import com.example.netmon.TrendMemory
import java.text.DateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max
import kotlin.math.min

/**
 * What the board hears around it: a Wi-Fi radar, a Bluetooth radar and the
 * Finder, as on the board's own Nearby page. Reading the tables every three
 * seconds is what keeps the board scanning quickly while this is open.
 */
class NearbyScreen(host: MainActivity) : Screen(host) {

    // On firmware without Nearby the tables are asked for only every ten
    // seconds, to notice an update, rather than every three.
    override val parts: List<Board.Part>
        get() = if (Board.missing(Board.Part.NEARBY)) listOf(Board.Part.NEARBY) else listOf(Board.Part.NEARBY_CONFIG)
    override val fastParts: List<Board.Part>
        get() = if (Board.missing(Board.Part.NEARBY)) emptyList() else listOf(Board.Part.NEARBY)
    private var wasMissing = false
    // While a device is being found the lists stand still, and the Finder's own
    // requests are what matter.
    override val fastMs: Long get() = if (tab == FINDER && ::finder.isInitialized && finder.active) 10_000L else 3_000L

    private val prefs get() = AppState.prefs
    private var tab = prefs.nearbyTab.let { if (it in TABS) it else WIFI }
    private val trends = TrendMemory()
    private val log = LiveLog()
    private var seenAt = 0L
    private var hiddenAt = 0L
    private var visible = false
    private var cal = prefs.calibration
    private var masked = prefs.masked
    private var hidden: Set<String> = prefs.hiddenGroups
    private var hidePrivate = prefs.hidePrivate

    lateinit var finder: FinderPane
        private set

    // header
    private lateinit var head: LinearLayout
    private lateinit var count: TextView
    private lateinit var state: TextView
    private lateinit var scanBtn: TextView
    private lateinit var needs: LinearLayout
    private lateinit var needsText: TextView
    private lateinit var body: LinearLayout
    private val tabChips = HashMap<String, TextView>()

    // panes
    private lateinit var wifiPane: LinearLayout
    private lateinit var blePane: LinearLayout
    private lateinit var finderBox: FrameLayout
    private lateinit var options: LinearLayout

    // Wi-Fi
    private lateinit var radarW: RadarView
    private lateinit var scaleW: TextView
    private lateinit var pickW: TextView
    private lateinit var pickWFind: TextView
    private lateinit var searchW: EditText
    private lateinit var countW: TextView
    private lateinit var sortW: TextView
    private lateinit var rowsW: KeyedRows<ApRow>
    private lateinit var emptyW: TextView
    private lateinit var countH: TextView
    private lateinit var rowsH: KeyedRows<ApRow>
    private lateinit var emptyH: TextView
    private lateinit var logW: LinearLayout

    // Bluetooth
    private lateinit var radarB: RadarView
    private lateinit var scaleB: TextView
    private lateinit var legendB: LinearLayout
    private val groupChips = HashMap<String, TextView>()
    private lateinit var privSwitch: Switch
    private lateinit var privCount: TextView
    private lateinit var pickB: TextView
    private lateinit var pickBFind: TextView
    private lateinit var searchB: EditText
    private lateinit var countB: TextView
    private lateinit var sortB: TextView
    private lateinit var rowsB: KeyedRows<BleRow>
    private lateinit var emptyB: TextView
    private lateinit var logB: LinearLayout

    // options
    private lateinit var wifiSwitch: Switch
    private lateinit var bleSwitch: Switch
    private val bgChips = HashMap<Int, TextView>()
    private lateinit var optMsg: TextView
    private lateinit var calBox: LinearLayout
    private lateinit var calToggle: TextView
    private val calFields = HashMap<String, EditText>()
    private var settingConfig = false

    private var selW: String? = null
    private var selB: String? = null
    private var sortKeyW = prefs.nearbySort("wifi", "rssi")
    private var sortKeyB = prefs.nearbySort("ble", "rssi")
    private var sortKeyH = prefs.nearbySort("history", "age")

    override fun build(): View {
        val c = ctx
        val col = c.column()
        finder = FinderPane(host, this)

        head = col.add(c.column(), top = 8)
        count = head.add(c.label("", 19f, T.TEXT, Fonts.medium, numbers = true))
        state = head.add(c.label("", 13f, T.TEXT2), top = 4)
        scanBtn = head.add(c.button("Scan now", Btn.SECONDARY) { scanNow() }, WRAP, WRAP, top = 12)

        needs = col.add(c.card(), top = 16)
        needs.add(c.cardTitle("Nearby needs newer firmware"))
        needsText = needs.add(c.label("", 14f, T.TEXT2), top = 6)
        needs.add(c.button("Go to firmware update", Btn.SECONDARY) { host.showTab(MainActivity.TAB_SETTINGS) }, WRAP, WRAP, top = 12)
        needs.visibility = View.GONE

        body = col.add(c.column())
        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val tr = c.row()
        scroller.addView(tr, FrameLayout.LayoutParams(WRAP, WRAP))
        for ((k, title) in listOf(WIFI to "Wi-Fi", BLE to "Bluetooth", FINDER to "Finder")) {
            val chip = c.chip(title, k == tab) { showPane(k) }
            tabChips[k] = chip
            tr.add(chip, WRAP, WRAP, end = 8)
        }
        body.add(scroller, top = 16)

        wifiPane = body.add(buildWifi())
        blePane = body.add(buildBle())
        finderBox = body.add(FrameLayout(c))
        finderBox.addView(finder.build(), FrameLayout.LayoutParams(MATCH, WRAP))
        options = body.add(buildOptions(), top = 16)
        body.add(c.hint("Distance is estimated from signal strength, so walls and bodies make things look farther than " +
            "they are; the Signal scale shows the strength itself. Direction on the radars is not measured: each dot " +
            "keeps a fixed bearing so you can follow it. The Finder gets a direction from you turning with the board. " +
            "▲ and ▼ mean the signal is getting stronger or weaker."), top = 16)
        applyPane()
        return c.page(col)
    }

    // --- Wi-Fi ----------------------------------------------------------------

    private fun buildWifi(): LinearLayout {
        val c = ctx
        val pane = c.column()
        val card = pane.add(c.card(), top = 12)
        val top = card.add(c.row())
        top.add(c.label("●", 12f, Pal.WIFI), WRAP, WRAP)
        top.add(c.label("Wi-Fi networks", 16f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f, start = 6)
        scaleW = top.add(c.button("", Btn.QUIET) { v -> chooseScale(v, ble = false) }, WRAP, WRAP)
        scaleW.minHeight = c.dp(38)
        radarW = RadarView(c)
        radarW.scaleM = prefs.radarScale(false)
        radarW.onPick = { k -> selW = k; render() }
        card.add(radarW, WRAP, WRAP, top = 6).centred()
        val legend = card.add(c.row(), top = 8)
        legend.gravity = Gravity.CENTER
        legend.add(c.label(legendText(), 12f, T.TEXT3), WRAP, WRAP)
        val pr = card.add(c.row(), top = 8)
        pr.gravity = Gravity.CENTER_VERTICAL
        pickW = pr.add(c.label("", 13f, T.TEXT2), 0, WRAP, weight = 1f)
        pickWFind = pr.add(c.button("Find it", Btn.QUIET) { selW?.let { find("wifi", it) } }, WRAP, WRAP, start = 6)

        searchW = pane.add(searchBox("Search name, address or security"), top = 12)

        val lt = pane.add(c.row(), top = 14)
        countW = lt.add(c.label("", 15f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f)
        sortW = lt.add(c.button("", Btn.QUIET) { v -> chooseSort(v, "wifi") }, WRAP, WRAP)
        sortW.minHeight = c.dp(40)
        val box = pane.add(c.card(), top = 4)
        box.setPadding(c.dp(12), c.dp(2), c.dp(12), c.dp(2))
        val list = box.add(c.column())
        rowsW = KeyedRows(list) { ApRow(history = false) }
        emptyW = box.add(c.label("", 14f, T.TEXT2), top = 12, bottom = 12)

        pane.add(c.cardTitle("Live log"), top = 18)
        pane.add(c.hint("Arrivals and departures while this screen is open."), top = 2)
        val lb = pane.add(c.card(), top = 6)
        lb.setPadding(c.dp(14), c.dp(4), c.dp(14), c.dp(4))
        logW = lb.add(c.column())

        val ht = pane.add(c.row(), top = 18)
        countH = ht.add(c.label("", 15f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f)
        val sortH = ht.add(c.button("", Btn.QUIET) { v -> chooseSort(v, "history") }, WRAP, WRAP)
        sortH.minHeight = c.dp(40)
        sortH.text = "Sort"
        pane.add(c.hint("Heard since the board started, and not in range now."), top = 2)
        val hb = pane.add(c.card(), top = 6)
        hb.setPadding(c.dp(12), c.dp(2), c.dp(12), c.dp(2))
        val hl = hb.add(c.column())
        rowsH = KeyedRows(hl) { ApRow(history = true) }
        emptyH = hb.add(c.label("", 14f, T.TEXT2), top = 12, bottom = 12)
        return pane
    }

    private fun legendText(): CharSequence {
        val sb = android.text.SpannableStringBuilder()
        fun mark(sym: String, text: String) {
            if (sb.isNotEmpty()) sb.append("     ")
            val s = sb.length
            sb.append(sym)
            sb.setSpan(android.text.style.ForegroundColorSpan(Pal.WIFI), s, s + sym.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.append(" ").append(text)
        }
        mark("●", "secured")
        mark("○", "open")
        mark("◉", "this board's network")
        return sb
    }

    // --- Bluetooth ----------------------------------------------------------

    private fun buildBle(): LinearLayout {
        val c = ctx
        val pane = c.column()
        val card = pane.add(c.card(), top = 12)
        val top = card.add(c.row())
        top.add(c.label("●", 12f, Pal.PERSONAL), WRAP, WRAP)
        top.add(c.label("Bluetooth devices", 16f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f, start = 6)
        scaleB = top.add(c.button("", Btn.QUIET) { v -> chooseScale(v, ble = true) }, WRAP, WRAP)
        scaleB.minHeight = c.dp(38)
        radarB = RadarView(c)
        radarB.scaleM = prefs.radarScale(true)
        radarB.onPick = { k -> selB = k; render() }
        card.add(radarB, WRAP, WRAP, top = 6).centred()

        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        legendB = c.row()
        scroller.addView(legendB, FrameLayout.LayoutParams(WRAP, WRAP))
        for (g in Air.GROUPS) {
            val chip = c.chip("", g !in hidden) {
                hidden = if (g in hidden) hidden - g else hidden + g
                prefs.hiddenGroups = hidden
                render()
            }
            groupChips[g] = chip
            legendB.add(chip, WRAP, WRAP, end = 6)
        }
        card.add(scroller, top = 8)
        val (pr, sw) = c.switchRow("Hide private addresses", hidePrivate) { on ->
            hidePrivate = on
            prefs.hidePrivate = on
            render()
        }
        privSwitch = sw
        privCount = c.label("", 13f, T.TEXT3, numbers = true)
        pr.addView(privCount, 1, LinearLayout.LayoutParams(WRAP, WRAP))
        card.add(pr, top = 4)

        val pk = card.add(c.row(), top = 4)
        pickB = pk.add(c.label("", 13f, T.TEXT2), 0, WRAP, weight = 1f)
        pickBFind = pk.add(c.button("Find it", Btn.QUIET) { selB?.let { find("ble", it) } }, WRAP, WRAP, start = 6)

        searchB = pane.add(searchBox("Search name, kind, address or maker"), top = 12)
        val lt = pane.add(c.row(), top = 14)
        countB = lt.add(c.label("", 15f, T.TEXT, Fonts.medium), 0, WRAP, weight = 1f)
        sortB = lt.add(c.button("", Btn.QUIET) { v -> chooseSort(v, "ble") }, WRAP, WRAP)
        sortB.minHeight = c.dp(40)
        pane.add(c.hint("Most phones and earbuds change their address every quarter hour or so, which is why one phone can " +
            "show up more than once; those say private. The kind comes from what a device advertises and is a best guess."), top = 2)
        val box = pane.add(c.card(), top = 6)
        box.setPadding(c.dp(12), c.dp(2), c.dp(12), c.dp(2))
        val list = box.add(c.column())
        rowsB = KeyedRows(list) { BleRow() }
        emptyB = box.add(c.label("", 14f, T.TEXT2), top = 12, bottom = 12)

        pane.add(c.cardTitle("Live log"), top = 18)
        pane.add(c.hint("Arrivals and departures while this screen is open."), top = 2)
        val lb = pane.add(c.card(), top = 6)
        lb.setPadding(c.dp(14), c.dp(4), c.dp(14), c.dp(4))
        logB = lb.add(c.column())
        return pane
    }

    private fun searchBox(hint: String): EditText {
        val e = ctx.input(hint, InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)
        e.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) { render() }
        })
        return e
    }

    // --- options ---------------------------------------------------------------

    private fun buildOptions(): LinearLayout {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("Scanning on the board"))
        card.add(c.hint("The board scans quickly while this screen is open, and otherwise at the interval below. " +
            "Its own network sweep always comes first."), top = 4)
        val (wr, ws) = c.switchRow("Wi-Fi", true) { on -> if (!settingConfig) setConfig(wifi = on) }
        wifiSwitch = ws
        card.add(wr, top = 8)
        val (br, bs) = c.switchRow("Bluetooth", true) { on -> if (!settingConfig) setConfig(ble = on) }
        bleSwitch = bs
        card.add(br)
        card.add(c.fieldLabel("While nobody is watching"), top = 10)
        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val r = c.row()
        scroller.addView(r, FrameLayout.LayoutParams(WRAP, WRAP))
        for (s in Air.BACKGROUND) {
            val chip = c.chip(Air.backgroundWord(s), false) { setConfig(backgroundS = s) }
            bgChips[s] = chip
            r.add(chip, WRAP, WRAP, end = 8)
        }
        card.add(scroller, top = 6)
        optMsg = card.add(c.label("", 13f, T.BAD), top = 8)
        optMsg.visibility = View.GONE

        calToggle = card.add(c.button("Distance calibration", Btn.QUIET) {
            val show = calBox.visibility != View.VISIBLE
            calBox.shown(show)
        }, WRAP, WRAP, top = 8)
        calToggle.setPadding(0, c.dp(8), c.dp(8), c.dp(8))
        calBox = card.add(c.column())
        calBox.add(c.hint("Signal strength one metre from the board, and how fast it falls off: 2 in open air, 3 or more " +
            "through walls. Hold a phone a metre away and read its signal off the list to set the first. Kept on this phone."))
        for ((k, label) in listOf("wp" to "Wi-Fi at 1 m (dBm)", "wn" to "Wi-Fi falloff", "bp" to "Bluetooth at 1 m (dBm)", "bn" to "Bluetooth falloff")) {
            calBox.add(c.fieldLabel(label), top = 10)
            val e = c.input("", InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED or InputType.TYPE_NUMBER_FLAG_DECIMAL)
            e.addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable?) { calEdited(k, s?.toString() ?: "") }
            })
            calFields[k] = e
            calBox.add(e, top = 4)
        }
        calBox.add(c.button("Reset to defaults", Btn.SECONDARY) {
            prefs.resetCalibration()
            cal = prefs.calibration
            fillCal()
            render()
        }, WRAP, WRAP, top = 12)
        calBox.visibility = View.GONE
        fillCal()
        val (mr, _) = c.switchRow("Mask names and addresses, for screenshots", masked) { on ->
            masked = on
            prefs.masked = on
            render()
        }
        card.add(mr, top = 8)
        return card
    }

    private fun fillCal() {
        // Up to two decimals, as typed: rounding to one here would quietly change
        // a value such as 2.75 the next time the screen is built.
        fun f(v: Double) = String.format(Locale.US, "%.2f", v).trimEnd('0').trimEnd('.')
        calFields["wp"]?.setText(f(cal.wifiAt1m))
        calFields["wn"]?.setText(f(cal.wifiFalloff))
        calFields["bp"]?.setText(f(cal.bleAt1m))
        calFields["bn"]?.setText(f(cal.bleFalloff))
    }

    private fun calEdited(k: String, s: String) {
        val v = s.trim().toDoubleOrNull() ?: return
        val next = when (k) {
            "wp" -> if (Calibration.atOneMetreOk(v)) cal.copy(wifiAt1m = v) else null
            "wn" -> if (Calibration.falloffOk(v)) cal.copy(wifiFalloff = v) else null
            "bp" -> if (Calibration.atOneMetreOk(v)) cal.copy(bleAt1m = v) else null
            else -> if (Calibration.falloffOk(v)) cal.copy(bleFalloff = v) else null
        } ?: return
        if (next == cal) return
        cal = next
        prefs.calibration = next
        render()
    }

    private fun setConfig(wifi: Boolean? = null, ble: Boolean? = null, backgroundS: Int? = null) {
        optMsg.visibility = View.GONE
        Board.run({ it.setNearbyConfig(wifi, ble, backgroundS) }) { cfg, err ->
            if (cfg != null) {
                Board.setNearbyConfig(cfg)
                if (cfg.ble && !cfg.bleReady) say(optMsg, "Bluetooth could not start on this board.")
                Board.refresh(Board.Part.NEARBY)
            } else {
                say(optMsg, err?.message ?: "Could not change that.")
                Board.refresh(Board.Part.NEARBY_CONFIG)
            }
            render()
        }
    }

    private fun say(tv: TextView, text: String) {
        tv.text = text
        tv.shown(text.isNotEmpty())
    }

    private fun scanNow() {
        scanBtn.enabled(false)
        scanBtn.text = "Scanning…"
        Board.run({ it.nearbyScan() }) { _, err ->
            if (err != null) state.put(err.message ?: "Could not ask for a scan.", T.BAD)
            Board.refresh(Board.Part.NEARBY)
        }
        // The scans take a few seconds; the button comes back when they should be done.
        scanBtn.postDelayed({
            scanBtn.enabled(true)
            scanBtn.text = "Scan now"
            Board.refresh(Board.Part.NEARBY)
        }, 6000)
    }

    private fun chooseScale(anchor: View, ble: Boolean) {
        val menu = PopupMenu(ctx, anchor)
        Air.SCALES.forEachIndexed { i, m -> menu.menu.add(0, i, i, if (m == 0) "Signal strength" else "Up to $m m") }
        menu.setOnMenuItemClickListener { item ->
            val m = Air.SCALES[item.itemId]
            prefs.setRadarScale(ble, m)
            (if (ble) radarB else radarW).scaleM = m
            render()
            true
        }
        menu.show()
    }

    private fun sortTitle(k: String) = when (k) {
        "rssi" -> "signal"
        "name" -> "name"
        "ch" -> "channel"
        "kind" -> "kind"
        "age" -> "last heard"
        else -> k
    }

    private fun chooseSort(anchor: View, list: String) {
        val keys = when (list) {
            "wifi" -> listOf("rssi", "name", "ch", "age")
            "ble" -> listOf("rssi", "name", "kind", "age")
            else -> listOf("age", "name", "rssi", "ch")
        }
        val menu = PopupMenu(ctx, anchor)
        keys.forEachIndexed { i, k -> menu.menu.add(0, i, i, "Sort by ${sortTitle(k)}") }
        menu.setOnMenuItemClickListener { item ->
            val k = keys[item.itemId]
            prefs.setNearbySort(list, k)
            when (list) {
                "wifi" -> sortKeyW = k
                "ble" -> sortKeyB = k
                else -> sortKeyH = k
            }
            render()
            true
        }
        menu.show()
    }

    // --- tabs ----------------------------------------------------------------

    private fun showPane(k: String) {
        if (k == tab) return
        val prev = tab
        tab = k
        prefs.nearbyTab = k
        if (prev == FINDER) finder.leave()
        applyPane()
        if (k == FINDER && visible) finder.enter()
        host.restartFastPoll()
        render()
    }

    private fun applyPane() {
        for ((k, chip) in tabChips) chip.styleChip(k == tab)
        wifiPane.shown(tab == WIFI)
        blePane.shown(tab == BLE)
        finderBox.shown(tab == FINDER)
        val finding = tab == FINDER && finder.active
        head.shown(!finding && !(Board.missing(Board.Part.NEARBY) && Board.nearby == null))
        options.shown(!finding)
    }

    /** Opens the Finder on one device: from a list, a radar, or the map. */
    fun find(type: String, addr: String) {
        if (!::finder.isInitialized) view
        finder.pick(type, addr)
        if (tab != FINDER) showPane(FINDER) else {
            applyPane()
            host.restartFastPoll()
            render()
        }
    }

    /** The Finder started or stopped: the header and the pace of the readings follow. */
    fun finderChanged() {
        applyPane()
        host.restartFastPoll()
    }

    override fun onShown() {
        visible = true
        val now = System.currentTimeMillis()
        if (hiddenAt == 0L || now - hiddenAt > 20_000) log.relearn(now)
        if (Board.nearbyConfig == null) Board.refresh(Board.Part.NEARBY_CONFIG)
        if (tab == FINDER) finder.enter()
    }

    override fun onHidden() {
        visible = false
        hiddenAt = System.currentTimeMillis()
        finder.leave()
    }

    override fun onPause() {
        if (!visible) return
        hiddenAt = System.currentTimeMillis()
        finder.leave()
    }

    override fun onResume() {
        if (!visible) return
        val now = System.currentTimeMillis()
        if (now - hiddenAt > 20_000) log.relearn(now)
        if (tab == FINDER) finder.enter()
    }

    // --- render ----------------------------------------------------------------

    private fun showBle(d: NearbyBle) = Air.group(d) !in hidden && !(hidePrivate && d.kind == "private")

    override fun render() {
        if (!::count.isInitialized) return
        val n = Board.nearby
        val missing = Board.missing(Board.Part.NEARBY)
        if (missing != wasMissing) {
            wasMissing = missing
            if (visible) host.restartFastPoll()
        }
        needs.shown(missing && n == null)
        body.shown(!(missing && n == null))
        if (missing && n == null) {
            val v = Board.health?.version
            needsText.put("The Wi-Fi and Bluetooth radars and the Finder need netmon firmware 0.10 or later (the Finder 0.11)" +
                (if (v != null) ". This monitor runs $v." else ".") + " The web pages and the app read the same board, so once it is updated both have them.")
            head.shown(false)
            return
        }
        // Back from the card above (the board updated, or another board): the header returns.
        head.shown(!(tab == FINDER && finder.active))
        if (n != null && Board.nearbyAtMs != seenAt) {
            seenAt = Board.nearbyAtMs
            absorb(n)
        }
        renderHead(n)
        renderOptions()
        when (tab) {
            WIFI -> renderWifi(n)
            BLE -> renderBle(n)
            else -> finder.render()
        }
        tabChips[WIFI]?.put(if (n == null) "Wi-Fi" else if (!n.wifiScan.enabled) "Wi-Fi off" else "Wi-Fi ${n.wifi.count { it.live }}")
        tabChips[BLE]?.put(if (n == null) "Bluetooth" else if (!n.bleScan.enabled) "Bluetooth off" else "Bluetooth ${n.ble.size}")
        tabChips[FINDER]?.put(if (finder.active) "Finder ●" else "Finder")
    }

    /** One new reading: trends, the live log, and arrivals and departures for the radars. */
    private fun absorb(n: Nearby) {
        val m = HashMap<String, Int>()
        for (a in n.wifi) m[a.bssid] = a.rssi
        for (d in n.ble) m[d.addr] = d.rssi
        trends.remember(m)
        val now = System.currentTimeMillis()
        val ch = log.track(now, n, { showBle(it) })
        for (k in ch.arrived) {
            if (n.wifi.any { it.bssid == k }) radarW.born[k] = now else radarB.born[k] = now
        }
        for (e in ch.left) {
            val r = if (e.wifi) radarW else radarB
            val rad = radius(e.rssi, !e.wifi, r.scaleM)
            r.ghosts.add(RadarGhost(Air.bearing(e.key), rad, if (e.wifi) Pal.WIFI else Pal.group(e.group), now))
        }
    }

    private fun renderHead(n: Nearby?) {
        if (n == null) {
            count.put(if (Board.link == Board.Link.LOST) "No readings" else "Listening")
            val err = Board.partErrors[Board.Part.NEARBY]
            state.put(err ?: if (Board.link == Board.Link.LOST) "The monitor is not answering." else "", if (err != null) T.BAD else T.TEXT2)
            return
        }
        count.put(Air.countLine(n))
        val stale = System.currentTimeMillis() - Board.nearbyAtMs > 12_000
        if (stale && Board.link == Board.Link.LOST) {
            state.put("Cannot reach the monitor. These lists are from ${Format.ago((System.currentTimeMillis() - Board.nearbyAtMs) / 1000)}.", T.BAD)
        } else {
            val s = Air.stateLine(n, masked)
            state.put(s.text, if (s.bad) T.BAD else T.TEXT2)
        }
    }

    private fun renderOptions() {
        val cfg = Board.nearbyConfig
        val n = Board.nearby
        settingConfig = true
        wifiSwitch.isChecked = cfg?.wifi ?: n?.wifiScan?.enabled ?: true
        bleSwitch.isChecked = cfg?.ble ?: n?.bleScan?.enabled ?: true
        settingConfig = false
        val bg = cfg?.backgroundS ?: n?.backgroundS
        for ((s, chip) in bgChips) chip.styleChip(s == bg)
    }

    private fun radius(rssi: Int, ble: Boolean, scale: Int): Double =
        if (scale == 0) max(0.0, min(1.0, (-20.0 - rssi) / 80))
        else min(Air.metres(rssi, ble, cal) / scale, 1.0)

    private fun pickText(name: String, rest: String): CharSequence {
        val sb = android.text.SpannableStringBuilder(name)
        sb.setSpan(android.text.style.ForegroundColorSpan(T.TEXT), 0, name.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        sb.setSpan(android.text.style.StyleSpan(android.graphics.Typeface.BOLD), 0, name.length, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        sb.append(rest)
        return sb
    }

    private fun apComparator(k: String): Comparator<NearbyAp> = when (k) {
        "name" -> Comparator { a, b ->
            val r = (if (a.ssid.isEmpty()) "￿" else a.ssid.lowercase()).compareTo(if (b.ssid.isEmpty()) "￿" else b.ssid.lowercase())
            if (r != 0) r else b.rssi - a.rssi
        }
        "ch" -> Comparator { a, b -> if (a.ch != b.ch) a.ch - b.ch else b.rssi - a.rssi }
        "age" -> Comparator { a, b -> if (a.ageS != b.ageS) a.ageS.compareTo(b.ageS) else b.rssi - a.rssi }
        else -> Comparator { a, b -> b.rssi - a.rssi }
    }

    private fun bleComparator(k: String): Comparator<NearbyBle> = when (k) {
        "name" -> Comparator { a, b ->
            val r = Air.bleName(a).lowercase().compareTo(Air.bleName(b).lowercase())
            if (r != 0) r else b.rssi - a.rssi
        }
        "kind" -> Comparator { a, b ->
            val ka = Air.kindWord(a.type)?.lowercase() ?: "￿"
            val kb = Air.kindWord(b.type)?.lowercase() ?: "￿"
            val r = ka.compareTo(kb)
            if (r != 0) r else b.rssi - a.rssi
        }
        "age" -> Comparator { a, b -> if (a.ageS != b.ageS) a.ageS.compareTo(b.ageS) else b.rssi - a.rssi }
        else -> Comparator { a, b -> b.rssi - a.rssi }
    }

    private fun renderWifi(n: Nearby?) {
        scaleW.put(Air.scaleWord(radarW.scaleM) + " ▾")
        scaleW.contentDescription = "Radar scale: ${Air.scaleWord(radarW.scaleM)}. Tap to change."
        sortW.put("By ${sortTitle(sortKeyW)}")
        if (n == null) {
            radarW.dots = emptyList()
            rowsW.clear(); rowsH.clear()
            emptyW.put("Listening…"); emptyW.shown(true)
            emptyH.shown(false)
            countW.put("In range"); countH.put("History")
            pickW.put("Tap a dot to see which network it is.")
            pickWFind.shown(false)
            return
        }
        val q = searchW.text?.toString()?.trim() ?: ""
        val live = n.wifi.filter { it.live }
        val gone = n.wifi.filter { !it.live }
        val mine = n.wifi.firstOrNull { it.joined }
        radarW.dots = live.map { a ->
            val m = Air.metres(a.rssi, false, cal)
            RadarDot(a.bssid, Air.bearing(a.bssid), radius(a.rssi, false, radarW.scaleM),
                radarW.scaleM > 0 && m > radarW.scaleM, Air.matches(a, q), a.joined, a.security == "Open",
                Pal.WIFI, 1.0, a.rssi, Air.apName(a, masked), "${a.rssi} dBm" + trends.arrow(a.bssid))
        }
        radarW.contentDescription = "Radar of ${live.size} Wi-Fi networks. The same networks are listed below."
        val sel = selW?.let { k -> live.firstOrNull { it.bssid == k } }
        if (sel == null) selW = null
        radarW.selected = selW
        if (sel != null) {
            val m = Air.metres(sel.rssi, false, cal)
            pickW.put(pickText(Air.apName(sel, masked), " · ${sel.rssi} dBm${trends.arrow(sel.bssid)} · ${Air.distance(m)} · " +
                "channel ${sel.ch} · ${sel.security}\n${if (masked) Air.maskAddr(sel.bssid) else sel.bssid}" +
                (if (sel.joined) " · this board is joined to it" else "")))
        } else pickW.put("Tap a dot to see which network it is.")
        pickWFind.shown(sel != null)

        val shown = live.filter { Air.matches(it, q) }.sortedWith(apComparator(sortKeyW))
        countW.put(if (shown.size != live.size) "In range  ${shown.size} of ${live.size}" else "In range  ${live.size}")
        rowsW.show(shown, { it.bssid }) { h, a -> h.bind(a, mine) }
        emptyW.shown(shown.isEmpty())
        emptyW.put(when {
            q.isNotEmpty() && live.isNotEmpty() -> "No network matches."
            !n.wifiScan.enabled -> "Wi-Fi scanning is off. Switch it on under Scanning on the board."
            n.wifiScan.count > 0 -> "No networks heard in the last scan."
            else -> "The first Wi-Fi scan has not finished."
        })
        val hist = gone.filter { Air.matches(it, q) }.sortedWith(apComparator(sortKeyH))
        countH.put(if (hist.size != gone.size) "History  ${hist.size} of ${gone.size}" else "History  ${gone.size}")
        rowsH.show(hist, { it.bssid }) { h, a -> h.bind(a, mine) }
        emptyH.shown(hist.isEmpty())
        emptyH.put(if (q.isNotEmpty() && gone.isNotEmpty()) "No network matches." else "Nothing yet: every network heard is still in range.")
        renderLog(logW, true)
    }

    private fun renderBle(n: Nearby?) {
        scaleB.put(Air.scaleWord(radarB.scaleM) + " ▾")
        scaleB.contentDescription = "Radar scale: ${Air.scaleWord(radarB.scaleM)}. Tap to change."
        sortB.put("By ${sortTitle(sortKeyB)}")
        val counts = HashMap<String, Int>()
        var priv = 0
        for (d in n?.ble ?: emptyList()) {
            val g = Air.group(d)
            counts[g] = (counts[g] ?: 0) + 1
            if (d.kind == "private") priv++
        }
        for ((g, chip) in groupChips) {
            val name = Air.GROUP_NAME[g] ?: g
            val sb = android.text.SpannableStringBuilder("● ")
            sb.setSpan(android.text.style.ForegroundColorSpan(Pal.group(g)), 0, 1, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.append("$name ${counts[g] ?: 0}")
            chip.text = sb
            chip.styleChip(g !in hidden)
            chip.contentDescription = "$name, ${counts[g] ?: 0}, " + if (g in hidden) "hidden" else "shown"
        }
        privCount.put("$priv  ")
        if (n == null) {
            radarB.dots = emptyList()
            rowsB.clear()
            emptyB.put("Listening…"); emptyB.shown(true)
            countB.put("Devices")
            pickB.put("Tap a dot to see which device it is.")
            pickBFind.shown(false)
            return
        }
        val q = searchB.text?.toString()?.trim() ?: ""
        val vis = n.ble.filter { showBle(it) }
        radarB.dots = vis.map { d ->
            val m = Air.metres(d.rssi, true, cal)
            RadarDot(d.addr, Air.bearing(d.addr), radius(d.rssi, true, radarB.scaleM),
                radarB.scaleM > 0 && m > radarB.scaleM, Air.matches(d, q), false, false,
                Pal.group(Air.group(d)), max(.35, 1 - max(0L, d.ageS - 10) / 60.0), d.rssi,
                Air.bleName(d, masked), "${d.rssi} dBm" + trends.arrow(d.addr))
        }
        radarB.contentDescription = "Radar of ${vis.size} Bluetooth devices. The same devices are listed below."
        val sel = selB?.let { k -> vis.firstOrNull { it.addr == k } }
        if (sel == null) selB = null
        radarB.selected = selB
        if (sel != null) {
            val m = Air.metres(sel.rssi, true, cal)
            val kind = Air.kindWord(sel.type)
            pickB.put(pickText(Air.bleName(sel, masked), " · ${sel.rssi} dBm${trends.arrow(sel.addr)} · ${Air.distance(m)}" +
                (if (kind != null) " · $kind" else "") + (if (sel.vendor.isNotEmpty()) " · ${sel.vendor}" else "") +
                "\n${if (masked) Air.maskAddr(sel.addr) else sel.addr}" + (if (sel.kind != "public") " · ${sel.kind} address" else "")))
        } else pickB.put("Tap a dot to see which device it is.")
        pickBFind.shown(sel != null)

        val shown = vis.filter { Air.matches(it, q) }.sortedWith(bleComparator(sortKeyB))
        countB.put(if (shown.size != n.ble.size) "Devices  ${shown.size} of ${n.ble.size}" else "Devices  ${n.ble.size}")
        rowsB.show(shown, { it.addr }) { h, d -> h.bind(d) }
        emptyB.shown(shown.isEmpty())
        emptyB.put(when {
            (q.isNotEmpty() || vis.size < n.ble.size) && n.ble.isNotEmpty() -> "Nothing matches, or it is hidden above."
            !n.bleScan.enabled -> "Bluetooth is off. Switch it on under Scanning on the board."
            n.bleScan.state == "unavailable" -> "Bluetooth could not start on this board."
            else -> "No Bluetooth devices heard yet."
        })
        renderLog(logB, false)
    }

    private var logKey = ""

    private fun renderLog(box: LinearLayout, wifi: Boolean) {
        val list = log.entries.filter { it.wifi == wifi }.take(12)
        val key = "$wifi|$masked|" + list.joinToString(",") { "${it.atMs}${it.key}${it.arrived}" }
        if (key == logKey && box.childCount > 0) return
        logKey = key
        box.removeAllViews()
        val c = ctx
        if (list.isEmpty()) {
            box.add(c.label(if (wifi) "Watching for networks arriving and leaving…" else "Watching for devices arriving and leaving…",
                13f, T.TEXT3), top = 8, bottom = 8)
            return
        }
        val tf = DateFormat.getTimeInstance(DateFormat.MEDIUM)
        list.forEachIndexed { i, e ->
            if (i > 0) box.addDivider()
            val r = c.row()
            r.setPadding(0, c.dp(7), 0, c.dp(7))
            r.add(c.label(tf.format(Date(e.atMs)), 12f, T.TEXT3, numbers = true), c.dp(76), WRAP)
            if (e.batch > 0) {
                r.add(c.label("${e.batch} ${if (wifi) "networks" else "devices"} appeared at once", 13.5f, T.TEXT), 0, WRAP, weight = 1f, start = 6)
            } else {
                r.add(c.label("●", 11f, if (wifi) Pal.WIFI else Pal.group(e.group)), WRAP, WRAP, start = 4)
                val name = c.label(e.shown(masked) + if (e.arrived) " appeared" else " left", 13.5f, if (e.arrived) T.TEXT else T.TEXT2)
                name.maxLines = 1
                name.ellipsize = TextUtils.TruncateAt.END
                r.add(name, 0, WRAP, weight = 1f, start = 8)
                r.add(c.label((if (e.arrived) "" else "last ") + "${e.rssi} dBm", 12f, T.TEXT3, numbers = true), WRAP, WRAP, start = 6)
            }
            box.add(r)
        }
    }

    // --- rows ------------------------------------------------------------------

    private inner class ApRow(private val history: Boolean) : KeyedRows.Holder(ctx) {
        private val row = ctx.row()
        private val mark = View(ctx)
        private val name = ctx.label("", 16f, T.TEXT)
        private val tag = ctx.label("", 12f, T.TEXT3)
        private val sub = ctx.label("", 12.5f, T.TEXT3, numbers = true)
        private val sig = ctx.label("", 13.5f, T.TEXT, numbers = true)
        private val dist = ctx.label("", 12f, T.TEXT3, numbers = true)
        private val findBtn: TextView
        private var key = ""
        private var lit: Boolean? = null

        init {
            val c = ctx
            row.setPadding(c.dp(2), c.dp(10), 0, c.dp(10))
            row.isClickable = true
            row.setOnClickListener { selW = key; render() }
            row.add(mark, c.dp(4), c.dp(34))
            val mid = c.column()
            val top = c.row()
            name.maxLines = 1
            name.ellipsize = TextUtils.TruncateAt.END
            top.add(name, 0, WRAP, weight = 1f)
            top.add(tag, WRAP, WRAP, start = 6)
            mid.add(top)
            sub.maxLines = 1
            sub.ellipsize = TextUtils.TruncateAt.END
            mid.add(sub, top = 2)
            row.add(mid, 0, WRAP, weight = 1f, start = 10)
            val right = c.column()
            right.gravity = Gravity.END
            sig.gravity = Gravity.END
            dist.gravity = Gravity.END
            right.add(sig, WRAP, WRAP)
            right.add(dist, WRAP, WRAP, top = 2)
            row.add(right, WRAP, WRAP, start = 8)
            findBtn = c.button("Find", Btn.QUIET) { find("wifi", key) }
            findBtn.minWidth = c.dp(52)
            findBtn.setPadding(c.dp(8), c.dp(6), c.dp(4), c.dp(6))
            if (!history) row.add(findBtn, WRAP, WRAP)
            view.add(row)
        }

        fun bind(a: NearbyAp, mine: NearbyAp?) {
            key = a.bssid
            mark.background = rounded(if (history) T.EDGE else Pal.WIFI, ctx.dpf(2f))
            name.put(Air.apName(a, masked), if (a.ssid.isEmpty()) T.TEXT2 else if (history) T.TEXT2 else T.TEXT)
            tag.put(when {
                a.joined -> "this board"
                mine != null && a.ssid.isNotEmpty() && a.ssid == mine.ssid -> "same name as yours"
                else -> ""
            })
            tag.shown(tag.text.isNotEmpty())
            val sec = if (a.security == "Open") "Open network" else a.security
            sub.put("ch ${a.ch} · $sec · ${if (masked) Air.maskAddr(a.bssid) else a.bssid}",
                if (a.security == "Open") T.WARN else T.TEXT3)
            if (history) {
                sig.put("last ${a.rssi} dBm", T.TEXT2)
                dist.put(Format.ago(a.ageS))
            } else {
                sig.put("${a.rssi} dBm" + trends.arrow(a.bssid), T.TEXT)
                dist.put(Air.distance(Air.metres(a.rssi, false, cal)))
            }
            val on = selW == a.bssid && !history
            if (lit != on) {
                lit = on
                row.background = ctx.pressable(if (on) rounded(T.ACCENT_TINT, ctx.dpf(10f)) else null, ctx.dpf(10f))
            }
            row.contentDescription = "${Air.apName(a, masked)}, ${a.rssi} dBm, channel ${a.ch}, ${a.security}" +
                (if (a.joined) ", this board is joined to it" else "")
            findBtn.contentDescription = "Find ${Air.apName(a, masked)}"
        }
    }

    private inner class BleRow : KeyedRows.Holder(ctx) {
        private val row = ctx.row()
        private val mark = View(ctx)
        private val name = ctx.label("", 16f, T.TEXT)
        private val tag = ctx.label("", 12f, T.TEXT3)
        private val sub = ctx.label("", 12.5f, T.TEXT3, numbers = true)
        private val sig = ctx.label("", 13.5f, T.TEXT, numbers = true)
        private val dist = ctx.label("", 12f, T.TEXT3, numbers = true)
        private val findBtn: TextView
        private var key = ""
        private var lit: Boolean? = null

        init {
            val c = ctx
            row.setPadding(c.dp(2), c.dp(10), 0, c.dp(10))
            row.isClickable = true
            row.setOnClickListener { selB = key; render() }
            row.add(mark, c.dp(4), c.dp(34))
            val mid = c.column()
            val top = c.row()
            name.maxLines = 1
            name.ellipsize = TextUtils.TruncateAt.END
            top.add(name, 0, WRAP, weight = 1f)
            tag.maxLines = 1
            top.add(tag, WRAP, WRAP, start = 6)
            mid.add(top)
            sub.maxLines = 1
            sub.ellipsize = TextUtils.TruncateAt.END
            mid.add(sub, top = 2)
            row.add(mid, 0, WRAP, weight = 1f, start = 10)
            val right = c.column()
            right.gravity = Gravity.END
            sig.gravity = Gravity.END
            dist.gravity = Gravity.END
            right.add(sig, WRAP, WRAP)
            right.add(dist, WRAP, WRAP, top = 2)
            row.add(right, WRAP, WRAP, start = 8)
            findBtn = c.button("Find", Btn.QUIET) { find("ble", key) }
            findBtn.minWidth = c.dp(52)
            findBtn.setPadding(c.dp(8), c.dp(6), c.dp(4), c.dp(6))
            row.add(findBtn, WRAP, WRAP)
            view.add(row)
        }

        fun bind(d: NearbyBle) {
            key = d.addr
            val g = Air.group(d)
            mark.background = rounded(Pal.group(g), ctx.dpf(2f))
            val own = Air.hasOwnName(d)
            name.put(Air.bleName(d, masked), if (own) T.TEXT else T.TEXT2)
            tag.put(if (own && d.vendor.isNotEmpty()) d.vendor else "")
            tag.shown(tag.text.isNotEmpty())
            val kind = Air.kindWord(d.type) ?: "kind not known"
            val addr = if (masked) Air.maskAddr(d.addr) else d.addr
            sub.put("$kind · $addr" + if (d.kind != "public") " · ${d.kind}" else "")
            sig.put("${d.rssi} dBm" + trends.arrow(d.addr))
            dist.put(if (d.ageS > 30) "heard ${Format.ago(d.ageS)}" else Air.distance(Air.metres(d.rssi, true, cal)))
            row.alpha = if (d.ageS > 30) .55f else 1f
            val on = selB == d.addr
            if (lit != on) {
                lit = on
                row.background = ctx.pressable(if (on) rounded(T.ACCENT_TINT, ctx.dpf(10f)) else null, ctx.dpf(10f))
            }
            row.contentDescription = "${Air.bleName(d, masked)}, $kind, ${d.rssi} dBm" +
                (if (d.kind == "private") ", private address" else "")
            findBtn.contentDescription = "Find ${Air.bleName(d, masked)}"
        }
    }

    override fun onBack(): Boolean {
        if (tab == FINDER && finder.onBack()) return true
        return false
    }

    companion object {
        const val WIFI = "wifi"
        const val BLE = "bluetooth"
        const val FINDER = "finder"
        val TABS = listOf(WIFI, BLE, FINDER)
    }
}
