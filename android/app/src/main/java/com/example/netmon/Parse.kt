package com.example.netmon

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * JSON to model. Every field is optional on the way in: an older firmware that
 * lacks a field still produces a usable object. Only text that is not JSON at
 * all, or the wrong shape, is an error.
 */
object Parse {

    fun health(text: String): Health = obj(text).run {
        Health(
            version = str("version"),
            wifi = str("wifi"),
            ssid = str("ssid"),
            ip = str("ip"),
            gateway = str("gateway"),
            subnet = str("subnet"),
            rssi = int("rssi"),
            uptimeS = long("uptime_s"),
            freeHeap = long("free_heap"),
            sweepable = bool("sweepable"),
            devices = int("devices"),
            scanPasses = long("scan_passes"),
            scanRemaining = long("scan_remaining"),
            lastPassMs = long("last_pass_ms"),
            passSeen = int("pass_seen"),
            arpCache = int("arp_cache"),
            latencyValid = bool("latency_valid"),
            latencyMs = long("latency_ms"),
            latencyAgeS = long("latency_age_s"),
            dhcpPackets = long("dhcp_packets"),
            events = int("events"),
            baselineOpen = bool("baseline_open"),
            baselineAnchored = bool("baseline_anchored"),
            baselineClosesInS = long("baseline_closes_in_s"),
            namesKnown = int("names_known"),
        )
    }

    fun devices(text: String): List<Device> = objects(text).map { o ->
        Device(
            mac = o.str("mac").uppercase(),
            ip = o.str("ip"),
            hostname = o.str("hostname"),
            vendor = o.str("vendor"),
            status = o.str("status", "unknown"),
            randomised = o.bool("randomised"),
            self = o.bool("self"),
            online = o.bool("online"),
            lastSeenS = o.long("last_seen_s"),
            upS = o.long("up_s"),
        )
    }

    fun events(text: String): List<BoardEvent> = objects(text).map { o ->
        BoardEvent(
            atS = o.long("at_s"),
            type = o.str("type"),
            mac = o.str("mac").uppercase(),
            ip = o.str("ip"),
            text = o.str("text"),
        )
    }

    fun latency(text: String): Latency = obj(text).run {
        Latency(
            valid = bool("valid"),
            rttMs = long("rtt_ms"),
            checkedS = long("checked_s"),
            ageS = long("age_s"),
            failures = long("failures"),
            method = str("method"),
        )
    }

    fun isp(text: String): Isp = obj(text).run {
        Isp(
            valid = bool("valid"),
            ip = str("ip"),
            isp = str("isp"),
            org = str("org"),
            asn = str("asn"),
            city = str("city"),
            region = str("region"),
            country = str("country"),
            timezone = str("timezone"),
            error = str("error"),
            rttMs = long("rtt_ms"),
            checkedAgeS = long("checked_age_s"),
            everChecked = bool("ever_checked"),
            gateway = str("gateway"),
            gatewayMac = str("gateway_mac").uppercase(),
            gatewayVendor = str("gateway_vendor"),
        )
    }

    fun config(text: String): BoardConfig = obj(text).run {
        val a = optJSONObject("active")
        BoardConfig(
            version = str("version"),
            ssid = str("ssid"),
            hasPassword = bool("has_password"),
            networks = int("networks"),
            useDhcp = bool("use_dhcp", true),
            ip = str("ip"),
            mask = str("mask"),
            gw = str("gw"),
            dns = str("dns"),
            scanIntervalS = int("scan_interval_s", 60),
            probeIntervalS = int("probe_interval_s", 30),
            offlineAfterS = int("offline_after_s", 180),
            learningWindowS = int("learning_window_s", 600),
            updateMax = long("update_max"),
            active = a?.let {
                ActiveLink(
                    ip = it.str("ip"),
                    mask = it.str("mask"),
                    gw = it.str("gw"),
                    dns = it.str("dns"),
                    mac = it.str("mac").uppercase(),
                    ssid = it.str("ssid"),
                    rssi = it.int("rssi"),
                    source = it.str("source"),
                )
            },
        )
    }

    fun networks(text: String): List<SavedNetwork> = objects(text).map { o ->
        SavedNetwork(
            order = o.int("order"),
            ssid = o.str("ssid"),
            hasPassword = o.bool("has_password"),
            active = o.bool("active"),
            boot = o.str("boot", "not_tried"),
            bootMs = o.long("boot_ms"),
            reason = o.int("reason"),
        )
    }

    fun dhcp(text: String): DhcpStatus = obj(text).run {
        val l = optJSONObject("local")
        DhcpStatus(
            listener = str("listener"),
            received = long("received"),
            capturePackets = long("capture_packets"),
            valid = bool("valid"),
            clientMac = str("client_mac").uppercase(),
            assignedIp = str("assigned_ip"),
            serverIp = str("server_ip"),
            leaseS = long("lease_s"),
            messageType = str("message_type"),
            hostname = str("hostname"),
            lastSeenS = long("last_seen_s"),
            local = l?.let {
                DhcpLocal(
                    ip = it.str("ip"),
                    mask = it.str("mask"),
                    gateway = it.str("gateway"),
                    dns = it.str("dns"),
                    mode = it.str("mode"),
                    hostname = it.str("hostname"),
                )
            },
        )
    }

    fun scan(text: String): List<WifiNetwork> =
        objects(text).map { WifiNetwork(it.str("ssid"), it.int("rssi")) }.filter { it.ssid.isNotEmpty() }

    /** The {"error": "..."} body the board sends with a 4xx or 5xx, if there is one. */
    fun errorText(text: String): String? = try {
        val o = JSONObject(text)
        o.str("error").takeIf { it.isNotBlank() }
    } catch (e: JSONException) {
        null
    }

    fun configBody(u: ConfigUpdate): String {
        val o = JSONObject()
        o.put("ssid", u.ssid)
        o.put("pass", u.pass)
        o.put("use_dhcp", u.useDhcp)
        if (!u.useDhcp) {
            o.put("ip", u.ip)
            o.put("mask", u.mask)
            o.put("gw", u.gw)
            o.put("dns", u.dns)
        }
        o.put("scan_interval_s", u.scanIntervalS)
        o.put("probe_interval_s", u.probeIntervalS)
        o.put("offline_after_s", u.offlineAfterS)
        o.put("learning_window_s", u.learningWindowS)
        return o.toString()
    }

    fun forgetBody(ssid: String): String = JSONObject().put("ssid", ssid).toString()

    // --- Nearby, Finder and Map --------------------------------------------

    fun nearby(text: String): Nearby = obj(text).run {
        val f = optJSONObject("finding")
        Nearby(
            version = str("version"),
            onLan = bool("on_lan", true),
            sweeping = bool("sweeping"),
            backgroundS = int("background_s"),
            finding = f?.let { Finding(it.str("type"), it.str("addr").uppercase(), it.str("name")) },
            wifiScan = airScan(optJSONObject("wifi_scan"), "scans"),
            bleScan = airScan(optJSONObject("ble_scan"), "bursts"),
            wifi = array(this, "wifi").map { o ->
                NearbyAp(
                    bssid = o.str("bssid").uppercase(),
                    ssid = o.str("ssid"),
                    ch = o.int("ch"),
                    rssi = o.int("rssi", -100),
                    security = o.str("security"),
                    live = o.bool("live"),
                    joined = o.bool("joined"),
                    ageS = o.long("age_s"),
                    knownS = o.long("known_s"),
                )
            }.filter { it.bssid.isNotEmpty() },
            ble = array(this, "ble").map { o ->
                NearbyBle(
                    addr = o.str("addr").uppercase(),
                    name = o.str("name"),
                    vendor = o.str("vendor"),
                    company = o.int("company", -1),
                    kind = o.str("kind", "private"),
                    type = o.str("type", "unknown").ifEmpty { "unknown" },
                    sure = o.int("sure"),
                    model = o.str("model"),
                    rssi = o.int("rssi", -100),
                    ageS = o.long("age_s"),
                    knownS = o.long("known_s"),
                    seen = o.long("seen"),
                )
            }.filter { it.addr.isNotEmpty() },
        )
    }

    private fun airScan(o: JSONObject?, countKey: String): AirScan =
        if (o == null) AirScan(false, "off", 0, 0, -1, 0, 0)
        else AirScan(
            enabled = o.bool("enabled"),
            state = o.str("state", "idle"),
            count = o.long(countKey),
            failures = o.long("failures"),
            ageS = o.long("age_s", -1),
            tookMs = o.long("took_ms"),
            dropped = o.long("dropped"),
        )

    fun nearbyConfig(text: String): NearbyConfig = obj(text).run {
        NearbyConfig(
            wifi = bool("wifi", true),
            ble = bool("ble", true),
            bleReady = bool("ble_ready", true),
            backgroundS = int("background_s", 120),
        )
    }

    /** Only the fields given are sent: the board leaves the others as they are. */
    fun nearbyConfigBody(wifi: Boolean? = null, ble: Boolean? = null, backgroundS: Int? = null): String {
        val o = JSONObject()
        if (wifi != null) o.put("wifi", wifi)
        if (ble != null) o.put("ble", ble)
        if (backgroundS != null) o.put("background_s", backgroundS)
        return o.toString()
    }

    fun find(text: String): FindStatus = obj(text).run {
        val rd = ArrayList<FindReading>()
        val a = optJSONArray("readings")
        if (a != null) {
            for (i in 0 until a.length()) {
                val r = a.optJSONArray(i) ?: continue
                if (r.length() < 3) continue
                rd.add(FindReading(r.optLong(0), r.optLong(1), r.optInt(2, -100)))
            }
        }
        FindStatus(
            active = bool("active"),
            type = str("type"),
            addr = str("addr").uppercase(),
            name = str("name"),
            kind = str("kind"),
            dtype = str("dtype", "unknown").ifEmpty { "unknown" },
            model = str("model"),
            vendor = str("vendor"),
            ch = int("ch"),
            security = str("security"),
            state = str("state"),
            why = str("why"),
            forS = long("for_s"),
            heardMs = long("heard_ms", -1),
            holdMs = long("hold_ms"),
            seq = long("seq"),
            readings = rd,
        )
    }

    /** Starts finding a device, keeps it going, or with [holdS] asks the sweep to wait for a turn. */
    fun findBody(type: String, addr: String, holdS: Int? = null): String {
        val o = JSONObject()
        o.put("type", type)
        o.put("addr", addr)
        if (holdS != null) o.put("hold_s", holdS)
        return o.toString()
    }

    const val FIND_STOP_BODY = "{\"stop\":true}"

    fun map(text: String): MapInfo = obj(text).run {
        val i = optJSONObject("isp")
        MapInfo(
            version = str("version"),
            wifi = str("wifi"),
            ssid = str("ssid"),
            ip = str("ip"),
            mac = str("mac").uppercase(),
            hostname = str("hostname"),
            gateway = str("gateway"),
            subnet = str("subnet"),
            rssi = int("rssi"),
            channel = int("channel"),
            bssid = str("bssid").uppercase(),
            uptimeS = long("uptime_s"),
            latencyValid = bool("latency_valid"),
            latencyMs = long("latency_ms"),
            isp = if (i == null) MapIsp(false, false, "", "", 0)
            else MapIsp(i.bool("checked"), i.bool("valid"), i.str("isp"), i.str("org"), i.long("age_s")),
            nearbyWifi = bool("nearby_wifi"),
            aps = array(this, "aps").map { o ->
                MapAp(o.str("bssid").uppercase(), o.int("ch"), o.int("rssi"), o.bool("live"), o.bool("joined"), o.long("age_s"))
            }.filter { it.bssid.isNotEmpty() },
        )
    }

    // --- helpers -----------------------------------------------------------

    private fun array(o: JSONObject, name: String): List<JSONObject> {
        val a = o.optJSONArray(name) ?: return emptyList()
        val out = ArrayList<JSONObject>(a.length())
        for (i in 0 until a.length()) out.add(a.optJSONObject(i) ?: continue)
        return out
    }

    private fun obj(text: String): JSONObject = try {
        JSONObject(text)
    } catch (e: JSONException) {
        throw ApiException(ApiException.Kind.BadData, "The board sent a reply this app cannot read.")
    }

    private fun objects(text: String): List<JSONObject> {
        val a = try {
            JSONArray(text)
        } catch (e: JSONException) {
            throw ApiException(ApiException.Kind.BadData, "The board sent a reply this app cannot read.")
        }
        val out = ArrayList<JSONObject>(a.length())
        for (i in 0 until a.length()) {
            val o = a.optJSONObject(i) ?: continue
            out.add(o)
        }
        return out
    }
}

// Missing and JSON null both read as the default. Android's optString turns a
// JSON null into the four letters "null", which is never what a screen wants.
internal fun JSONObject.str(name: String, def: String = ""): String =
    if (isNull(name)) def else optString(name, def)

internal fun JSONObject.int(name: String, def: Int = 0): Int =
    if (isNull(name)) def else optInt(name, def)

internal fun JSONObject.long(name: String, def: Long = 0L): Long =
    if (isNull(name)) def else optLong(name, def)

internal fun JSONObject.bool(name: String, def: Boolean = false): Boolean =
    if (isNull(name)) def else optBoolean(name, def)
