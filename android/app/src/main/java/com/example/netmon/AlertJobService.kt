package com.example.netmon

import android.app.job.JobParameters
import android.app.job.JobService
import android.content.Context

/**
 * The periodic check behind new-device notifications. It only works while the
 * phone can reach the board: on the same Wi-Fi, or over Bluetooth once paired
 * and within range. A check that cannot reach it ends quietly and waits for
 * the next one.
 */
class AlertJobService : JobService() {

    @Volatile private var worker: Thread? = null

    override fun onStartJob(params: JobParameters): Boolean {
        val app = applicationContext
        val t = Thread({
            try {
                BackgroundCheck.run(app)
            } catch (e: Throwable) {
                // A failed check is retried at the next interval.
            } finally {
                jobFinished(params, false)
            }
        }, "netmon-check")
        worker = t
        t.start()
        return true
    }

    override fun onStopJob(params: JobParameters): Boolean {
        worker?.interrupt()
        return false
    }
}

object BackgroundCheck {

    fun run(context: Context) {
        AppState.init(context)
        BleLink.init(context)
        val p = AppState.prefs
        if (p.alertMode == AlertMode.OFF) return
        val first = RoutePlan.initial(p.linkMode, p.boardUrl, p.boardBle, p.lastRoute) ?: return
        NetRoute.pinIfNeeded(context)
        val now = System.currentTimeMillis()
        // The route the app used last, then the other one, as the app itself would.
        var c = client(first)
        var h = health(c)
        if (h == null) {
            val alt = RoutePlan.fallback(p.linkMode, first, p.boardUrl, p.boardBle) ?: return
            if (LinkCodec.isBle(alt) && (!BleLink.switchedOn(context) || !BleLink.bonded(context, LinkCodec.address(alt)))) return
            c = client(alt)
            h = health(c) ?: return
        }
        AppState.absorbHealth(now, h)
        if (h.inSetupMode) return
        val devices = try {
            c.devices()
        } catch (e: ApiException) {
            return
        }
        val tell = AppState.absorbDevices(devices)
        try {
            AppState.absorbEvents(now, h.uptimeS, c.events())
        } catch (e: ApiException) {
            // The history can wait for the next check.
        }
        AppState.flushLatency()
        Alerts.notifyDevices(context, tell)
    }

    private fun client(base: String) = NetmonClient(base).apply {
        connectTimeoutMs = 4000
        readTimeoutMs = 7000
    }

    private fun health(c: NetmonClient): Health? = try {
        c.health()
    } catch (e: ApiException) {
        null
    }
}
