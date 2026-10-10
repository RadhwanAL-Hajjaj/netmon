package com.example.netmon.ui

import android.content.res.ColorStateList
import android.text.InputFilter
import android.text.InputType
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.widget.FrameLayout
import com.example.netmon.ApiException
import com.example.netmon.AppState
import com.example.netmon.BleLink
import com.example.netmon.Board
import com.example.netmon.Discovery
import com.example.netmon.LinkCodec
import com.example.netmon.MainActivity
import com.example.netmon.NetRoute
import com.example.netmon.NetmonClient

/** Finding the board: shown on first start, and whenever the address needs changing. */
class ConnectScreen(host: MainActivity) : Screen(host), Discovery.Listener {

    override val parts = emptyList<Board.Part>()

    private lateinit var status: TextView
    private lateinit var spinner: ProgressBar
    private lateinit var foundBox: LinearLayout
    private lateinit var foundCard: LinearLayout
    private lateinit var again: TextView
    private lateinit var address: EditText
    private lateinit var connectBtn: TextView
    private lateinit var manualMsg: TextView
    private lateinit var current: TextView
    private lateinit var keep: TextView
    private var discovery: Discovery? = null
    private var foundCount = 0

    // Over Bluetooth
    private val main = Handler(Looper.getMainLooper())
    private lateinit var pairedBox: LinearLayout
    private lateinit var heardBox: LinearLayout
    private lateinit var btStatus: TextView
    private lateinit var btLook: TextView
    private var scan: BleLink.Scan? = null
    private val heard = LinkedHashMap<String, BleLink.Heard>()
    private val stopScan = Runnable { endScan("") }

    override fun build(): View {
        val c = ctx
        val col = c.column()
        col.add(c.label("Find your monitor", 26f, T.TEXT, Fonts.light), top = 16)
        col.add(c.label("On the monitor's Wi-Fi, the app finds it by itself. A monitor that could not join Wi-Fi opens " +
            "its own network, netmon-setup, at 192.168.4.1. A phone paired with the monitor can also reach it over " +
            "Bluetooth: see the end of this page.", 14f, T.TEXT2), top = 8)

        current = col.add(c.label("", 14f, T.TEXT2, numbers = true), top = 16)
        keep = col.add(c.button("Keep using it", Btn.SECONDARY) { host.closeConnect() }, WRAP, WRAP, top = 8)

        val sr = col.add(c.row(), top = 24)
        spinner = ProgressBar(c, null, android.R.attr.progressBarStyleSmall)
        spinner.indeterminateTintList = ColorStateList.valueOf(T.ACCENT)
        sr.add(spinner, c.dp(20), c.dp(20))
        status = sr.add(c.label("", 14f, T.TEXT2, numbers = true), 0, WRAP, weight = 1f, start = 10)

        foundCard = col.add(c.card(), top = 12)
        foundCard.setPadding(c.dp(12), c.dp(4), c.dp(12), c.dp(4))
        foundBox = foundCard.add(c.column())
        foundCard.visibility = View.GONE
        again = col.add(c.button("Search again", Btn.QUIET) { startSearch() }, WRAP, WRAP, top = 8)

        val manual = col.add(c.card(), top = 20)
        manual.add(c.cardTitle("Enter the address"))
        manual.add(c.hint("An IP address such as 192.168.2.30, or host:port if you reach it through a port forward or a VPN."), top = 6)
        address = manual.add(c.input("192.168.2.30", InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI), top = 10)
        address.imeOptions = EditorInfo.IME_ACTION_GO
        address.setOnEditorActionListener { _, action, _ ->
            if (action == EditorInfo.IME_ACTION_GO) { tryManual(); true } else false
        }
        connectBtn = manual.add(c.button("Connect", Btn.PRIMARY) { tryManual() }, MATCH, WRAP, top = 12)
        manualMsg = manual.add(c.label("", 14f, T.BAD), top = 8)
        manualMsg.visibility = View.GONE

        val bt = col.add(c.card(), top = 20)
        bt.add(c.cardTitle("Over Bluetooth"))
        bt.add(c.hint("A phone paired with the monitor reaches it away from its Wi-Fi, within about 10 metres. " +
            "Needs firmware 0.12 or later."), top = 6)
        pairedBox = bt.add(c.column(), top = 6)
        btLook = bt.add(c.button("Look for monitors over Bluetooth", Btn.SECONDARY) { startScan() }, MATCH, WRAP, top = 10)
        btStatus = bt.add(c.label("", 13f, T.TEXT2), top = 8)
        btStatus.visibility = View.GONE
        heardBox = bt.add(c.column(), top = 4)
        return c.page(col)
    }

    // --- Bluetooth -------------------------------------------------------------------

    /** The monitors this phone is paired with: the one the app knows, and any Android holds by the name netmon. */
    private fun renderPaired() {
        pairedBox.removeAllViews()
        val c = ctx
        val list = LinkedHashMap<String, String>()
        AppState.prefs.boardBle?.let { list[LinkCodec.address(it)] = "netmon" }
        for ((addr, name) in BleLink.pairedBoards(c)) list.putIfAbsent(addr, name)
        list.entries.forEachIndexed { i, (addr, name) ->
            if (i > 0) pairedBox.addDivider()
            pairedBox.add(boardRow(name, "$addr · paired", "Use") { choose(LinkCodec.SCHEME + addr) })
        }
    }

    private fun boardRow(title: String, detail: String, action: String, onTap: () -> Unit): View {
        val c = ctx
        val r = c.row()
        r.setPadding(c.dp(4), c.dp(12), c.dp(4), c.dp(12))
        r.background = c.pressable(null, c.dpf(12f))
        r.isClickable = true
        val col = c.column()
        col.add(c.label(title, 17f, T.TEXT, Fonts.medium))
        col.add(c.label(detail, 13f, T.TEXT2, numbers = true), top = 2)
        r.add(col, 0, WRAP, weight = 1f)
        r.add(c.label(action, 15f, T.ACCENT, Fonts.medium), WRAP, WRAP, start = 8)
        r.setOnClickListener { onTap() }
        return r
    }

    private fun startScan() {
        host.askBluetooth(scan = true) {
            if (!BleLink.switchedOn(ctx)) {
                host.askBluetoothOn()
                return@askBluetooth
            }
            endScan("")
            heard.clear()
            heardBox.removeAllViews()
            val sc = BleLink.Scan(ctx) { h -> onHeard(h) }
            val why = sc.start()
            if (why != null) {
                btStatus.text = why
                btStatus.visibility = View.VISIBLE
                return@askBluetooth
            }
            scan = sc
            btLook.enabled(false)
            btStatus.text = "Looking for monitors nearby"
            btStatus.visibility = View.VISIBLE
            main.postDelayed(stopScan, 20_000)
            renderPaired()
        }
    }

    private fun endScan(text: String) {
        main.removeCallbacks(stopScan)
        val sc = scan ?: return
        scan = null
        sc.stop()
        if (!::btLook.isInitialized) return
        btLook.enabled(true)
        btStatus.text = text.ifEmpty {
            if (heard.isEmpty()) "No monitor answered over Bluetooth. Check it is switched on, nearby, and has its Bluetooth link on."
            else "Done looking."
        }
    }

    private fun onHeard(h: BleLink.Heard) {
        if (!::heardBox.isInitialized) return
        heard[h.address] = h
        heardBox.removeAllViews()
        heard.values.forEachIndexed { i, x ->
            if (i > 0) heardBox.addDivider()
            val what = when {
                x.paired -> "paired"
                x.pairing -> "ready to pair"
                else -> "not paired"
            }
            heardBox.add(boardRow(x.name, "${x.address} · $what · ${x.rssi} dBm", if (x.paired) "Use" else "Pair") {
                if (x.paired) choose(LinkCodec.SCHEME + x.address) else pairWith(x)
            })
        }
    }

    /**
     * Pairs with a monitor heard over Bluetooth. The code is the one its
     * Settings page shows, on some device on its Wi-Fi, or the owner's own;
     * the person enters it here and the app gives it to Android.
     */
    private fun pairWith(h: BleLink.Heard) {
        val c = ctx
        val box = c.column()
        box.add(c.label(if (h.pairing) "Enter the 6-digit code the monitor's Settings page shows, or your own pairing code."
            else "Enter your 6-digit pairing code. The monitor opens a pairing window at startup — if it isn't " +
                "responding, restart it and try within 2 minutes. You can also open Settings on its Wi-Fi to start " +
                "a pairing window manually.", 15f, T.TEXT2))
        val code = box.add(c.input("6-digit code", InputType.TYPE_CLASS_NUMBER), top = 12)
        code.filters = arrayOf(InputFilter.LengthFilter(6))
        code.letterSpacing = 0.12f
        val msg = box.add(c.label("", 14f, T.BAD), top = 8)
        msg.visibility = View.GONE
        val d = AlertDialog.Builder(c)
            .setTitle("Pair with ${h.name}")
            .setView(FrameLayout(c).apply {
                setPadding(c.dp(24), c.dp(8), c.dp(24), c.dp(4))
                addView(box, FrameLayout.LayoutParams(MATCH, WRAP))
            })
            .setPositiveButton("Pair", null)
            .setNegativeButton("Cancel", null)
            .create()
        d.setOnShowListener {
            d.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val digits = code.text.toString().trim()
                if (digits.length != 6 || digits.any { it !in '0'..'9' }) {
                    msg.text = "The code is six digits."
                    msg.visibility = View.VISIBLE
                    return@setOnClickListener
                }
                d.dismiss()
                runPair(h, digits)
            }
        }
        d.show()
        code.requestFocus()
    }

    private fun runPair(h: BleLink.Heard, digits: String) {
        endScan("Pairing with ${h.address}")
        val c = ctx
        val box = c.column()
        val status = box.add(c.label("Connecting to the monitor", 15f, T.TEXT2))
        box.add(c.hint("The app enters the code for you. If Android asks for it anyway, type it there."), top = 10)
        val cancelled = java.util.concurrent.atomic.AtomicBoolean(false)
        val d = AlertDialog.Builder(c)
            .setTitle("Pairing")
            .setView(FrameLayout(c).apply {
                setPadding(c.dp(24), c.dp(8), c.dp(24), c.dp(4))
                addView(box, FrameLayout.LayoutParams(MATCH, WRAP))
            })
            .setNegativeButton("Cancel") { _, _ -> cancelled.set(true) }
            .setCancelable(false)
            .create()
        d.show()
        Thread({
            val result: Any = try {
                BleLink.pair(h.address, digits, 110_000, { cancelled.get() }) { byApp ->
                    if (d.isShowing) status.text = if (byApp) "Code entered. Finishing" else "Type the code where Android asks for it"
                }
            } catch (e: ApiException) {
                e
            }
            main.post {
                if (d.isShowing) d.dismiss()
                when (result) {
                    is BleLink.PairResult -> when (result.result) {
                        BleLink.Paired.YES -> choose(LinkCodec.SCHEME + h.address)
                        BleLink.Paired.FAILED -> btStatus.text = "Pairing did not finish. " + result.why
                        BleLink.Paired.CANCELLED -> btStatus.text = ""
                    }
                    is ApiException -> btStatus.text = result.message ?: "Pairing did not work."
                }
                btStatus.visibility = if (btStatus.text.isEmpty()) View.GONE else View.VISIBLE
            }
        }, "netmon-pair").start()
    }

    override fun onShown() {
        val base = Board.base
        current.visibility = if (base != null) View.VISIBLE else View.GONE
        keep.visibility = if (base != null) View.VISIBLE else View.GONE
        if (base != null) {
            current.text = "Using ${NetmonClient.display(base)} now."
            if (address.text.isEmpty()) address.setText(NetmonClient.display(base))
        }
        startSearch()
        renderPaired()
    }

    override fun onHidden() {
        discovery?.stop()
        discovery = null
        endScan("")
    }

    private fun startSearch() {
        discovery?.stop()
        foundBox.removeAllViews()
        foundCount = 0
        foundCard.visibility = View.GONE
        again.visibility = View.GONE
        spinner.visibility = View.VISIBLE
        val lan = NetRoute.local(ctx)
        status.text = if (lan == null) "This phone is not on Wi-Fi. Looking anyway." else "Looking on ${subnetText(lan)}"
        val d = Discovery(ctx, this)
        discovery = d
        // The board's address on Wi-Fi, never its Bluetooth route: looking
        // for boards is a network search.
        d.start(Board.wifiBase)
    }

    private fun subnetText(lan: NetRoute.Local): String = "the network around ${lan.ipText}"

    override fun onFound(board: Discovery.Found) {
        if (!::foundBox.isInitialized) return
        val c = ctx
        if (foundCount > 0) foundBox.addDivider()
        foundCount++
        val r = c.row()
        r.setPadding(c.dp(4), c.dp(12), c.dp(4), c.dp(12))
        r.background = c.pressable(null, c.dpf(12f))
        r.isClickable = true
        val col = c.column()
        col.add(c.label(NetmonClient.display(board.base), 17f, T.TEXT, Fonts.medium, numbers = true))
        col.add(c.label("netmon ${board.version}, found by ${board.via}" + if (board.login) ", asks for a password" else "",
            13f, T.TEXT2), top = 2)
        r.add(col, 0, WRAP, weight = 1f)
        r.add(c.label("Use", 15f, T.ACCENT, Fonts.medium), WRAP, WRAP, start = 8)
        r.setOnClickListener { choose(board.base) }
        foundBox.add(r)
        foundCard.visibility = View.VISIBLE
    }

    override fun onProgress(checked: Int, total: Int) {
        if (!::status.isInitialized) return
        status.text = "Checked $checked of $total addresses"
    }

    override fun onFinished(foundAny: Boolean) {
        if (!::status.isInitialized) return
        discovery = null
        spinner.visibility = View.GONE
        again.visibility = View.VISIBLE
        status.text = when {
            foundCount == 1 -> "Found one monitor."
            foundCount > 1 -> "Found $foundCount monitors."
            else -> "No monitor answered on this network. Check that it is powered and on the same Wi-Fi, or enter its address."
        }
    }

    private fun tryManual() {
        val text = address.text.toString().trim()
        val base = try {
            NetmonClient.normalize(text)
        } catch (e: IllegalArgumentException) {
            showManual(e.message ?: "That does not look like an address.")
            return
        }
        connectBtn.enabled(false)
        connectBtn.text = "Checking"
        manualMsg.visibility = View.GONE
        Thread {
            NetRoute.pinIfNeeded(ctx)
            val found = try {
                Discovery.check(base)
            } catch (e: IllegalArgumentException) {
                null
            }
            host.runOnUiThread {
                connectBtn.enabled(true)
                connectBtn.text = "Connect"
                if (found != null) choose(found.base)
                else showManual("No netmon monitor answered at ${NetmonClient.display(base)}.")
            }
        }.start()
    }

    private fun showManual(text: String) {
        manualMsg.text = text
        manualMsg.visibility = View.VISIBLE
    }

    private fun choose(base: String) {
        discovery?.stop()
        discovery = null
        endScan("")
        Board.connect(base)
        host.closeConnect()
    }

    override fun render() {}
}
