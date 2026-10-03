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

    // --- helpers -----------------------------------------------------------

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
