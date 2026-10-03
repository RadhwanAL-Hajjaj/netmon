package com.example.netmon

import android.app.job.JobParameters
import android.app.job.JobService
import android.content.Context

/**
 * The periodic check behind new-device notifications. It only works while the
 * phone can reach the board, which normally means being on the same Wi-Fi; a
 * check that cannot reach it ends quietly and waits for the next one.
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
        val base = AppState.prefs.boardUrl ?: return
        if (AppState.prefs.alertMode == AlertMode.OFF) return
        NetRoute.pinIfNeeded(context)
        val c = NetmonClient(base)
        c.connectTimeoutMs = 4000
        c.readTimeoutMs = 7000
        val now = System.currentTimeMillis()
        val h = try {
            c.health()
        } catch (e: ApiException) {
            return
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
}
