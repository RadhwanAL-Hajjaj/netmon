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
 */
object Board {

    enum class Part { HEALTH, DEVICES, EVENTS, LATENCY, ISP, CONFIG, NETWORKS, DHCP, NEARBY, NEARBY_CONFIG, MAP }

    enum class Link { NONE, CONNECTING, LIVE, LOST }

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private val io: ExecutorService = Executors.newSingleThreadExecutor { r ->
        Thread(r, "netmon-board").apply { isDaemon = true }
    }
    private val queued = EnumSet.noneOf(Part::class.java)
    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    private var generation = 0

    var base: String? = null
        private set
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

    val link: Link
        get() = when {
            base == null -> Link.NONE
            failures >= 2 || (failures > 0 && lastOkMs == 0L) -> Link.LOST
            lastOkMs == 0L -> Link.CONNECTING
            else -> Link.LIVE
        }

    fun init(context: Context) {
        app = context.applicationContext
        base = AppState.prefs.boardUrl
    }

    fun addListener(l: () -> Unit) { listeners.add(l) }
    fun removeListener(l: () -> Unit) { listeners.remove(l) }

    private fun changed() { for (l in listeners) l() }

    /** Switches to another board. Readings of the old one are dropped, late ones included. */
    fun connect(address: String) {
        val b = NetmonClient.normalize(address)
        val different = b != base
        base = b
        AppState.prefs.boardUrl = b
        if (different) {
            AppState.forgetBoardData()
            clear()
        }
        failures = 0
        changed()
        Alerts.schedule(app)
        refresh(Part.HEALTH, Part.DEVICES, Part.EVENTS)
    }

    fun forget() {
        base = null
        AppState.prefs.boardUrl = null
        clear()
        changed()
        Alerts.schedule(app)
    }

    private fun clear() {
        generation++
        health = null; devices = null; latency = null; isp = null
        config = null; networks = null; dhcp = null
        nearby = null; nearbyAtMs = 0L; nearbyConfig = null; map = null
        partErrors.clear()
        partStatus.clear()
        lastOkMs = 0L; lastError = null; failures = 0
        synchronized(queued) { queued.clear() }
    }

    fun refresh(vararg parts: Part) = refresh(parts.toList())

    fun refresh(parts: Collection<Part>) {
        val b = base ?: return
        if (paused || parts.isEmpty()) return
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
        val client = NetmonClient(b)
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
                fetch(client, part, gen)
            } catch (e: ApiException) {
                main.post { if (gen == generation) failed(part, e) }
                // An unreachable board would only time out again for each
                // remaining reading; the next poll tries afresh.
                if (e.kind == ApiException.Kind.Unreachable || e.kind == ApiException.Kind.Timeout) {
                    synchronized(queued) { queued.clear() }
                    return
                }
            } catch (e: RuntimeException) {
                // A reading this app cannot make sense of must not take the app down.
                main.post { if (gen == generation) failed(part, ApiException(ApiException.Kind.BadData, e.toString())) }
            }
        }
    }

    private fun fetch(c: NetmonClient, part: Part, gen: Int) {
        val now = System.currentTimeMillis()
        when (part) {
            Part.HEALTH -> {
                val h = c.health()
                AppState.absorbHealth(now, h)
                publish(gen, part) { health = h }
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
        }
    }

    private inline fun publish(gen: Int, part: Part, crossinline set: () -> Unit) {
        main.post {
            if (gen != generation) return@post
            set()
            partErrors.remove(part)
            partStatus.remove(part)
            failures = 0
            lastOkMs = System.currentTimeMillis()
            lastError = null
            changed()
        }
    }

    private fun failed(part: Part, e: ApiException) {
        when (e.kind) {
            ApiException.Kind.Unreachable, ApiException.Kind.Timeout -> {
                failures++
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
    fun <T> run(work: (NetmonClient) -> T, done: (T?, ApiException?) -> Unit) {
        val b = base
        if (b == null) {
            done(null, ApiException(ApiException.Kind.Unreachable, "No monitor is set up yet."))
            return
        }
        val gen = generation
        io.execute {
            NetRoute.pinIfNeeded(app)
            val result = try {
                Result.success(work(NetmonClient(b)))
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
                    done(null, err)
                }
            }
        }
    }

    /** Lets the screens set readings they fetched themselves, such as a forced ISP lookup. */
    fun setIsp(i: Isp) { isp = i; changed() }
    fun setNetworks(n: List<SavedNetwork>) { networks = n; changed() }
    fun setNearbyConfig(c: NearbyConfig) { nearbyConfig = c; changed() }

    /** True when the last try at [part] was answered 404: this board's firmware does not have it. */
    fun missing(part: Part): Boolean = partStatus[part] == 404
}
