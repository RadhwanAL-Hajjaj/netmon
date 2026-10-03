package com.example.netmon

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * The network map's grouping and layout, the same as the board's own Map page
 * (pages.h, MAP_HTML): the router in the middle, a bubble per group of
 * devices on a ring around it, the way out to the internet straight up. The
 * ring grows until nothing overlaps; on a narrow screen it is squeezed sideways
 * and stretched down, so text stays readable rather than everything shrinking
 * to fit. Units are the web page's pixels, which the view scales to the screen.
 * No Android types, so it runs in plain JVM tests.
 */
object NetMap {

    const val DOT = 6.5
    const val GAP = 26.0
    const val HUB = 21.0
    const val IW = 134.0
    const val IH = 38.0
    /** Width per character of a group title, for laying out, as on the web page. */
    const val CHAR_W = 6.6

    class Kind(val key: String, val title: String, val name: Regex, val maker: Regex?)

    // What a device probably is: its name first, which says the most, then its
    // maker, which is often ambiguous and so only used where it is not.
    val KINDS = listOf(
        Kind("net", "Network gear",
            Regex("\\b(router|gateway|modem|switch|access ?point|extender|repeater|mesh|deco|eero|orbi|unifi|ubnt|mikrotik|openwrt|fritz|velop|powerline|devolo)\\b"),
            Regex("routerboard|ubiquiti|mikrotik|netgear|tp-link|zyxel|avm|eero|plume|cisco|aruba|ruckus|draytek|sagemcom|technicolor|arris|linksys|devolo|schneider|apc by")),
        Kind("nas", "Servers and storage",
            Regex("(\\bnas\\b|server|synology|diskstation|qnap|truenas|unraid|proxmox|esxi|docker|homelab|pihole|pi-hole|home-?assistant)"),
            Regex("synology|qnap|vmware|pcs systemtechnik|western digital|seagate")),
        Kind("pc", "Computers",
            Regex("(macbook|imac|mac-?mini|mac-?pro|mac-?studio|desktop|laptop|notebook|\\bpc\\b|thinkpad|latitude|\\bxps\\b|surface|ubuntu|debian|fedora|linux|raspberry|raspberrypi|\\bpi\\d?\\b|\\bnuc\\b|workstation|chromebook)"),
            Regex("raspberry|intel corp|dell|lenovo|micro-star|gigabyte|microsoft")),
        Kind("phone", "Phones and tablets",
            Regex("(iphone|ipad|android|galaxy|pixel|oneplus|xiaomi|redmi|poco|oppo|vivo|realme|honor|motorola|\\bmoto\\b|nokia|phone|tablet|\\bsm-[a-z]\\d)"),
            null),
        Kind("media", "TV, media and games",
            Regex("(\\btv\\b|-tv|tv-|roku|chromecast|fire ?tv|firestick|apple ?tv|appletv|shield|sonos|bose|denon|marantz|yamaha|webos|bravia|vizio|hisense|\\btcl\\b|kodi|plex|xbox|playstation|\\bps[345]\\b|nintendo|steam ?deck|homepod)"),
            Regex("sonos|roku|bose|denon|vizio|hisense|nintendo|sony interactive|nvidia|valve")),
        Kind("print", "Printers",
            Regex("(printer|\\bprint|epson|canon|brother|laserjet|officejet|deskjet|kyocera|xerox|lexmark|ricoh)"),
            Regex("seiko epson|canon|brother|kyocera|xerox|lexmark|ricoh")),
        Kind("cam", "Cameras",
            Regex("(camera|\\bcam\\b|\\bipc\\b|doorbell|dahua|hikvision|ezviz|reolink|imou|wyze ?cam|eufy ?cam)"),
            Regex("dahua|hangzhou huacheng|hikvision|ezviz|reolink|axis comm")),
        Kind("iot", "Smart home",
            Regex("(\\besp[-_]?|esp32|esp8266|tuya|shelly|tasmota|sonoff|wled|\\bhue\\b|nest|\\bring\\b|arlo|tapo|kasa|meross|govee|ecobee|tado|netatmo|roborock|ecovacs|roomba|dreame|plug|bulb|light|sensor|thermostat|\\becho\\b|alexa|google-?home|broadlink|yeelight|aqara|lifx|nanoleaf|switchbot|zigbee|matter)"),
            Regex("espressif|tuya|allterco|shelly|itead|signify|philips lighting|nest labs|ecobee|tado|netatmo|roborock|ecovacs|irobot|broadlink|yeelight|lumi united|lifx|nanoleaf|wiz connected|texas instruments|usr iot")),
    )

    private val EXTRA = mapOf("priv" to "Private addresses", "other" to "Not identified")
    val STATUS = listOf("unknown" to "Not recognised", "private" to "Private addresses", "known" to "Recognised", "off" to "Offline")

    fun kindOf(d: Device): String {
        val n = d.hostname.lowercase()
        val v = d.vendor.lowercase()
        if (n.isNotEmpty()) for (k in KINDS) if (k.name.containsMatchIn(n)) return k.key
        if (v.isNotEmpty()) for (k in KINDS) if (k.maker != null && k.maker.containsMatchIn(v)) return k.key
        return if (d.randomised) "priv" else "other"
    }

    fun title(k: String): String {
        for (x in KINDS) if (x.key == k) return x.title
        for ((key, t) in STATUS) if (key == k) return t
        return EXTRA[k] ?: k
    }

    /** known, private, unknown, or off for any device that is offline. */
    fun cls(d: Device): String = if (d.online) d.status else "off"

    class Item(val device: Device?, val ap: MapAp?, val me: Boolean) {
        var x = 0.0
        var y = 0.0
    }

    class Group(val key: String, val title: String, val wifi: Boolean, val items: List<Item>) {
        val aps: List<MapAp> get() = items.mapNotNull { it.ap }
        var lines: List<String> = emptyList()
        var lw = 0.0          // label width
        var lh = 0.0          // label height
        var at: List<DoubleArray> = emptyList()   // each item's place, from the bubble's centre
        var r = 0.0
        var a = 0.0
        var x = 0.0
        var y = 0.0
    }

    /**
     * A finished map. box: left, top, width, height of everything, in map
     * units; iy: the internet box's centre height.
     */
    class Plan(val groups: List<Group>, val ringR: Double, val iy: Double, val box: DoubleArray, val scale: Double) {
        val items: List<Item> get() = groups.flatMap { it.items }
    }

    /** The board itself, from the device list when it is there. */
    fun meOf(info: MapInfo, devices: List<Device>): Device =
        devices.firstOrNull { it.self } ?: Device(info.mac, info.ip, info.hostname, "", "known", false, true, true, 0, info.uptimeS)

    fun groups(info: MapInfo, devices: List<Device>, byStatus: Boolean, showOffline: Boolean, narrow: Boolean): List<Group> {
        val g = HashMap<String, ArrayList<Device>>()
        for (d in devices) {
            if (d.self || d.ip == info.gateway) continue
            if (!d.online && !showOffline) continue
            val k = if (byStatus) cls(d) else kindOf(d)
            g.getOrPut(k) { ArrayList() }.add(d)
        }
        val order = if (byStatus) STATUS.map { it.first } else KINDS.map { it.key } + listOf("priv", "other")
        val out = ArrayList<Group>()
        // Your Wi-Fi first, up and to the right: its access points and this board.
        if (info.wifi == "connected") {
            val aps = info.aps.sortedWith(Comparator { a, b ->
                if (a.joined != b.joined) (if (a.joined) -1 else 1) else b.rssi - a.rssi
            })
            val items = ArrayList<Item>()
            for (a in aps) items.add(Item(null, a, false))
            items.add(Item(meOf(info, devices), null, true))
            out.add(Group("wifi", if (info.ssid.isNotEmpty()) "Wi-Fi “${info.ssid}”" else "Wi-Fi", true, items))
        }
        for (k in order) {
            val list = g[k] ?: continue
            val sorted = list.sortedWith(Comparator { a, b ->
                if (a.online != b.online) (if (a.online) -1 else 1)
                else Format.ipKey(a.ip).compareTo(Format.ipKey(b.ip))
            })
            out.add(Group(k, title(k), false, sorted.map { Item(it, null, false) }))
        }
        // On a narrow screen long names go on two lines, so the bubbles at the
        // edges do not need the width of a whole name each.
        val maxc = if (narrow) 12 else 34
        for (c in out) {
            val n = c.items.size
            c.lines = wrap(c.title, maxc)
            c.lw = max(54.0, (c.lines.maxOfOrNull { it.length } ?: 0) * CHAR_W)
            c.lh = c.lines.size * 13.0 + 16
            if (!c.wifi) {
                c.at = packed(n, 9.6)
                c.r = max(24.0, 9.6 * sqrt(n.toDouble()) + 12)
                continue
            }
            // This board in the middle, its network's access points round it.
            val k = c.aps.size
            val rho = if (k < 2) 0.0 else max(30.0, 13 / sin(PI / k))
            val at = ArrayList<DoubleArray>()
            for (i in 0 until k) {
                if (k == 1) {
                    at.add(doubleArrayOf(-15.0, 0.0))
                } else {
                    val t = -PI / 2 + i * 2 * PI / k
                    at.add(doubleArrayOf(rho * cos(t), rho * sin(t)))
                }
            }
            at.add(if (k == 1) doubleArrayOf(15.0, 0.0) else doubleArrayOf(0.0, 0.0))
            c.at = at
            c.r = if (k == 1) 36.0 else if (k == 0) 22.0 else rho + 18
        }
        return out
    }

    /** n dots packed in a sunflower, spacing c. */
    fun packed(n: Int, c: Double): List<DoubleArray> {
        val out = ArrayList<DoubleArray>()
        for (k in 0 until n) {
            if (n == 1) {
                out.add(doubleArrayOf(0.0, 0.0))
                break
            }
            val r = c * sqrt(k + .5)
            val a = k * 2.39996
            out.add(doubleArrayOf(r * cos(a), r * sin(a)))
        }
        return out
    }

    fun clip(s: String, n: Int): String = if (s.length > n) s.substring(0, n - 1) + "…" else s

    fun wrap(s: String, n: Int): List<String> {
        if (s.length <= n) return listOf(s)
        var out = ArrayList<String>()
        var line = ""
        for (x in s.split(' ')) {
            if (line.isNotEmpty() && "$line $x".length > n) {
                out.add(line)
                line = x
            } else {
                line = if (line.isNotEmpty()) "$line $x" else x
            }
        }
        if (line.isNotEmpty()) out.add(line)
        if (out.size > 2) out = arrayListOf(out[0], clip(out.subList(1, out.size).joinToString(" "), n))
        return out.map { clip(it, n + 4) }
    }

    private class Box(val x0: Double, val x1: Double, val y0: Double, val y1: Double)

    private fun boxes(c: Group) = Box(c.x - c.lw / 2, c.x + c.lw / 2, c.y + c.r + 2, c.y + c.r + 2 + c.lh)

    private fun hitRect(a: Box, b: Box) = a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1

    private fun hitCircle(c: Group, b: Box): Boolean {
        val x = max(b.x0, min(c.x, b.x1))
        val y = max(b.y0, min(c.y, b.y1))
        return hypot(c.x - x, c.y - y) < c.r + 4
    }

    private class Layout(val ringR: Double, val iy: Double, val box: DoubleArray)

    private fun layout(cs: List<Group>, ex: Double, ey: Double): Layout {
        val n = cs.size
        val tau = 2 * PI
        var total = 0.0
        for (c in cs) total += 2 * c.r + GAP
        var r = max(HUB + 70, total / tau * 1.05)
        for (tries in 0 until 90) {
            // Leave room straight up for the line out to the internet.
            val keep = asin(min(.95, (IW / 2 + 12) / r))
            val span = tau - 2 * keep
            var a = -PI / 2 + keep
            var sum = 0.0
            for (c in cs) sum += 2 * c.r + GAP
            if (sum == 0.0) sum = 1.0
            for (c in cs) {
                val share = span * (2 * c.r + GAP) / sum
                c.a = a + share / 2
                a += share
                c.x = r * ex * cos(c.a)
                c.y = r * ey * sin(c.a)
            }
            var ok = true
            var i = 0
            while (i < n && ok) {
                val c = cs[i]
                val bc = boxes(c)
                if (hypot(c.x, c.y) < HUB + c.r + 36) ok = false
                if (abs(c.x) < HUB + c.lw / 2 && bc.y0 < HUB + 34 && bc.y1 > -HUB) ok = false
                if (c.y < 0 && abs(c.x) < c.r + 8) ok = false
                if (bc.y0 < 0 && abs(c.x) < c.lw / 2 + 4) ok = false
                var j = 0
                while (j < n && ok) {
                    if (j != i) {
                        val e = cs[j]
                        val be = boxes(e)
                        if (j > i && hypot(c.x - e.x, c.y - e.y) < c.r + e.r + 10) ok = false
                        if (hitCircle(e, bc) || (j > i && hitRect(bc, be))) ok = false
                    }
                    j++
                }
                i++
            }
            if (ok) break
            r *= 1.06
        }
        var top = -HUB
        for (c in cs) top = min(top, c.y - c.r)
        val iy = min(top - IH / 2 - 16, -HUB - 64)
        var x0 = -IW / 2
        var x1 = IW / 2
        var y0 = iy - IH / 2
        var y1 = HUB + 34
        for (c in cs) {
            val h = max(c.r, c.lw / 2)
            x0 = min(x0, c.x - h)
            x1 = max(x1, c.x + h)
            y0 = min(y0, c.y - c.r)
            y1 = max(y1, c.y + c.r + c.lh + 4)
        }
        return Layout(r, iy, doubleArrayOf(x0 - 12, y0 - 12, x1 - x0 + 24, y1 - y0 + 24))
    }

    /**
     * Lays the map out for a view [width] units wide, trying narrower and
     * taller shapes on a narrow screen and keeping the one that can be drawn
     * largest. Null when the board is not on a network yet.
     */
    fun plan(info: MapInfo, devices: List<Device>, byStatus: Boolean, showOffline: Boolean, width: Double): Plan? {
        if (info.wifi != "connected") return null
        val narrow = width < 560
        val tries = if (narrow) listOf(1.0 to 1.0, .86 to 1.18, .74 to 1.36, .62 to 1.6, .5 to 1.9, .42 to 2.2)
        else listOf(1.0 to 1.0, 1.15 to .9)
        var best: Plan? = null
        for ((ex, ey) in tries) {
            val cs = groups(info, devices, byStatus, showOffline, narrow)
            val l = layout(cs, ex, ey)
            val scale = width / l.box[2]
            val b = best
            if (b == null || scale > b.scale + .04) best = Plan(cs, l.ringR, l.iy, l.box, scale)
            if (scale >= .9) break
        }
        val p = best ?: return null
        // Never drawn larger than 1.25 times: wider screens get more room round it.
        val box = p.box
        if (width / box[2] > 1.25) {
            val extra = width / 1.25 - box[2]
            box[0] -= extra / 2
            box[2] += extra
        }
        for (c in p.groups) {
            c.items.forEachIndexed { k, item ->
                item.x = c.x + c.at[k][0]
                item.y = c.y + c.at[k][1]
            }
        }
        return Plan(p.groups, p.ringR, p.iy, box, width / box[2])
    }

    /** What a tap at map point (x, y) lands on: a dot within [reach], the router, the internet, or a group. */
    sealed class Hit {
        class Dot(val item: Item) : Hit()
        object Router : Hit()
        object Internet : Hit()
        class InGroup(val group: Group) : Hit()
    }

    fun hit(p: Plan, x: Double, y: Double, reach: Double): Hit? {
        var best: Item? = null
        var bd = reach * reach
        for (c in p.groups) for (item in c.items) {
            val dx = item.x - x
            val dy = item.y - y
            val d = dx * dx + dy * dy
            if (d < bd) { bd = d; best = item }
        }
        if (best != null) return Hit.Dot(best)
        if (x * x + y * y < (HUB + 6) * (HUB + 6)) return Hit.Router
        if (abs(x) < IW / 2 && abs(y - p.iy) < IH / 2) return Hit.Internet
        for (c in p.groups) {
            if (hypot(c.x - x, c.y - y) < c.r + 4 ||
                (abs(c.x - x) < c.lw / 2 && y > c.y + c.r && y < c.y + c.r + c.lh)) return Hit.InGroup(c)
        }
        return null
    }

    /** A map frame put together from /api/health, for firmware before 0.11: no access points, no provider. */
    fun fromHealth(h: Health, devices: List<Device>?): MapInfo {
        val me = devices?.firstOrNull { it.self }
        return MapInfo(
            version = h.version, wifi = h.wifi, ssid = h.ssid, ip = h.ip, mac = me?.mac ?: "",
            hostname = me?.hostname?.ifEmpty { "netmon" } ?: "netmon", gateway = h.gateway, subnet = h.subnet,
            rssi = h.rssi, channel = 0, bssid = "", uptimeS = h.uptimeS, latencyValid = h.latencyValid,
            latencyMs = h.latencyMs, isp = MapIsp(false, false, "", "", 0), nearbyWifi = false, aps = emptyList(),
            fromBoard = false,
        )
    }
}
