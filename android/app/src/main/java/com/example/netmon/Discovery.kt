package com.example.netmon

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper
import java.net.Inet4Address
import java.net.InetAddress
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * Finds netmon boards on the phone's network, three ways at once:
 *  - the address used last time,
 *  - the board's mDNS adverts (_http._tcp, added in 0.9.x, and ArduinoOTA's
 *    _arduino._tcp), which Android can browse but does not resolve as a name,
 *  - a sweep of the phone's subnet asking every address for /api/health.
 * Whatever answers like a netmon board is reported once, on the main thread.
 */
class Discovery(context: Context, private val listener: Listener) {

    interface Listener {
        fun onFound(board: Found)
        fun onProgress(checked: Int, total: Int)
        fun onFinished(foundAny: Boolean)
    }

    data class Found(val base: String, val version: String, val via: String)

    private val app = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val stopped = AtomicBoolean(false)
    private val found = LinkedHashMap<String, Found>()
    private var pool: ExecutorService? = null
    private var nsd: NsdManager? = null
    private val nsdListeners = ArrayList<NsdManager.DiscoveryListener>()
    private val resolveQueue = ArrayList<NsdServiceInfo>()
    private var resolving = false

    fun start(saved: String?) {
        NetRoute.pinIfNeeded(app)
        val p = Executors.newFixedThreadPool(32)
        pool = p
        if (saved != null) p.execute { probe(saved, "last used") }
        startNsd()

        val lan = NetRoute.local(app)
        val hosts = if (lan == null) emptyList() else Subnet.hosts(lan.ip, lan.prefix, 1024)
        // The setup network always puts the board at 192.168.4.1; ask there first.
        val ordered = ArrayList<String>()
        if (lan != null && lan.ipText.startsWith("192.168.4.")) ordered.add("192.168.4.1")
        for (h in hosts) if (h !in ordered) ordered.add(h)

        val total = ordered.size
        val checked = AtomicInteger(0)
        val remaining = AtomicInteger(total)
        if (total == 0) {
            // No local network: only the saved address and mDNS can help.
            main.postDelayed({ finish() }, 6000)
        }
        for (h in ordered) {
            p.execute {
                if (!stopped.get()) probe("http://$h", "network sweep", quick = true)
                val n = checked.incrementAndGet()
                if (n % 16 == 0 || n == total) main.post { if (!stopped.get()) listener.onProgress(n, total) }
                if (remaining.decrementAndGet() == 0) {
                    // Give mDNS a moment longer than the sweep before calling it done.
                    main.postDelayed({ finish() }, 1500)
                }
            }
        }
    }

    fun stop() {
        if (!stopped.compareAndSet(false, true)) return
        stopNsd()
        pool?.shutdownNow()
        pool = null
    }

    private fun finish() {
        if (stopped.get()) return
        val any = synchronized(found) { found.isNotEmpty() }
        stop()
        listener.onFinished(any)
    }

    private fun probe(base: String, via: String, quick: Boolean = false) {
        if (stopped.get()) return
        val c = try {
            NetmonClient(base)
        } catch (e: IllegalArgumentException) {
            return
        }
        if (quick) {
            c.connectTimeoutMs = 700
            c.readTimeoutMs = 2500
        } else {
            c.connectTimeoutMs = 2500
            c.readTimeoutMs = 4000
        }
        val h = c.identify() ?: return
        report(Found(c.base, h.version, via))
    }

    private fun report(f: Found) {
        val fresh = synchronized(found) {
            if (found.containsKey(f.base)) false else { found[f.base] = f; true }
        }
        if (fresh) main.post { if (!stopped.get()) listener.onFound(f) }
    }

    // --- mDNS -----------------------------------------------------------------

    private fun startNsd() {
        val m = app.getSystemService(NsdManager::class.java) ?: return
        nsd = m
        for (type in listOf("_http._tcp.", "_arduino._tcp.")) {
            val l = object : NsdManager.DiscoveryListener {
                override fun onDiscoveryStarted(serviceType: String) {}
                override fun onDiscoveryStopped(serviceType: String) {}
                override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {}
                override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
                override fun onServiceLost(serviceInfo: NsdServiceInfo) {}
                override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                    val name = serviceInfo.serviceName ?: return
                    // Anything named netmon; the sweep catches a renamed board anyway.
                    if (!name.lowercase().contains("netmon")) return
                    main.post { queueResolve(serviceInfo) }
                }
            }
            try {
                m.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, l)
                nsdListeners.add(l)
            } catch (e: RuntimeException) {
                // Some builds refuse a second browse; the sweep still runs.
            }
        }
    }

    private fun stopNsd() {
        val m = nsd ?: return
        for (l in nsdListeners) {
            try {
                m.stopServiceDiscovery(l)
            } catch (e: RuntimeException) {
            }
        }
        nsdListeners.clear()
        nsd = null
    }

    // NsdManager resolves one service at a time and fails a second request made
    // while one is running, so resolves are queued. Runs on the main thread.
    private fun queueResolve(info: NsdServiceInfo) {
        if (stopped.get()) return
        resolveQueue.add(info)
        pumpResolve()
    }

    private fun pumpResolve() {
        val m = nsd ?: return
        if (resolving || resolveQueue.isEmpty() || stopped.get()) return
        val next = resolveQueue.removeAt(0)
        resolving = true
        val cb = object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                main.post { resolving = false; pumpResolve() }
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                @Suppress("DEPRECATION")
                val host: InetAddress? = serviceInfo.host
                val port = serviceInfo.port
                main.post { resolving = false; pumpResolve() }
                if (host is Inet4Address) {
                    val addr = host.hostAddress ?: return
                    val base = if (port == 80 || port <= 0) "http://$addr" else "http://$addr:$port"
                    // ArduinoOTA advertises its upload port, not the web server's.
                    val web = if (serviceInfo.serviceType?.contains("arduino") == true) "http://$addr" else base
                    try {
                        pool?.execute { probe(web, "mDNS") }
                    } catch (e: java.util.concurrent.RejectedExecutionException) {
                        // The search was stopped while this answer was on its way.
                    }
                }
            }
        }
        try {
            @Suppress("DEPRECATION")
            m.resolveService(next, cb)
        } catch (e: RuntimeException) {
            resolving = false
        }
    }

    companion object {
        /** Blocking check that an address typed by hand is a netmon board. */
        fun check(address: String): Found? {
            val c = NetmonClient(address)
            c.connectTimeoutMs = 4000
            c.readTimeoutMs = 6000
            val h = c.identify() ?: return null
            return Found(c.base, h.version, "entered")
        }
    }
}
