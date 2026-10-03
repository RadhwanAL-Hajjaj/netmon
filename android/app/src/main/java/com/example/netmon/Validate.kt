package com.example.netmon

/**
 * The firmware's own settings checks (validate.h), run on the phone first so a
 * mistake is caught before the board is asked to restart. The board checks
 * again and has the last word; the messages are the board's words.
 */
object Validate {

    fun credentials(ssid: String, pass: String): String? = when {
        ssid.isEmpty() -> "network name is required"
        ssid.toByteArray(Charsets.UTF_8).size > 32 -> "network name is over 32 characters"
        pass.toByteArray(Charsets.UTF_8).size > 64 -> "password is over 64 characters"
        else -> null
    }

    fun maskContiguous(mask: Int): Boolean {
        val inv = mask.inv()
        return (inv and (inv + 1)) == 0
    }

    fun static(ip: String, mask: String, gw: String): String? {
        val a = Subnet.parse(ip) ?: return "IP address is not valid"
        val m = Subnet.parse(mask)
        if (m == null || m == 0 || !maskContiguous(m)) return "subnet mask is not valid"
        val g = Subnet.parse(gw) ?: return "gateway address is not valid"
        if ((a and m) != (g and m)) return "gateway is outside the subnet, the device would be unreachable"
        return null
    }

    /** DNS may be left blank: the board then uses the gateway. */
    fun dns(dns: String): String? =
        if (dns.isBlank() || Subnet.parse(dns) != null) null else "DNS address is not valid"

    fun intervals(scan: Int, probe: Int, offline: Int, learning: Int): String? {
        val ok = scan in 10..3600 && probe in 10..3600 && offline in 30..86400 && learning in 0..86400
        return if (ok) null else "an interval is out of range"
    }
}
