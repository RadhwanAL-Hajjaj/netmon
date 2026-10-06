package com.example.netmon.ui

import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import android.content.res.ColorStateList
import com.example.netmon.ApiException
import com.example.netmon.AppState
import com.example.netmon.BleLink
import com.example.netmon.BleStatus
import com.example.netmon.Board
import com.example.netmon.LinkCodec
import com.example.netmon.LinkMode
import com.example.netmon.MainActivity

/**
 * Settings, Bluetooth: pairing this phone with the board, how the app
 * reaches it, and the board's own Bluetooth link.
 *
 * Pairing needs the board's code, which only someone on its network can see
 * (or the owner's own code, set on the board's Settings page): the app asks
 * for a pairing window over Wi-Fi, gets the code, and gives it to Android
 * itself while the phone pairs. Away from the board's Wi-Fi, the code comes
 * from its Settings page instead and the pairing starts from the Find your
 * monitor screen.
 */
class LinkCard(private val host: MainActivity) {

    private val main = Handler(Looper.getMainLooper())
    private lateinit var state: TextView
    private lateinit var modeRow: LinearLayout
    private val modeChips = HashMap<LinkMode, TextView>()
    private lateinit var modeDetail: TextView
    private lateinit var pairBtn: TextView
    private lateinit var forgetBtn: TextView
    private lateinit var boardLine: KV
    private lateinit var phonesLine: KV
    private lateinit var codeLine: KV
    private lateinit var lastLine: KV
    private lateinit var boardSwitch: android.widget.Switch
    private lateinit var boardRow: LinearLayout
    private lateinit var forgetAllBtn: TextView
    private lateinit var msg: TextView
    private var busy = false
    private var settingSwitch = false

    fun build(): View {
        val c = host
        val card = c.card()
        card.add(c.cardTitle("Bluetooth"))
        state = card.add(c.label("", 14f, T.TEXT2), top = 6)

        card.add(c.fieldLabel("How the app reaches the monitor"), top = 14)
        modeRow = card.add(c.row(), top = 6)
        for (m in LinkMode.values()) {
            val chip = c.chip(m.title, m == AppState.prefs.linkMode) { chooseMode(m) }
            modeChips[m] = chip
            modeRow.add(chip, WRAP, WRAP, end = 8)
        }
        modeDetail = card.add(c.label("", 13f, T.TEXT2), top = 8)

        val r = card.add(c.row(), top = 14)
        pairBtn = r.add(c.button("Pair this phone", Btn.PRIMARY) { pair() }, 0, WRAP, weight = 1f)
        forgetBtn = r.add(c.button("Forget", Btn.SECONDARY) { confirmForgetHere() }, WRAP, WRAP, start = 8)

        card.addDivider(top = 16, bottom = 4)
        card.add(c.fieldLabel("On the monitor"), top = 10)
        val (row, sw) = c.switchRow("Bluetooth link", true) { on -> if (!settingSwitch) switchBoard(on) }
        boardRow = card.add(row, top = 2)
        boardSwitch = sw
        boardLine = card.addKV("Address", top = 2)
        phonesLine = card.addKV("Paired phones")
        codeLine = card.addKV("Pairing code")
        lastLine = card.addKV("Last attempt")
        forgetAllBtn = card.add(c.button("Forget every paired phone", Btn.DANGER) { confirmForgetAll() }, WRAP, WRAP, top = 10)
        msg = card.add(c.label("", 14f, T.TEXT2), top = 8)
        msg.visibility = View.GONE
        card.add(c.hint("Bluetooth reaches about 10 metres, through a wall or two. Firmware updates still go over Wi-Fi. " +
            "Needs firmware 0.12 or later on the monitor."), top = 10)
        return card
    }

    private fun say(text: String, color: Int = T.TEXT2) {
        msg.text = text
        msg.setTextColor(color)
        msg.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    fun render() {
        if (!::state.isInitialized) return
        Board.lostReason?.let {
            Board.lostReason = null
            say(it, T.BAD)
        }
        val p = AppState.prefs
        val paired = p.boardBle != null
        val b = Board.ble
        val missing = Board.missing(Board.Part.BLE)
        state.put(when {
            missing -> "This monitor's firmware has no Bluetooth link. Update it to 0.12 or later."
            Board.onBluetooth -> "Using Bluetooth now" + if (Board.routeChangedAuto) ", because the monitor did not answer on Wi-Fi." else "."
            paired && b?.enabled == false -> "Paired, but the monitor's Bluetooth link is switched off."
            paired -> "Paired. The app can reach the monitor over Bluetooth, away from its Wi-Fi."
            else -> "Not paired. Pair this phone to reach the monitor over Bluetooth, away from its Wi-Fi."
        }, if (missing) T.WARN else T.TEXT2)

        val mode = p.linkMode
        for ((m, chip) in modeChips) {
            chip.styleChip(m == mode)
            chip.enabled(paired || m != LinkMode.BLUETOOTH)
        }
        modeDetail.put(mode.detail)
        modeRow.shown(paired)
        modeDetail.shown(paired)
        pairBtn.text = if (paired) "Pair again" else "Pair this phone"
        pairBtn.enabled(!busy && !missing)
        forgetBtn.shown(paired)

        boardRow.shown(b != null)
        boardLine.show(b != null)
        phonesLine.show(b != null)
        // From firmware 0.13: the owner's own code, and how the last pairing went.
        codeLine.show(b?.codeKnown == true)
        lastLine.show(b?.last != null)
        forgetAllBtn.shown(b != null && b.bonds > 0)
        if (b != null) {
            settingSwitch = true
            boardSwitch.isChecked = b.enabled
            boardSwitch.isEnabled = b.available && !busy
            settingSwitch = false
            boardLine.set(if (b.available) b.addr else "Bluetooth could not start on the monitor")
            phonesLine.set("${b.bonds} of ${b.maxBonds}" + if (b.connected > 0) ", ${b.connected} connected now" else "")
            codeLine.set(if (b.ownCode) "The owner's own" else "New random code each time")
            b.last?.let { l ->
                lastLine.set(l.text.removeSuffix(".") + ", " + com.example.netmon.Format.ago(l.ageS),
                    if (l.why == "paired") T.TEXT else T.WARN)
            }
        }
    }

    // --- how the app reaches the board ------------------------------------------

    private fun chooseMode(m: LinkMode) {
        if (m == LinkMode.BLUETOOTH && AppState.prefs.boardBle == null) return
        Board.setLinkMode(m)
        render()
    }

    // --- pairing -----------------------------------------------------------------

    /**
     * Opens a pairing window on the board over Wi-Fi, gets its code, and
     * pairs, giving Android the code itself. Over Bluetooth alone the board
     * cannot show this phone a code it could trust, so that way goes through
     * the board's Settings page.
     */
    private fun pair() {
        if (busy) return
        val wifi = Board.wifiBase
        if (wifi == null) {
            explainAway()
            return
        }
        host.askBluetooth(scan = false) {
            if (!BleLink.switchedOn(host)) {
                host.askBluetoothOn()
                return@askBluetooth
            }
            busy = true
            say("Asking the monitor for a code")
            render()
            Board.runVia(wifi, { it.blePair() }) { st, err ->
                busy = false
                render()
                when {
                    st != null && st.pairing && st.code.length == 6 && st.addr.isNotEmpty() -> {
                        say("")
                        showCode(st)
                    }
                    err != null && (err.kind == ApiException.Kind.Unreachable || err.kind == ApiException.Kind.Timeout) -> explainAway()
                    else -> say(err?.message ?: "The monitor did not open a pairing window.", T.BAD)
                }
            }
        }
    }

    private fun explainAway() {
        host.dialog("Pair away from the monitor's Wi-Fi",
            host.label("The monitor shows its pairing code on its own Settings page. On any phone or computer on the " +
                "monitor's Wi-Fi, open the monitor's address, go to Settings, Bluetooth and press Pair a phone. Then, " +
                "on this phone, use Find your monitor, Look for monitors over Bluetooth, tap the monitor and enter " +
                "the code.", 15f, T.TEXT2),
            "Find your monitor", { host.showConnect() }, negative = "Close")
    }

    /**
     * The code, large, while the phone pairs. The app gives it to Android
     * itself; should Android ask anyway, it is there to type.
     */
    private fun showCode(st: BleStatus) {
        val c = host
        val box = c.column()
        box.add(c.label("The monitor's code. The app enters it for you; if Android asks for it, type it there.", 15f, T.TEXT2))
        val code = box.add(c.label(st.code.substring(0, 3) + " " + st.code.substring(3), 40f, T.TEXT, Fonts.medium, numbers = true), top = 10)
        code.letterSpacing = 0.08f
        code.contentDescription = st.code.toCharArray().joinToString(" ")
        val wait = box.add(c.row(), top = 14)
        val spin = ProgressBar(c, null, android.R.attr.progressBarStyleSmall)
        spin.indeterminateTintList = ColorStateList.valueOf(T.ACCENT)
        wait.add(spin, c.dp(20), c.dp(20))
        val status = wait.add(c.label("Connecting to the monitor", 14f, T.TEXT2), 0, WRAP, weight = 1f, start = 10)
        box.add(c.hint("The code works for one phone, for the next two minutes. Three wrong tries close the window."), top = 12)
        val cancelled = java.util.concurrent.atomic.AtomicBoolean(false)
        val d = AlertDialog.Builder(c)
            .setTitle("Pair with the monitor")
            .setView(FrameLayout(c).apply {
                setPadding(c.dp(24), c.dp(8), c.dp(24), c.dp(4))
                addView(box, FrameLayout.LayoutParams(MATCH, WRAP))
            })
            .setNegativeButton("Cancel") { _, _ -> cancelled.set(true) }
            .setCancelable(false)
            .create()
        d.show()
        main.postDelayed({ if (d.isShowing && status.text == "Connecting to the monitor") status.text = "Pairing" }, 3000)
        busy = true
        render()
        val addr = st.addr
        val digits = st.code
        Thread({
            val result = try {
                BleLink.pair(addr, digits, 110_000, { cancelled.get() }) { byApp ->
                    if (d.isShowing) status.text = if (byApp) "Code entered. Finishing" else "Type the code where Android asks for it"
                }
            } catch (e: ApiException) {
                main.post {
                    busy = false
                    if (d.isShowing) d.dismiss()
                    say(e.message ?: "Pairing did not work.", T.BAD)
                    render()
                }
                return@Thread
            }
            main.post {
                busy = false
                if (d.isShowing) d.dismiss()
                when (result.result) {
                    BleLink.Paired.YES -> {
                        Board.paired(addr)
                        say("Paired. The app now reaches the monitor over Bluetooth whenever Wi-Fi cannot.", T.OK)
                        Board.refresh(Board.Part.BLE)
                    }
                    BleLink.Paired.FAILED -> {
                        say("Pairing did not finish. " + result.why, T.BAD)
                        explainFailure()
                    }
                    BleLink.Paired.CANCELLED -> {
                        say("")
                        Board.runVia(Board.wifiBase, { it.blePair(stop = true) }) { _, _ -> Board.refresh(Board.Part.BLE) }
                    }
                }
                render()
            }
        }, "netmon-pair").start()
    }

    /**
     * The board's own account of the attempt (firmware 0.13), which knows
     * better than Android whether the code was wrong or the connection went,
     * and whether its window is still open for another try.
     */
    private fun explainFailure() {
        Board.runVia(Board.wifiBase, { it.ble() }) { st, _ ->
            if (st == null) return@runVia
            Board.setBle(st)
            if (st.result != "failed" || st.whyText.isEmpty()) return@runVia
            val more = when {
                st.pairing -> " The window is still open with the same code: tap Pair again to try once more" +
                    (if (st.triesLeft in 1..2) " (${st.triesLeft} ${if (st.triesLeft == 1) "try" else "tries"} left)." else ".")
                st.triesLeft == 0 -> " Three tries failed, so the window closed. Pair again for a new window."
                else -> " Pair again for a new window."
            }
            say("Pairing did not finish. The monitor says: " + st.whyText + more, T.BAD)
            render()
        }
    }

    // --- forgetting ----------------------------------------------------------------

    private fun confirmForgetHere() {
        host.dialog("Forget the monitor on this phone?",
            host.label("This phone stops using Bluetooth for the monitor and drops the pairing. The monitor keeps " +
                "its side until it forgets every phone; pair again at any time.", 15f, T.TEXT2),
            "Forget", { forgetHere() })
    }

    private fun forgetHere(then: String = "This phone no longer uses Bluetooth for the monitor.") {
        val bl = AppState.prefs.boardBle
        if (bl == null) {
            say(then, T.OK)
            render()
            return
        }
        val addr = LinkCodec.address(bl)
        Thread({
            BleLink.unpair(addr)
            main.post {
                Board.unpaired()
                say(then, T.OK)
                render()
            }
        }, "netmon-unpair").start()
    }

    private fun confirmForgetAll() {
        val viaBt = Board.onBluetooth
        host.dialog("Forget every paired phone?",
            host.label("Every phone, this one included, has to pair again with a new code before it can use " +
                "Bluetooth." + if (viaBt) " This phone is using Bluetooth now, so it loses the monitor until it is on " +
                "the monitor's Wi-Fi." else "", 15f, T.TEXT2),
            "Forget all", { forgetAll() })
    }

    private fun forgetAll() {
        busy = true
        render()
        Board.run({ it.bleForget() }) { st, err ->
            busy = false
            if (st != null) Board.setBle(st)
            if (err != null && err.kind != ApiException.Kind.Unreachable) {
                say(err.message ?: "The monitor did not forget its phones.", T.BAD)
                render()
                return@run
            }
            // This phone's side of the pairing is useless now.
            forgetHere("The monitor forgot every paired phone. Pair again for a new code.")
        }
    }

    // --- the board's switch ---------------------------------------------------------

    private fun switchBoard(on: Boolean) {
        if (!on && Board.onBluetooth) {
            host.dialog("Switch the monitor's Bluetooth off?",
                host.label("This phone is using Bluetooth now, so it loses the monitor until it is on the monitor's Wi-Fi.", 15f, T.TEXT2),
                "Switch off", { setBoard(false) })
            render()      // put the switch back until confirmed
            return
        }
        setBoard(on)
    }

    private fun setBoard(on: Boolean) {
        busy = true
        render()
        Board.run({ it.setBle(on) }) { st, err ->
            busy = false
            if (st != null) Board.setBle(st)
            if (err != null) {
                say(err.message ?: "The monitor did not change its Bluetooth link.", T.BAD)
                Board.refresh(Board.Part.BLE)
            } else {
                say("")
            }
            render()
        }
        Toast.makeText(host, if (on) "Switching the monitor's Bluetooth on" else "Switching the monitor's Bluetooth off", Toast.LENGTH_SHORT).show()
    }
}
