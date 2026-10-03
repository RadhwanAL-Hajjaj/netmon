package com.example.netmon.ui

import android.content.res.ColorStateList
import android.text.InputType
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import com.example.netmon.Board
import com.example.netmon.Discovery
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

    override fun build(): View {
        val c = ctx
        val col = c.column()
        col.add(c.label("Find your monitor", 26f, T.TEXT, Fonts.light), top = 16)
        col.add(c.label("This phone needs to be on the same Wi-Fi as the monitor. A monitor that could not join Wi-Fi " +
            "opens its own network, netmon-setup; join that and it answers at 192.168.4.1.", 14f, T.TEXT2), top = 8)

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
        return c.page(col)
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
    }

    override fun onHidden() {
        discovery?.stop()
        discovery = null
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
        d.start(Board.base)
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
        col.add(c.label("netmon ${board.version}, found by ${board.via}", 13f, T.TEXT2), top = 2)
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
        Board.connect(base)
        host.closeConnect()
    }

    override fun render() {}
}
