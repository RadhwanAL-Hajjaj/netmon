// Renders the app's painters (radars, the Finder, the trace, the heat bar and the
// network map) to PNG with sample data, for looking at off the phone. See
// tools/test/shots.sh.
import android.graphics.Canvas
import com.example.netmon.*
import com.example.netmon.ui.*
import java.awt.image.BufferedImage
import java.io.File
import javax.imageio.ImageIO
import org.json.JSONObject

const val D = 2.625f   // a 420 dpi phone

fun shot(name: String, w: Int, h: Int, bg: Int = T.BG, draw: (Canvas) -> Unit) {
    val img = BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB)
    val g = img.createGraphics()
    val c = Canvas(g, w, h)
    c.drawColor(bg)
    draw(c)
    g.dispose()
    val out = File(System.getenv("SHOTS") ?: "shots", "$name.png")
    out.parentFile.mkdirs()
    ImageIO.write(img, "png", out)
    println("wrote $out")
}

fun main() {
    // The firmware page tests' simulated board: 27 devices, two access points.
    val input = JSONObject(File(System.getenv("LAN")).readText())
    val info = Parse.map(input.getJSONObject("map").toString())
    val devs = Parse.devices(input.getJSONArray("devices").toString())
    val cal = Calibration()
    val now = 1_800_000_000_000L

    // --- Wi-Fi radar
    val aps = listOf(
        Triple("50:91:E3:12:34:56", "HOME-2.4", -38), Triple("52:91:E3:12:34:57", "HOME-2.4", -71),
        Triple("AC:84:C6:AA:00:01", "Corner Cafe", -62), Triple("AC:84:C6:AA:00:02", "", -66),
        Triple("C8:3A:35:10:20:30", "Tenda_5A2B", -74), Triple("E4:6F:13:00:11:22", "D-Link guest", -81),
        Triple("00:1D:7E:33:44:55", "Linksys01234", -84), Triple("F0:9F:C2:66:77:88", "UBNT-Office", -77),
        Triple("9C:53:22:33:44:01", "Galaxy A54 hotspot", -58))
    val wdots = aps.map { (k, s, r) ->
        val m = Air.metres(r, false, cal)
        RadarDot(k, Air.bearing(k), Math.min(m / 30, 1.0), m > 30, true, k == "50:91:E3:12:34:56", s == "D-Link guest",
            Pal.WIFI, 1.0, r, if (s.isEmpty()) "hidden network" else s, "$r dBm" + if (r == -58) " ▲" else "")
    }
    val size = (360 * D).toInt()
    shot("radar-wifi", size, size) { c ->
        RadarPainter(D).draw(c, size.toFloat(), wdots, listOf(RadarGhost(2.2, 0.55, Pal.WIFI, now - 1500)),
            mapOf("9C:53:22:33:44:01" to now - 700), 30, "AC:84:C6:AA:00:01", now, 0.9)
    }
    // --- Bluetooth radar, signal scale
    val ble = listOf(
        listOf("5D:21:8A:00:11:22", "AirPods Pro", "p", -52), listOf("6E:44:01:9A:BC:DE", "Find My tracker", "t", -77),
        listOf("4A:12:F0:33:21:10", "Apple device", "u", -66), listOf("D0:03:DF:4E:12:34", "Galaxy Buds2 (12AB)", "p", -61),
        listOf("C1:44:22:FA:01:99", "Forerunner 255", "p", -70), listOf("F3:21:44:AB:01:22", "Windows laptop", "p", -64),
        listOf("C4:9E:11:22:33:44", "Tile", "t", -80), listOf("A4:C1:38:55:66:77", "LYWSD03MMC", "h", -79),
        listOf("CC:88:26:11:00:55", "[TV] Samsung Q60", "h", -75), listOf("E2:11:09:44:21:7A", "unnamed device", "u", -88))
    val bdots = ble.map { x ->
        val r = x[3] as Int
        RadarDot(x[0] as String, Air.bearing(x[0] as String), Math.max(0.0, Math.min(1.0, (-20.0 - r) / 80)), false, true,
            false, false, Pal.group(x[2] as String), 1.0, r, x[1] as String, "$r dBm")
    }
    shot("radar-ble-signal", size, size) { c ->
        RadarPainter(D).draw(c, size.toFloat(), bdots, emptyList(), emptyMap(), 0, null, now, null)
    }

    // --- the Finder
    val fs = (300 * D).toInt()
    val turnPts = (0 until 26).map { i ->
        val a = i / 40.0 * 2 * Math.PI
        FinderMath.TurnPoint(a, Math.round(-70 + 9 * Math.cos(a - 100 * Math.PI / 180) + 2 * Math.sin(i * 1.7)).toInt())
    }
    shot("finder-turning", fs, fs) { c ->
        FinderPainter(D).draw(c, fs.toFloat(), FinderPic(4.2, Pal.heat(FinderMath.hot(4.2)), 26 / 40.0 * 2 * Math.PI, 0.0,
            turnPts, null, null, 1.0, false, "ahead, where you started", false), now)
    }
    val all = (0 until 40).map { i ->
        val a = i / 40.0 * 2 * Math.PI
        FinderMath.TurnPoint(a, Math.round(-70 + 9 * Math.cos(a - 100 * Math.PI / 180) + 2 * Math.sin(i * 1.7)).toInt())
    }
    val dir = FinderMath.direction(all)
    // Sensor mode: the phone has since turned 60 degrees right, so the picture turns 60 left.
    shot("finder-after-turn", fs, fs) { c ->
        FinderPainter(D).draw(c, fs.toFloat(), FinderPic(2.4, Pal.heat(FinderMath.hot(2.4)), null, -Math.PI / 3, all, dir.curve,
            dir.a, 0.9, false, "ahead of you", false), now)
    }
    shot("finder-found", fs, fs) { c ->
        FinderPainter(D).draw(c, fs.toFloat(), FinderPic(0.5, Pal.heat(FinderMath.hot(0.5)), null, 0.0, emptyList(), null,
            null, 1.0, true, null, false), now + 300)
    }
    // --- trace and heat bar
    val tw = (330 * D).toInt()
    val th = (84 * D).toInt()
    val track = FinderTrack()
    for (i in 0 until 55) track.add(now - 55_000 + i * 1000L, (-82 + i * 0.4 + 6 * Math.sin(i * 1.3)).toInt() - if (i == 30) 18 else 0)
    shot("trace", tw, th, T.SURFACE) { c ->
        val p = track.points
        TracePainter(D).draw(c, tw.toFloat(), th.toFloat(), LongArray(p.size) { p[it].t }, IntArray(p.size) { p[it].r },
            DoubleArray(p.size) { p[it].e }, now)
    }
    shot("heat", tw, (20 * D).toInt(), T.SURFACE) { c -> HeatBarPainter(D).draw(c, tw.toFloat(), 20 * D, 0.62) }

    // --- the map
    for ((w, byStatus, off) in listOf(Triple(360, false, false), Triple(360, true, true), Triple(412, false, true), Triple(800, false, false))) {
        val plan = NetMap.plan(info, devs, byStatus, off, w.toDouble())!!
        val pw = (w * D).toInt()
        val ph = (plan.box[3] * plan.scale * D).toInt() + 1
        val sel = if (w == 360 && !byStatus) MapSel.Device("A4:CF:12:44:55:66") else if (w == 412) MapSel.Group("iot") else null
        shot("map-$w-${if (byStatus) "status" else "kind"}${if (off) "-offline" else ""}", pw, ph, Pal.SCOPE) { c ->
            MapPainter(D).draw(c, plan, info, sel, (plan.scale * D).toFloat())
        }
    }
}
