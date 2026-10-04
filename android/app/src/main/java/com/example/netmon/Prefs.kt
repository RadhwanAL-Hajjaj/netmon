package com.example.netmon

import android.content.Context
import android.content.SharedPreferences

/** Everything the app keeps between runs, in one SharedPreferences file. */
class Prefs(context: Context) {

    private val sp: SharedPreferences = context.getSharedPreferences("netmon", Context.MODE_PRIVATE)

    /** The board's address on Wi-Fi: "http://192.168.2.27". */
    var boardUrl: String?
        get() = sp.getString("board_url", null)
        set(v) = sp.edit().putString("board_url", v).apply()

    // --- The Bluetooth link (app 1.2) --------------------------------------------

    /** The same board over Bluetooth, once this phone is paired with it: "ble://D4:E9:F4:A3:B8:AE". */
    var boardBle: String?
        get() = sp.getString("board_ble", null)
        set(v) = sp.edit().putString("board_ble", v).apply()

    /** The board's Wi-Fi MAC, which tells it apart however it is reached. */
    var boardMac: String?
        get() = sp.getString("board_mac", null)
        set(v) = sp.edit().putString("board_mac", v).apply()

    /** The route that answered last, to start on next time. */
    var lastRoute: String?
        get() = sp.getString("last_route", null)
        set(v) = sp.edit().putString("last_route", v).apply()

    var linkMode: LinkMode
        get() = LinkMode.of(sp.getString("link_mode", null))
        set(v) = sp.edit().putString("link_mode", v.key).apply()

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

    // --- Nearby, the Finder and the map --------------------------------------

    /** wifi, bluetooth or finder. */
    var nearbyTab: String
        get() = sp.getString("nearby_tab", "wifi") ?: "wifi"
        set(v) = sp.edit().putString("nearby_tab", v).apply()

    /** A radar's scale in metres, 0 for plain signal strength. */
    fun radarScale(ble: Boolean): Int =
        sp.getInt(if (ble) "radar_ble" else "radar_wifi", if (ble) 20 else 30).let { if (it in Air.SCALES) it else if (ble) 20 else 30 }

    fun setRadarScale(ble: Boolean, m: Int) = sp.edit().putInt(if (ble) "radar_ble" else "radar_wifi", m).apply()

    /** Bluetooth colour groups switched off in the legend. */
    var hiddenGroups: Set<String>
        get() = (sp.getString("hidden_groups", "") ?: "").split(',').filter { it in Air.GROUPS }.toSet()
        set(v) = sp.edit().putString("hidden_groups", v.joinToString(",")).apply()

    var hidePrivate: Boolean
        get() = sp.getBoolean("hide_private", false)
        set(v) = sp.edit().putBoolean("hide_private", v).apply()

    var masked: Boolean
        get() = sp.getBoolean("masked", false)
        set(v) = sp.edit().putBoolean("masked", v).apply()

    var calibration: Calibration
        get() {
            val d = Calibration()
            fun f(k: String, def: Double, ok: (Double) -> Boolean): Double {
                val v = sp.getFloat(k, Float.NaN).toDouble()
                return if (!v.isNaN() && ok(v)) v else def
            }
            return Calibration(
                f("cal_wp", d.wifiAt1m) { Calibration.atOneMetreOk(it) },
                f("cal_wn", d.wifiFalloff) { Calibration.falloffOk(it) },
                f("cal_bp", d.bleAt1m) { Calibration.atOneMetreOk(it) },
                f("cal_bn", d.bleFalloff) { Calibration.falloffOk(it) },
            )
        }
        set(c) = sp.edit()
            .putFloat("cal_wp", c.wifiAt1m.toFloat()).putFloat("cal_wn", c.wifiFalloff.toFloat())
            .putFloat("cal_bp", c.bleAt1m.toFloat()).putFloat("cal_bn", c.bleFalloff.toFloat())
            .apply()

    fun resetCalibration() = sp.edit().remove("cal_wp").remove("cal_wn").remove("cal_bp").remove("cal_bn").apply()

    /** How a Nearby list is sorted: rssi, name, ch, kind or age. */
    fun nearbySort(list: String, def: String): String = sp.getString("sort_$list", def) ?: def

    fun setNearbySort(list: String, key: String) = sp.edit().putString("sort_$list", key).apply()

    /** The device the Finder was last asked for, "ble|AA:BB:..." or "wifi|...", kept so it can carry on. */
    var finderTarget: String?
        get() = sp.getString("finder_target", null)
        set(v) = sp.edit().putString("finder_target", v).apply()

    var finderSound: Boolean
        get() = sp.getBoolean("finder_sound", false)
        set(v) = sp.edit().putBoolean("finder_sound", v).apply()

    /** The Finder's picker: t trackers, b Bluetooth, w Wi-Fi. */
    var finderPick: String
        get() = sp.getString("finder_pick", "") ?: ""
        set(v) = sp.edit().putString("finder_pick", v).apply()

    /** list or map. */
    var devicesView: String
        get() = sp.getString("devices_view", "list") ?: "list"
        set(v) = sp.edit().putString("devices_view", v).apply()

    var mapByStatus: Boolean
        get() = sp.getBoolean("map_by_status", false)
        set(v) = sp.edit().putBoolean("map_by_status", v).apply()

    var mapOffline: Boolean
        get() = sp.getBoolean("map_offline", false)
        set(v) = sp.edit().putBoolean("map_offline", v).apply()

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
