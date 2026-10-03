package com.example.netmon.ui

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
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
import android.widget.TextView
import com.example.netmon.Air
import com.example.netmon.ApiException
import com.example.netmon.AppState
import com.example.netmon.Beeper
import com.example.netmon.Board
import com.example.netmon.FindStatus
import com.example.netmon.FinderMath
import com.example.netmon.FinderTrack
import com.example.netmon.HeadingLog
import com.example.netmon.MainActivity
import com.example.netmon.NearbyAp
import com.example.netmon.NearbyBle
import com.example.netmon.TurnSensor
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * The Finder: one device, listened for closely while you walk up to it with
 * the board. The board sends every reading raw; this smooths them, works out
 * warmer and colder, and from a turn on the spot with the board held against
 * you, which way the signal is strongest. Where the phone has a rotation
 * sensor it follows the turn itself, so you can turn at your own pace, and
 * afterwards an arrow keeps pointing the way as you turn.
 */
class FinderPane(private val host: MainActivity, private val screen: NearbyScreen) {

    private val ctx: Context get() = host
    private val prefs get() = AppState.prefs
    /** Names and addresses masked for screenshots, as on the lists. */
    private val masked: Boolean get() = prefs.masked
    private val main = Handler(Looper.getMainLooper())

    /** "ble" or "wifi", and the address. */
    private var type: String? = null
    private var addr: String? = null
    private var info: FindStatus? = null
    private val track = FinderTrack()
    private var after = 0L
    private var lastOk = 0L
    private var err = ""
    private var inView = false
    private var gen = 0
    private var wasFound = false
    private var said = ""

    private val sensor = TurnSensor(host)
    private val headings = HeadingLog()
    private var headingNow: Double? = null
    private var lastHeadingDraw = 0L
    private val beeper = Beeper()
    private var sound = prefs.finderSound

    private class Turn(val sensor: Boolean, val atMs: Long, val lengthMs: Long) {
        var t0 = 0L
        var h0 = 0.0
        var endAt = 0L
        var held: Boolean? = null
        var tick = -1
        val pts = ArrayList<FinderMath.TurnPoint>()
    }

    private class Dir(val d: FinderMath.Direction, val at: Long, val pts: List<FinderMath.TurnPoint>, val sensor: Boolean, val h0: Double)

    private var turn: Turn? = null
    private var dir: Dir? = null

    val active: Boolean get() = addr != null

    // views
    private lateinit var root: LinearLayout
    private lateinit var pickBox: LinearLayout
    private lateinit var pickMsg: TextView
    private val pickChips = HashMap<String, TextView>()
    private lateinit var pickSearch: EditText
    private lateinit var pickRows: KeyedRows<PickRow>
    private lateinit var pickEmpty: TextView
    private lateinit var goBox: LinearLayout
    private lateinit var fName: TextView
    private lateinit var fWhat: TextView
    private lateinit var fState: TextView
    private lateinit var fPriv: TextView
    private lateinit var fBig: TextView
    private lateinit var fTrend: TextView
    private lateinit var fProx: TextView
    private lateinit var fHeat: HeatBarView
    private lateinit var fMeta: TextView
    private lateinit var fRadar: FinderRadarView
    private lateinit var fTrace: TraceView
    private lateinit var fTurn: TextView
    private lateinit var fSound: TextView
    private lateinit var fTurnMsg: TextView
    private lateinit var fSay: TextView
    private lateinit var helpBox: LinearLayout

    init {
        prefs.finderTarget?.let { s ->
            val i = s.indexOf('|')
            if (i > 0) {
                val t = s.substring(0, i)
                if (t == "ble" || t == "wifi") { type = t; addr = s.substring(i + 1) }
            }
        }
        sensor.listener = { h, t -> onHeading(h, t) }
    }

    fun build(): View {
        val c = ctx
        root = c.column()

        // --- the picker
        pickBox = root.add(c.column(), top = 12)
        pickBox.add(c.cardTitle("What are you looking for?"))
        pickBox.add(c.hint("The board does the listening, not your phone, so take it with you: run it from a USB power bank " +
            "and keep this screen open. Pick a device here, or press Find beside it in the Wi-Fi or Bluetooth list."), top = 4)
        pickMsg = pickBox.add(c.label("", 14f, T.BAD), top = 10)
        pickMsg.visibility = View.GONE
        val scroller = HorizontalScrollView(c)
        scroller.isHorizontalScrollBarEnabled = false
        val cr = c.row()
        scroller.addView(cr, FrameLayout.LayoutParams(WRAP, WRAP))
        for (k in listOf("t", "b", "w")) {
            val chip = c.chip("", false) {
                prefs.finderPick = k
                render()
            }
            pickChips[k] = chip
            cr.add(chip, WRAP, WRAP, end = 8)
        }
        pickBox.add(scroller, top = 12)
        pickSearch = pickBox.add(c.input("Search what to find", InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS), top = 10)
        pickSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) { render() }
        })
        val box = pickBox.add(c.card(), top = 10)
        box.setPadding(c.dp(12), c.dp(2), c.dp(12), c.dp(2))
        val list = box.add(c.column())
        pickRows = KeyedRows(list) { PickRow() }
        pickEmpty = box.add(c.label("", 14f, T.TEXT2), top = 12, bottom = 12)

        // --- finding
        goBox = root.add(c.column(), top = 4)
        val top = goBox.add(c.row(), top = 8)
        top.gravity = Gravity.TOP
        val who = top.add(c.column(), 0, WRAP, weight = 1f)
        fName = who.add(c.label("", 22f, T.TEXT, Fonts.medium))
        fWhat = who.add(c.label("", 13f, T.TEXT2), top = 2)
        top.add(c.button("Change", Btn.SECONDARY) { stopFinding() }, WRAP, WRAP, start = 8)
        fState = goBox.add(c.label("", 14f, T.TEXT2), top = 10)
        fPriv = goBox.add(c.label("It uses a private address, which it changes every so often (phones and many trackers do, " +
            "some every quarter hour). When it does, it drops out here: pick it again from the list.", 13f, T.TEXT3), top = 6)

        val card = goBox.add(c.card(), top = 12)
        val big = card.add(c.row())
        big.gravity = Gravity.BOTTOM
        val bl = big.add(c.column(), 0, WRAP, weight = 1f)
        fBig = bl.add(c.label("–", 40f, T.TEXT, Fonts.light, numbers = true))
        bl.add(c.label("away, by signal strength", 12.5f, T.TEXT3))
        fTrend = big.add(c.label("", 17f, T.TEXT, Fonts.medium), WRAP, WRAP, start = 8)
        fProx = card.add(c.label("", 17f, T.TEXT, Fonts.medium), top = 12)
        fHeat = card.add(HeatBarView(c), top = 8)
        val ends = card.add(c.row(), top = 2)
        ends.add(c.label("far", 11.5f, T.TEXT3), 0, WRAP, weight = 1f)
        ends.add(c.label("here", 11.5f, T.TEXT3), WRAP, WRAP)
        fMeta = card.add(c.label("", 12.5f, T.TEXT3, numbers = true), top = 10)

        fRadar = FinderRadarView(c)
        val rw = goBox.add(FrameLayout(c), top = 16)
        rw.addView(fRadar, FrameLayout.LayoutParams(WRAP, WRAP, Gravity.CENTER_HORIZONTAL))
        fRadar.contentDescription = "Where the device is: the ring is the estimated distance; an arrow appears after a turn"

        val tc = goBox.add(c.card(), top = 16)
        tc.add(c.label("Signal over the last minute", 13f, T.TEXT2))
        fTrace = tc.add(TraceView(c), top = 8)
        val tl = tc.add(c.row(), top = 2)
        tl.add(c.label("a minute ago", 11f, T.TEXT3), 0, WRAP, weight = 1f)
        tl.add(c.label("dBm", 11f, T.TEXT3), 0, WRAP, weight = 1f).gravity = Gravity.CENTER
        tl.add(c.label("now", 11f, T.TEXT3), 0, WRAP, weight = 1f).gravity = Gravity.END

        fTurn = goBox.add(c.button("Turn to find the direction", Btn.PRIMARY) { if (turn != null) cancelTurn() else startTurn() }, MATCH, WRAP, top = 16)
        fTurnMsg = goBox.add(c.label("", 15f, T.TEXT), top = 10)
        fTurnMsg.visibility = View.GONE
        val br = goBox.add(c.row(), top = 10)
        fSound = br.add(c.button("", Btn.SECONDARY) { toggleSound() }, 0, WRAP, weight = 1f)
        br.add(c.button("Stop finding", Btn.SECONDARY) { stopFinding() }, 0, WRAP, weight = 1f, start = 8)
        fSay = goBox.add(c.label("", 1f, T.BG))
        fSay.accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE

        val how = goBox.add(c.button("How to find it", Btn.QUIET) { helpBox.shown(helpBox.visibility != View.VISIBLE) }, WRAP, WRAP, top = 12)
        how.setPadding(0, c.dp(8), c.dp(8), c.dp(8))
        helpBox = goBox.add(c.column())
        val steps = listOf(
            "Take the board with you. It is the board that hears the device, so a board left on a shelf will not get any " +
                "closer. Run it from a USB power bank, keep this screen open, and stay within reach of your Wi-Fi.",
            "Walk slowly, a few steps at a time, and pause. One reading can jump several dB from the last, so go by the trend " +
                "over a few seconds: warmer means closer, colder means turn back or try another way.",
            if (sensor.available) "For a direction, hold the board flat against your chest and the phone in front of you, press " +
                "Turn to find the direction, and turn slowly on the spot, all the way round. Your body blocks the signal from " +
                "behind you, so it is strongest when you face the device. The phone follows your turn, and afterwards the arrow " +
                "keeps pointing the way."
            else "For a direction, hold the board flat against your chest, press Turn to find the direction, then turn on the " +
                "spot to your right in step with the hand. Your body blocks the signal from behind you, so it is strongest " +
                "when you face the device.",
            "Close in, look and listen rather than trust the figure. Walls, furniture and bags make things seem farther than " +
                "they are: a tracker in a bag can read a few metres away when it is next to you.",
        )
        steps.forEachIndexed { i, s ->
            val r = helpBox.add(c.row(), top = 8)
            r.gravity = Gravity.TOP
            r.add(c.label("${i + 1}.", 14f, T.TEXT2), c.dp(22), WRAP)
            r.add(c.label(s, 14f, T.TEXT2), 0, WRAP, weight = 1f)
        }
        helpBox.add(c.hint("A device that changes its address (most phones, and some trackers every quarter hour or so) " +
            "drops out when it does. Pick it again from the list."), top = 10)
        helpBox.visibility = View.GONE
        styleSound()
        view()
        return root
    }

    // --- choosing --------------------------------------------------------------

    /**
     * Finds this device: from a list, a radar, the picker or the map. When the
     * Finder is on the screen it starts at once; otherwise it starts when the
     * Finder tab is shown.
     */
    fun pick(t: String, a: String) {
        val wasIn = inView
        val same = t == type && a == addr
        if (!same) {
            leave()
            reset()
        }
        type = t
        addr = a
        err = ""
        prefs.finderTarget = "$t|$a"
        if (::root.isInitialized) view()
        if (wasIn && !inView) enter()
        screen.finderChanged()
    }

    /**
     * Lets the device go with a reason, staying on the picker: the board
     * refused it, or another page took the Finder over.
     */
    private fun drop(message: String) {
        gen++
        main.removeCallbacksAndMessages(null)
        turn = null
        beeper.stop()
        sensor.stop()
        headingNow = null
        host.keepScreenOn("finder", false)
        type = null
        addr = null
        prefs.finderTarget = null
        reset()
        err = message
        view()
        screen.finderChanged()
        render()
    }

    private fun reset() {
        track.reset()
        info = null
        after = 0L
        lastOk = 0L
        turn = null
        dir = null
        headings.clear()
        beeper.paused = false
        wasFound = false
        said = ""
        if (::fTurnMsg.isInitialized) {
            fTurnMsg.text = ""
            fTurnMsg.visibility = View.GONE
            fTurn.text = "Turn to find the direction"
        }
    }

    fun stopFinding() {
        val wasIn = inView
        leave()
        type = null
        addr = null
        prefs.finderTarget = null
        reset()
        // Still on the screen, now with the picker: a device picked there starts at once.
        if (wasIn) enter() else view()
        screen.finderChanged()
        Board.refresh(Board.Part.NEARBY)
        render()
    }

    fun onBack(): Boolean {
        if (turn != null) {
            cancelTurn()
            return true
        }
        return false
    }

    /** The Finder tab is on the screen: start, or carry on with, the device chosen. */
    fun enter() {
        if (inView) return
        inView = true
        view()
        if (addr == null) return
        gen++
        // "Cannot reach the board" counts from here: an answer from before the
        // pane was left says nothing about the board now.
        lastOk = System.currentTimeMillis()
        start()
        if (sound) beeper.start()
        if (sensor.available) sensor.start()
        host.keepScreenOn("finder", true)
    }

    /** Off the screen: the board goes back to scanning for everything; the device stays chosen. */
    fun leave() {
        if (!inView) return
        inView = false
        gen++
        main.removeCallbacksAndMessages(null)
        if (turn != null) cancelTurn()
        // A direction the phone's sensor followed is kept in that sensor's own
        // frame, which may start again from somewhere else once it is switched
        // off: an arrow drawn from it later could point anywhere.
        if (dir?.sensor == true) {
            dir = null
            turnMsg("")
        }
        beeper.stop()
        sensor.stop()
        headingNow = null
        host.keepScreenOn("finder", false)
        if (addr != null) Board.run({ it.findStop() }) { _, _ -> }
    }

    private fun view() {
        if (!::root.isInitialized) return
        val on = addr != null
        pickBox.shown(!on)
        goBox.shown(on)
        pickMsg.text = err
        pickMsg.shown(err.isNotEmpty())
    }

    // --- talking to the board ---------------------------------------------------

    private fun start() {
        val t = type ?: return
        val a = addr ?: return
        val g = gen
        Board.run({ it.findStart(t, a) }) { res, e ->
            if (g != gen || t != type || a != addr) return@run
            if (res != null) {
                err = ""
                info = res
                after = res.seq
                lastOk = System.currentTimeMillis()
                val heard = track.heardAgo(lastOk)
                if (track.points.isEmpty() || heard > 5000) track.sinceMs = lastOk
                render()
                schedulePoll(g, 1000)
            } else if (e != null && (e.kind == ApiException.Kind.Unreachable || e.kind == ApiException.Kind.Timeout)) {
                render()
                main.postDelayed({ if (g == gen) start() }, 3000)
            } else {
                // Bluetooth switched off, the device not heard lately, or a board without the Finder.
                drop(when {
                    e?.status == 404 && e.message?.startsWith("This board's firmware") == true ->
                        "The Finder needs netmon firmware 0.11 or later. Update the monitor from Settings, Firmware update."
                    else -> e?.message ?: "Could not start finding that."
                })
            }
        }
    }

    private fun schedulePoll(g: Int, ms: Long) {
        main.postDelayed({ if (g == gen) poll(g) }, ms)
    }

    private fun poll(g: Int) {
        val t = type ?: return
        val a = addr ?: return
        Board.run({ it.find(after) }) { d, _ ->
            if (g != gen || t != type || a != addr) return@run
            if (d == null) {
                render()
                schedulePoll(g, 1000)
                return@run
            }
            lastOk = System.currentTimeMillis()
            // The board let it go (asked too seldom, or restarted): ask again.
            if (!d.active) {
                start()
                return@run
            }
            // Another page or phone is finding something else on this board now:
            // leave it be, rather than the two taking it from each other every second.
            if (d.addr != a) {
                val other = if (d.name.isNotEmpty()) (if (masked) Air.maskName(d.name) else d.name)
                    else if (masked) Air.maskAddr(d.addr) else d.addr
                drop("Another page or phone is using the Finder on this board now, for $other. Pick yours again to take it back.")
                return@run
            }
            if (d.seq < after) after = 0
            val now = System.currentTimeMillis()
            for (r in d.readings) if (r.seq > after) addReading(now - r.msAgo, r.rssi)
            after = max(after, d.seq)
            info = d
            render()
            schedulePoll(g, 1000)
        }
    }

    private fun addReading(t: Long, r: Int) {
        track.add(t, r)
        val u = turn ?: return
        if (u.t0 == 0L || t < u.t0) return
        if (!u.sensor) {
            if (t <= u.t0 + u.lengthMs) u.pts.add(FinderMath.TurnPoint((t - u.t0).toDouble() / u.lengthMs * 2 * PI, r))
        } else {
            if (u.endAt != 0L && t > u.endAt) return
            val cum = headings.at(t) ?: return
            u.pts.add(FinderMath.TurnPoint(FinderMath.norm(cum), r))
        }
    }

    /** Asks the board to hold its network sweep off for the turn; 0 lets it go again. */
    private fun hold(s: Int, done: ((Boolean) -> Unit)? = null) {
        val t = type ?: return
        val a = addr ?: return
        Board.run({ it.findStart(t, a, s) }) { res, _ -> done?.invoke(res != null && res.holdMs > 0) }
    }

    // --- turning on the spot ------------------------------------------------------

    private fun startTurn() {
        if (addr == null) return
        val useSensor = sensor.available && headingNow != null
        val pm = track.rate(System.currentTimeMillis())
        val secs = FinderMath.turnSeconds(pm)
        dir = null
        val u = Turn(useSensor, System.currentTimeMillis(), secs * 1000L)
        turn = u
        beeper.paused = true
        hold(if (useSensor) 60 else secs + 6) { held -> if (turn === u) u.held = held }
        fTurn.text = "Cancel the turn"
        turnTick()
    }

    private fun cancelTurn() {
        turn = null
        beeper.paused = false
        main.removeCallbacks(tickRunnable)
        hold(0)
        if (::fTurn.isInitialized) {
            fTurn.text = "Turn to find the direction"
            fTurnMsg.text = ""
            fTurnMsg.visibility = View.GONE
        }
        render()
    }

    private val tickRunnable = Runnable { turnTick() }

    private fun turnTick() {
        val u = turn ?: return
        main.removeCallbacks(tickRunnable)
        val now = System.currentTimeMillis()
        if (u.t0 == 0L) {
            val left = 3 - floor((now - u.atMs) / 1000.0).toInt()
            if (left > 0) {
                val note = if (u.held == false) " (The board may stop listening for a few seconds of this turn to check your " +
                    "network; it did so for a turn not long ago.)" else ""
                turnMsg((if (u.sensor) "Hold the board flat against your chest and the phone in front of you. Turning in $left…"
                else "Hold the board flat against your chest and face ahead. Turning in $left…") + note)
                main.postDelayed(tickRunnable, 250)
                return
            }
            u.t0 = now
            if (u.sensor) {
                val h = headingNow
                if (h == null) {
                    // The sensor went quiet: time the turn instead.
                    turn = Turn(false, u.atMs, u.lengthMs).also { it.t0 = now; it.held = u.held }
                } else {
                    headings.clear()
                    headings.add(now, h)
                    u.h0 = h
                }
            }
            if (sound) beeper.tone(880.0, 90, .2)
        }
        val v = turn ?: return
        if (!v.sensor) {
            val f = (now - v.t0).toDouble() / v.lengthMs
            if (f >= 1) {
                if (now - v.t0 < v.lengthMs + 1500) {
                    turnMsg("Hold still a moment…")
                    main.postDelayed(tickRunnable, 250)
                    return
                }
                turnDone()
                return
            }
            val q8 = floor(f * 8).toInt()
            if (sound && q8 != v.tick) { v.tick = q8; beeper.tone(1250.0, 25, .12) }
            turnMsg("Turn slowly to your right, in step with the hand: once round in ${(v.lengthMs / 1000.0).roundToInt()} seconds. " +
                "${(f * 100).roundToInt()}%")
        } else {
            val turned = abs(headings.turned)
            if ((v.endAt != 0L && now - v.endAt >= 1600) || now - v.t0 > 120_000) {
                turnDone()
                return
            }
            val q8 = floor(turned / (2 * PI) * 8).toInt()
            if (sound && q8 != v.tick && v.endAt == 0L) { v.tick = q8; beeper.tone(1250.0, 25, .12) }
            turnMsg(if (v.endAt != 0L) "Round once. Hold still a moment…"
            else "Turn slowly on the spot, either way, all the way round: ${(turned / (2 * PI) * 100).roundToInt().coerceAtMost(100)}%. " +
                "Keep the board against your chest.")
        }
        render()
        main.postDelayed(tickRunnable, 250)
    }

    private fun onHeading(h: Double, t: Long) {
        headingNow = h
        val u = turn
        if (u != null && u.sensor && u.t0 != 0L && u.endAt == 0L) {
            headings.add(t, h)
            if (abs(headings.turned) >= 2 * PI) u.endAt = t
        }
        // With an answer on screen the arrow follows the phone; redrawn at most 25 times a second.
        if ((u != null || dir?.sensor == true) && t - lastHeadingDraw > 40) {
            lastHeadingDraw = t
            if (::fRadar.isInitialized) fRadar.pic = pic(System.currentTimeMillis())
            if (u == null) dir?.let { d -> if (d.d.ok) turnMsg(FinderMath.liveText(d.d, rel(d) ?: d.d.a)) }
        }
    }

    private fun turnDone() {
        val u = turn ?: return
        turn = null
        beeper.paused = false
        main.removeCallbacks(tickRunnable)
        fTurn.text = "Turn to find the direction"
        hold(0)
        val d = FinderMath.direction(u.pts)
        val r = Dir(d, System.currentTimeMillis(), ArrayList(u.pts), u.sensor, u.h0)
        dir = r
        turnMsg(if (r.sensor) FinderMath.liveText(d, rel(r) ?: d.a) else FinderMath.dirText(d))
        if (sound) beeper.tone(if (d.ok) 1320.0 else 440.0, 160, .2)
        render()
    }

    /** Where the strongest direction is from where the phone faces now. */
    private fun rel(r: Dir): Double? {
        val h = headingNow ?: return null
        return FinderMath.norm(r.h0 + r.d.a - h)
    }

    private fun turnMsg(s: String) {
        if (!::fTurnMsg.isInitialized) return
        fTurnMsg.put(s)
        fTurnMsg.shown(s.isNotEmpty())
    }

    // --- sound and touch --------------------------------------------------------------

    private fun toggleSound() {
        sound = !sound
        prefs.finderSound = sound
        if (sound && inView && addr != null) beeper.start() else beeper.stop()
        styleSound()
    }

    private fun styleSound() {
        if (!::fSound.isInitialized) return
        fSound.text = if (sound) "Sound on" else "Sound off"
        fSound.contentDescription = if (sound) "Beeps on: tap to turn them off" else "Beeps off: tap to turn them on"
    }

    private fun buzz() {
        try {
            val v = ctx.getSystemService(Vibrator::class.java) ?: return
            if (!v.hasVibrator()) return
            v.vibrate(VibrationEffect.createWaveform(longArrayOf(0, 90, 60, 90), -1))
        } catch (e: RuntimeException) {
            // No vibration on this phone, or not allowed: the screen says it anyway.
        }
    }

    // --- render ----------------------------------------------------------------------

    private fun bleTarget(): NearbyBle? {
        val a = addr ?: return null
        Board.nearby?.ble?.firstOrNull { it.addr == a }?.let { return it }
        val i = info
        if (i != null && i.active && i.addr == a) return NearbyBle(a, i.name, i.vendor, -1, i.kind, i.dtype, 0, i.model, -100, 0, 0, 0)
        return null
    }

    private fun apTarget(): NearbyAp? {
        val a = addr ?: return null
        Board.nearby?.wifi?.firstOrNull { it.bssid == a }?.let { return it }
        val i = info
        if (i != null && i.active && i.addr == a) return NearbyAp(a, i.name, i.ch, -100, i.security, true, false, 0, 0)
        return null
    }

    private fun distance(r: Int): Double = Air.metres(r, type == "ble", AppState.prefs.calibration)

    private fun pic(now: Long): FinderPic {
        val r = track.value()
        val heard = track.heardAgo(now)
        val have = r != null && heard < 30_000
        val d = if (have) distance(r!!) else null
        val heat = Pal.heat(if (d != null) FinderMath.hot(d) else 0.0)
        val u = turn
        val dr = dir
        val h = headingNow
        var rot = 0.0
        var turned: Double? = null
        var pts: List<FinderMath.TurnPoint> = emptyList()
        var curve: List<Double?>? = null
        var arrow: Double? = null
        var fade = 1.0
        var top: String? = null
        if (u != null && u.t0 != 0L) {
            pts = u.pts
            if (u.sensor) {
                turned = headings.turned
                rot = if (h != null) u.h0 - h else -turned
                top = "ahead of you"
            } else {
                turned = (now - u.t0).toDouble() / u.lengthMs * 2 * PI
                if (turned > 2 * PI) turned = 2 * PI
                top = "ahead, where you started"
            }
        } else if (dr != null) {
            pts = dr.pts
            curve = dr.d.curve
            if (dr.sensor && h != null) {
                rot = dr.h0 - h
                top = if (dr.d.ok) "ahead of you" else null
            } else top = if (dr.d.ok) (if (dr.sensor) "ahead, where you finished" else "ahead, where you started") else null
            if (dr.d.ok) {
                arrow = dr.d.a
                fade = max(.3, 1 - (now - dr.at) / 180_000.0)
            }
        }
        val found = FinderMath.found(d, heard)
        return FinderPic(d, heat, turned, rot, pts, curve, arrow, fade, found, top, beam = u == null && motionOn())
    }

    fun render() {
        if (!::root.isInitialized) return
        view()
        if (addr == null) {
            renderPicker()
            return
        }
        val now = System.currentTimeMillis()
        val ble = type == "ble"
        val i = info
        val m = masked
        val shownAddr = addr?.let { if (m) Air.maskAddr(it) else it } ?: ""
        if (ble) {
            val x = bleTarget()
            fName.put(if (x != null) Air.bleName(x, m) else "Bluetooth device")
            val parts = ArrayList<String>()
            if (x != null) {
                Air.kindWord(x.type)?.let { parts.add(it) }
                if (x.name.isNotEmpty() && x.vendor.isNotEmpty()) parts.add(x.vendor)
                if (x.kind.isNotEmpty()) parts.add("${x.kind} address")
            }
            parts.add(shownAddr)
            fWhat.put(parts.joinToString(" · "))
            fPriv.shown(x?.kind == "private")
        } else {
            val x = apTarget()
            fName.put(if (x != null) Air.apName(x, m) else "Wi-Fi network")
            val parts = arrayListOf("Wi-Fi network")
            if (x != null && x.ch > 0) parts.add("channel ${x.ch}")
            if (x != null && x.security.isNotEmpty()) parts.add(x.security)
            parts.add(shownAddr)
            fWhat.put(parts.joinToString(" · "))
            fPriv.shown(false)
        }

        val r = track.value()
        val heard = track.heardAgo(now)
        val have = r != null && heard < 30_000
        val d = if (have) distance(r!!) else null
        var bad = false
        val s = when {
            lastOk != 0L && now - lastOk > 6000 -> {
                bad = true
                "Cannot reach the board. If you have carried it out of reach of your Wi-Fi, come back a little."
            }
            i == null -> "Starting…"
            i.state == "off" -> {
                bad = true
                if (ble) "Bluetooth is switched off on the board, so it cannot listen for this. Switch it on under Scanning on the board."
                else "Wi-Fi scanning is switched off on the board, so it cannot look for this."
            }
            i.state == "unavailable" -> { bad = true; "Bluetooth could not start on this board." }
            i.state == "paused" -> when (i.why) {
                "sweep" -> "Paused for a few seconds while netmon checks your network."
                "probe" -> "Paused for a moment while netmon times its connection."
                "scan" -> "Waiting a moment for a scan to finish."
                "update" -> "Paused while the firmware updates."
                "start" -> "Could not start listening just now. Trying again."
                else -> "Paused for a moment."
            }
            track.points.isEmpty() -> if (i.forS > 20) "Listening, and nothing heard from it yet. It may be out of range, " +
                "switched off, or have changed its address." else "Listening for it, and nothing else…"
            heard > 20_000 -> "Not heard for ${heard / 1000} s. Keep still a moment; if it stays quiet it may be out of range, " +
                "or have changed its address."
            ble -> "Listening for it, and nothing else."
            else -> "Looking for it on channel ${if (i.ch > 0) i.ch else apTarget()?.ch ?: "?"}, about once a second."
        }
        fState.put(s, if (bad) T.BAD else T.TEXT2)

        val found = FinderMath.found(d, heard)
        fBig.put(if (d != null) FinderMath.big(d) else "–", if (found) Pal.heat(1.0) else T.TEXT)
        val tr = if (have) track.trend(now) else null
        fTrend.put(if (have) FinderMath.trendWord(tr) else "", when {
            tr != null && tr >= 3 -> Pal.heat(.85)
            tr != null && tr <= -3 -> Pal.heat(.1)
            else -> T.TEXT2
        })
        fProx.put(when {
            found -> "Very close: within arm’s reach. Look around here."
            d != null -> FinderMath.prox(d)
            track.points.isNotEmpty() -> "Lost it for now"
            else -> "Waiting for a reading"
        })
        fHeat.value = if (d != null) FinderMath.hot(d) else null
        val pm = track.rate(now)
        fMeta.put(listOf(
            if (have) "$r dBm" else "no signal",
            when {
                pm == null -> "counting readings"
                pm >= 90 -> "${(pm / 60.0).roundToInt()} readings a second"
                else -> "$pm a minute"
            },
            if (track.points.isNotEmpty()) "heard " + (if (heard < 1500) "just now" else "${heard / 1000} s ago") else "not heard yet",
        ).joinToString(" · "))

        if (found && !wasFound) buzz()
        wasFound = found
        val say = if (d != null) (if (found) "Very close" else FinderMath.prox(d)) +
            (if (tr == null) "" else if (tr >= 3) ", warmer" else if (tr <= -3) ", colder" else "") else ""
        if (say != said) {
            said = say
            fSay.text = say
        }
        beeper.level = if (r != null && heard < 10_000 && d != null) FinderMath.hot(d) else null

        fRadar.pic = pic(now)
        val pts = track.points
        fTrace.set(LongArray(pts.size) { pts[it].t }, IntArray(pts.size) { pts[it].r }, DoubleArray(pts.size) { pts[it].e })
    }

    // --- the picker ----------------------------------------------------------------------

    private fun renderPicker() {
        val n = Board.nearby
        if (n == null) {
            pickRows.clear()
            pickEmpty.put("Listening…")
            pickEmpty.shown(true)
            for ((k, chip) in pickChips) chip.put(when (k) { "t" -> "Trackers"; "b" -> "Bluetooth"; else -> "Wi-Fi" })
            return
        }
        val trackers = n.ble.filter { Air.group(it) == "t" }
        val live = n.wifi.filter { it.live }
        var c = prefs.finderPick
        if (c != "t" && c != "b" && c != "w") c = if (trackers.isNotEmpty()) "t" else "b"
        for ((k, chip) in pickChips) {
            chip.put(when (k) { "t" -> "Trackers ${trackers.size}"; "b" -> "Bluetooth ${n.ble.size}"; else -> "Wi-Fi ${live.size}" })
            chip.styleChip(k == c)
        }
        val q = pickSearch.text?.toString()?.trim() ?: ""
        val cal = AppState.prefs.calibration
        val mask = masked
        class Opt(val key: String, val wifi: Boolean, val name: String, val sub: String, val color: Int, val rssi: Int, val m: Double)
        val opts: List<Opt> = if (c == "w") live.filter { Air.matches(it, q) }.map {
            Opt(it.bssid, true, Air.apName(it, mask), "channel ${it.ch} · ${it.security}", Pal.WIFI, it.rssi, Air.metres(it.rssi, false, cal))
        } else (if (c == "t") trackers else n.ble).filter { Air.matches(it, q) }.map {
            val sub = listOf(Air.kindWord(it.type) ?: "kind not known", if (it.name.isNotEmpty()) it.vendor else "",
                if (it.kind != "public") "${it.kind} address" else "").filter { s -> s.isNotEmpty() }.joinToString(" · ")
            Opt(it.addr, false, Air.bleName(it, mask), sub, Pal.group(Air.group(it)), it.rssi, Air.metres(it.rssi, true, cal))
        }
        val sorted = opts.sortedWith(Comparator { a, b -> b.rssi - a.rssi })
        pickRows.show(sorted, { it.key }) { h, o -> h.bind(o.key, o.wifi, o.name, o.sub, o.color, o.rssi, o.m) }
        pickEmpty.shown(sorted.isEmpty())
        pickEmpty.put(when {
            q.isNotEmpty() -> "Nothing matches."
            c == "t" -> "No trackers heard. AirTags, Tiles, SmartTags and the like show here when the board hears them; " +
                "choose Bluetooth to pick from everything."
            c == "b" -> if (n.bleScan.enabled) "No Bluetooth devices heard yet." else "Bluetooth is off."
            else -> if (n.wifiScan.enabled) "No networks in range." else "Wi-Fi scanning is off."
        })
    }

    private inner class PickRow : KeyedRows.Holder(ctx) {
        private val row = ctx.row()
        private val mark = View(ctx)
        private val name = ctx.label("", 16f, T.TEXT, Fonts.medium)
        private val sub = ctx.label("", 12.5f, T.TEXT3)
        private val sig = ctx.label("", 13f, T.TEXT2, numbers = true)
        private val dist = ctx.label("", 12f, T.TEXT3, numbers = true)
        private var key = ""
        private var wifi = false

        init {
            val c = ctx
            row.setPadding(c.dp(2), c.dp(11), 0, c.dp(11))
            row.background = c.pressable(null, c.dpf(10f))
            row.isClickable = true
            row.setOnClickListener { pick(if (wifi) "wifi" else "ble", key) }
            row.add(mark, c.dp(10), c.dp(10))
            val mid = c.column()
            name.maxLines = 1
            name.ellipsize = TextUtils.TruncateAt.END
            sub.maxLines = 1
            sub.ellipsize = TextUtils.TruncateAt.END
            mid.add(name)
            mid.add(sub, top = 2)
            row.add(mid, 0, WRAP, weight = 1f, start = 12)
            val right = c.column()
            right.gravity = Gravity.END
            sig.gravity = Gravity.END
            dist.gravity = Gravity.END
            right.add(sig, WRAP, WRAP)
            right.add(dist, WRAP, WRAP)
            row.add(right, WRAP, WRAP, start = 8)
            row.add(c.label("Find", 15f, T.ACCENT, Fonts.medium), WRAP, WRAP, start = 14, end = 4)
            view.add(row)
        }

        fun bind(k: String, w: Boolean, n: String, s: String, color: Int, rssi: Int, m: Double) {
            key = k
            wifi = w
            mark.background = rounded(color, ctx.dpf(5f))
            name.put(n)
            sub.put(s)
            sig.put("$rssi dBm")
            dist.put(Air.distance(m))
            row.contentDescription = "Find $n, $s, $rssi dBm"
        }
    }
}
