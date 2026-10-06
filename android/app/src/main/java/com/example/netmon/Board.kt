package com.example.netmon

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.util.EnumMap
import java.util.EnumSet
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * The latest readings from the board, for the screens.
 *
 * All requests go through one worker thread, in order: the board's web server
 * handles one client at a time, and a burst of parallel requests would only
 * queue up inside it and time out. Results are applied on the main thread,
 * which is the only thread that reads the fields below.
 *
 * A board may be reachable two ways: over Wi-Fi at its address, and over the
 * Bluetooth link once this phone is paired with it. [base] is the one in use;
 * RoutePlan decides when to change, and the readings carry on either way.
 */
object Board {

    enum class Part { HEALTH, DEVICES, EVENTS, LATENCY, ISP, CONFIG, NETWORKS, DHCP, NEARBY, NEARBY_CONFIG, MAP, BLE, REPORTS, AUTH }

    enum class Link { NONE, CONNECTING, LIVE, LOST }

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private val io: ExecutorService = Executors.newSingleThreadExecutor { r ->
        Thread(r, "netmon-board").apply { isDaemon = true }
    }
    private val queued = EnumSet.noneOf(Part::class.java)
    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    private var generation = 0

    /** The way to the board in use now: "http://…" over Wi-Fi, "ble://…" over Bluetooth. */
    var base: String? = null
        private set
    /** The board's address on Wi-Fi, when known. */
    val wifiBase: String? get() = AppState.prefs.boardUrl
    /** The board over Bluetooth, once this phone is paired with it. */
    val bleBase: String? get() = AppState.prefs.boardBle
    val onBluetooth: Boolean get() = LinkCodec.isBle(base)
    var health: Health? = null
        private set
    var devices: List<Device>? = null
        private set
    var latency: Latency? = null
        private set
    var isp: Isp? = null
        private set
    var config: BoardConfig? = null
        private set
    var networks: List<SavedNetwork>? = null
        private set
    var dhcp: DhcpStatus? = null
        private set
    var nearby: Nearby? = null
        private set
    /** When [nearby] was read, phone clock. */
    var nearbyAtMs = 0L
        private set
    var nearbyConfig: NearbyConfig? = null
        private set
    var map: MapInfo? = null
        private set
    var ble: BleStatus? = null
        private set
    var reports: ReportList? = null
        private set
    /** Signing in, as the board sees this phone (firmware 0.13). */
    var auth: AuthInfo? = null
        private set

    /**
     * The board wants a password the app cannot give it by itself (firmware
     * 0.13): the screens ask. Readings wait until it is given.
     */
    var needsLogin = false
        private set

    /** Readings that failed for a reason other than the board being unreachable. */
    val partErrors = EnumMap<Part, String>(Part::class.java)
    /** The HTTP status behind each of [partErrors], when there was one: 404 means the firmware lacks it. */
    val partStatus = EnumMap<Part, Int>(Part::class.java)
    var lastOkMs = 0L
        private set
    var lastError: String? = null
        private set
    private var failures = 0
    /** Set while a firmware upload has the board to itself. */
    @Volatile var paused = false

    // Moved to an address not used for this board before: the board's MAC
    // before the move, "" when none was known. The first health reading
    // settles whether it is the same board (RoutePlan.sameBoard).
    private var verifyMac: String? = null
    private var lastWifiCheckMs = 0L
    private var wifiChecking = false
    // The board's clock was set from this phone during this run of the app.
    private var clockSentFor: String? = null

    val link: Link
        get() = when {
            base == null -> Link.NONE
            failures >= 2 || (failures > 0 && lastOkMs == 0L) -> Link.LOST
            lastOkMs == 0L -> Link.CONNECTING
            else -> Link.LIVE
        }

    fun init(context: Context) {
        app = context.applicationContext
        val p = AppState.prefs
        base = RoutePlan.initial(p.linkMode, p.boardUrl, p.boardBle, p.lastRoute)
    }

    fun addListener(l: () -> Unit) { listeners.add(l) }
    fun removeListener(l: () -> Unit) { listeners.remove(l) }

    private fun changed() { for (l in listeners) l() }

    /**
     * Switches to the board at [address], over Wi-Fi or ("ble://…")
     * Bluetooth. An address this board is already known by is just a change
     * of route. Any other may be this board somewhere new or another board:
     * the first health reading tells, by the board's MAC, and only then is
     * what the phone learned about the old one let go.
     */
    fun connect(address: String) {
        val b = NetmonClient.normalize(address)
        val p = AppState.prefs
        val known = b == p.boardUrl || b == p.boardBle
        if (!known) {
            verifyMac = p.boardMac ?: ""
            clear()
            // Not known yet to be the same board: it gets no session until
            // it says it is, and the saved password only then (Auth.renew).
            Auth.dropSession()
        }
        needsLogin = false
        if (LinkCodec.isBle(b)) p.boardBle = b else p.boardUrl = b
        base = b
        p.lastRoute = b
        failures = 0
        routeChangedAuto = false
        changed()
        Alerts.schedule(app)
        refresh(Part.HEALTH, Part.DEVICES, Part.EVENTS)
    }

    /**
     * Uses another route to the same board: no readings are dropped.
     * [auto] marks a change RoutePlan made rather than the person.
     */
    fun useRoute(route: String, auto: Boolean = false) {
        if (route == base) return
        base = route
        AppState.prefs.lastRoute = route
        failures = 0
        routeChangedAuto = auto
        changed()
        refresh(Part.HEALTH)
    }

    /** True when the last change of route was RoutePlan's own doing. */
    var routeChangedAuto = false
        private set

    /** Why Bluetooth was given up, for the screens to say once; cleared when read. */
    var lostReason: String? = null

    /** The last error reaching the board, in the board's or the link's own words. */
    val lastProblem: String? get() = lastError

    /** Changes how the app reaches the board, and the route now if the new mode rules this one out. */
    fun setLinkMode(mode: LinkMode) {
        val p = AppState.prefs
        p.linkMode = mode
        val w = p.boardUrl
        val bl = p.boardBle
        when (mode) {
            LinkMode.WIFI -> if (w != null) useRoute(w)
            LinkMode.BLUETOOTH -> if (bl != null) useRoute(bl)
            LinkMode.AUTO -> {}
        }
        changed()
    }

    /** Pairing done: the board is now reachable over Bluetooth too. */
    fun paired(bleAddress: String) {
        AppState.prefs.boardBle = NetmonClient.normalize(LinkCodec.SCHEME + bleAddress)
        changed()
    }

    /** This phone is no longer paired with the board: Wi-Fi only from now on. */
    fun unpaired() {
        val p = AppState.prefs
        val bl = p.boardBle
        p.boardBle = null
        if (p.linkMode == LinkMode.BLUETOOTH) p.linkMode = LinkMode.AUTO
        if (bl != null && base == bl) {
            val w = p.boardUrl
            if (w != null) useRoute(w) else forget()
        }
        changed()
    }

    fun forget() {
        base = null
        val p = AppState.prefs
        p.boardUrl = null
        p.boardBle = null
        p.boardMac = null
        p.lastRoute = null
        Auth.forgetBoard()
        needsLogin = false
        clear()
        changed()
        Alerts.schedule(app)
    }

    private fun clear() {
        generation++
        health = null; devices = null; latency = null; isp = null
        config = null; networks = null; dhcp = null
        nearby = null; nearbyAtMs = 0L; nearbyConfig = null; map = null; ble = null; reports = null; auth = null
        partErrors.clear()
        partStatus.clear()
        lastOkMs = 0L; lastError = null; failures = 0
        synchronized(queued) { queued.clear() }
    }

    fun refresh(vararg parts: Part) = refresh(parts.toList())

    fun refresh(parts: Collection<Part>) {
        val b = base ?: return
        if (paused || parts.isEmpty() || needsLogin) return
        recheckWifi()
        val start: Boolean
        synchronized(queued) {
            start = queued.isEmpty()
            queued.addAll(parts)
        }
        if (start) {
            val gen = generation
            io.execute { drain(b, gen) }
        }
    }

    private fun drain(b: String, gen: Int) {
        NetRoute.pinIfNeeded(app)
        var route = b
        var client = Auth.client(route)
        var switched = false
        while (true) {
            if (paused) {
                synchronized(queued) { queued.clear() }
                return
            }
            val part = synchronized(queued) {
                val p = queued.firstOrNull()
                if (p != null) queued.remove(p)
                p
            } ?: return
            try {
                fetch(client, part, gen, route)
            } catch (e: ApiException) {
                val unreachable = e.kind == ApiException.Kind.Unreachable || e.kind == ApiException.Kind.Timeout
                // Not answering this way: once per round, the other way, if
                // there is one this phone can use.
                if (unreachable && !switched) {
                    val alt = fallback(route)
                    if (alt != null) {
                        switched = true
                        route = alt
                        client = Auth.client(alt)
                        main.post { if (gen == generation) useRoute(alt, auto = true) }
                        synchronized(queued) { queued.add(part) }
                        continue
                    }
                }
                main.post {
                    if (gen != generation) return@post
                    failed(part, e)
                    // The monitor no longer knows this phone, or Android no
                    // longer holds the pairing: stop using Bluetooth until it
                    // is paired again, and say so where the app was.
                    if (e.kind == ApiException.Kind.NotPaired && LinkCodec.isBle(route)) {
                        unpaired()
                        lostReason = e.message
                    }
                }
                // An unreachable board would only time out again for each
                // remaining reading; the next poll tries afresh. One that
                // wants a password wants it for every reading.
                if (unreachable || e.kind == ApiException.Kind.NotPaired || e.kind == ApiException.Kind.LoginRequired) {
                    synchronized(queued) { queued.clear() }
                    return
                }
            } catch (e: RuntimeException) {
                // A reading this app cannot make sense of must not take the app down.
                main.post { if (gen == generation) failed(part, ApiException(ApiException.Kind.BadData, e.toString())) }
            }
        }
    }

    /** The other route to the board, when there is one this phone can use now. */
    private fun fallback(failed: String): String? {
        val p = AppState.prefs
        val alt = RoutePlan.fallback(p.linkMode, failed, p.boardUrl, p.boardBle) ?: return null
        // Only a board Android is paired with, with Bluetooth on: anything
        // else would fail at once, or bring up a pairing prompt unasked.
        if (LinkCodec.isBle(alt) && (!BleLink.switchedOn(app) || !BleLink.bonded(app, LinkCodec.address(alt)))) return null
        return alt
    }

    /**
     * While on Bluetooth, looks for the board over Wi-Fi now and then, in
     * the background: when it answers there, Wi-Fi takes over again.
     */
    private fun recheckWifi() {
        val p = AppState.prefs
        val w = p.boardUrl ?: return
        val now = System.currentTimeMillis()
        if (wifiChecking) return
        if (!RoutePlan.recheckWifi(p.linkMode, base, w, NetRoute.local(app) != null, now - lastWifiCheckMs)) return
        wifiChecking = true
        lastWifiCheckMs = now
        val gen = generation
        val mac = p.boardMac
        Thread({
            NetRoute.pinIfNeeded(app)
            val h = try {
                val c = NetmonClient(w)
                c.connectTimeoutMs = 1500
                c.readTimeoutMs = 3000
                c.identify()
            } catch (e: RuntimeException) {
                null
            }
            main.post {
                wifiChecking = false
                if (gen != generation || h == null) return@post
                // An answer from some other board at that address is no way back.
                if (mac != null && h.id.isNotEmpty() && !h.id.equals(mac, ignoreCase = true)) return@post
                if (onBluetooth && AppState.prefs.linkMode == LinkMode.AUTO) useRoute(w, auto = true)
            }
        }, "netmon-wifi-check").start()
    }

    /**
     * What a health reading teaches about the board's routes: whether a new
     * address is the same board, its MAC, and over Bluetooth where it is on
     * Wi-Fi now. Main thread.
     */
    private fun learn(h: Health, route: String) {
        val p = AppState.prefs
        val before = verifyMac
        if (before != null) {
            verifyMac = null
            if (!RoutePlan.sameBoard(before, h)) {
                // Another board: the other route and what the phone learned
                // belonged to the old one.
                if (LinkCodec.isBle(route)) p.boardUrl = null else p.boardBle = null
                AppState.forgetBoardData()
            }
        }
        if (h.mac.isNotEmpty()) p.boardMac = h.mac
        if (LinkCodec.isBle(route)) RoutePlan.learnedWifi(h, p.boardUrl)?.let { p.boardUrl = it }
        // The board has no clock of its own: it dates its saved reports by
        // this phone's, given once per run of the app (firmware 0.12).
        if (h.clockUnset && clockSentFor != route) {
            clockSentFor = route
            run({ it.setClock(System.currentTimeMillis() / 1000) }) { _, _ -> }
        }
    }

    private fun fetch(c: NetmonClient, part: Part, gen: Int, route: String) {
        val now = System.currentTimeMillis()
        when (part) {
            Part.HEALTH -> {
                val h = c.health()
                AppState.absorbHealth(now, h)
                publish(gen, part) { health = h; learn(h, route) }
            }
            Part.DEVICES -> {
                val d = c.devices()
                val tell = AppState.absorbDevices(d)
                if (tell.isNotEmpty()) Alerts.notifyDevices(app, tell)
                publish(gen, part) { devices = d }
            }
            Part.EVENTS -> {
                // The event list is stamped in seconds since the board started,
                // so it is only meaningful next to an uptime read at the same time.
                val h = c.health()
                val e = c.events()
                AppState.absorbHealth(now, h)
                AppState.absorbEvents(now, h.uptimeS, e)
                publish(gen, part) { health = h }
            }
            Part.LATENCY -> {
                val l = c.latency()
                publish(gen, part) { latency = l }
            }
            Part.ISP -> {
                val i = c.isp(false)
                publish(gen, part) { isp = i }
            }
            Part.CONFIG -> {
                val cf = c.config()
                publish(gen, part) { config = cf }
            }
            Part.NETWORKS -> {
                val n = c.networks()
                publish(gen, part) { networks = n }
            }
            Part.DHCP -> {
                val d = c.dhcp()
                publish(gen, part) { dhcp = d }
            }
            Part.NEARBY -> {
                val n = c.nearby()
                val at = System.currentTimeMillis()
                publish(gen, part) { nearby = n; nearbyAtMs = at }
            }
            Part.NEARBY_CONFIG -> {
                val n = c.nearbyConfig()
                publish(gen, part) { nearbyConfig = n }
            }
            Part.MAP -> {
                val m = c.map()
                publish(gen, part) { map = m }
            }
            Part.BLE -> {
                val b = c.ble()
                publish(gen, part) { ble = b }
            }
            Part.REPORTS -> {
                val r = c.reports()
                publish(gen, part) { reports = r }
            }
            Part.AUTH -> {
                val a = c.auth()
                publish(gen, part) { auth = a }
            }
        }
    }

    private inline fun publish(gen: Int, part: Part, crossinline set: () -> Unit) {
        main.post {
            if (gen != generation) return@post
            set()
            partErrors.remove(part)
            partStatus.remove(part)
            needsLogin = false
            failures = 0
            lastOkMs = System.currentTimeMillis()
            lastError = null
            changed()
        }
    }

    private fun failed(part: Part, e: ApiException) {
        when (e.kind) {
            ApiException.Kind.Unreachable, ApiException.Kind.Timeout, ApiException.Kind.NotPaired -> {
                failures++
                lastError = e.message
            }
            ApiException.Kind.LoginRequired -> {
                // With a saved password that just could not be used (the
                // board stopped answering midway), the next poll tries again.
                if (Auth.needed) needsLogin = true
                lastError = e.message
            }
            else -> {
                partErrors[part] = e.message ?: "Something went wrong."
                if (e.status != 0) partStatus[part] = e.status else partStatus.remove(part)
            }
        }
        changed()
    }

    /**
     * Runs a one-off request (saving settings, forgetting a network, a forced
     * lookup) on the same worker as the readings, and reports on the main
     * thread. [done] gets the result or the error, never both.
     */
    fun <T> run(work: (NetmonClient) -> T, done: (T?, ApiException?) -> Unit) = runVia(base, work, done)

    /** As [run], over a route of the caller's choosing: Wi-Fi for pairing, say. */
    fun <T> runVia(route: String?, work: (NetmonClient) -> T, done: (T?, ApiException?) -> Unit) {
        val b = route
        if (b == null) {
            done(null, ApiException(ApiException.Kind.Unreachable, "No monitor is set up yet."))
            return
        }
        val gen = generation
        io.execute {
            NetRoute.pinIfNeeded(app)
            val result = try {
                Result.success(work(Auth.client(b)))
            } catch (e: ApiException) {
                Result.failure(e)
            } catch (e: RuntimeException) {
                Result.failure(ApiException(ApiException.Kind.BadData, e.message ?: e.toString()))
            }
            main.post {
                if (gen != generation) {
                    // Switched to another board meanwhile: still answer, so the
                    // caller can reset its buttons, but never with the old data.
                    done(null, ApiException(ApiException.Kind.Unreachable, "Switched to another monitor."))
                    return@post
                }
                val err = result.exceptionOrNull() as? ApiException
                if (err == null) {
                    failures = 0
                    lastOkMs = System.currentTimeMillis()
                    done(result.getOrNull(), null)
                } else {
                    if (err.kind == ApiException.Kind.LoginRequired && Auth.needed && !needsLogin) {
                        needsLogin = true
                        changed()
                    }
                    done(null, err)
                }
            }
        }
    }

    /**
     * Signs in with a password the person typed; [save] keeps it, encrypted,
     * so the app signs in again by itself. Then reads everything afresh.
     */
    fun signIn(password: String, save: Boolean, done: (ApiException?) -> Unit) {
        runVia(base, { c -> Auth.signIn(c, password, save) }) { a, err ->
            if (err == null && a != null) {
                auth = a
                needsLogin = false
                lastError = null
                changed()
                refresh(Part.HEALTH, Part.DEVICES, Part.EVENTS, Part.AUTH)
            }
            done(err)
        }
    }

    /** Ends this phone's session on the board and forgets the saved password. */
    fun signOut(done: () -> Unit) {
        runVia(base, { c -> c.logout() }) { _, _ ->
            Auth.signedOut()
            auth = null
            needsLogin = true
            changed()
            done()
        }
    }

    /** Lets the screens set readings they fetched themselves, such as a forced ISP lookup. */
    fun setIsp(i: Isp) { isp = i; changed() }
    fun setNetworks(n: List<SavedNetwork>) { networks = n; changed() }
    fun setNearbyConfig(c: NearbyConfig) { nearbyConfig = c; changed() }
    fun setBle(b: BleStatus) { ble = b; changed() }
    fun setReports(r: ReportList) { reports = r; changed() }

    /** True when the last try at [part] was answered 404: this board's firmware does not have it. */
    fun missing(part: Part): Boolean = partStatus[part] == 404
}
