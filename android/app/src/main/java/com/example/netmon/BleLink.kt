package com.example.netmon

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock

/**
 * The phone's end of the Bluetooth link: one GATT connection to one board at
 * a time, carrying requests the way the firmware expects them (LinkCodec).
 *
 * Every request waits its turn behind a lock, as over Wi-Fi: the board serves
 * a phone one request at a time. The connection is opened by the first
 * request, kept while requests keep coming, and closed a minute after the
 * last, so the board's few connections are not held by a phone in a pocket.
 *
 * Only a board this phone has paired with is ever connected to for requests.
 * Pairing is its own step ([pair]), started by the person with a code the
 * board shows on its Settings page or in this app over Wi-Fi, or the owner's
 * own code; a request never brings up Android's pairing prompt by surprise.
 * From app 1.3 the app enters the code itself, and from firmware 0.13 every
 * request also carries the session from signing in (NetmonClient).
 */
@SuppressLint("MissingPermission")    // every entry point checks first: see permitted()
object BleLink : LinkTransport {

    private const val CONNECT_MS = 9_000L
    private const val SETUP_MS = 10_000L
    private const val WRITE_MS = 6_000L
    private const val IDLE_MS = 60_000L
    private const val MTU_ASK = 517
    // GATT statuses that mean the link is not encrypted with keys this
    // board knows: still being set up, or a pairing the board has forgotten.
    private val AUTH_STATUS = setOf(5, 8, 15, 137)

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private val lock = ReentrantLock()
    private val events = LinkedBlockingQueue<Event>()
    // Answer frames that arrived while a write was still waiting for its
    // acknowledgement: kept for receive(), never dropped.
    private val early = java.util.ArrayDeque<ByteArray>()

    @Volatile private var generation = 0
    private var gatt: BluetoothGatt? = null
    private var ready: String? = null          // the address, once the link is set up
    private var rx: BluetoothGattCharacteristic? = null
    @Volatile private var mtu = 23
    private var nextId = 1
    @Volatile private var lastUsedMs = 0L

    private class Event(val gen: Int, val kind: Int, val status: Int = 0, val value: Int = 0, val data: ByteArray? = null)

    private const val CONNECTED = 1
    private const val DISCONNECTED = 2
    private const val MTU = 3
    private const val SERVICES = 4
    private const val DESC_WRITTEN = 5
    private const val CHAR_WRITTEN = 6
    private const val NOTIFIED = 7

    fun init(context: Context) {
        app = context.applicationContext
        Link.transport = this
    }

    // --- what the phone allows -------------------------------------------------

    /** The permission Android 12 and later want before any Bluetooth connection. */
    const val CONNECT = "android.permission.BLUETOOTH_CONNECT"
    const val SCAN = "android.permission.BLUETOOTH_SCAN"
    const val LOCATION = "android.permission.ACCESS_FINE_LOCATION"

    /** Permissions still to ask for: to connect, and with [scan] also to look for boards nearby. */
    fun missing(context: Context, scan: Boolean): Array<String> {
        val want = ArrayList<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            want.add(CONNECT)
            if (scan) want.add(SCAN)
        } else if (scan) {
            want.add(LOCATION)
        }
        return want.filter { context.checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }.toTypedArray()
    }

    fun permitted(context: Context): Boolean = missing(context, false).isEmpty()

    fun adapter(context: Context): BluetoothAdapter? =
        context.getSystemService(BluetoothManager::class.java)?.adapter

    /** True when this phone has Bluetooth LE and it is switched on. */
    fun switchedOn(context: Context): Boolean = adapter(context)?.isEnabled == true

    /** True when Android holds a pairing with this address. */
    fun bonded(context: Context, address: String): Boolean = try {
        permitted(context) && adapter(context)?.getRemoteDevice(address)?.bondState == BluetoothDevice.BOND_BONDED
    } catch (e: RuntimeException) {
        false
    }

    /** The netmon boards this phone is paired with: Android's own list, by name. */
    fun pairedBoards(context: Context): List<Pair<String, String>> {
        if (!permitted(context)) return emptyList()
        val a = adapter(context) ?: return emptyList()
        return try {
            a.bondedDevices.filter { d ->
                d.type != BluetoothDevice.DEVICE_TYPE_CLASSIC && (d.name ?: "").lowercase().contains("netmon")
            }.map { it.address.uppercase() to (it.name ?: "netmon") }
        } catch (e: RuntimeException) {
            emptyList()
        }
    }

    // --- requests ------------------------------------------------------------------

    override fun exchange(address: String, message: ByteArray, timeoutMs: Int): LinkReply {
        lock.lock()
        try {
            lastUsedMs = System.currentTimeMillis()
            if (ready != address || gatt == null) open(address)
            val id = nextId
            nextId = if (nextId >= 255) 1 else nextId + 1
            try {
                return LinkCodec.exchange(pipe, id, message, timeoutMs.toLong())
            } catch (e: LinkCodec.Closed) {
                close()
                throw ApiException(ApiException.Kind.Unreachable, "The Bluetooth connection to the monitor dropped.")
            } catch (e: ApiException) {
                // An answer that never finished leaves the link in doubt: start
                // the next request on a fresh one.
                if (e.kind == ApiException.Kind.Timeout || e.kind == ApiException.Kind.NotPaired) close()
                throw e
            }
        } finally {
            lastUsedMs = System.currentTimeMillis()
            scheduleIdle()
            lock.unlock()
        }
    }

    /** Drops the connection now, as when switching boards or leaving Bluetooth. */
    fun disconnect() {
        lock.lock()
        try {
            close()
        } finally {
            lock.unlock()
        }
    }

    private val idle = Runnable {
        if (lock.tryLock()) {
            try {
                if (gatt != null && System.currentTimeMillis() - lastUsedMs >= IDLE_MS) close()
            } finally {
                lock.unlock()
            }
        }
    }

    private fun scheduleIdle() {
        main.removeCallbacks(idle)
        main.postDelayed(idle, IDLE_MS + 500)
    }

    private val pipe = object : LinkCodec.Pipe {
        override val frameCap: Int get() = minOf(mtu - 3, 244)

        override fun send(frame: ByteArray) {
            val g = gatt ?: throw LinkCodec.Closed("not connected")
            val c = rx ?: throw LinkCodec.Closed("not connected")
            val gen = generation
            var busy = 0
            var auth = 0
            while (true) {
                if (!writeCharacteristic(g, c, frame)) {
                    // Android runs one GATT operation at a time and says no
                    // while the last one is finishing.
                    if (++busy > 40) throw LinkCodec.Closed("the phone would not send")
                    Thread.sleep(25)
                    continue
                }
                val e = await(gen, CHAR_WRITTEN, WRITE_MS) ?: throw LinkCodec.Closed("no reply to a write")
                when {
                    e.status == BluetoothGatt.GATT_SUCCESS -> return
                    e.status in AUTH_STATUS -> {
                        // Encryption may still be starting; a board that has
                        // forgotten this phone never lets it finish.
                        if (++auth > 4) {
                            throw ApiException(ApiException.Kind.NotPaired,
                                "The monitor no longer knows this phone. Pair it again in Settings, Bluetooth.")
                        }
                        Thread.sleep(1000)
                    }
                    else -> throw LinkCodec.Closed("write failed, status ${e.status}")
                }
            }
        }

        override fun receive(timeoutMs: Long): ByteArray? {
            early.pollFirst()?.let { return it }
            val until = System.currentTimeMillis() + timeoutMs
            while (true) {
                val left = until - System.currentTimeMillis()
                if (left <= 0) return null
                val e = events.poll(left, TimeUnit.MILLISECONDS) ?: return null
                if (e.gen != generation) continue
                if (e.kind == DISCONNECTED) throw LinkCodec.Closed("disconnected")
                if (e.kind == NOTIFIED && e.data != null) return e.data
            }
        }
    }

    // --- connecting ------------------------------------------------------------------

    private fun device(address: String): BluetoothDevice {
        if (!permitted(app)) {
            throw ApiException(ApiException.Kind.Unreachable, "netmon needs the Nearby devices permission to use Bluetooth.")
        }
        val a = adapter(app) ?: throw ApiException(ApiException.Kind.Unreachable, "This phone has no Bluetooth.")
        if (!a.isEnabled) throw ApiException(ApiException.Kind.Unreachable, "Bluetooth is off on this phone.")
        return try {
            a.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            throw ApiException(ApiException.Kind.Unreachable, "That is not a Bluetooth address.")
        }
    }

    private fun open(address: String) {
        close()
        val dev = device(address)
        if (dev.bondState != BluetoothDevice.BOND_BONDED) {
            throw ApiException(ApiException.Kind.NotPaired,
                "This phone is not paired with the monitor. Pair it in Settings, Bluetooth, while on the monitor's Wi-Fi.")
        }
        val gen = connect(dev)
        setUp(gen, address)
    }

    /**
     * Opens the GATT connection. A first try that Android gives up on at
     * once (its well-known status 133) is made once more; one that simply
     * hears nothing for [CONNECT_MS] is not, since the board is out of reach.
     */
    private fun connect(dev: BluetoothDevice): Int {
        for (attempt in 1..2) {
            val gen = ++generation
            events.clear()
            val g = dev.connectGatt(app, false, Callback(gen), BluetoothDevice.TRANSPORT_LE)
                ?: throw ApiException(ApiException.Kind.Unreachable, "Android would not open a Bluetooth connection.")
            gatt = g
            val e = awaitAny(gen, setOf(CONNECTED, DISCONNECTED), CONNECT_MS)
            if (e != null && e.kind == CONNECTED && e.status == BluetoothGatt.GATT_SUCCESS) return gen
            close()
            if (e == null) break
            Thread.sleep(400)
        }
        throw ApiException(ApiException.Kind.Unreachable,
            "The monitor did not answer over Bluetooth. Check it is switched on and within about 10 metres.")
    }

    /** The larger MTU, the service, and notifications on: the link ready for requests. */
    private fun setUp(gen: Int, address: String) {
        val g = gatt ?: throw ApiException(ApiException.Kind.Unreachable, "The Bluetooth connection dropped.")
        try {
            mtu = 23
            if (g.requestMtu(MTU_ASK)) {
                val e = await(gen, MTU, 5000)
                if (e != null && e.status == BluetoothGatt.GATT_SUCCESS) mtu = e.value
            }
            if (!g.discoverServices()) throw LinkCodec.Closed("discovery refused")
            val s = await(gen, SERVICES, SETUP_MS) ?: throw LinkCodec.Closed("no services")
            if (s.status != BluetoothGatt.GATT_SUCCESS) throw LinkCodec.Closed("discovery failed")
            val svc = g.getService(UUID.fromString(LinkCodec.SERVICE))
                ?: throw ApiException(ApiException.Kind.Http,
                    "That Bluetooth device has no netmon link. It needs firmware 0.12 or later.", 404)
            val r = svc.getCharacteristic(UUID.fromString(LinkCodec.RX))
            val t = svc.getCharacteristic(UUID.fromString(LinkCodec.TX))
            if (r == null || t == null) throw ApiException(ApiException.Kind.Http, "The board's Bluetooth link is incomplete.", 404)
            if (!g.setCharacteristicNotification(t, true)) throw LinkCodec.Closed("notifications refused")
            val d = t.getDescriptor(UUID.fromString(LinkCodec.CCCD)) ?: throw LinkCodec.Closed("no CCCD")
            var auth = 0
            while (true) {
                if (!writeDescriptor(g, d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)) {
                    Thread.sleep(50)
                    if (++auth > 20) throw LinkCodec.Closed("descriptor write refused")
                    continue
                }
                val e = await(gen, DESC_WRITTEN, WRITE_MS) ?: throw LinkCodec.Closed("no reply to the descriptor write")
                if (e.status == BluetoothGatt.GATT_SUCCESS) break
                if (e.status !in AUTH_STATUS || ++auth > 4) throw LinkCodec.Closed("descriptor write failed, status ${e.status}")
                Thread.sleep(1000)
            }
            rx = r
            ready = address
        } catch (e: LinkCodec.Closed) {
            close()
            throw ApiException(ApiException.Kind.Unreachable, "The Bluetooth connection to the monitor dropped while it was being set up.")
        } catch (e: ApiException) {
            close()
            throw e
        }
    }

    private fun close() {
        val g = gatt
        gatt = null
        ready = null
        rx = null
        mtu = 23
        early.clear()
        generation++                 // whatever the old connection still says is ignored
        if (g != null) {
            try {
                g.disconnect()
            } catch (e: RuntimeException) {
            }
            try {
                g.close()
            } catch (e: RuntimeException) {
            }
        }
    }

    private fun await(gen: Int, kind: Int, timeoutMs: Long): Event? {
        val e = awaitAny(gen, setOf(kind, DISCONNECTED), timeoutMs) ?: return null
        if (e.kind == DISCONNECTED) throw LinkCodec.Closed("disconnected")
        return e
    }

    private fun awaitAny(gen: Int, kinds: Set<Int>, timeoutMs: Long): Event? {
        val until = System.currentTimeMillis() + timeoutMs
        while (true) {
            val left = until - System.currentTimeMillis()
            if (left <= 0) return null
            val e = events.poll(left, TimeUnit.MILLISECONDS) ?: return null
            if (e.gen != gen) continue
            if (e.kind in kinds) return e
            if (e.kind == NOTIFIED && e.data != null) early.add(e.data)
        }
    }

    @Suppress("DEPRECATION")
    private fun writeCharacteristic(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
        } else {
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            c.value = value
            g.writeCharacteristic(c)
        }

    @Suppress("DEPRECATION")
    private fun writeDescriptor(g: BluetoothGatt, d: BluetoothGattDescriptor, value: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, value) == 0
        } else {
            d.value = value
            g.writeDescriptor(d)
        }

    /** One per connection, so anything an old connection still reports is told apart. */
    private class Callback(val gen: Int) : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val kind = if (newState == BluetoothProfile.STATE_CONNECTED) CONNECTED else DISCONNECTED
            events.add(Event(gen, kind, status))
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            events.add(Event(gen, MTU, status, mtu))
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            events.add(Event(gen, SERVICES, status))
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            events.add(Event(gen, DESC_WRITTEN, status))
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            events.add(Event(gen, CHAR_WRITTEN, status))
        }

        // Android 13 and later.
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            events.add(Event(gen, NOTIFIED, data = value.copyOf()))
        }

        // Before Android 13: the value is read from the characteristic, at once,
        // before the next notification overwrites it.
        @Deprecated("Deprecated in Java")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            val v = characteristic.value ?: return
            events.add(Event(gen, NOTIFIED, data = v.copyOf()))
        }
    }

    // --- pairing -------------------------------------------------------------------------

    /** How a pairing attempt ended. */
    enum class Paired { YES, FAILED, CANCELLED }

    /**
     * A pairing attempt's end: [why] in words when it failed, and whether
     * the app gave Android the code itself ([codeByApp]) or Android had to
     * ask the person for it.
     */
    class PairResult(val result: Paired, val why: String = "", val codeByApp: Boolean = false)

    // Hidden in the SDK, unchanged since Android 4.4: why a pairing ended
    // without a bond, as ACTION_BOND_STATE_CHANGED carries it.
    private const val EXTRA_REASON = "android.bluetooth.device.extra.REASON"

    private fun unbondText(reason: Int): String = when (reason) {
        1 -> "The code did not match. Check it against the monitor's Settings page, and that a pairing window is open there."
        2 -> "The monitor turned the pairing down. Open a pairing window on it, then try again."
        3 -> "Pairing was cancelled on this phone."
        4 -> "The monitor went out of reach before pairing finished."
        5 -> "The phone was busy looking for devices. Try again in a moment."
        6 -> "Nobody entered the code in time."
        7 -> "Too many attempts just now. Wait a minute, then try again."
        8 -> "The monitor stopped the pairing: its window may have closed, or the code was wrong."
        else -> "Pairing did not finish."
    }

    /**
     * Pairs this phone with the board at [address], whose pairing window is
     * open. The phone asks to pair over a connection it opens itself: from
     * firmware 0.13 the board no longer asks first, since its request and the
     * phone's crossed and broke pairing on many phones. When Android wants the
     * board's code, the app gives it [code] itself, so no prompt appears; a
     * phone that will not take it from an app, or no [code], brings up
     * Android's own prompt for the person to type it. [asked] is told, on the
     * main thread, which of the two happened.
     *
     * An old pairing Android still holds is dropped first, so the new one
     * starts clean. On YES the link is left set up, ready for requests.
     */
    fun pair(address: String, code: String?, timeoutMs: Long, cancelled: () -> Boolean,
             asked: (byApp: Boolean) -> Unit = {}): PairResult {
        require(code == null || (code.length == 6 && code.all { it in '0'..'9' })) { "The code is six digits." }
        lock.lock()
        try {
            close()
            val dev = device(address)
            if (dev.bondState == BluetoothDevice.BOND_BONDED) {
                removeBond(dev)
                val until = System.currentTimeMillis() + 4000
                while (dev.bondState != BluetoothDevice.BOND_NONE && System.currentTimeMillis() < until) Thread.sleep(100)
                if (dev.bondState == BluetoothDevice.BOND_BONDED) {
                    throw ApiException(ApiException.Kind.NotPaired,
                        "Android still holds an old pairing with the monitor. Forget netmon in the phone's Bluetooth settings, then pair again.")
                }
            }
            val states = LinkedBlockingQueue<IntArray>()
            val gave = java.util.concurrent.atomic.AtomicBoolean(false)
            val watcher = object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    val d = bondDevice(intent) ?: return
                    if (!d.address.equals(address, ignoreCase = true)) return
                    when (intent.action) {
                        BluetoothDevice.ACTION_BOND_STATE_CHANGED -> states.add(intArrayOf(
                            intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR),
                            intent.getIntExtra(EXTRA_REASON, 0)))
                        BluetoothDevice.ACTION_PAIRING_REQUEST -> {
                            // A passkey the board shows arrives as a PIN request;
                            // Android sends its six digits on as the passkey.
                            val variant = intent.getIntExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT, BluetoothDevice.ERROR)
                            var took = false
                            if (code != null && variant == BluetoothDevice.PAIRING_VARIANT_PIN) {
                                took = try {
                                    d.setPin(code.toByteArray(Charsets.US_ASCII))
                                } catch (e: SecurityException) {
                                    false
                                }
                                // Nobody else needs to hear of it: no prompt.
                                if (took && isOrderedBroadcast) abortBroadcast()
                            }
                            gave.set(took)
                            asked(took)
                        }
                    }
                }
            }
            registerPairingWatcher(watcher)
            try {
                val gen = connect(dev)
                if (dev.bondState == BluetoothDevice.BOND_NONE && !dev.createBond()) {
                    close()
                    return PairResult(Paired.FAILED, "Android would not start pairing. Switch Bluetooth off and on, then try again.")
                }
                val until = System.currentTimeMillis() + timeoutMs
                var started = false
                while (dev.bondState != BluetoothDevice.BOND_BONDED) {
                    if (cancelled()) {
                        close()
                        if (dev.bondState == BluetoothDevice.BOND_BONDING) removeBond(dev)
                        return PairResult(Paired.CANCELLED)
                    }
                    if (System.currentTimeMillis() > until) {
                        close()
                        return PairResult(Paired.FAILED, "Pairing took too long.", gave.get())
                    }
                    val s = states.poll(300, TimeUnit.MILLISECONDS)
                    if (s != null && s[0] == BluetoothDevice.BOND_BONDING) started = true
                    if (s != null && s[0] == BluetoothDevice.BOND_BONDED) break
                    if (s != null && s[0] == BluetoothDevice.BOND_NONE && started) {
                        close()
                        return PairResult(Paired.FAILED, unbondText(s[1]), gave.get())
                    }
                    // The connection dropped before the pairing finished.
                    var e = events.poll()
                    while (e != null) {
                        if (e.gen == gen && e.kind == DISCONNECTED && dev.bondState != BluetoothDevice.BOND_BONDED) {
                            // Its bond state follows a moment later, with the reason.
                            val late = settled(states, 1500)
                            close()
                            if (dev.bondState == BluetoothDevice.BOND_BONDED) return PairResult(Paired.YES, codeByApp = gave.get())
                            val why = if (late != null && late[0] == BluetoothDevice.BOND_NONE && late[1] != 0) unbondText(late[1])
                                else "The Bluetooth connection dropped before pairing finished."
                            return PairResult(Paired.FAILED, why, gave.get())
                        }
                        e = events.poll()
                    }
                }
                // Paired over this very connection: finish setting it up. If
                // that goes wrong the pairing stands all the same, and the next
                // request connects afresh.
                try {
                    setUp(gen, address)
                    lastUsedMs = System.currentTimeMillis()
                    scheduleIdle()
                } catch (e: ApiException) {
                    close()
                }
                return PairResult(Paired.YES, codeByApp = gave.get())
            } finally {
                try {
                    app.unregisterReceiver(watcher)
                } catch (e: IllegalArgumentException) {
                }
            }
        } finally {
            lock.unlock()
        }
    }

    /** The first bond state after [states] stops saying "bonding", within [ms]; null if none came. */
    private fun settled(states: LinkedBlockingQueue<IntArray>, ms: Long): IntArray? {
        val until = System.currentTimeMillis() + ms
        while (true) {
            val left = until - System.currentTimeMillis()
            if (left <= 0) return null
            val s = states.poll(left, TimeUnit.MILLISECONDS) ?: return null
            if (s[0] != BluetoothDevice.BOND_BONDING) return s
        }
    }

    /** Drops Android's own pairing with a board, as when the board has forgotten every phone. */
    fun unpair(address: String) {
        lock.lock()
        try {
            if (ready == address) close()
            val dev = try {
                device(address)
            } catch (e: ApiException) {
                return
            }
            if (dev.bondState != BluetoothDevice.BOND_NONE) removeBond(dev)
        } finally {
            lock.unlock()
        }
    }

    // There is no public call for this; every Android since 4.4 has the hidden
    // one. If it is ever taken away, the person is told how to do it by hand.
    private fun removeBond(dev: BluetoothDevice): Boolean = try {
        dev.javaClass.getMethod("removeBond").invoke(dev) as? Boolean ?: false
    } catch (e: Exception) {
        false
    }

    @Suppress("DEPRECATION")
    private fun bondDevice(intent: Intent): BluetoothDevice? =
        if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        }

    // Bond changes, and Android asking for the code. That second one is an
    // ordered broadcast: heard first, at a high priority, the app can answer
    // it and stop Android's own prompt from appearing.
    private fun registerPairingWatcher(r: BroadcastReceiver) {
        val f = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        f.addAction(BluetoothDevice.ACTION_PAIRING_REQUEST)
        f.priority = IntentFilter.SYSTEM_HIGH_PRIORITY - 1
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(r, f, Context.RECEIVER_EXPORTED)
        } else {
            app.registerReceiver(r, f)
        }
    }

    // --- looking for boards --------------------------------------------------------------

    /** A board heard advertising the link. */
    data class Heard(val address: String, val name: String, val rssi: Int, val pairing: Boolean, val paired: Boolean)

    /**
     * Listens for boards advertising the link, reporting each on the main
     * thread as it is heard (again as its signal or pairing window changes).
     * Returns null when started, or why it could not start.
     */
    class Scan(context: Context, private val heard: (Heard) -> Unit) {
        private val ctx = context.applicationContext
        private val ui = Handler(Looper.getMainLooper())
        private var running = false

        private val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                report(result)
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                for (r in results) report(r)
            }
        }

        private fun report(r: ScanResult) {
            val rec = r.scanRecord
            val ad = LinkCodec.advert(rec?.getManufacturerSpecificData(LinkCodec.MANUFACTURER))
            val name = rec?.deviceName ?: try {
                r.device.name
            } catch (e: SecurityException) {
                null
            } ?: "netmon"
            val paired = try {
                r.device.bondState == BluetoothDevice.BOND_BONDED
            } catch (e: SecurityException) {
                false
            }
            val h = Heard(r.device.address.uppercase(), name, r.rssi, ad?.pairing == true, paired)
            ui.post { if (running) heard(h) }
        }

        fun start(): String? {
            if (missing(ctx, true).isNotEmpty()) return "netmon needs permission to look for Bluetooth devices."
            val a = adapter(ctx) ?: return "This phone has no Bluetooth."
            if (!a.isEnabled) return "Bluetooth is off on this phone."
            val sc = a.bluetoothLeScanner ?: return "Bluetooth is off on this phone."
            val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(UUID.fromString(LinkCodec.SERVICE))).build()
            val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
            return try {
                sc.startScan(listOf(filter), settings, cb)
                running = true
                null
            } catch (e: RuntimeException) {
                "The phone would not start looking: ${e.message}"
            }
        }

        fun stop() {
            if (!running) return
            running = false
            try {
                adapter(ctx)?.bluetoothLeScanner?.stopScan(cb)
            } catch (e: RuntimeException) {
            }
        }
    }
}
