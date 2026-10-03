package com.example.netmon.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.text.Editable
import android.text.InputType
import android.text.TextUtils
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.Device
import com.example.netmon.Format
import com.example.netmon.MainActivity

class DevicesScreen(host: MainActivity) : Screen(host) {

    override val parts = listOf(Board.Part.HEALTH, Board.Part.DEVICES)

    private enum class Filter(val key: String, val title: String) {
        ALL("all", "All"), ONLINE("online", "Online"), UNKNOWN("unknown", "Unrecognised"),
        PRIVATE("private", "Private"), OFFLINE("offline", "Offline")
    }

    private enum class Sort(val key: String, val title: String) {
        IP("ip", "address"), NAME("name", "name"), STATUS("status", "status"),
        MAKER("vendor", "maker"), UPTIME("uptime", "time online")
    }

    private var filter = Filter.ALL
    private var sort = Sort.values().firstOrNull { it.key == AppState.prefs.deviceSort } ?: Sort.IP
    private lateinit var search: EditText
    private val chips = HashMap<Filter, TextView>()
    private lateinit var sortButton: TextView
    private lateinit var listBox: LinearLayout
    private lateinit var empty: TextView
    private lateinit var matchNote: TextView
    private var renderedKey = ""
    private var pendingMac: String? = null

    override fun build(): View {
        val c = ctx
        val col = c.column()

        search = col.add(c.input("Search name, address or maker", InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS), top = 8)
        search.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) { render() }
        })

        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val chipRow = c.row()
        scroller.addView(chipRow, android.widget.FrameLayout.LayoutParams(WRAP, WRAP))
        for (f in Filter.values()) {
            val chip = c.chip(f.title, f == filter) {
                filter = f
                for ((k, v) in chips) v.styleChip(k == filter)
                render()
            }
            chips[f] = chip
            chipRow.add(chip, WRAP, WRAP, end = 8)
        }
        col.add(scroller, top = 12)

        val sortRow = col.add(c.row(), top = 10)
        matchNote = sortRow.add(c.label("", 13f, T.TEXT2), 0, WRAP, weight = 1f)
        sortButton = sortRow.add(c.button("", Btn.QUIET) { v -> chooseSort(v) }, WRAP, WRAP)
        sortButton.setPadding(c.dp(10), c.dp(6), c.dp(4), c.dp(6))
        sortButton.minHeight = c.dp(40)

        val box = col.add(c.card(), top = 6)
        box.setPadding(c.dp(12), c.dp(4), c.dp(12), c.dp(4))
        listBox = box.add(c.column())
        empty = box.add(c.label("", 14f, T.TEXT2), top = 14, bottom = 14)
        empty.gravity = Gravity.CENTER

        col.add(c.hint("Names come from DHCP, so a device shows its name after it next joins the network. " +
            "Marks on the left: green known, blue private address, red unrecognised, grey offline."), top = 14)

        return c.page(col)
    }

    /** Opens a device's details once its row exists, as when arriving from a notification. */
    fun reveal(mac: String) {
        pendingMac = mac.uppercase()
        render()
    }

    private fun chooseSort(anchor: View) {
        val menu = PopupMenu(ctx, anchor)
        Sort.values().forEachIndexed { i, s -> menu.menu.add(0, i, i, "Sort by ${s.title}") }
        menu.setOnMenuItemClickListener { item ->
            sort = Sort.values()[item.itemId]
            AppState.prefs.deviceSort = sort.key
            render()
            true
        }
        menu.show()
    }

    private fun matches(d: Device, q: String): Boolean {
        if (q.isEmpty()) return true
        val hay = listOf(d.ip, d.mac, d.mac.replace(":", ""), d.vendor, d.hostname, Format.statusWord(d.status),
            Format.deviceName(d)).joinToString(" ").lowercase()
        return q.lowercase().split(' ').filter { it.isNotEmpty() }.all { hay.contains(it) }
    }

    private fun rank(d: Device) = when {
        d.isUnknown -> 0
        d.isPrivate -> 1
        else -> 2
    }

    private fun comparator(): Comparator<Device> = when (sort) {
        Sort.IP -> Comparator { a, b -> Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip)) }
        Sort.NAME -> Comparator { a, b ->
            val r = Format.deviceName(a).lowercase().compareTo(Format.deviceName(b).lowercase())
            if (r != 0) r else Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip))
        }
        Sort.STATUS -> Comparator { a, b ->
            val r = rank(a) - rank(b)
            if (r != 0) r else Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip))
        }
        Sort.MAKER -> Comparator { a, b ->
            val r = Format.vendorLine(a).lowercase().compareTo(Format.vendorLine(b).lowercase())
            if (r != 0) r else Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip))
        }
        Sort.UPTIME -> Comparator { a, b ->
            if (a.online != b.online) (if (a.online) -1 else 1)
            else if (a.online) b.upS.compareTo(a.upS)
            else a.lastSeenS.compareTo(b.lastSeenS)
        }
    }

    override fun render() {
        if (!::listBox.isInitialized) return
        val all = Board.devices
        sortButton.text = "By ${sort.title}"

        val counts = mapOf(
            Filter.ALL to (all?.size ?: 0),
            Filter.ONLINE to (all?.count { it.online } ?: 0),
            Filter.UNKNOWN to (all?.count { it.isUnknown } ?: 0),
            Filter.PRIVATE to (all?.count { it.isPrivate } ?: 0),
            Filter.OFFLINE to (all?.count { !it.online } ?: 0),
        )
        for ((f, chip) in chips) {
            val n = counts[f] ?: 0
            chip.text = if (all == null) f.title else "${f.title} $n"
        }

        if (all == null) {
            listBox.removeAllViews()
            renderedKey = ""
            empty.visibility = View.VISIBLE
            empty.text = if (Board.link == Board.Link.LOST) "The monitor is not answering." else "Reading the device list"
            matchNote.text = ""
            return
        }

        val q = search.text?.toString()?.trim() ?: ""
        val shown = all.filter { d ->
            when (filter) {
                Filter.ALL -> true
                Filter.ONLINE -> d.online
                Filter.UNKNOWN -> d.isUnknown
                Filter.PRIVATE -> d.isPrivate
                Filter.OFFLINE -> !d.online
            } && matches(d, q)
        }.sortedWith(comparator())

        matchNote.text = if (q.isNotEmpty() || filter != Filter.ALL) "${shown.size} of ${all.size} devices" else "${all.size} devices"

        val key = shown.joinToString("|") { "${it.mac},${it.ip},${it.hostname},${it.status},${it.online},${it.upS / 60},${it.lastSeenS / 60}" }
        if (key != renderedKey) {
            renderedKey = key
            listBox.removeAllViews()
            shown.forEachIndexed { i, d ->
                if (i > 0) listBox.addDivider()
                listBox.add(rowFor(d))
            }
        }
        empty.visibility = if (shown.isEmpty()) View.VISIBLE else View.GONE
        empty.text = when {
            all.isEmpty() -> "No devices found yet. The first sweep runs a minute after the monitor starts."
            q.isNotEmpty() -> "No device matches \"$q\"."
            else -> "No ${filter.title.lowercase()} devices."
        }

        val want = pendingMac
        if (want != null) {
            val d = all.firstOrNull { it.mac == want }
            if (d != null) {
                pendingMac = null
                showDetail(d)
            }
        }
    }

    private fun rowFor(d: Device): View {
        val c = ctx
        val r = c.row()
        r.setPadding(c.dp(4), c.dp(12), c.dp(4), c.dp(12))
        r.background = c.pressable(null, c.dpf(12f))
        r.isClickable = true
        r.setOnClickListener { showDetail(d) }

        val mark = View(c)
        mark.background = rounded(T.status(d.status, d.online), c.dpf(2f))
        r.add(mark, c.dp(4), c.dp(38))

        val mid = c.column()
        val top = c.row()
        val name = c.label(Format.deviceName(d), 16f, if (d.online) T.TEXT else T.TEXT2, if (d.isUnknown) Fonts.medium else Fonts.regular)
        name.maxLines = 1
        name.ellipsize = TextUtils.TruncateAt.END
        top.add(name, 0, WRAP, weight = 1f)
        if (d.self) top.add(c.label("this monitor", 12f, T.TEXT3), WRAP, WRAP, start = 8)
        mid.add(top)
        val second = c.row()
        second.add(c.label(d.ip.ifEmpty { "no address" }, 13f, T.TEXT2, numbers = true), WRAP, WRAP)
        val maker = c.label(Format.vendorLine(d), 13f, T.TEXT3)
        maker.maxLines = 1
        maker.ellipsize = TextUtils.TruncateAt.END
        second.add(maker, 0, WRAP, weight = 1f, start = 12)
        mid.add(second, top = 2)
        r.add(mid, 0, WRAP, weight = 1f, start = 12)

        val right = c.column()
        right.gravity = Gravity.END
        if (d.online) {
            right.add(c.label(Format.duration(d.upS), 13f, T.TEXT2, numbers = true).apply { gravity = Gravity.END }, WRAP, WRAP)
        } else {
            right.add(c.label("offline", 12f, T.TEXT3).apply { gravity = Gravity.END }, WRAP, WRAP)
            right.add(c.label(Format.ago(d.lastSeenS), 12f, T.TEXT3, numbers = true).apply { gravity = Gravity.END }, WRAP, WRAP)
        }
        r.add(right, WRAP, WRAP, start = 10)

        r.contentDescription = "${Format.deviceName(d)}, ${Format.statusWord(d.status)}, ${d.ip}, " +
            if (d.online) "online for ${Format.duration(d.upS)}" else "offline, last seen ${Format.ago(d.lastSeenS)}"
        return r
    }

    private fun showDetail(d: Device) {
        val c = ctx
        val body = c.column()
        val status = if (d.online) Format.statusWord(d.status) else "${Format.statusWord(d.status)}, offline"
        body.add(c.label(status, 14f, T.status(d.status, d.online), Fonts.medium))
        val why = Format.statusExplained(d)
        if (why.isNotEmpty()) body.add(c.label(why, 13f, T.TEXT2), top = 4)
        body.add(c.column().apply { minimumHeight = c.dp(6) })
        body.addKV("Address", top = 8).set(d.ip)
        body.addKV("Hardware address").set(d.mac)
        body.addKV("Maker").set(if (d.vendor.isNotBlank()) d.vendor else Format.vendorLine(d))
        body.addKV("Name from DHCP").set(d.hostname.ifBlank { "Not heard yet" })
        body.addKV("Address type").set(if (d.randomised) "Randomised (private)" else "Set by the manufacturer")
        if (d.online) body.addKV("Online for").set(Format.duration(d.upS))
        body.addKV("Last seen").set(Format.ago(d.lastSeenS))

        val actions = body.add(c.row(), top = 16)
        actions.add(c.button("Copy address", Btn.SECONDARY) { copy("IP address", d.ip) }, 0, WRAP, weight = 1f)
        actions.add(c.button("Copy MAC", Btn.SECONDARY) { copy("MAC address", d.mac) }, 0, WRAP, weight = 1f, start = 8)
        if (d.online && d.ip.isNotBlank()) {
            body.add(c.button("Open its web page", Btn.QUIET) {
                try {
                    host.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("http://${d.ip}/")))
                } catch (e: RuntimeException) {
                    Toast.makeText(c, "No browser is installed.", Toast.LENGTH_SHORT).show()
                }
            }, WRAP, WRAP, top = 6)
        }
        c.dialog(Format.deviceName(d), body, "Close", null, negative = null)
    }

    private fun copy(label: String, text: String) {
        val cm = ctx.getSystemService(ClipboardManager::class.java) ?: return
        cm.setPrimaryClip(ClipData.newPlainText(label, text))
        Toast.makeText(ctx, "Copied $text", Toast.LENGTH_SHORT).show()
    }
}
