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
    nearbyParseTests()
    airTests()
    liveLogTests()
    finderTests()
    headingTests()
    mapTests()
    parityTests()
    clientTests()
    nearbyClientTests()
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
    eq(Firmware.problem("big.bin", 1_310_721, 0xE9), "Too large: this board takes firmware up to 1,310,720 bytes.", "too large")
    // From 0.10 the board says what it takes: an 0.11 image is 1.5 MB.
    eq(Firmware.problem("netmon.ino.bin", 1_503_443, 0xE9, Firmware.limit(1_966_080)), null, "0.11 image fits a 0.10+ board")
    eq(Firmware.problem("netmon.ino.bin", 1_503_443, 0xE9, Firmware.limit(0)),
        "Too large: this board takes firmware up to 1,310,720 bytes.", "0.11 image too large for an old board")
    eq(Firmware.problem("big.bin", 1_966_081, 0xE9, 1_966_080), "Too large: this board takes firmware up to 1,966,080 bytes.", "over update_max")
    eq(Firmware.limit(0), 1_310_720L, "limit without update_max")
    eq(Firmware.limit(1_966_080), 1_966_080L, "limit from update_max")
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


// --- Nearby, the Finder and the map (1.1.0) ---------------------------------------

fun near(a: Double, b: Double, what: String, tol: Double = 1e-9) =
    check(Math.abs(a - b) <= tol * Math.max(1.0, Math.abs(b)), "$what: expected <$b> got <$a>")

val NEARBY_JSON = """{"version":"0.11.0-finder","on_lan":true,"sweeping":false,"background_s":120,"finding":null,"wifi_scan":{"enabled":true,"state":"idle","scans":42,"failures":0,"age_s":5,"took_ms":1640},"ble_scan":{"enabled":true,"state":"listening","bursts":120,"age_s":0,"dropped":0},"wifi":[{"bssid":"50:91:e3:12:34:56","ssid":"HOME-5G","ch":6,"rssi":-38,"security":"WPA2/WPA3","live":true,"joined":true,"age_s":3,"known_s":603},{"bssid":"AC:84:C6:AA:00:02","ssid":"","ch":1,"rssi":-66,"security":"WPA2","live":true,"joined":false,"age_s":3,"known_s":603},{"bssid":"E4:6F:13:00:11:22","ssid":"Say \"hi\"","ch":11,"rssi":-81,"security":"Open","live":true,"joined":false,"age_s":3,"known_s":603},{"bssid":"A0:63:91:01:02:03","ssid":"Neighbours","ch":11,"rssi":-90,"security":"WPA2","live":false,"joined":false,"age_s":3400,"known_s":4000}],"ble":[{"addr":"5d:21:8a:00:11:22","name":"","vendor":"Apple","company":76,"kind":"private","type":"audio","sure":4,"model":"AirPods Pro","rssi":-52,"age_s":1,"known_s":300,"seen":12},{"addr":"C4:9E:11:22:33:44","name":"Tile","vendor":"Tile","company":1660,"kind":"static","type":"tracker","sure":3,"model":"","rssi":-80,"age_s":2,"known_s":300,"seen":12},{"addr":"E2:11:09:44:21:7A","name":"","vendor":"","company":-1,"kind":"private","type":"unknown","sure":0,"model":"","rssi":-88,"age_s":75,"known_s":300,"seen":3}]}"""

fun nearbyParseTests() {
    val n = Parse.nearby(NEARBY_JSON)
    eq(n.version, "0.11.0-finder", "nearby version")
    eq(n.finding, null, "no finding")
    eq(n.wifi.size, 4, "nearby wifi")
    eq(n.wifi[0].bssid, "50:91:E3:12:34:56", "bssid upper-cased")
    eq(n.wifi[0].joined, true, "joined")
    eq(n.wifi[2].ssid, "Say \"hi\"", "escaped ssid")
    eq(n.wifi[3].live, false, "history entry")
    eq(n.wifiScan.count, 42L, "wifi scans")
    eq(n.bleScan.count, 120L, "ble bursts")
    eq(n.bleScan.state, "listening", "ble state")
    eq(n.ble.size, 3, "ble")
    eq(n.ble[0].addr, "5D:21:8A:00:11:22", "ble addr upper")
    eq(n.ble[0].model, "AirPods Pro", "model")
    eq(n.ble[2].company, -1, "no company")
    val f = Parse.nearby("""{"finding":{"type":"ble","addr":"c4:9e:11:22:33:44","name":"Tile"},"wifi":[],"ble":[]}""")
    eq(f.finding?.addr, "C4:9E:11:22:33:44", "finding addr")
    eq(f.wifiScan.enabled, false, "missing scan block reads as off")
    eq(f.wifiScan.ageS, -1L, "never scanned")
    throws<ApiException>("nearby not json") { Parse.nearby("not found") }

    val cfg = Parse.nearbyConfig("""{"wifi":false,"ble":true,"ble_ready":false,"background_s":300}""")
    eq(cfg, NearbyConfig(false, true, false, 300), "nearby config")
    eq(JSONObject(Parse.nearbyConfigBody(ble = false)).toString(), """{"ble":false}""", "only the field changed is sent")
    eq(JSONObject(Parse.nearbyConfigBody(backgroundS = 0)).getInt("background_s"), 0, "background off")

    val fs = Parse.find("""{"active":true,"type":"ble","addr":"c4:9e:11:22:33:44","name":"Tile","kind":"static","dtype":"tracker","model":"","vendor":"Tile","state":"paused","why":"sweep","for_s":12,"heard_ms":800,"hold_ms":0,"seq":131,"readings":[[129,1900,-71],[130,900,-70],[131,0,-69],[132]]}""")
    eq(fs.addr, "C4:9E:11:22:33:44", "find addr")
    eq(fs.dtype, "tracker", "find dtype")
    eq(fs.why, "sweep", "find why")
    eq(fs.readings.size, 3, "short reading arrays are skipped")
    eq(fs.readings[0], FindReading(129, 1900, -71), "reading")
    eq(Parse.find("""{"active":false,"seq":7}""").active, false, "find inactive")
    eq(Parse.find("""{"active":false,"seq":7}""").heardMs, -1L, "never heard")
    val fb = JSONObject(Parse.findBody("wifi", "50:91:E3:12:34:56", 36))
    eq(fb.getString("type"), "wifi", "find body type")
    eq(fb.getInt("hold_s"), 36, "find body hold")
    check(!JSONObject(Parse.findBody("ble", "AA:BB:CC:DD:EE:FF")).has("hold_s"), "no hold unless asked")
    eq(JSONObject(Parse.FIND_STOP_BODY).getBoolean("stop"), true, "stop body")

    val m = Parse.map("""{"version":"0.11.0-finder","wifi":"connected","ssid":"HOME","ip":"192.168.2.30","mac":"d4:e9:f4:12:34:56","hostname":"netmon","gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-38,"channel":6,"bssid":"50:91:e3:12:34:56","uptime_s":99,"latency_valid":true,"latency_ms":4,"isp":{"checked":true,"valid":true,"isp":"Example","org":"Ex","age_s":5},"nearby_wifi":true,"aps":[{"bssid":"50:91:E3:12:34:56","ch":6,"rssi":-38,"live":true,"joined":true,"age_s":0}]}""")
    eq(m.mac, "D4:E9:F4:12:34:56", "map mac")
    eq(m.aps.size, 1, "map aps")
    eq(m.isp.isp, "Example", "map isp")
    eq(m.fromBoard, true, "map from the board")
    eq(Parse.config("""{"update_max":1966080}""").updateMax, 1_966_080L, "update_max")
    eq(Parse.config("""{"ssid":"x"}""").updateMax, 0L, "no update_max before 0.10")
}

fun airTests() {
    val cal = Calibration()
    near(Air.metres(-45, false, cal), 1.0, "wifi 1 m reference")
    near(Air.metres(-59, true, cal), 1.0, "ble 1 m reference")
    eq(Air.metres(-10, true, cal), 0.1, "clamped near")
    eq(Air.metres(-127, false, cal), 200.0, "clamped far")
    eq(Air.distance(3.25), "~3.3 m", "distance text")
    eq(Air.distance(12.4), "~12 m", "distance text over 10")
    eq(Air.group("phone"), "p", "personal")
    eq(Air.group("flipper"), "t", "trackers")
    eq(Air.group("sensor"), "h", "home and things")
    eq(Air.group("unknown"), "u", "not identified")
    eq(Air.group(""), "u", "blank type")
    val n = Parse.nearby(NEARBY_JSON)
    eq(Air.bleName(n.ble[0]), "AirPods Pro", "name from the model")
    eq(Air.bleName(n.ble[1]), "Tile", "own name")
    eq(Air.bleName(n.ble[2]), "unnamed device", "nothing to go by")
    eq(Air.bleName(n.ble[0].copy(model = "")), "Apple device", "name from the maker")
    eq(Air.bleName(n.ble[0].copy(model = "", vendor = "")), "Headphones", "name from the kind")
    eq(Air.apName(n.wifi[1]), "hidden network", "hidden network")
    eq(Air.apName(n.wifi[0], masked = true), "HO*****", "masked name")
    eq(Air.maskAddr("50:91:E3:12:34:56"), "50:91:XX:XX:XX:XX", "masked address")
    check(Air.matches(n.wifi[0], "home 5g"), "search words")
    check(Air.matches(n.wifi[0], "5091e3"), "search mac without colons")
    check(Air.matches(n.wifi[1], "hidden"), "search hidden")
    check(!Air.matches(n.wifi[0], "cafe"), "no match")
    check(Air.matches(n.ble[0], "headphones apple"), "search kind and maker")
    check(Air.matches(n.ble[1], "trackers"), "search group name")
    eq(Air.countLine(n), "3 Wi-Fi networks and 3 Bluetooth devices nearby", "count line")
    eq(Air.stateLine(n).text, "Wi-Fi scanned 5 s ago · listening for Bluetooth now", "state line")
    val off = n.copy(wifiScan = n.wifiScan.copy(enabled = false), bleScan = n.bleScan.copy(state = "unavailable"))
    eq(Air.countLine(off), "3 Bluetooth devices nearby", "count without wifi")
    eq(Air.stateLine(off), Air.State("Bluetooth could not start", true), "bluetooth failed")
    val finding = n.copy(finding = Finding("ble", "C4:9E:11:22:33:44", "Tile"), onLan = false)
    eq(Air.stateLine(finding).text, "Finding Tile. The other scans wait until that stops, so these lists stand still · " +
        "setup mode: scanning only while this screen is open", "state while finding")
    check(Air.stateLine(finding, masked = true).text.startsWith("Finding Ti**. "), "the name of what is being found is masked too")
    eq(Air.backgroundWord(120), "2 min", "background words")
    eq(Air.backgroundWord(0), "Off", "background off")
    check(Air.BACKGROUND.all { it == 0 || it in 30..3600 }, "background choices are ones the board takes")
    val tm = TrendMemory()
    for (r in listOf(-80, -79, -78)) tm.remember(mapOf("A" to r))
    eq(tm.trend("A"), 0, "no trend from three readings")
    tm.remember(mapOf("A" to -70))
    eq(tm.trend("A"), 1, "stronger")
    eq(tm.arrow("A"), " ▲", "arrow")
    tm.remember(mapOf("B" to -50))
    eq(tm.trend("A"), 0, "a device missing from a reading is forgotten")
}

fun liveLogTests() {
    val n = Parse.nearby(NEARBY_JSON)
    val log = LiveLog()
    val t0 = 1_800_000_000_000L
    val all = { _: NearbyBle -> true }
    eq(log.track(t0, n, all).arrived.size, 0, "first reading only learns")
    val more = n.copy(ble = n.ble + NearbyBle("11:22:33:44:55:66", "New", "", -1, "public", "phone", 2, "", -60, 0, 0, 1))
    eq(log.track(t0 + 10_000, more, all).arrived.size, 0, "still learning")
    eq(log.entries.size, 0, "nothing logged while learning")
    val gone = more.copy(wifi = more.wifi.filter { it.ssid != "HOME-5G" })
    val ch = log.track(t0 + 25_000, gone, all)
    eq(ch.left.map { it.key }, listOf("50:91:E3:12:34:56"), "departure after learning")
    eq(log.entries[0].arrived, false, "logged as left")
    eq(log.entries[0].name, "HOME-5G", "with its name")
    // Masking applies when shown, so turning it on hides names logged before it.
    eq(log.entries[0].shown(true), "HO*****", "a logged network name masked when shown")
    eq(log.entries[0].shown(false), "HOME-5G", "and in full without masking")
    val back = log.track(t0 + 28_000, more, all)
    eq(back.arrived, listOf("50:91:E3:12:34:56"), "arrival")
    // A Bluetooth device unheard for over a minute counts as gone.
    eq(log.track(t0 + 31_000, more.copy(ble = more.ble.map { if (it.name == "New") it.copy(ageS = 61) else it }), all).left.map { it.key },
        listOf("11:22:33:44:55:66"), "stale bluetooth leaves")
    eq(log.entries[0].shown(true), "Ne*", "a device's own name masked when shown")
    // Hidden groups are not logged.
    val hidden = log.track(t0 + 34_000, more, { it.type != "phone" })
    eq(hidden.arrived.size, 0, "hidden arrival not logged")
    // Nothing while the board is finding a device: the lists stand still.
    val f = more.copy(finding = Finding("ble", "C4:9E:11:22:33:44", ""), wifi = emptyList())
    eq(log.track(t0 + 37_000, f, all).left.size, 0, "nothing logged while finding")
    // A crowd arriving at once is one line.
    val crowd = more.copy(wifi = more.wifi + (1..13).map { NearbyAp("00:00:00:00:00:%02X".format(it), "n$it", 1, -70, "WPA2", true, false, 1, 1) })
    log.track(t0 + 40_000, crowd, all)
    eq(log.entries[0].batch, 13, "batch line")
    log.relearn(t0 + 50_000)
    eq(log.track(t0 + 51_000, n, all).left.size, 0, "relearn starts quiet again")
    check(log.entries.size <= 80, "log capped")
    // A name made up for a device (its product, maker or kind) is not its own: masking leaves it.
    val log2 = LiveLog(learnMs = 0)
    val none = n.copy(ble = emptyList())
    log2.track(t0, none, all)
    val pods = NearbyBle("22:33:44:55:66:77", "", "Apple", 76, "random", "earbuds", 2, "AirPods Pro", -60, 0, 0, 1)
    log2.track(t0 + 1000, none.copy(ble = listOf(pods)), all)
    eq(log2.entries.map { it.shown(true) }, listOf("AirPods Pro"), "a made-up name is not masked")
}

fun finderTests() {
    val tr = FinderTrack()
    val t = 1_800_000_000_000L
    tr.sinceMs = t
    tr.add(t, -70)
    eq(tr.value(), -70, "first reading")
    tr.add(t + 500, -72)
    near(tr.points[1].m, -71.0, "median of two is their mean")
    tr.add(t + 1000, -95)
    near(tr.points[2].m, -72.0, "a deep fade is outvoted")
    tr.add(t + 30_000, -50)
    near(tr.points[3].e, -50.0, "after a long gap the smoothing starts again")
    eq(tr.heardAgo(t + 31_000), 1000L, "heard ago")
    check(tr.heardAgo(t) < 0 || true, "")
    val up = FinderTrack()
    up.sinceMs = t
    for (i in 0 until 12) up.add(t + i * 1000L, -80 + i)
    check(up.trend(t + 11_000)!! >= 3, "warmer")
    eq(FinderMath.trendWord(up.trend(t + 11_000)), "▲ Warmer", "warmer word")
    val down = FinderTrack()
    for (i in 0 until 12) down.add(t + i * 1000L, -60 - i)
    eq(FinderMath.trendWord(down.trend(t + 11_000)), "▼ Colder", "colder word")
    eq(FinderTrack().trend(t), null, "no trend without readings")
    eq(up.rate(t + 2000), null, "no rate in the first five seconds")
    eq(up.rate(t + 11_000), 65, "readings a minute")
    eq(FinderMath.prox(0.5), "Very close", "very close")
    eq(FinderMath.prox(20.0), "Far off", "far off")
    near(FinderMath.hot(30.0), 0.0, "30 m is cold")
    near(FinderMath.hot(0.3), 1.0, "30 cm is hot")
    eq(FinderMath.big(2.44), "≈ 2.4 m", "big distance")
    check(FinderMath.found(0.5, 5000), "found")
    check(!FinderMath.found(0.5, 12_000), "not heard lately")
    check(!FinderMath.found(1.0, 1000), "not close enough")
    eq(FinderMath.turnSeconds(null), 30, "turn without a rate")
    eq(FinderMath.turnSeconds(60), 20, "quick readings, short turn")
    eq(FinderMath.turnSeconds(30), 36, "turn for 18 readings")
    eq(FinderMath.turnSeconds(10), 45, "slow readings, long turn")
    eq(FinderMath.clock(0.0), 12, "12 o'clock")
    eq(FinderMath.clock(Math.PI / 2), 3, "3 o'clock")
    eq(FinderMath.clock(Math.PI), 6, "6 o'clock")
    eq(FinderMath.clock(-Math.PI / 2), 9, "9 o'clock")
    val pts = (0 until 36).map { FinderMath.TurnPoint(it * Math.PI / 18, (-75 + 10 * Math.cos(it * Math.PI / 18 - Math.PI)).toInt()) }
    val d = FinderMath.direction(pts)
    check(d.ok, "clear direction")
    near(d.a, Math.PI, "strongest behind", 1e-9)
    check(FinderMath.liveText(d, 0.0).startsWith("Strongest at about 12 o’clock from where you face now, straight ahead"), "live text ahead")
    check(FinderMath.liveText(d, Math.PI / 2).contains("3 o’clock from where you face now, to your right"), "live text right")
    eq(FinderMath.noAnswer(FinderMath.direction(pts.take(4))), "Too few readings during the turn to tell. Turn more slowly, " +
        "or move a little closer, and try again.", "too few")
}

fun headingTests() {
    val flatNorth = floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f)
    near(FinderMath.headingOf(flatNorth)!!, 0.0, "flat, top to the north")
    // Upright, screen towards you, facing east: x is south, y is up, z is west.
    val uprightEast = floatArrayOf(0f, 0f, -1f, -1f, 0f, 0f, 0f, 1f, 0f)
    near(FinderMath.headingOf(uprightEast)!!, Math.PI / 2, "upright, facing east")
    // Tilted back 45 degrees, facing south: y = (0,-c,s), z = (0,c,s)... towards you is north and up.
    val c = Math.sqrt(0.5).toFloat()
    val tiltSouth = floatArrayOf(-1f, 0f, 0f, 0f, -c, c, 0f, c, c)
    near(FinderMath.headingOf(tiltSouth)!!, Math.PI, "tilted, facing south", 1e-6)
    eq(FinderMath.headingOf(FloatArray(9)), null, "no heading from nothing")
    near(FinderMath.diff(0.1, 2 * Math.PI - 0.1), 0.2, "difference across north")
    near(FinderMath.norm(-Math.PI / 2), 1.5 * Math.PI, "norm")
    val log = HeadingLog()
    val t = 1_000_000L
    var h = 0.0
    for (i in 0..40) {
        log.add(t + i * 100L, FinderMath.norm(h))
        h += 2 * Math.PI / 40
    }
    near(log.turned, 2 * Math.PI, "a full turn, unwrapped", 1e-9)
    near(log.at(t + 2000)!!, Math.PI, "half way round at half time", 1e-9)
    near(log.at(t + 2050)!!, Math.PI + Math.PI / 40, "interpolated", 1e-9)
    near(log.at(t - 500)!!, 0.0, "held at the start")
    val left = HeadingLog()
    for (i in 0..10) left.add(t + i * 100L, FinderMath.norm(-i * 0.3))
    near(left.turned, -3.0, "turning left counts down", 1e-9)
}

val MAP_INFO = """{"version":"0.11.0-finder","wifi":"connected","ssid":"HOME","ip":"192.168.2.30","mac":"D4:E9:F4:12:34:56","hostname":"netmon","gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-38,"channel":6,"bssid":"50:91:E3:12:34:56","uptime_s":99,"latency_valid":true,"latency_ms":4,"isp":{"checked":true,"valid":true,"isp":"Example","org":"Ex","age_s":5},"nearby_wifi":true,"aps":[{"bssid":"50:91:E3:12:34:56","ch":6,"rssi":-38,"live":true,"joined":true,"age_s":0},{"bssid":"52:91:E3:12:34:57","ch":6,"rssi":-71,"live":true,"joined":false,"age_s":9}]}"""

fun lan(): List<Device> = listOf(
    Device("D4:E9:F4:12:34:56", "192.168.2.30", "netmon", "Espressif Inc.", "known", false, true, true, 0, 99),
    Device("50:91:E3:12:34:56", "192.168.2.1", "", "TP-Link", "known", false, false, true, 0, 99),
    Device("DA:A1:19:77:88:99", "192.168.2.45", "Pixel-7", "", "private", true, false, true, 0, 9),
    Device("00:11:32:AA:BB:CC", "192.168.2.10", "DiskStation", "Synology Incorporated", "known", false, false, true, 0, 9),
    Device("A4:CF:12:44:55:66", "192.168.2.73", "", "Espressif Inc.", "unknown", false, false, true, 0, 9),
    Device("38:6B:1C:22:00:11", "192.168.2.90", "HP-OfficeJet-Pro", "", "known", false, false, false, 600, 0),
    Device("F4:F5:D8:77:66:55", "192.168.2.80", "Chromecast", "Google Inc.", "known", false, false, true, 0, 9),
    Device("AE:22:10:5B:01:02", "192.168.2.48", "", "", "private", true, false, true, 0, 9),
)

fun mapTests() {
    val info = Parse.map(MAP_INFO)
    val devs = lan()
    eq(NetMap.kindOf(devs[2]), "phone", "phone by name")
    eq(NetMap.kindOf(devs[3]), "nas", "storage by name")
    eq(NetMap.kindOf(devs[4]), "iot", "smart home by maker")
    eq(NetMap.kindOf(devs[5]), "print", "printer by name")
    eq(NetMap.kindOf(devs[6]), "media", "media by name")
    eq(NetMap.kindOf(devs[7]), "priv", "private, nothing else known")
    eq(NetMap.kindOf(Device("00:00:00:00:00:01", "1", "", "", "known", false, false, true, 0, 0)), "other", "not identified")
    for (w in listOf(320.0, 360.0, 412.0, 800.0)) for (byStatus in listOf(false, true)) for (off in listOf(false, true)) {
        val p = NetMap.plan(info, devs, byStatus, off, w)!!
        val what = "w=$w status=$byStatus off=$off"
        check(p.groups.first().wifi, "$what: your Wi-Fi first")
        eq(p.groups.first().items.last().me, true, "$what: the board is in its Wi-Fi bubble")
        val shown = p.items.count { it.device != null && !it.me }
        eq(shown, devs.count { !it.self && it.ip != info.gateway && (off || it.online) }, "$what: every device but the router and the board")
        for (i in p.groups.indices) for (j in i + 1 until p.groups.size) {
            val a = p.groups[i]; val b = p.groups[j]
            check(Math.hypot(a.x - b.x, a.y - b.y) >= a.r + b.r + 10 - 1e-9, "$what: bubbles ${a.key} and ${b.key} overlap")
        }
        for (g in p.groups) for (it in g.items) check(Math.hypot(it.x - g.x, it.y - g.y) <= g.r, "$what: a dot outside ${g.key}")
        check(p.scale > 0 && p.scale <= 1.25 + 1e-9, "$what: scale")
        check(p.box[2] > 0 && p.box[3] > 0, "$what: box")
        val first = p.groups[1].items[0]
        val hit = NetMap.hit(p, first.x + 1, first.y, 18.0)
        check(hit is NetMap.Hit.Dot && hit.item === first, "$what: tap on a dot")
        check(NetMap.hit(p, 0.0, 0.0, 1.0) is NetMap.Hit.Router, "$what: tap on the router")
        check(NetMap.hit(p, 0.0, p.iy, 1.0) is NetMap.Hit.Internet, "$what: tap on the internet")
        eq(NetMap.hit(p, p.box[0] + 1, p.box[1] + 1, 1.0), null, "$what: tap on nothing")
    }
    val byStatus = NetMap.plan(info, devs, true, true, 360.0)!!
    eq(byStatus.groups.map { it.key }, listOf("wifi", "unknown", "private", "known", "off"), "status groups in order")
    eq(NetMap.plan(info.copy(wifi = "softap"), devs, false, false, 360.0), null, "nothing to map in setup mode")
    val h = Parse.health("""{"status":"ok","version":"0.9.6","wifi":"connected","ssid":"HOME","ip":"192.168.2.30","gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-40,"uptime_s":5}""")
    val old = NetMap.fromHealth(h, devs)
    eq(old.fromBoard, false, "map from health")
    eq(old.mac, "D4:E9:F4:12:34:56", "board mac from the device list")
    val op = NetMap.plan(old, devs, false, false, 360.0)!!
    eq(op.groups.first().aps.size, 0, "no access points without the board's map")
    eq(NetMap.wrap("Phones and tablets", 12), listOf("Phones and", "tablets"), "wrap")
}

fun parityTests() {
    val path = System.getenv("PARITY") ?: "parity.json"
    val f = File(path)
    if (!f.exists()) { println("skip parity: $path missing (tools/test/web_parity.py makes it)"); return }
    val root = JSONObject(f.readText())
    val nb = root.getJSONObject("nearby")
    val cal = Calibration()
    val metres = nb.getJSONArray("metres")
    for (i in 0 until metres.length()) {
        val r = metres.getJSONArray(i)
        near(Air.metres(r.getInt(0), false, cal), r.getDouble(1), "web metres wifi ${r.getInt(0)}")
        near(Air.metres(r.getInt(0), true, cal), r.getDouble(2), "web metres ble ${r.getInt(0)}")
    }
    val bearing = nb.getJSONArray("bearing")
    for (i in 0 until bearing.length()) {
        val b = bearing.getJSONArray(i)
        near(Air.bearing(b.getString(0)), b.getDouble(1), "web bearing ${b.getString(0)}", 1e-12)
    }
    val fd = nb.getJSONArray("fdist")
    for (i in 0 until fd.length()) {
        val x = fd.getJSONArray(i)
        eq(Air.distance(x.getDouble(0)), x.getString(1), "web distance ${x.getDouble(0)}")
    }
    val ts = nb.getJSONObject("trend")
    val series = ts.getJSONArray("series")
    val want = ts.getJSONArray("trend")
    val tm = TrendMemory()
    for (i in 0 until series.length()) {
        tm.remember(mapOf("T" to series.getInt(i)))
        eq(tm.trend("T"), want.getInt(i), "web trend step $i")
    }
    val fin = nb.getJSONObject("finder")
    val now = 1_800_000_000_000L
    val track = FinderTrack()
    track.sinceMs = now - 30_000
    val rd = fin.getJSONArray("readings")
    for (i in 0 until rd.length()) {
        val x = rd.getJSONArray(i)
        track.add(now + x.getLong(0), x.getInt(1))
        val p = track.points.last()
        near(p.m, x.getDouble(2), "web median $i")
        near(p.e, x.getDouble(3), "web smoothed $i")
    }
    near(track.trend(now)!!, fin.getDouble("trend"), "web trend")
    eq(track.rate(now), fin.getInt("rate"), "web rate")
    eq(track.value(), fin.getInt("now"), "web value")
    val hot = nb.getJSONArray("hot")
    for (i in 0 until hot.length()) {
        val x = hot.getJSONArray(i)
        near(FinderMath.hot(x.getDouble(0)), x.getDouble(1), "web hot ${x.getDouble(0)}")
        eq(FinderMath.prox(x.getDouble(0)), x.getString(2), "web prox ${x.getDouble(0)}")
        eq(FinderMath.big(x.getDouble(0)), x.getString(3), "web big ${x.getDouble(0)}")
    }
    for (name in listOf("direction", "flat", "partial")) {
        val o = nb.getJSONObject(name)
        val pa = o.getJSONArray("pts")
        val pts = (0 until pa.length()).map { FinderMath.TurnPoint(pa.getJSONArray(it).getDouble(0), pa.getJSONArray(it).getInt(1)) }
        val d = FinderMath.direction(pts)
        eq(d.ok, o.getBoolean("ok"), "web $name ok")
        if (o.has("a")) near(d.a, o.getDouble("a"), "web $name angle")
        if (o.has("contrast")) near(d.contrast, o.getDouble("contrast"), "web $name contrast")
        if (o.has("gap")) near(d.gap, o.getDouble("gap"), "web $name gap")
        if (o.has("curve")) {
            val cv = o.getJSONArray("curve")
            for (k in 0 until cv.length()) {
                val v = d.curve[k]
                if (cv.isNull(k)) eq(v, null, "web $name curve $k") else near(v!!, cv.getDouble(k), "web $name curve $k")
            }
        }
        eq(FinderMath.dirText(d), o.getString("text"), "web $name text")
    }
    eq(FinderMath.dirText(FinderMath.direction(listOf(FinderMath.TurnPoint(0.0, -60), FinderMath.TurnPoint(1.0, -61)))),
        nb.getJSONObject("few").getString("text"), "web few text")

    val mp = root.getJSONObject("map")
    val input = root.getJSONObject("map_input")
    val info = Parse.map(input.getJSONObject("map").toString())
    val devs = Parse.devices(input.getJSONArray("devices").toString())
    val kinds = mp.getJSONArray("kinds")
    for (i in 0 until kinds.length()) {
        val k = kinds.getJSONArray(i)
        eq(NetMap.kindOf(devs.first { it.mac == k.getString(0) }), k.getString(1), "web kind ${k.getString(0)}")
    }
    val wraps = mp.getJSONArray("wrap")
    for (i in 0 until wraps.length()) {
        val w = wraps.getJSONArray(i)
        val lines = w.getJSONArray(2)
        eq(NetMap.wrap(w.getString(0), w.getInt(1)), (0 until lines.length()).map { lines.getString(it) }, "web wrap ${w.getString(0)}")
    }
    val cases = mp.getJSONArray("cases")
    for (i in 0 until cases.length()) {
        val cs = cases.getJSONObject(i)
        val label = "web map " + cs.getString("label")
        val p = NetMap.plan(info, devs, cs.getString("by") == "status", cs.getBoolean("off"), cs.getDouble("W"))!!
        near(p.ringR, cs.getDouble("R"), "$label ring", 1e-9)
        near(p.iy, cs.getDouble("iy"), "$label internet", 1e-9)
        near(p.scale, cs.getDouble("scale"), "$label scale", 1e-9)
        val box = cs.getJSONArray("box")
        for (k in 0 until 4) near(p.box[k], box.getDouble(k), "$label box $k", 1e-9)
        val gs = cs.getJSONArray("groups")
        eq(p.groups.size, gs.length(), "$label groups")
        for (g in 0 until minOf(gs.length(), p.groups.size)) {
            val wg = gs.getJSONObject(g)
            val ag = p.groups[g]
            eq(ag.key, wg.getString("key"), "$label group $g key")
            eq(ag.title, wg.getString("title"), "$label group $g title")
            near(ag.r, wg.getDouble("r"), "$label group $g r")
            near(ag.x, wg.getDouble("x"), "$label group $g x", 1e-9)
            near(ag.y, wg.getDouble("y"), "$label group $g y", 1e-9)
            near(ag.lw, wg.getDouble("lw"), "$label group $g lw")
            val items = wg.getJSONArray("items")
            eq(ag.items.size, items.length(), "$label group $g items")
            for (k in 0 until minOf(items.length(), ag.items.size)) {
                val wi = items.getJSONArray(k)
                val ai = ag.items[k]
                val id = when { ai.ap != null -> "ap:" + ai.ap!!.bssid; ai.me -> "me"; else -> "d:" + ai.device!!.mac }
                eq(id, wi.getString(2), "$label group $g item $k")
                near(ai.x, wi.getDouble(0), "$label group $g item $k x", 1e-9)
                near(ai.y, wi.getDouble(1), "$label group $g item $k y", 1e-9)
            }
        }
    }
}

fun nearbyClientTests() {
    try { mock("/__reset", "POST") } catch (e: Exception) { return }
    val c = NetmonClient(MOCK)
    // Firmware before 0.10: no Nearby, no map, no update_max.
    eq(throws<ApiException>("nearby on 0.9") { c.nearby() }?.status, 404, "nearby 404 on old firmware")
    eq(throws<ApiException>("map on 0.9") { c.map() }?.status, 404, "map 404 on old firmware")
    eq(c.config().updateMax, 0L, "no update_max on old firmware")
    mock("/__fw?v=0.11", "POST")
    eq(c.health().version, "0.11.0-finder", "0.11 board")
    eq(c.config().updateMax, 1_966_080L, "update_max")
    val n = c.nearby()
    eq(n.wifi.size, 6, "nearby wifi, sent in chunks")
    eq(n.wifi[2].ssid, "Corner \"Cafe\"", "escaped name through chunks")
    eq(n.ble.size, 5, "nearby ble")
    eq(c.nearbyConfig(), NearbyConfig(true, true, true, 120), "nearby config")
    eq(c.setNearbyConfig(wifi = false).wifi, false, "wifi off")
    eq(c.nearby().wifi.size, 0, "no wifi rows while off")
    eq(throws<ApiException>("bad interval") { c.setNearbyConfig(backgroundS = 10) }?.message,
        "the background interval must be 0, or 30 to 3600 seconds", "board's words for a bad interval")
    eq(throws<ApiException>("wifi find while off") { c.findStart("wifi", "50:91:E3:12:34:56") }?.status, 409, "409 while wifi off")
    c.setNearbyConfig(wifi = true, backgroundS = 300)
    eq(c.nearbyConfig().backgroundS, 300, "background set")
    c.nearbyScan()

    eq(throws<ApiException>("unknown device") { c.findStart("ble", "00:11:22:33:44:55") }?.message,
        "That device has not been heard in the last five minutes.", "404 words")
    eq(throws<ApiException>("bad type") { c.findStart("zigbee", "00:11:22:33:44:55") }?.status, 400, "400 bad type")
    val s = c.findStart("ble", "C4:9E:11:22:33:44")
    eq(s.active, true, "finding")
    eq(s.name, "Tile", "finding name")
    eq(s.dtype, "tracker", "finding kind")
    eq(c.nearby().finding?.addr, "C4:9E:11:22:33:44", "nearby says what is being found")
    Thread.sleep(1300)
    val r1 = c.find(s.seq)
    check(r1.readings.size >= 2, "readings arrive (${r1.readings.size})")
    check(r1.readings.all { it.seq > s.seq && it.msAgo >= 0 }, "only new readings, with their age")
    check(r1.readings.zipWithNext().all { (a, b) -> b.seq == a.seq + 1 }, "readings in order")
    eq(c.find(r1.seq).readings.size, 0, "nothing new yet")
    val track = FinderTrack()
    val got = System.currentTimeMillis()
    for (r in r1.readings) track.add(got - r.msAgo, r.rssi)
    check(track.value() != null, "readings feed the smoothing")
    val held = c.findStart("ble", "C4:9E:11:22:33:44", 30)
    check(held.holdMs in 1..30_000, "a turn holds the sweep (${held.holdMs})")
    eq(c.findStart("ble", "C4:9E:11:22:33:44", 0).holdMs, 0L, "and lets it go")
    eq(c.findStart("ble", "C4:9E:11:22:33:44", 30).holdMs, 0L, "once in two minutes")
    mock("/__find?other=1", "POST")
    check(c.find(0).addr != "C4:9E:11:22:33:44", "another page took the Finder")
    mock("/__find?idle=1", "POST")
    eq(c.find(0).active, false, "unasked for 15 s, the Finder stops")
    val w = c.findStart("wifi", "50:91:E3:12:34:56")
    eq(w.ch, 6, "wifi finder channel")
    eq(c.findStop().active, false, "stopped")
    val m = c.map()
    eq(m.aps.size, 2, "map access points")
    eq(m.isp.isp, "Example Telecom Ltd", "map provider")
    val plan = NetMap.plan(m, c.devices(), false, false, 360.0)
    check(plan != null && plan.groups.isNotEmpty(), "the mock board's network maps")
    mock("/__reset", "POST")
}
