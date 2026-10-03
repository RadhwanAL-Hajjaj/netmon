package com.example.netmon.ui

import android.net.Uri
import android.provider.OpenableColumns
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.EditText
import android.widget.Toast
import android.content.res.ColorStateList
import android.os.Handler
import android.os.Looper
import com.example.netmon.AlertMode
import com.example.netmon.Alerts
import com.example.netmon.ApiException
import com.example.netmon.AppState
import com.example.netmon.Board
import com.example.netmon.BuildInfo
import com.example.netmon.ConfigUpdate
import com.example.netmon.Firmware
import com.example.netmon.Format
import com.example.netmon.MainActivity
import com.example.netmon.NetmonClient
import com.example.netmon.RestartWatch
import com.example.netmon.SavedNetwork
import com.example.netmon.Validate
import com.example.netmon.WifiNetwork

class SettingsScreen(host: MainActivity) : Screen(host) {

    override val parts = listOf(Board.Part.HEALTH, Board.Part.CONFIG, Board.Part.NETWORKS, Board.Part.DHCP)

    private val main = Handler(Looper.getMainLooper())

    // This monitor
    private lateinit var boardAddr: KV
    private lateinit var boardFw: KV
    private lateinit var boardUp: KV

    // Wi-Fi form
    private lateinit var wifiNow: TextView
    private lateinit var setupNote: TextView
    private lateinit var ssid: EditText
    private lateinit var scanBtn: TextView
    private lateinit var pass: EditText
    private lateinit var passHint: TextView
    private lateinit var dhcpChip: TextView
    private lateinit var staticChip: TextView
    private lateinit var staticBox: LinearLayout
    private lateinit var ip: EditText
    private lateinit var mask: EditText
    private lateinit var gw: EditText
    private lateinit var dns: EditText
    private lateinit var advToggle: TextView
    private lateinit var advBox: LinearLayout
    private lateinit var scanS: EditText
    private lateinit var probeS: EditText
    private lateinit var offlineS: EditText
    private lateinit var learnS: EditText
    private lateinit var saveBtn: TextView
    private lateinit var saveMsg: TextView
    private var useDhcp = true
    private var formFor: String? = null
    private var saving = false

    // Network history
    private lateinit var netList: LinearLayout
    private lateinit var netNote: TextView
    private lateinit var netMsg: TextView
    private var netKey = ""

    // DHCP
    private lateinit var dListener: KV
    private lateinit var dReceived: KV
    private lateinit var dStatus: KV
    private lateinit var dClient: KV
    private lateinit var dIp: KV
    private lateinit var dServer: KV
    private lateinit var dLease: KV
    private lateinit var dMessage: KV
    private lateinit var dHost: KV
    private lateinit var dLast: KV
    private lateinit var dCount: KV

    // Firmware
    private lateinit var fwRunning: TextView
    private lateinit var fwInfo: TextView
    private lateinit var fwKey: EditText
    private lateinit var fwRemember: Switch
    private lateinit var fwBtn: TextView
    private lateinit var fwBar: ProgressBar
    private lateinit var fwMsg: TextView
    private var image: ByteArray? = null
    private var imageName = ""
    private var imageProblem: String? = null
    private var uploading = false

    // Notifications
    private val modeChips = HashMap<AlertMode, TextView>()
    private lateinit var modeDetail: TextView
    private val everyChips = HashMap<Int, TextView>()
    private lateinit var blocked: LinearLayout

    override fun build(): View {
        val c = ctx
        val col = c.column()
        col.add(buildBoardCard(), top = 8)
        col.add(buildWifiCard(), top = 16)
        col.add(buildHistoryCard(), top = 16)
        col.add(buildDhcpCard(), top = 16)
        col.add(buildFirmwareCard(), top = 16)
        col.add(buildAlertsCard(), top = 16)
        col.add(buildAboutCard(), top = 16)
        return c.page(col)
    }

    // --- This monitor -------------------------------------------------------

    private fun buildBoardCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("This monitor"))
        boardAddr = card.addKV("Address", top = 10)
        boardFw = card.addKV("Firmware")
        boardUp = card.addKV("Running for")
        val r = card.add(c.row(), top = 14)
        r.add(c.button("Use another monitor", Btn.SECONDARY) { host.showConnect() }, 0, WRAP, weight = 1f)
        r.add(c.button("Restart it", Btn.DANGER) { confirmReboot() }, 0, WRAP, weight = 1f, start = 8)
        return card
    }

    private fun confirmReboot() {
        ctx.dialog("Restart the monitor?", ctx.label("It stops watching for about ten seconds, and its own event list starts over. " +
            "This phone keeps its copy.", 15f, T.TEXT2), "Restart", {
            Board.run({ it.reboot() }) { _, err ->
                val text = if (err == null || err.kind == ApiException.Kind.Unreachable) "Restarting. It should answer again in about ten seconds."
                else err.message ?: "The monitor did not restart."
                Toast.makeText(ctx, text, Toast.LENGTH_LONG).show()
            }
        })
    }

    // --- Wi-Fi ----------------------------------------------------------------

    private fun buildWifiCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("Wi-Fi"))
        wifiNow = card.add(c.label("", 14f, T.TEXT2), top = 6)
        setupNote = card.add(c.label("This monitor could not join any network it knows, so it opened its own. " +
            "Pick a network and save. Keep the address automatic unless the new network uses fixed addresses.", 14f, T.WARN), top = 8)

        card.add(c.fieldLabel("Network name"), top = 14)
        val sr = card.add(c.row(), top = 6)
        ssid = sr.add(c.input("Network name"), 0, WRAP, weight = 1f)
        scanBtn = sr.add(c.button("Scan", Btn.SECONDARY) { doScan() }, WRAP, WRAP, start = 8)

        card.add(c.fieldLabel("Password"), top = 12)
        pass = card.add(c.input("unchanged", InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD), top = 6)
        passHint = card.add(c.hint("Leave blank to keep the saved password."), top = 6)

        card.add(c.fieldLabel("How the monitor gets its address"), top = 14)
        val mr = card.add(c.row(), top = 6)
        dhcpChip = mr.add(c.chip("Automatically", true) { setMode(true) }, WRAP, WRAP)
        staticChip = mr.add(c.chip("Fixed address", false) { setMode(false) }, WRAP, WRAP, start = 8)

        staticBox = card.add(c.column(), top = 4)
        ip = addField(staticBox, "Address", "192.168.2.30")
        mask = addField(staticBox, "Netmask", "255.255.255.0")
        gw = addField(staticBox, "Gateway", "192.168.2.1")
        dns = addField(staticBox, "DNS", "blank to use the gateway")
        staticBox.visibility = View.GONE

        advToggle = card.add(c.button("Show advanced monitoring", Btn.QUIET) { toggleAdvanced() }, WRAP, WRAP, top = 10)
        advToggle.setPadding(0, c.dp(8), c.dp(8), c.dp(8))
        advBox = card.add(c.column())
        scanS = addField(advBox, "Scan interval, seconds (10 to 3600)", "60", number = true)
        probeS = addField(advBox, "Gateway check interval, seconds (10 to 3600)", "30", number = true)
        offlineS = addField(advBox, "Offline after, seconds (30 to 86400)", "180", number = true)
        learnS = addField(advBox, "Learning window, seconds (0 to 86400)", "600", number = true)
        advBox.add(c.hint("New devices seen during the learning window count as known. Short intervals mean more Wi-Fi traffic."), top = 8)
        advBox.visibility = View.GONE

        saveBtn = card.add(c.button("Save and restart", Btn.PRIMARY) { confirmSave() }, MATCH, WRAP, top = 16)
        saveMsg = card.add(c.label("", 14f, T.TEXT2), top = 8)
        saveMsg.visibility = View.GONE
        card.add(c.hint("Saving restarts the monitor. If it cannot join the network, it opens a setup network called " +
            "netmon-setup at 192.168.4.1."), top = 8)
        return card
    }

    private fun addField(box: LinearLayout, label: String, hint: String, number: Boolean = false): EditText {
        box.add(ctx.fieldLabel(label), top = 12)
        val type = if (number) InputType.TYPE_CLASS_NUMBER else (InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)
        return box.add(ctx.input(hint, type), top = 6)
    }

    private fun setMode(dhcp: Boolean) {
        useDhcp = dhcp
        dhcpChip.styleChip(dhcp)
        staticChip.styleChip(!dhcp)
        staticBox.visibility = if (dhcp) View.GONE else View.VISIBLE
    }

    private fun toggleAdvanced() {
        val show = advBox.visibility != View.VISIBLE
        advBox.visibility = if (show) View.VISIBLE else View.GONE
        advToggle.text = if (show) "Hide advanced monitoring" else "Show advanced monitoring"
    }

    private fun fillForm() {
        val cfg = Board.config ?: return
        val key = "${Board.base}|${cfg.version}|${cfg.ssid}"
        if (formFor == key) return
        formFor = key
        ssid.setText(cfg.ssid)
        pass.setText("")
        pass.hint = if (cfg.hasPassword) "unchanged" else "none, this is an open network"
        passHint.text = if (cfg.hasPassword) "Leave blank to keep the saved password." else "This network is stored without a password."
        setMode(cfg.useDhcp)
        if (!cfg.useDhcp) {
            ip.setText(cfg.ip)
            mask.setText(cfg.mask)
            gw.setText(cfg.gw)
            dns.setText(cfg.dns)
        } else {
            val a = cfg.active
            // Offer the address in use now as the starting point for a fixed one.
            if (a != null && a.source == "dhcp") {
                ip.setText(a.ip); mask.setText(a.mask); gw.setText(a.gw); dns.setText(a.dns)
            }
        }
        scanS.setText(cfg.scanIntervalS.toString())
        probeS.setText(cfg.probeIntervalS.toString())
        offlineS.setText(cfg.offlineAfterS.toString())
        learnS.setText(cfg.learningWindowS.toString())
    }

    private fun say(tv: TextView, text: String, color: Int = T.TEXT2) {
        tv.text = text
        tv.setTextColor(color)
        tv.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun doScan() {
        scanBtn.enabled(false)
        scanBtn.text = "Scanning"
        Board.run({ it.scan() }) { list, err ->
            scanBtn.enabled(true)
            scanBtn.text = "Scan"
            if (list == null) {
                say(saveMsg, err?.message?.let { "Wi-Fi scan failed: $it" } ?: "Wi-Fi scan failed or is busy; try again in a few seconds.", T.BAD)
                return@run
            }
            if (list.isEmpty()) {
                say(saveMsg, "No networks found.", T.TEXT2)
                return@run
            }
            showNetworks(list)
        }
    }

    private fun showNetworks(list: List<WifiNetwork>) {
        val c = ctx
        val box = c.column()
        val scroll = ScrollView(c)
        scroll.addView(box, FrameLayout.LayoutParams(MATCH, WRAP))
        var dialog: android.app.AlertDialog? = null
        list.forEachIndexed { i, n ->
            if (i > 0) box.addDivider()
            val r = c.row()
            r.setPadding(c.dp(4), c.dp(12), c.dp(4), c.dp(12))
            r.background = c.pressable(null, c.dpf(10f))
            r.isClickable = true
            val bars = SignalBars(c)
            bars.bars = Format.signalBars(n.rssi)
            r.add(bars, WRAP, WRAP)
            r.add(c.label(n.ssid, 16f), 0, WRAP, weight = 1f, start = 12)
            r.add(c.label("${n.rssi} dBm", 13f, T.TEXT2, numbers = true), WRAP, WRAP, start = 8)
            r.setOnClickListener {
                ssid.setText(n.ssid)
                pass.setText("")
                pass.hint = "password for ${n.ssid}"
                passHint.text = "Enter the password, or leave blank if this network is already saved or is open."
                dialog?.dismiss()
            }
            box.add(r)
        }
        dialog = c.dialog("Networks in range", scroll, null, null, negative = "Close")
    }

    private fun intOf(e: EditText): Int? = e.text.toString().trim().toIntOrNull()

    private fun confirmSave() {
        if (saving) return
        val cfg = Board.config
        val name = ssid.text.toString()
        val pw = pass.text.toString()
        Validate.credentials(name, pw)?.let { say(saveMsg, cap(it), T.BAD); return }
        if (!useDhcp) {
            Validate.static(ip.text.toString().trim(), mask.text.toString().trim(), gw.text.toString().trim())?.let { say(saveMsg, cap(it), T.BAD); return }
            Validate.dns(dns.text.toString())?.let { say(saveMsg, cap(it), T.BAD); return }
        }
        val sc = intOf(scanS) ?: cfg?.scanIntervalS ?: 60
        val pr = intOf(probeS) ?: cfg?.probeIntervalS ?: 30
        val of = intOf(offlineS) ?: cfg?.offlineAfterS ?: 180
        val le = intOf(learnS) ?: cfg?.learningWindowS ?: 600
        Validate.intervals(sc, pr, of, le)?.let { say(saveMsg, cap(it) + ".", T.BAD); return }

        val update = ConfigUpdate(
            ssid = name, pass = pw, useDhcp = useDhcp,
            ip = ip.text.toString().trim(), mask = mask.text.toString().trim(), gw = gw.text.toString().trim(),
            dns = dns.text.toString().trim(),
            scanIntervalS = sc, probeIntervalS = pr, offlineAfterS = of, learningWindowS = le,
        )
        val moving = cfg != null && cfg.active?.ssid != name
        val text = buildString {
            append("The monitor restarts")
            if (moving) append(" and joins $name") else append(" on $name")
            append(if (useDhcp) " with an automatic address." else " at ${update.ip}.")
            append(" If it cannot join, it opens netmon-setup at 192.168.4.1.")
            if (moving) append(" This phone will need to be on $name to reach it.")
        }
        ctx.dialog("Save and restart?", ctx.label(text, 15f, T.TEXT2), "Save and restart", { save(update) })
    }

    private fun cap(s: String) = s.replaceFirstChar { it.uppercase() }

    private fun save(update: ConfigUpdate) {
        saving = true
        saveBtn.enabled(false)
        say(saveMsg, "Saving")
        Board.run({ c ->
            c.saveConfig(update)
            // Same as the web page: the board applies settings when it starts.
            try {
                c.reboot()
            } catch (e: ApiException) {
                // A reset that cuts the answer short still happened.
            }
        }) { _, err ->
            saving = false
            saveBtn.enabled(true)
            if (err != null) {
                say(saveMsg, err.message ?: "Could not save.", T.BAD)
            } else {
                say(saveMsg, "Saved. The monitor is restarting.", T.OK)
                formFor = null
                main.postDelayed({ Board.refresh(Board.Part.HEALTH, Board.Part.CONFIG, Board.Part.NETWORKS) }, 12_000)
            }
        }
    }

    // --- Network history ----------------------------------------------------

    private fun buildHistoryCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("Network history"))
        card.add(c.hint("Networks this monitor has been set up on, newest first. At start-up it tries them in this order, " +
            "waiting up to 12 seconds on each one that does not answer."), top = 6)
        netList = card.add(c.column(), top = 6)
        netNote = card.add(c.label("", 13f, T.TEXT2), top = 8)
        netMsg = card.add(c.label("", 13f, T.BAD), top = 6)
        netMsg.visibility = View.GONE
        return card
    }

    private fun renderNetworks() {
        val list = Board.networks
        val err = Board.partErrors[Board.Part.NETWORKS]
        if (list == null) {
            netList.removeAllViews()
            netKey = ""
            netNote.text = err ?: "Reading"
            return
        }
        val key = list.joinToString("|") { "${it.order},${it.ssid},${it.active},${it.boot},${it.bootMs},${it.reason},${it.hasPassword}" }
        if (key != netKey) {
            netKey = key
            netList.removeAllViews()
            list.forEachIndexed { i, n ->
                if (i > 0) netList.addDivider()
                netList.add(networkRow(n))
            }
        }
        netNote.text = Format.historyNote(list)
        netNote.visibility = if (netNote.text.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun networkRow(n: SavedNetwork): View {
        val c = ctx
        val r = c.row()
        r.gravity = Gravity.TOP
        r.setPadding(0, c.dp(12), 0, c.dp(12))
        r.add(c.label(n.order.toString(), 14f, T.TEXT3, numbers = true), c.dp(22), WRAP)
        val mid = c.column()
        mid.add(c.label(n.ssid, 15f, T.TEXT, Fonts.medium))
        mid.add(c.label(if (n.hasPassword) "Password saved" else "Open network", 12.5f, T.TEXT3), top = 2)
        val j = Format.joinText(n)
        val tone = when (j.tone) {
            Format.Tone.Good -> T.OK
            Format.Tone.Warn -> T.WARN
            Format.Tone.Bad -> T.BAD
            Format.Tone.Quiet -> T.TEXT2
        }
        mid.add(c.label("${j.label}, ${j.detail}", 13f, tone), top = 6)
        r.add(mid, 0, WRAP, weight = 1f)
        if (n.active) {
            val pill = c.label("In use", 12f, T.OK, Fonts.medium)
            pill.background = rounded(0, c.dpf(10f), c.dp(1), T.OK)
            pill.setPadding(c.dp(8), c.dp(2), c.dp(8), c.dp(2))
            r.add(pill, WRAP, WRAP, start = 8)
        } else {
            r.add(c.button("Forget", Btn.SECONDARY) { confirmForget(n) }, WRAP, WRAP, start = 8)
        }
        return r
    }

    private fun confirmForget(n: SavedNetwork) {
        ctx.dialog("Forget ${n.ssid}?", ctx.label("The monitor stops trying this network at start-up. You can add it again from the Wi-Fi form.",
            15f, T.TEXT2), "Forget", {
            netMsg.visibility = View.GONE
            Board.run({ c ->
                c.forget(n.ssid)
                c.networks()
            }) { list, err ->
                if (list != null) Board.setNetworks(list)
                if (err != null) {
                    netMsg.text = err.message ?: "Could not forget that network."
                    netMsg.visibility = View.VISIBLE
                    Board.refresh(Board.Part.NETWORKS)
                }
            }
        })
    }

    // --- DHCP -----------------------------------------------------------------

    private fun buildDhcpCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("DHCP"))
        dListener = card.addKV("Listener", top = 10)
        dReceived = card.addKV("Packets received")
        dStatus = card.addKV("Status")
        dClient = card.addKV("Client MAC")
        dIp = card.addKV("Requested IP")
        dServer = card.addKV("DHCP server")
        dLease = card.addKV("Lease")
        dMessage = card.addKV("Message")
        dHost = card.addKV("Hostname")
        dLast = card.addKV("Last packet")
        dCount = card.addKV("DHCP messages")
        card.add(c.hint("Learned from the requests devices broadcast when they join the network, which is also where names " +
            "come from. A device that stays connected shows up here only after it reconnects or restarts."), top = 10)
        return card
    }

    private fun renderDhcp() {
        val d = Board.dhcp
        val err = Board.partErrors[Board.Part.DHCP]
        if (d == null) {
            dListener.set(err ?: "Reading")
            for (kv in listOf(dReceived, dStatus, dClient, dIp, dServer, dLease, dMessage, dHost, dLast, dCount)) kv.show(false)
            return
        }
        dListener.set(Format.listenerText(d.listener), if (d.listener == "failed") T.BAD else T.TEXT)
        dReceived.show(d.listener.isNotEmpty())
        dReceived.set(d.received.toString())
        dStatus.show(!d.valid)
        dStatus.set("No DHCP request heard yet")
        for (kv in listOf(dClient, dIp, dServer, dLease, dMessage, dHost, dLast, dCount)) kv.show(d.valid)
        if (d.valid) {
            dClient.set(d.clientMac)
            dIp.set(d.assignedIp)
            dServer.set(d.serverIp.takeIf { it != "0.0.0.0" })
            dLease.set(if (d.leaseS > 0) Format.duration(d.leaseS) else null)
            dMessage.set(d.messageType)
            dHost.set(d.hostname)
            dLast.set(Format.ago(d.lastSeenS))
            dCount.set(d.capturePackets.toString())
        }
    }

    // --- Firmware update ----------------------------------------------------

    private fun buildFirmwareCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("Firmware update"))
        fwRunning = card.add(c.label("", 14f, T.TEXT2), top = 6)
        card.add(c.hint("In Arduino IDE use Sketch, Export Compiled Binary, then choose the file ending in .ino.bin, " +
            "not the bootloader, partitions or merged one."), top = 6)
        card.add(c.button("Choose firmware file", Btn.SECONDARY) {
            host.pickFirmware { uri -> loadImage(uri) }
        }, WRAP, WRAP, top = 12)
        fwInfo = card.add(c.label("", 14f, T.TEXT2, numbers = true), top = 8)
        fwInfo.visibility = View.GONE

        card.add(c.fieldLabel("Update password"), top = 14)
        fwKey = card.add(c.input("the firmware's OTA password", InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD), top = 6)
        val saved = AppState.prefs.updateKey
        if (saved != null) fwKey.setText(saved)
        card.add(c.hint("The update password set in secrets.h when the firmware was built."), top = 6)
        val rr = card.add(c.row(), top = 8)
        rr.add(c.label("Remember it on this phone", 14f, T.TEXT2), 0, WRAP, weight = 1f)
        fwRemember = Switch(c)
        fwRemember.isChecked = saved != null
        fwRemember.thumbTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(T.ACCENT, T.TEXT2))
        fwRemember.trackTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(0x88FFB020.toInt(), T.EDGE))
        fwRemember.setOnCheckedChangeListener { _, on -> if (!on) AppState.prefs.updateKey = null }
        rr.add(fwRemember, WRAP, WRAP)

        fwBtn = card.add(c.button("Upload and install", Btn.PRIMARY) { startUpload() }, MATCH, WRAP, top = 14)
        fwBar = ProgressBar(c, null, android.R.attr.progressBarStyleHorizontal)
        fwBar.max = 100
        fwBar.progressTintList = ColorStateList.valueOf(T.ACCENT)
        fwBar.progressBackgroundTintList = ColorStateList.valueOf(T.EDGE)
        card.add(fwBar, MATCH, c.dp(6), top = 14)
        fwBar.visibility = View.GONE
        fwMsg = card.add(c.label("", 14f, T.TEXT2), top = 8)
        fwMsg.visibility = View.GONE
        return card
    }

    fun loadImage(uri: Uri) {
        say(fwInfo, "Reading the file")
        val resolver = host.contentResolver
        Thread {
            var name = "firmware.bin"
            var size = -1L
            try {
                resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { cur ->
                    if (cur.moveToFirst()) {
                        if (!cur.isNull(0)) name = cur.getString(0)
                        if (!cur.isNull(1)) size = cur.getLong(1)
                    }
                }
            } catch (e: RuntimeException) {
            }
            var bytes: ByteArray? = null
            var problem: String?
            try {
                val limit = Firmware.APP_PARTITION_BYTES + 1
                val data = resolver.openInputStream(uri)?.use { input ->
                    val out = java.io.ByteArrayOutputStream()
                    val buf = ByteArray(16384)
                    while (out.size() < limit) {
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                    }
                    out.toByteArray()
                }
                if (data == null) {
                    problem = "Could not open that file."
                } else {
                    val len = if (size >= 0) maxOf(size, data.size.toLong()) else data.size.toLong()
                    problem = Firmware.problem(name, len, if (data.isNotEmpty()) data[0].toInt() and 0xFF else null)
                    if (problem == null) bytes = data
                    size = len
                }
            } catch (e: Exception) {
                problem = "Could not read that file: ${e.message}"
            }
            val version = bytes?.let { Firmware.versionIn(it) }
            val finalName = name
            val finalSize = size
            val finalProblem = problem
            val finalBytes = bytes
            main.post {
                image = finalBytes
                imageName = finalName
                imageProblem = finalProblem
                if (finalProblem != null) {
                    say(fwInfo, "$finalName: $finalProblem", T.BAD)
                } else {
                    val v = if (version != null) ", contains $version" else ""
                    say(fwInfo, "$finalName, ${Format.fileSize(finalSize)}$v", T.TEXT)
                }
                say(fwMsg, "")
            }
        }.start()
    }

    private fun startUpload() {
        if (uploading) return
        val img = image
        if (img == null) {
            say(fwMsg, imageProblem ?: "Choose the firmware file first: the one ending in .ino.bin.", T.BAD)
            return
        }
        val key = fwKey.text.toString()
        if (key.isEmpty()) {
            say(fwMsg, "Enter the update password.", T.BAD)
            fwKey.requestFocus()
            return
        }
        val base = Board.base ?: return
        val oldVersion = Board.health?.version ?: ""
        val remember = fwRemember.isChecked
        val name = imageName
        uploading = true
        fwBtn.enabled(false)
        host.keepScreenOn(true)
        say(fwMsg, "Checking the password")

        Thread {
            val client = NetmonClient(base)
            val started = System.currentTimeMillis()
            var outcome: RestartWatch.Outcome? = null
            var failure: String? = null
            try {
                if (!client.checkUpdateKey(key)) {
                    failure = "Wrong update password."
                } else {
                    AppState.prefs.updateKey = if (remember) key else null
                    Board.paused = true
                    main.post {
                        fwBar.progress = 0
                        fwBar.visibility = View.VISIBLE
                        say(fwMsg, "Uploading 0%")
                    }
                    var lastPct = -1
                    try {
                        client.uploadFirmware(img, name, key) { sent, total ->
                            val pct = ((sent * 100) / maxOf(1L, total)).toInt()
                            if (pct != lastPct) {
                                lastPct = pct
                                main.post {
                                    fwBar.progress = pct
                                    say(fwMsg, if (pct < 100) "Uploading $pct%" else "Installing")
                                }
                            }
                        }
                        main.post { say(fwMsg, "Installed. The monitor is restarting.", T.OK) }
                    } catch (e: ApiException) {
                        if (e.kind != ApiException.Kind.Unreachable && e.kind != ApiException.Kind.Timeout) throw e
                        main.post { say(fwMsg, "The connection dropped. Checking whether the monitor restarted.") }
                    }
                    outcome = RestartWatch.await(base, started, oldVersion)
                }
            } catch (e: ApiException) {
                failure = if (e.kind == ApiException.Kind.Unauthorized) "Wrong update password." else e.message
            } finally {
                Board.paused = false
            }
            val o = outcome
            val f = failure
            main.post {
                uploading = false
                fwBtn.enabled(true)
                host.keepScreenOn(false)
                if (f != null) {
                    fwBar.visibility = View.GONE
                    say(fwMsg, f, T.BAD)
                } else if (o != null) {
                    when (o) {
                        is RestartWatch.Outcome.Updated -> say(fwMsg, "Updated. Now running ${o.version}.", T.OK)
                        is RestartWatch.Outcome.SameVersion -> say(fwMsg, "Restarted, still running ${o.version}: the file carries the same version.", T.OK)
                        is RestartWatch.Outcome.NotRestarted -> say(fwMsg, "The monitor did not restart and is still running ${o.version}, so nothing was installed.", T.BAD)
                        RestartWatch.Outcome.NoAnswer -> say(fwMsg, "No answer from the monitor after two and a half minutes. If it does not come back, reflash it over USB.", T.BAD)
                        RestartWatch.Outcome.Cancelled -> say(fwMsg, "")
                    }
                }
                formFor = null
                Board.refresh(Board.Part.HEALTH, Board.Part.CONFIG, Board.Part.NETWORKS, Board.Part.DHCP)
            }
        }.start()
    }

    // --- Notifications ------------------------------------------------------

    private fun buildAlertsCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("Notifications"))
        card.add(c.fieldLabel("Tell me about"), top = 10)
        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val mr = c.row()
        scroller.addView(mr, FrameLayout.LayoutParams(WRAP, WRAP))
        for (m in AlertMode.values()) {
            val chip = c.chip(m.title, m == AppState.prefs.alertMode) { host.enableAlerts(m) }
            modeChips[m] = chip
            mr.add(chip, WRAP, WRAP, end = 8)
        }
        card.add(scroller, top = 6)
        modeDetail = card.add(c.label("", 13f, T.TEXT2), top = 8)

        card.add(c.fieldLabel("Check every"), top = 14)
        val er = card.add(c.row(), top = 6)
        for (m in listOf(15, 30, 60)) {
            val chip = c.chip("$m min", m == AppState.prefs.alertMinutes) {
                AppState.prefs.alertMinutes = m
                Alerts.schedule(ctx)
                render()
            }
            everyChips[m] = chip
            er.add(chip, WRAP, WRAP, end = 8)
        }

        blocked = card.add(c.column(), top = 12)
        blocked.add(c.label("Android is blocking notifications from this app.", 14f, T.WARN))
        blocked.add(c.button("Allow notifications", Btn.SECONDARY) { host.askNotificationPermission() }, WRAP, WRAP, top = 8)

        card.add(c.hint("Checks run in the background while this phone can reach the monitor, usually when it is on the same " +
            "Wi-Fi. Android may run them later than asked to save battery. While the app is open it checks every 10 seconds."), top = 12)
        return card
    }

    private fun renderAlerts() {
        val mode = AppState.prefs.alertMode
        for ((m, chip) in modeChips) chip.styleChip(m == mode)
        modeDetail.text = mode.detail
        val every = AppState.prefs.alertMinutes
        for ((m, chip) in everyChips) {
            chip.styleChip(m == every)
            chip.enabled(mode != AlertMode.OFF)
        }
        blocked.visibility = if (mode != AlertMode.OFF && !Alerts.allowed(ctx)) View.VISIBLE else View.GONE
    }

    // --- About ----------------------------------------------------------------

    private fun buildAboutCard(): View {
        val c = ctx
        val card = c.card()
        card.add(c.cardTitle("About"))
        card.addKV("App", top = 10).set("netmon for Android ${BuildInfo.VERSION_NAME}")
        card.add(c.hint("Talks to the monitor's own web API; nothing leaves your network except the monitor's own " +
            "provider lookup. Network history needs firmware 0.9.4 or later, and the DHCP listener status 0.9.6."), top = 8)
        return card
    }

    // --- render -----------------------------------------------------------------

    override fun onShown() {
        renderAlerts()
    }

    override fun render() {
        if (!::boardAddr.isInitialized) return
        val h = Board.health
        val base = Board.base
        boardAddr.set(base?.let { NetmonClient.display(it) })
        boardFw.set(h?.version)
        boardUp.set(h?.let { Format.duration(it.uptimeS) })
        fwRunning.text = if (h != null) "Running ${h.version}." else "Firmware version not read yet."

        val cfg = Board.config
        val a = cfg?.active
        setupNote.visibility = if (a?.source == "softap" || h?.inSetupMode == true) View.VISIBLE else View.GONE
        wifiNow.text = when {
            a == null -> if (Board.partErrors[Board.Part.CONFIG] != null) Board.partErrors[Board.Part.CONFIG] else "Reading the settings"
            a.source == "softap" -> "Setup network ${a.ssid} at ${a.ip}"
            else -> "Connected to ${a.ssid}, ${a.rssi} dBm, address ${a.ip}" + if (a.source == "dhcp") " from the router" else " (fixed)"
        }
        if (!saving) fillForm()
        renderNetworks()
        renderDhcp()
        renderAlerts()
    }
}
