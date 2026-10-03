package com.example.netmon

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.example.netmon.ui.Btn
import com.example.netmon.ui.ConnectScreen
import com.example.netmon.ui.DevicesScreen
import com.example.netmon.ui.EventsScreen
import com.example.netmon.ui.Fonts
import com.example.netmon.ui.InternetScreen
import com.example.netmon.ui.MATCH
import com.example.netmon.ui.NearbyScreen
import com.example.netmon.ui.OverviewScreen
import com.example.netmon.ui.Screen
import com.example.netmon.ui.SettingsScreen
import com.example.netmon.ui.T
import com.example.netmon.ui.WRAP
import com.example.netmon.ui.add
import com.example.netmon.ui.button
import com.example.netmon.ui.column
import com.example.netmon.ui.dialog
import com.example.netmon.ui.dp
import com.example.netmon.ui.dpf
import com.example.netmon.ui.label
import com.example.netmon.ui.pressable
import com.example.netmon.ui.rounded
import com.example.netmon.ui.row

class MainActivity : Activity() {

    companion object {
        const val TAB_OVERVIEW = 0
        const val TAB_DEVICES = 1
        const val TAB_NEARBY = 2
        const val TAB_EVENTS = 3
        const val TAB_INTERNET = 4
        const val TAB_SETTINGS = 5
        private const val POLL_MS = 10_000L
        private const val REQ_FILE = 11
        private const val REQ_NOTIFY = 12
        private const val STATE_TAB = "tab"
    }

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var content: FrameLayout
    private lateinit var nav: LinearLayout
    private lateinit var pill: TextView
    private lateinit var versionLabel: TextView
    private val navIcons = ArrayList<ImageView>()
    private val navLabels = ArrayList<TextView>()

    private lateinit var screens: List<Screen>
    private var connect: ConnectScreen? = null
    private var showing: Screen? = null
    private var tab = TAB_OVERVIEW
    private var fileCallback: ((Uri) -> Unit)? = null

    private val boardListener: () -> Unit = { onBoardChanged() }
    private val tick = object : Runnable {
        override fun run() {
            poll()
            handler.postDelayed(this, POLL_MS)
        }
    }
    // The quicker readings a screen asks for, such as the Nearby tables.
    private val fastTick = object : Runnable {
        override fun run() {
            val s = showing
            if (s == null || s is ConnectScreen || s.fastParts.isEmpty() || s.fastMs <= 0) return
            Board.refresh(s.fastParts)
            handler.postDelayed(this, s.fastMs)
        }
    }
    private var resumed = false
    private val awake = HashSet<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppState.init(this)
        @Suppress("DEPRECATION")
        window.statusBarColor = T.BG
        @Suppress("DEPRECATION")
        window.navigationBarColor = T.BG

        val root = column()
        root.setBackgroundColor(T.BG)
        root.add(buildTopBar(), MATCH, dp(56))
        content = FrameLayout(this)
        root.add(content, MATCH, 0, weight = 1f)
        nav = buildNav()
        root.add(nav, MATCH, WRAP)
        setContentView(root)
        applyInsets(root)

        screens = listOf(OverviewScreen(this), DevicesScreen(this), NearbyScreen(this), EventsScreen(this),
            InternetScreen(this), SettingsScreen(this))

        val savedTab = savedInstanceState?.getInt(STATE_TAB, TAB_OVERVIEW) ?: TAB_OVERVIEW
        if (Board.base == null) showConnect() else showTab(tabFrom(intent) ?: savedTab)
        revealFrom(intent)
        CrashLog.take(this)?.let { showCrash(it) }
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        Board.addListener(boardListener)
        handler.removeCallbacks(tick)
        handler.post(tick)
        restartFastPoll()
        showing?.onResume()
        onBoardChanged()
    }

    override fun onPause() {
        super.onPause()
        resumed = false
        Board.removeListener(boardListener)
        handler.removeCallbacks(tick)
        handler.removeCallbacks(fastTick)
        showing?.onPause()
        AppState.flushLatency()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(STATE_TAB, tab)
    }

    override fun onDestroy() {
        showing?.onHidden()
        super.onDestroy()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        val t = tabFrom(intent)
        if (t != null && Board.base != null) showTab(t)
        revealFrom(intent)
    }

    private fun tabFrom(i: Intent?): Int? = when (i?.getStringExtra(Alerts.EXTRA_TAB)) {
        "devices" -> TAB_DEVICES
        "nearby" -> TAB_NEARBY
        "events" -> TAB_EVENTS
        else -> null
    }

    private fun revealFrom(i: Intent?) {
        val mac = i?.getStringExtra(Alerts.EXTRA_MAC) ?: return
        i.removeExtra(Alerts.EXTRA_MAC)
        (screens[TAB_DEVICES] as DevicesScreen).reveal(mac)
    }

    // --- chrome -----------------------------------------------------------------

    private fun buildTopBar(): View {
        val bar = row()
        bar.setPadding(dp(16), 0, dp(12), 0)
        val mark = ImageView(this)
        mark.setImageResource(R.drawable.ic_brand)
        mark.imageTintList = android.content.res.ColorStateList.valueOf(T.ACCENT)
        mark.contentDescription = null
        bar.add(mark, dp(22), dp(22))
        bar.add(label("netmon", 19f, T.TEXT, Fonts.medium), WRAP, WRAP, start = 10)
        versionLabel = bar.add(label("", 12f, T.TEXT3, numbers = true), 0, WRAP, weight = 1f, start = 8)
        versionLabel.maxLines = 1
        pill = bar.add(label("", 13f, T.TEXT2, numbers = true), WRAP, WRAP)
        pill.setPadding(dp(12), dp(7), dp(12), dp(7))
        pill.background = pressable(rounded(T.SURFACE, dpf(16f)), dpf(16f))
        pill.isClickable = true
        pill.setOnClickListener { showConnect() }
        return bar
    }

    private fun buildNav(): LinearLayout {
        val bar = row()
        bar.setBackgroundColor(T.BG)
        bar.setPadding(0, dp(6), 0, dp(8))
        val items = listOf(
            R.drawable.ic_nav_overview to "Overview",
            R.drawable.ic_nav_devices to "Devices",
            R.drawable.ic_nav_nearby to "Nearby",
            R.drawable.ic_nav_events to "Events",
            R.drawable.ic_nav_internet to "Internet",
            R.drawable.ic_nav_settings to "Settings",
        )
        items.forEachIndexed { i, (icon, text) ->
            val item = column()
            item.gravity = Gravity.CENTER_HORIZONTAL
            item.setPadding(0, dp(4), 0, dp(4))
            item.background = pressable(null, dpf(16f))
            item.isClickable = true
            item.contentDescription = text
            val iv = ImageView(this)
            iv.setImageResource(icon)
            iv.scaleType = ImageView.ScaleType.CENTER
            // Six destinations share the width: the pill behind the selected
            // icon is never wider than its slot leaves room for.
            item.add(iv, dp(52), dp(30))
            val tv = label(text, 11.5f, T.TEXT2)
            tv.gravity = Gravity.CENTER
            tv.maxLines = 1
            // With a large font setting a label shrinks to fit rather than wrap.
            tv.setAutoSizeTextTypeUniformWithConfiguration(8, 12, 1, TypedValue.COMPLEX_UNIT_SP)
            item.add(tv, MATCH, dp(18), top = 2)
            item.setOnClickListener { showTab(i) }
            navIcons.add(iv)
            navLabels.add(tv)
            bar.add(item, 0, WRAP, weight = 1f)
        }
        val wrap = column()
        wrap.add(View(this).apply { setBackgroundColor(T.EDGE) }, MATCH, maxOf(1, dp(1) / 2))
        wrap.add(bar)
        return wrap
    }

    private fun styleNav() {
        for (i in navIcons.indices) {
            val on = i == tab
            val color = if (on) T.ACCENT else T.TEXT2
            navIcons[i].imageTintList = android.content.res.ColorStateList.valueOf(color)
            navIcons[i].background = if (on) rounded(T.ACCENT_TINT, dpf(15f)) else null
            navLabels[i].setTextColor(if (on) T.ACCENT else T.TEXT2)
            navLabels[i].typeface = if (on) Fonts.medium else Fonts.regular
            (navIcons[i].parent as? View)?.isSelected = on
        }
    }

    private fun applyInsets(root: View) {
        // Only an edge-to-edge window (Android 15 and later, for an app that
        // targets 35 or higher) draws under the system bars. Below that the
        // window already sits between them, and padding again would double up.
        if (Build.VERSION.SDK_INT < 35 || applicationInfo.targetSdkVersion < 35) return
        root.setOnApplyWindowInsetsListener { v, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                val ime = insets.getInsets(WindowInsets.Type.ime())
                v.setPadding(bars.left, bars.top, bars.right, maxOf(bars.bottom, ime.bottom))
            } else {
                @Suppress("DEPRECATION")
                v.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            insets
        }
    }

    private fun renderTopBar() {
        val h = Board.health
        versionLabel.text = h?.version ?: ""
        val base = Board.base
        val (dot, words) = when (Board.link) {
            Board.Link.NONE -> T.TEXT3 to "Not set up"
            Board.Link.CONNECTING -> T.TEXT3 to "Connecting"
            Board.Link.LOST -> T.BAD to "Not answering"
            Board.Link.LIVE -> if (h?.inSetupMode == true) T.WARN to "Setup mode"
                else T.OK to (base?.let { NetmonClient.display(it) } ?: "Live")
        }
        val sb = SpannableStringBuilder("●  ")
        sb.setSpan(ForegroundColorSpan(dot), 0, 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        sb.append(words)
        pill.text = sb
        pill.contentDescription = "Monitor: $words. Tap to change."
    }

    // --- navigation -------------------------------------------------------------

    fun showTab(i: Int) {
        tab = i.coerceIn(0, screens.size - 1)
        nav.visibility = View.VISIBLE
        switchTo(screens[tab])
        styleNav()
        poll()
        restartFastPoll()
    }

    /** Starts, or restarts at a new pace, the quicker readings the showing screen asks for. */
    fun restartFastPoll() {
        handler.removeCallbacks(fastTick)
        val s = showing ?: return
        if (!resumed || s is ConnectScreen || s.fastParts.isEmpty() || s.fastMs <= 0) return
        handler.post(fastTick)
    }

    /** Opens the Finder for one device: from the map, or from anywhere else that names one. */
    fun openFinder(type: String, addr: String) {
        if (Board.base == null) return
        showTab(TAB_NEARBY)
        (screens[TAB_NEARBY] as NearbyScreen).find(type, addr)
    }

    /** Shows one device's details, as the Devices list does. */
    fun showDevice(mac: String) {
        (screens[TAB_DEVICES] as DevicesScreen).detailFor(mac)
    }

    fun showConnect() {
        val c = connect ?: ConnectScreen(this).also { connect = it }
        nav.visibility = View.GONE
        switchTo(c)
        restartFastPoll()
    }

    /** Leaves the connect screen, if there is a board to go back to. */
    fun closeConnect() {
        if (Board.base == null) return
        showTab(tab)
    }

    private fun switchTo(s: Screen) {
        if (showing === s) {
            s.render()
            return
        }
        showing?.onHidden()
        content.removeAllViews()
        val v = s.view
        (v.parent as? FrameLayout)?.removeView(v)
        content.addView(v, FrameLayout.LayoutParams(MATCH, MATCH))
        showing = s
        s.onShown()
        s.render()
        renderTopBar()
    }

    private fun poll() {
        val s = showing ?: return
        if (s is ConnectScreen) return
        Board.refresh(listOf(Board.Part.HEALTH) + s.parts)
    }

    private fun onBoardChanged() {
        if (!::pill.isInitialized) return
        renderTopBar()
        showing?.render()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val s = showing
        when {
            s is ConnectScreen && Board.base != null -> closeConnect()
            s?.onBack() == true -> {}
            s !is ConnectScreen && tab != TAB_OVERVIEW -> showTab(TAB_OVERVIEW)
            else -> @Suppress("DEPRECATION") super.onBackPressed()
        }
    }

    // --- services for the screens ------------------------------------------------

    fun pickFirmware(callback: (Uri) -> Unit) {
        fileCallback = callback
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
        try {
            @Suppress("DEPRECATION")
            startActivityForResult(i, REQ_FILE)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(this, "No file picker is available on this phone.", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_FILE && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            val cb = fileCallback
            if (cb != null) {
                cb(uri)
            } else {
                // Android restarted the app while the picker was open, and the
                // callback went with it. The file is still wanted by Settings.
                if (Board.base == null) return
                showTab(TAB_SETTINGS)
                (screens[TAB_SETTINGS] as SettingsScreen).loadImage(uri)
            }
        }
    }

    /**
     * Keeps the screen on while anything asks for it: a firmware upload, or
     * the Finder while you walk with the phone in hand.
     */
    fun keepScreenOn(who: String, on: Boolean) {
        if (on) awake.add(who) else awake.remove(who)
        if (awake.isNotEmpty()) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    /** Switches the notification mode, asking Android for permission when it is needed. */
    fun enableAlerts(mode: AlertMode) {
        val prefs = AppState.prefs
        prefs.alertMode = mode
        prefs.alertPromptDone = true
        Alerts.schedule(this)
        if (mode != AlertMode.OFF) askNotificationPermission()
        onBoardChanged()
    }

    fun askNotificationPermission() {
        if (Alerts.needsRuntimePermission(this)) {
            requestPermissions(arrayOf(Alerts.PERMISSION), REQ_NOTIFY)
        } else if (!Alerts.allowed(this)) {
            openNotificationSettings()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_NOTIFY) return
        val granted = grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            dialog("Notifications are blocked",
                label("Android did not allow notifications, so alerts cannot appear. You can allow them in the app's settings.", 15f, T.TEXT2),
                "Open settings", { openNotificationSettings() }, negative = "Not now")
        }
        onBoardChanged()
    }

    private fun openNotificationSettings() {
        val i = Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, packageName)
        try {
            startActivity(i)
        } catch (e: ActivityNotFoundException) {
            startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
        }
    }

    private fun showCrash(text: String) {
        val tv = label(text, 11f, T.TEXT2)
        tv.typeface = Typeface.MONOSPACE
        tv.setTextIsSelectable(true)
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        val scroll = ScrollView(this)
        scroll.addView(tv, FrameLayout.LayoutParams(MATCH, WRAP))
        val box = column()
        box.add(label("The app stopped unexpectedly last time. The details below help fix it; copy them into a message if you want it looked at.", 14f, T.TEXT2))
        box.add(scroll, MATCH, dp(260), top = 12)
        box.add(button("Copy details", Btn.SECONDARY) {
            getSystemService(ClipboardManager::class.java)?.setPrimaryClip(ClipData.newPlainText("netmon crash", text))
            Toast.makeText(this, "Copied", Toast.LENGTH_SHORT).show()
        }, WRAP, WRAP, top = 12)
        dialog("Last crash", box, "Close", null, negative = null)
    }
}
