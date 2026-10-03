import com.example.netmon.*
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

var passed = 0
var failed = 0

fun check(cond: Boolean, what: String) {
    if (cond) passed++ else { failed++; println("FAIL: $what") }
}

fun eq(actual: Any?, expected: Any?, what: String) =
    check(actual == expected, "$what: expected <$expected> got <$actual>")

inline fun <reified T : Throwable> throws(what: String, block: () -> Unit): T? {
    try {
        block()
    } catch (t: Throwable) {
        if (t is T) { passed++; return t }
        failed++; println("FAIL: $what: threw ${t.javaClass.simpleName}: ${t.message}"); return null
    }
    failed++; println("FAIL: $what: did not throw")
    return null
}

val BINS = File(System.getenv("BINS") ?: "bins")
val MOCK = System.getenv("MOCK") ?: "http://127.0.0.1:18080"

fun main() {
    parseTests()
    formatTests()
    firmwareTests()
    subnetTests()
    historyTests()
    alertTests()
    latencyTests()
    normalizeTests()
    validateTests()
    clientTests()
    println("passed $passed, failed $failed")
    if (failed > 0) System.exit(1)
}

fun parseTests() {
    val h = Parse.health("""{"status":"ok","version":"0.9.6-status-hints","wifi":"connected","ssid":"HOME-5G","ip":"192.168.2.30","gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-35,"uptime_s":3725,"free_heap":181234,"sweepable":true,"devices":6,"scan_passes":61,"scan_remaining":0,"last_pass_ms":6512,"pass_seen":5,"pass_merges":212,"arp_cache":10,"latency_valid":true,"latency_ms":4,"latency_age_s":12,"dhcp_packets":3,"events":9,"baseline_open":false,"baseline_anchored":true,"baseline_closes_in_s":0,"names_known":4}""")
    eq(h.version, "0.9.6-status-hints", "health.version")
    eq(h.rssi, -35, "health.rssi")
    eq(h.uptimeS, 3725L, "health.uptime")
    eq(h.sweepable, true, "health.sweepable")
    eq(h.lastPassMs, 6512L, "health.last_pass_ms")
    eq(h.namesKnown, 4, "health.names_known")
    eq(h.inSetupMode, false, "health not setup")

    // A 0.9.2-era reply: no names_known, no baseline_anchored.
    val old = Parse.health("""{"status":"ok","version":"0.9.2","wifi":"softap","ssid":"netmon-setup","ip":"192.168.4.1","rssi":0,"uptime_s":5}""")
    eq(old.namesKnown, 0, "old health default")
    eq(old.baselineAnchored, false, "old health bool default")
    eq(old.inSetupMode, true, "softap is setup mode")

    val d = Parse.devices("""[{"mac":"d4:e9:f4:12:34:56","ip":"192.168.2.30","hostname":"netmon","vendor":"Espressif Inc.","status":"known","randomised":false,"self":true,"online":true,"last_seen_s":3,"up_s":3600},{"mac":"00:11:32:AA:BB:CC","ip":"192.168.2.20","hostname":"Desk \"PC\"","vendor":"","status":"known","randomised":false,"self":false,"online":false,"last_seen_s":7300,"up_s":0},{"mac":"X","hostname":null}]""")
    eq(d.size, 3, "devices count")
    eq(d[0].mac, "D4:E9:F4:12:34:56", "mac upper-cased")
    eq(d[0].self, true, "self flag")
    eq(d[1].hostname, "Desk \"PC\"", "escaped quote in hostname")
    eq(d[2].hostname, "", "null hostname is empty, not 'null'")
    eq(d[2].status, "unknown", "missing status defaults to unknown")

    val e = Parse.events("""[{"at_s":3700,"type":"seen","mac":"7c:9e:bd:01:02:03","ip":"192.168.2.77","text":"device first seen"}]""")
    eq(e[0].atS, 3700L, "event at_s")
    eq(e[0].mac, "7C:9E:BD:01:02:03", "event mac upper")

    val l = Parse.latency("""{"valid":false,"rtt_ms":0,"checked_s":120,"age_s":7,"failures":3,"method":"tcp"}""")
    eq(l.valid, false, "latency valid")
    eq(l.failures, 3L, "latency failures")

    val c = Parse.config("""{"version":"0.9.6-status-hints","ssid":"HOME-5G","has_password":true,"networks":3,"use_dhcp":true,"ip":"0.0.0.0","mask":"0.0.0.0","gw":"0.0.0.0","dns":"0.0.0.0","scan_interval_s":60,"probe_interval_s":30,"offline_after_s":180,"learning_window_s":600,"active":{"ip":"192.168.2.30","mask":"255.255.255.0","gw":"192.168.2.1","dns":"192.168.2.1","mac":"D4:E9:F4:12:34:56","ssid":"HOME-5G","rssi":-35,"source":"dhcp"}}""")
    eq(c.active?.source, "dhcp", "config active source")
    eq(c.learningWindowS, 600, "config learning window")
    eq(Parse.config("""{"ssid":"x"}""").useDhcp, true, "use_dhcp defaults to true like the firmware")

    val n = Parse.networks("""[{"order":1,"ssid":"Office-WiFi","has_password":true,"active":false,"boot":"failed","boot_ms":3120,"reason":205},{"order":2,"ssid":"HOME-5G","has_password":true,"active":true,"boot":"joined","boot_ms":2410,"reason":0}]""")
    eq(n[0].reason, 205, "network reason")
    eq(n[1].active, true, "network active")

    val dh = Parse.dhcp("""{"listener":"listening","received":14,"capture_packets":3,"valid":true,"client_mac":"da:a1:19:77:88:99","assigned_ip":"192.168.2.45","server_ip":"192.168.2.1","lease_s":7200,"message_type":"REQUEST","hostname":"Pixel-7","last_seen_s":905,"local":{"ip":"192.168.2.30","mask":"255.255.255.0","gateway":"192.168.2.1","dns":"192.168.2.1","mode":"dhcp","hostname":"netmon"}}""")
    eq(dh.local?.hostname, "netmon", "dhcp local")
    eq(dh.clientMac, "DA:A1:19:77:88:99", "dhcp mac upper")
    // Before 0.9.6 there was no listener field.
    eq(Parse.dhcp("""{"valid":false}""").listener, "", "old dhcp listener blank")

    val s = Parse.scan("""[{"ssid":"HOME-5G","rssi":-38},{"ssid":"Say \"hi\"","rssi":-80},{"ssid":"","rssi":-90}]""")
    eq(s.size, 2, "scan drops blank ssid")
    eq(s[1].ssid, "Say \"hi\"", "scan escaped quote")

    eq(Parse.errorText("""{"error":"invalid update key"}"""), "invalid update key", "error text")
    eq(Parse.errorText("not found"), null, "plain text error body")

    val bad = throws<ApiException>("bad json throws") { Parse.health("<html>") }
    eq(bad?.kind, ApiException.Kind.BadData, "bad json kind")
    throws<ApiException>("array expected") { Parse.devices("{}") }

    val body = JSONObject(Parse.configBody(ConfigUpdate("HOME-5G", "", true, "", "", "", "", 60, 30, 180, 600)))
    eq(body.getBoolean("use_dhcp"), true, "body use_dhcp")
    eq(body.has("ip"), false, "dhcp body has no static fields")
    eq(body.getString("pass"), "", "blank password is sent (means keep)")
    val st = JSONObject(Parse.configBody(ConfigUpdate("Lab", "pw", false, "10.0.0.9", "255.255.255.0", "10.0.0.1", "", 90, 30, 180, 600)))
    eq(st.getString("ip"), "10.0.0.9", "static body ip")
    eq(st.getInt("scan_interval_s"), 90, "body interval")
    eq(JSONObject(Parse.forgetBody("Cafe \"Guest\"")).getString("ssid"), "Cafe \"Guest\"", "forget body escapes")
}

fun formatTests() {
    eq(Format.duration(45), "45s", "dur s")
    eq(Format.duration(750), "12m", "dur m")
    eq(Format.duration(12000), "3h 20m", "dur h")
    eq(Format.duration(190000), "2d 4h", "dur d")
    eq(Format.duration(-5), "0s", "dur negative")
    eq(Format.ago(2), "just now", "ago now")
    eq(Format.ago(40), "40 s ago", "ago s")
    eq(Format.ago(300), "5 min ago", "ago min")
    eq(Format.ago(7300), "2 h ago", "ago h")
    eq(Format.ago(90000), "yesterday", "ago yesterday")
    eq(Format.ago(300000), "3 days ago", "ago days")
    eq(Format.shortVendor("TP-LINK TECHNOLOGIES CO.,LTD."), "TP-LINK", "vendor tplink")
    eq(Format.shortVendor("Espressif Inc."), "Espressif", "vendor espressif")
    eq(Format.shortVendor("QNAP Systems, Inc."), "QNAP Systems", "vendor qnap")
    eq(Format.shortVendor("Inc."), "Inc.", "vendor all boilerplate keeps original")
    eq(Format.shortVendor(""), "", "vendor blank")
    check(Format.ipKey("192.168.2.9") < Format.ipKey("192.168.2.10"), "ip numeric order")
    eq(Format.ipKey("netmon.local"), Long.MAX_VALUE, "non-ip sorts last")
    eq(Format.signalBars(-35), 4, "bars 4")
    eq(Format.signalBars(-70), 2, "bars 2")
    eq(Format.signalBars(0), 0, "bars none")
    eq(Format.joinReason(205), "connection failed (code 205)", "reason 205")
    eq(Format.joinReason(999), "reason (code 999)", "unknown reason")
    eq(Format.joinReason(0), "", "no reason")
    val nets = listOf(
        SavedNetwork(1, "Office-WiFi", true, false, "failed", 3120, 205),
        SavedNetwork(2, "HOME-5G", true, true, "joined", 2410, 0),
        SavedNetwork(3, "Cafe", false, false, "not_tried", 0, 0),
    )
    eq(Format.joinText(nets[0]).label, "Could not join", "join failed label")
    eq(Format.joinText(nets[0]).detail, "in range; connection failed (code 205)", "join failed detail")
    eq(Format.joinText(nets[1]).detail, "in 2.4 s", "join ok detail")
    eq(Format.joinText(nets[2]).tone, Format.Tone.Quiet, "not tried tone")
    eq(Format.joinText(SavedNetwork(1, "x", true, false, "refused", 900, 15)).detail,
        "check the password; handshake timed out (code 15)", "refused detail")
    eq(Format.historyNote(nets), "Office-WiFi was tried first and cost 3.1 s at start-up. Saving settings moves HOME-5G to the top.", "history note")
    eq(Format.historyNote(listOf(nets[1])), "", "history note nothing lost")
    eq(Format.historyNote(emptyList()), "No networks remembered yet.", "history note empty")
    eq(Format.historyNote(listOf(SavedNetwork(1, "A", true, false, "not_found", 12000, 201))),
        "None of these answered at start-up, so the board opened netmon-setup.", "history note setup")
    eq(Format.place("Lisbon", "Lisbon", "Portugal"), "Lisbon, Portugal", "place dedupe")
    eq(Format.place("", "", ""), "", "place blank")
    eq(Format.fileSize(1_000_000), "977 KB", "size KB")
    eq(Format.fileSize(1_121_904), "1.07 MB", "size MB")
    eq(Format.listenerText("paused"), "Paused for a Wi-Fi scan or an update", "listener text")
    val dev = Device("DA:A1:19:77:88:99", "192.168.2.45", "", "", "private", true, false, true, 3, 9)
    eq(Format.deviceName(dev), "Private device", "name for randomised without hostname")
    eq(Format.vendorLine(dev), "Randomised address", "vendor line randomised")
    eq(Format.eventTitle(EventHistory.TYPE_RESTART), "Monitor restarted", "restart title")
}

fun firmwareTests() {
    eq(Firmware.problem("netmon.ino.bin", 1_121_904, 0xE9), null, "good image")
    eq(Firmware.problem("netmon.ino.bootloader.bin", 20_000, 0xE9),
        "That is the bootloader image, not the firmware. Choose the file ending in .ino.bin.", "bootloader")
    eq(Firmware.problem("netmon.ino.merged.bin", 4_000_000, 0xE9)?.startsWith("That is the merged image"), true, "merged")
    eq(Firmware.problem("notes.txt", 400_000, 0xE9), "That is not a .bin file.", "not bin")
    eq(Firmware.problem("big.bin", 1_310_721, 0xE9), "Too large: the app partition holds 1,310,720 bytes.", "too large")
    eq(Firmware.problem("big.bin", 1_310_720, 0xE9), null, "exactly the partition size is fine")
    eq(Firmware.problem("small.bin", 262_143, 0xE9), "Too small to be netmon firmware.", "too small")
    eq(Firmware.problem("x.BIN", 400_000, 0x00), "That file is not an ESP32 firmware image.", "bad magic")
    eq(Firmware.problem("x.bin", 400_000, null), null, "unreadable first byte is left to the board")
    for ((f, v) in listOf("netmon-0.9.4.ino.bin" to "0.9.4-history-upload", "netmon-0.9.5.ino.bin" to "0.9.5-dhcp-names",
            "netmon-0.9.6.ino.bin" to "0.9.6-status-hints")) {
        val file = File(BINS, f)
        if (file.exists()) eq(Firmware.versionIn(file.readBytes()), v, "version in $f") else println("skip: $f missing")
    }
    eq(Firmware.versionIn(ByteArray(1000)), null, "no version in zeros")
}

fun subnetTests() {
    val ip = Subnet.parse("192.168.2.30")!!
    eq(Subnet.format(ip), "192.168.2.30", "ip round trip")
    eq(Subnet.parse("192.168.2"), null, "short ip")
    eq(Subnet.parse("192.168.2.256"), null, "octet too big")
    eq(Subnet.parse(" 10.0.0.1 "), Subnet.format(Subnet.parse("10.0.0.1")!!).let { Subnet.parse(it) }, "trim")
    eq(Subnet.parse("1.2.3.-4"), null, "negative octet")
    val h24 = Subnet.hosts(ip, 24)
    eq(h24.size, 253, "/24 host count without self")
    check("192.168.2.30" !in h24 && "192.168.2.0" !in h24 && "192.168.2.255" !in h24, "/24 excludes self, net, broadcast")
    eq(h24.first(), "192.168.2.1", "/24 starts at .1")
    val h16 = Subnet.hosts(ip, 16)
    eq(h16.size, 1024, "/16 limited")
    eq(h16.take(253).all { it.startsWith("192.168.2.") }, true, "/16 own block first")
    check(h16.any { it.startsWith("192.168.3.") } && h16.any { it.startsWith("192.168.1.") }, "/16 neighbours next")
    val h30 = Subnet.hosts(Subnet.parse("10.0.0.5")!!, 30)
    eq(h30, listOf("10.0.0.6"), "/30")
    val top = Subnet.hosts(Subnet.parse("10.0.0.5")!!, 23)
    check(top.none { it == "10.0.0.0" || it == "10.0.1.255" }, "/23 excludes net and broadcast")
    eq(top.size, 509, "/23 host count without self")
    check(Subnet.looksLikeNetmon("""{"status":"ok","version":"0.9.6","scan_passes":1,"sweepable":true}"""), "netmon health recognised")
    check(!Subnet.looksLikeNetmon("""{"status":"ok","version":"1"}"""), "other json rejected")
    check(!Subnet.looksLikeNetmon("<html>"), "html rejected")
}

fun historyTests() {
    val tmp = File.createTempFile("hist", ".json")
    tmp.delete()
    val h = EventHistory(tmp, capacity = 50)
    val t0 = 1_800_000_000_000L
    val ev1 = listOf(
        BoardEvent(3700, "scan_done", "S", "", "ARP sweep finished"),
        BoardEvent(3690, "scan", "S", "", "ARP sweep started"),
        BoardEvent(3400, "seen", "A", "1", "device first seen"),
        BoardEvent(60, "seen", "B", "2", "device first seen"),
    )
    eq(h.merge(t0, 3725, ev1), 2, "sweeps skipped, two added")
    eq(h.merge(t0 + 10_000, 3735, ev1), 0, "same reading adds nothing")
    val all = h.all()
    eq(all[0].mac, "A", "newest first")
    eq(all[0].wallMs, t0 - 325_000, "clock time from uptime")
    val ev2 = listOf(BoardEvent(3730, "offline", "B", "2", "device went offline")) + ev1
    eq(h.merge(t0 + 10_000, 3735, ev2), 1, "one new event")
    h.save()
    val h2 = EventHistory(tmp, capacity = 50)
    h2.load()
    eq(h2.size(), 3, "reloaded size")
    eq(h2.merge(t0 + 20_000, 3745, ev2), 0, "reload remembers what it has seen")
    // Restart: uptime goes back to 40 s.
    val ev3 = listOf(BoardEvent(35, "seen", "A", "1", "device first seen"))
    eq(h2.merge(t0 + 100_000, 40, ev3), 2, "restart entry plus the new run's event")
    eq(h2.all()[0].type, "seen", "newest is the new run's event")
    eq(h2.all()[1].type, EventHistory.TYPE_RESTART, "then the restart")
    eq(h2.all()[1].wallMs, t0 + 100_000 - 40_000, "restart at the new boot time")
    eq(h2.all()[2].type, "offline", "then the previous run's events")
    // A restart missed while away: uptime larger than before, but boot time moved a lot.
    val missed = h2.merge(t0 + 86_400_000, 50_000, emptyList())
    eq(missed, 1, "restart detected from moved boot time")
    // Drift within allowance after a long gap is not a restart.
    val quiet = h2.merge(t0 + 86_400_000 * 2, 50_000 + 86_400 + 20, emptyList())
    eq(quiet, 0, "small drift is not a restart")
    // Capacity.
    val many = (1..80).map { BoardEvent(50_000L + 86_400 + it, "back", "M$it", "", "device returned") }
    h2.merge(t0 + 86_400_000 * 2 + 100_000, 50_000 + 86_400 + 120, many)
    eq(h2.size(), 50, "capacity kept")
    check(h2.rememberNames(listOf(Device("A", "1", "phone", "", "known", false, false, true, 0, 0))), "names changed")
    eq(h2.nameFor("A"), "phone", "name remembered")
    check(!h2.rememberNames(listOf(Device("A", "1", "phone", "", "known", false, false, true, 0, 0))), "names unchanged")
    h2.save()
    val h3 = EventHistory(tmp)
    h3.load()
    eq(h3.nameFor("A"), "phone", "names persisted")
    // Clearing sticks: events the board still lists do not come back.
    val hc = EventHistory(tmp)
    hc.merge(t0, 3725, ev1)
    check(hc.size() > 0, "history before clear")
    hc.clear(t0 + 1000)
    eq(hc.merge(t0 + 10_000, 3735, ev1), 0, "cleared events stay cleared")
    eq(hc.merge(t0 + 20_000, 3745, listOf(BoardEvent(3744, "back", "Z", "9", "device returned")) + ev1), 1, "new events still arrive")
    hc.save()
    val hc2 = EventHistory(tmp)
    hc2.load()
    eq(hc2.merge(t0 + 30_000, 3755, ev1), 0, "clear survives a reload")
    tmp.writeText("{broken")
    val h4 = EventHistory(tmp)
    h4.load()
    eq(h4.size(), 0, "damaged file loads empty")
    tmp.delete()
}

fun dev(mac: String, status: String, self: Boolean = false) =
    Device(mac, "192.168.2.9", "", "", status, status == "private", self, true, 1, 1)

fun alertTests() {
    val m = AlertMemory()
    eq(AlertRules.evaluate(AlertMode.UNRECOGNISED, listOf(dev("A", "known"), dev("U", "unknown")), m).size, 0, "baseline is silent")
    eq(AlertRules.evaluate(AlertMode.UNRECOGNISED, listOf(dev("A", "known"), dev("U", "unknown")), m).size, 0, "already-flagged unknown stays quiet")
    val r = AlertRules.evaluate(AlertMode.UNRECOGNISED, listOf(dev("A", "known"), dev("V", "unknown"), dev("P", "private")), m)
    eq(r.map { it.mac }, listOf("V"), "new unknown alerts; private does not in this mode")
    eq(AlertRules.evaluate(AlertMode.UNRECOGNISED, listOf(dev("V", "unknown")), m).size, 0, "only once")
    val r2 = AlertRules.evaluate(AlertMode.EVERY_NEW, listOf(dev("Q", "private"), dev("K", "known"), dev("A", "known")), m)
    eq(r2.map { it.mac }, listOf("Q", "K"), "every-new alerts for unseen devices")
    eq(AlertRules.evaluate(AlertMode.EVERY_NEW, listOf(dev("S", "known", self = true)), m).size, 0, "never the monitor itself")
    val off = AlertMemory()
    AlertRules.evaluate(AlertMode.OFF, listOf(dev("A", "known")), off)
    eq(AlertRules.evaluate(AlertMode.OFF, listOf(dev("N", "unknown")), off).size, 0, "off never alerts")
    eq(AlertRules.evaluate(AlertMode.EVERY_NEW, listOf(dev("N", "unknown")), off).size, 1, "unknown seen while off still alerts once alerts are on")
    eq(AlertMode.of("every_new"), AlertMode.EVERY_NEW, "mode from key")
    eq(AlertMode.of(null), AlertMode.OFF, "mode default")
    val big = AlertMemory(baselined = true)
    AlertRules.evaluate(AlertMode.OFF, (1..2100).map { dev("M$it", "known") }, big)
    eq(big.seen.size, AlertRules.MAX_SEEN, "seen capped")
    check("M2100" in big.seen && "M1" !in big.seen, "oldest dropped first")
}

fun latencyTests() {
    val log = LatencyLog(capacity = 5)
    val t = 1_800_000_000_000L
    check(log.record(t, true, 4, 12), "first sample")
    check(!log.record(t + 10_000, true, 4, 22), "same probe read again")
    check(log.record(t + 30_000, false, 0, 2), "failed probe")
    check(log.record(t + 60_000, true, 9, 2), "third")
    val s = log.stats()!!
    eq(s.count, 3, "stats count")
    eq(s.lost, 1, "stats lost")
    eq(s.minMs, 4L, "stats min")
    eq(s.maxMs, 9L, "stats max")
    val copy = LatencyLog(capacity = 5)
    copy.decode(log.encode())
    eq(copy.all(), log.all(), "encode round trip")
    for (i in 1..10) log.record(t + 60_000 + i * 30_000L, true, i.toLong(), 0)
    eq(log.size(), 5, "capacity")
    copy.decode("garbage;1:2;x:y")
    eq(copy.size(), 1, "decode skips junk")
}

fun normalizeTests() {
    eq(NetmonClient.normalize("192.168.2.30"), "http://192.168.2.30", "bare ip")
    eq(NetmonClient.normalize(" http://192.168.2.30/settings "), "http://192.168.2.30", "strip path")
    eq(NetmonClient.normalize("netmon.local:80"), "http://netmon.local", "default port dropped")
    eq(NetmonClient.normalize("192.168.2.30:8080"), "http://192.168.2.30:8080", "port kept")
    throws<IllegalArgumentException>("blank") { NetmonClient.normalize("  ") }
    throws<IllegalArgumentException>("junk") { NetmonClient.normalize("http://bad host") }
    throws<IllegalArgumentException>("ftp") { NetmonClient.normalize("ftp://x") }
    eq(NetmonClient.display("http://192.168.2.30"), "192.168.2.30", "display")
}

fun validateTests() {
    eq(Validate.credentials("", ""), "network name is required", "ssid required")
    eq(Validate.credentials("x".repeat(32), ""), null, "32-byte ssid ok")
    eq(Validate.credentials("x".repeat(33), ""), "network name is over 32 characters", "33-byte ssid")
    // The firmware counts bytes: eleven Arabic letters are 22 bytes, seventeen are 34.
    eq(Validate.credentials("ب".repeat(17), ""), "network name is over 32 characters", "ssid length in bytes")
    eq(Validate.credentials("a", "p".repeat(65)), "password is over 64 characters", "long password")
    eq(Validate.static("192.168.2.30", "255.255.255.0", "192.168.2.1"), null, "good static")
    eq(Validate.static("192.168.2.30", "255.0.255.0", "192.168.2.1"), "subnet mask is not valid", "non-contiguous mask")
    eq(Validate.static("192.168.2.30", "0.0.0.0", "192.168.2.1"), "subnet mask is not valid", "zero mask")
    eq(Validate.static("192.168.2.300", "255.255.255.0", "192.168.2.1"), "IP address is not valid", "bad ip")
    eq(Validate.static("192.168.2.30", "255.255.255.0", "192.168.3.1"),
        "gateway is outside the subnet, the device would be unreachable", "gateway outside")
    eq(Validate.static("10.1.2.3", "255.255.0.0", "10.1.9.1"), null, "/16 gateway ok")
    eq(Validate.dns(""), null, "blank dns ok")
    eq(Validate.dns("1.1.1"), "DNS address is not valid", "bad dns")
    eq(Validate.intervals(60, 30, 180, 600), null, "default intervals")
    eq(Validate.intervals(9, 30, 180, 600), "an interval is out of range", "scan too short")
    eq(Validate.intervals(60, 30, 29, 600), "an interval is out of range", "offline too short")
    eq(Validate.intervals(60, 30, 180, 0), null, "no learning window is allowed")
    check(Validate.maskContiguous(-256) && !Validate.maskContiguous(0x00FF00FF), "mask contiguity")
}

fun mock(path: String, method: String = "GET"): String {
    val c = URL(MOCK + path).openConnection() as HttpURLConnection
    c.requestMethod = method
    if (method == "POST") { c.doOutput = true; c.setFixedLengthStreamingMode(0); c.outputStream.close() }
    return c.inputStream.use { String(it.readBytes()) }
}

fun sha256(b: ByteArray) = MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }

fun clientTests() {
    try { mock("/__reset", "POST") } catch (e: Exception) { println("skip client tests: mock not running ($e)"); failed++; return }
    val c = NetmonClient(MOCK)
    val h = c.health()
    eq(h.version, "0.9.6-status-hints", "client health")
    eq(c.devices().size, 6, "client devices")
    eq(c.devices()[5].hostname, "Desk \"PC\"", "client escaped hostname")
    eq(c.events().size, 7, "client events")
    eq(c.latency().method, "tcp", "client latency")
    eq(c.isp(false).checkedAgeS, 1800L, "client isp cached")
    eq(c.isp(true).checkedAgeS, 0L, "client isp forced")
    eq(c.config().active?.ssid, "HOME-5G", "client config")
    eq(c.networks().map { it.ssid }, listOf("Office-WiFi", "HOME-5G", "Cafe Guest"), "client networks")
    eq(c.dhcp().listener, "listening", "client dhcp")
    eq(c.scan().size, 3, "client scan")

    c.saveConfig(ConfigUpdate("HOME-5G", "", true, "", "", "", "", 60, 30, 180, 600))
    val sent = JSONObject(mock("/__config"))
    eq(sent.getString("ssid"), "HOME-5G", "config body reached the board")
    eq(sent.getBoolean("use_dhcp"), true, "config body use_dhcp")
    val e1 = throws<ApiException>("blank ssid refused") { c.saveConfig(ConfigUpdate("", "", true, "", "", "", "", 60, 30, 180, 600)) }
    eq(e1?.message, "network name is required", "board's own words")
    eq(e1?.status, 400, "status 400")

    val e2 = throws<ApiException>("forget active") { c.forget("HOME-5G") }
    eq(e2?.message, "the board is using this network right now", "forget active words")
    c.forget("Cafe Guest")
    eq(c.networks().size, 2, "forgotten")
    val e3 = throws<ApiException>("forget missing") { c.forget("Nope") }
    eq(e3?.message, "that network is not remembered", "forget missing words")

    eq(c.checkUpdateKey("wrong"), false, "wrong key")
    eq(c.checkUpdateKey("test-key"), true, "right key")

    val img95 = File(BINS, "netmon-0.9.5.ino.bin")
    if (img95.exists()) {
        val bytes = img95.readBytes()
        val bad = throws<ApiException>("wrong key upload") { c.uploadFirmware(bytes, "netmon.ino.bin", "nope") { _, _ -> } }
        eq(bad?.kind, ApiException.Kind.Unauthorized, "upload 401")
        var last = -1L
        var total = 0L
        var monotonic = true
        val t0 = System.currentTimeMillis()
        c.uploadFirmware(bytes, "netmon.ino.bin", "test-key") { s, t -> if (s < last) monotonic = false; last = s; total = t }
        check(monotonic, "progress only moves forward")
        eq(last, total, "progress ends at the total")
        val up = org.json.JSONArray(mock("/__uploads"))
        val rec = up.getJSONObject(up.length() - 1)
        eq(rec.getInt("bytes"), bytes.size, "all bytes arrived")
        eq(rec.getString("sha256"), sha256(bytes), "bytes arrived intact")
        eq(rec.getString("filename"), "netmon.ino.bin", "filename sent")
        eq(rec.getLong("content_length"), total, "content length matches progress total")
        val out = RestartWatch.await(MOCK, t0, "0.9.6-status-hints", firstLookMs = 1000, retryMs = 200, giveUpAfterMs = 20_000)
        eq(out, RestartWatch.Outcome.Updated("0.9.5-dhcp-names"), "watch sees the new version")
        // Same image again: restarts, same version.
        val t1 = System.currentTimeMillis()
        Thread.sleep(1100)
        c.uploadFirmware(bytes, "netmon.ino.bin", "test-key") { _, _ -> }
        eq(RestartWatch.await(MOCK, t1, "0.9.5-dhcp-names", firstLookMs = 1000, retryMs = 200, giveUpAfterMs = 20_000),
            RestartWatch.Outcome.SameVersion("0.9.5-dhcp-names"), "watch sees a restart with the same version")
        val junk = ByteArray(300_000) { 1 }
        val e4 = throws<ApiException>("wrong magic refused by board") { c.uploadFirmware(junk, "junk.bin", "test-key") { _, _ -> } }
        eq(e4?.message, "firmware update failed: Wrong Magic Byte", "board's update error")
        // Not restarted: health answers straight away with an old uptime.
        Thread.sleep(1500)
        eq(RestartWatch.await(MOCK, System.currentTimeMillis(), "x", firstLookMs = 10, retryMs = 10, giveUpAfterMs = 5_000) is RestartWatch.Outcome.NotRestarted,
            true, "no restart detected")
        mock("/__mode?x=down", "POST")
        eq(RestartWatch.await(MOCK, System.currentTimeMillis() - 10_000, "x", firstLookMs = 10, retryMs = 50, giveUpAfterMs = 11_000),
            RestartWatch.Outcome.NoAnswer, "gives up when the board never answers")
        mock("/__mode?x=normal", "POST")
    } else println("skip upload: image missing")

    mock("/__mode?x=badjson", "POST")
    eq(throws<ApiException>("bad json") { c.health() }?.kind, ApiException.Kind.BadData, "html reply is bad data")
    mock("/__mode?x=slow", "POST")
    val slow = NetmonClient(MOCK).apply { readTimeoutMs = 1000 }
    eq(throws<ApiException>("timeout") { slow.health() }?.kind, ApiException.Kind.Timeout, "slow board times out")
    mock("/__mode?x=normal", "POST")
    val dead = NetmonClient("http://127.0.0.1:1").apply { connectTimeoutMs = 1000 }
    eq(throws<ApiException>("unreachable") { dead.health() }?.kind, ApiException.Kind.Unreachable, "nothing listening")
    mock("/__mode?x=old", "POST")
    val e5 = throws<ApiException>("404 on old firmware") { c.networks() }
    eq(e5?.status, 404, "404 surfaced")
    eq(e5?.message?.startsWith("This board's firmware does not have that feature"), true, "404 explained")
    mock("/__reset", "POST")
}
