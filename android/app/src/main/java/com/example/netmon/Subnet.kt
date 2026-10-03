package com.example.netmon

import org.json.JSONException
import org.json.JSONObject

/** IPv4 arithmetic for the "find my monitor" sweep. */
object Subnet {

    fun parse(ip: String): Int? {
        val p = ip.trim().split('.')
        if (p.size != 4) return null
        var v = 0
        for (part in p) {
            if (part.isEmpty() || part.length > 3 || !part.all { it in '0'..'9' }) return null
            val n = part.toInt()
            if (n > 255) return null
            v = (v shl 8) or n
        }
        return v
    }

    fun format(ip: Int): String =
        "${(ip ushr 24) and 255}.${(ip ushr 16) and 255}.${(ip ushr 8) and 255}.${ip and 255}"

    /**
     * Addresses worth probing on the phone's own network, nearest block first.
     * Anything wider than a /24 is cut down to the /24 around the phone, then
     * widened a block at a time up to [limit] hosts: a /16 is 65,534 hosts and
     * the monitor is almost always in the phone's own /24.
     */
    fun hosts(phoneIp: Int, prefix: Int, limit: Int = 1024): List<String> {
        val pfx = prefix.coerceIn(8, 30)
        val mask = if (pfx == 0) 0 else (-1 shl (32 - pfx))
        val net = phoneIp and mask
        val bcast = net or mask.inv()
        val out = ArrayList<String>()
        if (pfx >= 24) {
            var a = net + 1
            while (a < bcast && out.size < limit) {
                if (a != phoneIp) out.add(format(a))
                a++
            }
            return out
        }
        // Wider than /24: the phone's own /24 first, then the neighbouring blocks.
        val home = phoneIp and -256
        val blocks = ArrayList<Int>()
        blocks.add(home)
        var step = 1
        while (blocks.size * 254 < limit) {
            val up = home + step * 256
            val down = home - step * 256
            var added = false
            if (up and mask == net && up <= bcast) { blocks.add(up); added = true }
            if (down and mask == net && down >= net) { blocks.add(down); added = true }
            if (!added) break
            step++
        }
        for (b in blocks) {
            // Inside a subnet wider than /24, x.x.x.0 and x.x.x.255 are
            // ordinary hosts; only the subnet's own ends are not.
            for (last in 0..255) {
                val a = b + last
                if (a == phoneIp || a == net || a == bcast) continue
                out.add(format(a))
                if (out.size >= limit) return out
            }
        }
        return out
    }

    /** Whether a /api/health reply came from a netmon board rather than some other web server. */
    fun looksLikeNetmon(text: String): Boolean = try {
        val o = JSONObject(text)
        o.has("version") && o.has("scan_passes") && o.has("sweepable") && o.optString("status") == "ok"
    } catch (e: JSONException) {
        false
    }
}
