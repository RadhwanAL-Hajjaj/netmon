package com.example.netmon

import android.content.Context
import android.content.SharedPreferences

/** Everything the app keeps between runs, in one SharedPreferences file. */
class Prefs(context: Context) {

    private val sp: SharedPreferences = context.getSharedPreferences("netmon", Context.MODE_PRIVATE)

    var boardUrl: String?
        get() = sp.getString("board_url", null)
        set(v) = sp.edit().putString("board_url", v).apply()

    var updateKey: String?
        get() = sp.getString("update_key", null)
        set(v) = sp.edit().putString("update_key", v).apply()

    var alertMode: AlertMode
        get() = AlertMode.of(sp.getString("alert_mode", null))
        set(v) = sp.edit().putString("alert_mode", v.key).apply()

    var alertMinutes: Int
        get() = sp.getInt("alert_minutes", 30)
        set(v) = sp.edit().putInt("alert_minutes", v).apply()

    var alertPromptDone: Boolean
        get() = sp.getBoolean("alert_prompt_done", false)
        set(v) = sp.edit().putBoolean("alert_prompt_done", v).apply()

    var latencySamples: String?
        get() = sp.getString("latency", null)
        set(v) = sp.edit().putString("latency", v).apply()

    var deviceSort: String
        get() = sp.getString("device_sort", "ip") ?: "ip"
        set(v) = sp.edit().putString("device_sort", v).apply()

    var eventFilter: String
        get() = sp.getString("event_filter", "all") ?: "all"
        set(v) = sp.edit().putString("event_filter", v).apply()

    // Insertion order matters (oldest are dropped first), and a StringSet does
    // not keep it, so the MAC lists are stored as one comma-separated string.
    fun loadAlertMemory(): AlertMemory = AlertMemory(
        baselined = sp.getBoolean("alert_baselined", false),
        seen = splitSet(sp.getString("alert_seen", "")),
        notified = splitSet(sp.getString("alert_notified", "")),
    )

    fun saveAlertMemory(m: AlertMemory) {
        sp.edit()
            .putBoolean("alert_baselined", m.baselined)
            .putString("alert_seen", m.seen.joinToString(","))
            .putString("alert_notified", m.notified.joinToString(","))
            .apply()
    }

    private fun splitSet(s: String?): LinkedHashSet<String> {
        val out = LinkedHashSet<String>()
        if (s.isNullOrEmpty()) return out
        for (p in s.split(',')) if (p.isNotEmpty()) out.add(p)
        return out
    }
}
