package com.example.netmon

enum class AlertMode(val key: String, val title: String, val detail: String) {
    OFF("off", "Off", "No notifications."),
    UNRECOGNISED(
        "unrecognised", "Unrecognised devices",
        "When the monitor flags a device it does not recognise.",
    ),
    EVERY_NEW(
        "every_new", "Every new device",
        "Whenever a device this phone has not seen before joins, including phones with private addresses.",
    );

    companion object {
        fun of(key: String?): AlertMode = values().firstOrNull { it.key == key } ?: OFF
    }
}

/**
 * What the app remembers between checks: every MAC it has seen, and which
 * ones it has already told you about. Insertion-ordered so the oldest can be
 * dropped when the sets get long.
 */
class AlertMemory(
    var baselined: Boolean = false,
    val seen: LinkedHashSet<String> = LinkedHashSet(),
    val notified: LinkedHashSet<String> = LinkedHashSet(),
)

object AlertRules {

    const val MAX_SEEN = 2000
    const val MAX_NOTIFIED = 500

    /**
     * Decides which devices deserve a notification and updates [memory].
     *
     * The first reading only records a baseline: switching alerts on should
     * not produce a burst about devices already on the screen. The board's own
     * device table lives in RAM, so after the board restarts everything it
     * finds is "known" again for the learning window; the memory here is what
     * still recognises a device as new to this phone.
     */
    fun evaluate(mode: AlertMode, devices: List<Device>, memory: AlertMemory): List<Device> {
        val out = ArrayList<Device>()
        if (!memory.baselined) {
            for (d in devices) {
                memory.seen.add(d.mac)
                if (d.isUnknown) memory.notified.add(d.mac)
            }
            memory.baselined = true
            trim(memory)
            return out
        }
        for (d in devices) {
            if (d.mac.isEmpty()) continue
            val firstTime = memory.seen.add(d.mac)
            if (d.self) continue
            val tell = when (mode) {
                AlertMode.OFF -> false
                AlertMode.UNRECOGNISED -> d.isUnknown && d.mac !in memory.notified
                AlertMode.EVERY_NEW -> (firstTime || d.isUnknown) && d.mac !in memory.notified
            }
            if (tell) {
                memory.notified.add(d.mac)
                out.add(d)
            }
        }
        trim(memory)
        return out
    }

    private fun trim(m: AlertMemory) {
        while (m.seen.size > MAX_SEEN) m.seen.remove(m.seen.first())
        while (m.notified.size > MAX_NOTIFIED) m.notified.remove(m.notified.first())
    }
}
