package com.example.netmon

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build

/** New-device notifications and the periodic check that raises them while the app is closed. */
object Alerts {

    const val CHANNEL = "new_devices"
    const val CHANNEL_WATCH = "lan_watch"
    const val EXTRA_TAB = "tab"
    const val EXTRA_MAC = "mac"
    private const val JOB_ID = 4201
    private const val TAG = "device"
    const val PERMISSION = "android.permission.POST_NOTIFICATIONS"

    fun ensureChannel(context: Context) {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL) != null) return
        val ch = NotificationChannel(CHANNEL, "New devices", NotificationManager.IMPORTANCE_HIGH)
        ch.description = "A device joined the network the monitor watches."
        nm.createNotificationChannel(ch)
    }

    fun ensureWatchChannel(context: Context) {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL_WATCH) != null) return
        val ch = NotificationChannel(CHANNEL_WATCH, "LAN watch", NotificationManager.IMPORTANCE_HIGH)
        ch.description = "The router, a DHCP server or an access point changed on the network the monitor watches."
        nm.createNotificationChannel(ch)
    }

    /**
     * The LAN watch's events (firmware 0.14) new to this phone. Only recent ones:
     * the first reading after the app is installed holds whatever the board
     * still remembers, and an alert from hours ago is history, not news.
     */
    fun notifyWatch(context: Context, events: List<LoggedEvent>) {
        if (AppState.prefs.alertMode == AlertMode.OFF || !allowed(context)) return
        val now = System.currentTimeMillis()
        val tell = events.filter { it.type in Format.WATCH && now - it.wallMs < 6 * 3_600_000L }
        if (tell.isEmpty()) return
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        ensureWatchChannel(context)
        for (e in tell) {
            val open = Intent(context, MainActivity::class.java)
                .putExtra(EXTRA_TAB, "events")
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            val pi = PendingIntent.getActivity(
                context, e.key.hashCode(), open,
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            val where = listOf(e.ip.takeIf { it.isNotBlank() && it != "0.0.0.0" }, e.mac.ifBlank { null })
                .filterNotNull().joinToString("   ")
            val n = Notification.Builder(context, CHANNEL_WATCH)
                .setSmallIcon(R.drawable.ic_stat_netmon)
                .setColor(0xFFF0605F.toInt())
                .setContentTitle(Format.eventTitle(e.type))
                .setContentText(e.text)
                .setStyle(Notification.BigTextStyle().bigText(listOf(e.text, where).filter { it.isNotBlank() }.joinToString("\n")))
                .setContentIntent(pi)
                .setAutoCancel(true)
                .setCategory(Notification.CATEGORY_STATUS)
                .setWhen(e.wallMs)
                .setShowWhen(true)
                .build()
            try {
                nm.notify(WATCH_TAG, e.key.hashCode(), n)
            } catch (x: SecurityException) {
                return
            }
        }
    }

    private const val WATCH_TAG = "watch"

    /** Whether Android will actually show them: the runtime permission, and the app-level switch. */
    fun allowed(context: Context): Boolean {
        if (Build.VERSION.SDK_INT >= 33 &&
            context.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED
        ) return false
        val nm = context.getSystemService(NotificationManager::class.java) ?: return false
        return nm.areNotificationsEnabled()
    }

    fun needsRuntimePermission(context: Context): Boolean =
        Build.VERSION.SDK_INT >= 33 && context.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED

    fun notifyDevices(context: Context, devices: List<Device>) {
        if (devices.isEmpty() || !allowed(context)) return
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        ensureChannel(context)
        for (d in devices) {
            val name = Format.deviceName(d)
            val title = if (d.isUnknown) "Unrecognised device on your network" else "New device on your network"
            val open = Intent(context, MainActivity::class.java)
                .putExtra(EXTRA_TAB, "devices")
                .putExtra(EXTRA_MAC, d.mac)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            val pi = PendingIntent.getActivity(
                context, d.mac.hashCode(), open,
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            val detail = buildString {
                append(name).append(" at ").append(d.ip.ifEmpty { "an unknown address" })
                append('\n').append(Format.vendorLine(d))
                append('\n').append(d.mac)
            }
            val n = Notification.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_stat_netmon)
                .setColor(0xFFFFB020.toInt())
                .setContentTitle(title)
                .setContentText("$name at ${d.ip}")
                .setStyle(Notification.BigTextStyle().bigText(detail))
                .setContentIntent(pi)
                .setAutoCancel(true)
                .setCategory(Notification.CATEGORY_STATUS)
                .setWhen(System.currentTimeMillis())
                .setShowWhen(true)
                .build()
            try {
                nm.notify(TAG, d.mac.hashCode(), n)
            } catch (e: SecurityException) {
                return
            }
        }
    }

    /** Starts, changes or stops the periodic check to match the settings. */
    fun schedule(context: Context) {
        val js = context.getSystemService(JobScheduler::class.java) ?: return
        val p = AppState.prefs
        if (p.alertMode == AlertMode.OFF || p.boardUrl == null) {
            js.cancel(JOB_ID)
            return
        }
        val minutes = p.alertMinutes.coerceIn(15, 180)
        val job = JobInfo.Builder(JOB_ID, ComponentName(context, AlertJobService::class.java))
            .setPeriodic(minutes * 60_000L, 5 * 60_000L)
            .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
            .setPersisted(true)
            .build()
        val existing = js.getPendingJob(JOB_ID)
        if (existing != null && existing.intervalMillis == job.intervalMillis) return
        js.schedule(job)
    }
}
