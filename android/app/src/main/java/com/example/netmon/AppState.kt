package com.example.netmon

import android.content.Context
import java.io.File

/**
 * State shared by the screens and the background check, which can run at the
 * same time in the same process. Every reading of the board goes through the
 * absorb functions, under one lock, so the history, the latency record and
 * the alert memory never see two updates interleave.
 */
object AppState {

    private val lock = Any()
    @Volatile private var ready = false

    lateinit var prefs: Prefs
        private set
    lateinit var history: EventHistory
        private set
    val latency = LatencyLog()
    private lateinit var memory: AlertMemory
    private var latencyDirtySince = 0L

    fun init(context: Context) {
        if (ready) return
        synchronized(lock) {
            if (ready) return
            val app = context.applicationContext
            prefs = Prefs(app)
            history = EventHistory(File(app.filesDir, "events.json"))
            history.load()
            latency.decode(prefs.latencySamples)
            memory = prefs.loadAlertMemory()
            ready = true
        }
    }

    /** Records the gateway probe carried in a health reading. */
    fun absorbHealth(nowMs: Long, h: Health) {
        synchronized(lock) {
            if (!h.inSetupMode && latency.record(nowMs, h.latencyValid, h.latencyMs, h.latencyAgeS)) {
                // Written at most once a minute; a lost sample or two costs nothing.
                if (latencyDirtySince == 0L) latencyDirtySince = nowMs
                if (nowMs - latencyDirtySince > 60_000) flushLatency()
            }
        }
    }

    fun flushLatency() {
        synchronized(lock) {
            prefs.latencySamples = latency.encode()
            latencyDirtySince = 0L
        }
    }

    /** Learns names and decides alerts. Returns the devices worth a notification. */
    fun absorbDevices(devices: List<Device>): List<Device> {
        synchronized(lock) {
            if (history.rememberNames(devices)) history.save()
            val tell = AlertRules.evaluate(prefs.alertMode, devices, memory)
            prefs.saveAlertMemory(memory)
            return tell
        }
    }

    /** Folds a reading of the board's event list into the on-phone history. */
    fun absorbEvents(nowMs: Long, uptimeS: Long, events: List<BoardEvent>): Int {
        synchronized(lock) {
            val added = history.merge(nowMs, uptimeS, events)
            if (added > 0) history.save()
            return added
        }
    }

    fun historySnapshot(): List<LoggedEvent> = synchronized(lock) { history.all() }

    fun nameFor(mac: String): String? = synchronized(lock) { history.nameFor(mac) }

    fun clearHistory() {
        synchronized(lock) {
            history.clear()
            history.save()
        }
    }

    fun latencySnapshot(): List<LatencySample> = synchronized(lock) { latency.all() }

    fun latencyStats(): LatencyLog.Stats? = synchronized(lock) { latency.stats() }

    /** A different board means a different network: forget what the last one taught. */
    fun forgetBoardData() {
        synchronized(lock) {
            latency.clear()
            prefs.latencySamples = ""
            memory = AlertMemory()
            prefs.saveAlertMemory(memory)
        }
    }
}
