package com.example.netmon

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.MalformedURLException
import java.net.SocketTimeoutException
import java.net.URL

class ApiException(val kind: Kind, message: String, val status: Int = 0, val retryS: Int = 0) : Exception(message) {
    /**
     * NotPaired: the board was reached over Bluetooth but this phone is not
     * (or no longer) paired with it, so it may not ask anything.
     * LoginRequired (firmware 0.13): the board wants a session this request
     * did not carry, or one it has ended. Unauthorized stays what it was: a
     * wrong password, the update one or, signing in, the login one.
     */
    enum class Kind { Unreachable, Timeout, Unauthorized, Http, BadData, NotPaired, LoginRequired }
}

/**
 * Talks to one board, over plain HTTP or over the Bluetooth link. Blocking:
 * call it from a worker thread.
 *
 * Over Wi-Fi the board runs the Arduino WebServer, which serves one client at
 * a time and closes every connection after the answer, so every request asks
 * for "Connection: close" and nothing is ever pooled or pipelined.
 *
 * Over Bluetooth ([base] is "ble://" and the board's Bluetooth address) the
 * same request goes as frames over the link (LinkCodec) and the answer comes
 * back as the same status and body, so everything above this class works the
 * same either way. The one exception is the firmware upload, which is Wi-Fi
 * only.
 */
class NetmonClient(address: String) {

    val base: String = normalize(address)

    /** True when this client goes over Bluetooth. */
    val viaBluetooth: Boolean get() = LinkCodec.isBle(base)

    var connectTimeoutMs = 4000
    var readTimeoutMs = 8000

    /**
     * The session every request carries from firmware 0.13: "Authorization:
     * Bearer", over Wi-Fi and over the Bluetooth link alike. Only ever set for
     * the board the app uses, never for an address being tried.
     */
    var token: String? = null

    /**
     * Asked for a new session when the board says to sign in, with the
     * password saved on this phone; the token, or null when there is none to
     * be had. The request is then made once more.
     */
    var renewSession: ((NetmonClient) -> String?)? = null

    fun health(): Health = Parse.health(get("/api/health"))

    /**
     * What is at this address, when it is a netmon board, otherwise null.
     * Asks nothing that needs signing in, and sends no session: from 0.13
     * GET /api/auth answers anyone; before that /api/health did.
     */
    fun identify(): BoardId? {
        try {
            return Parse.boardId(get("/api/auth", readTimeoutMs, signed = false))
        } catch (e: ApiException) {
            // A board from before 0.13 has no /api/auth; anything else that
            // does not answer it is no netmon board either.
            if (e.kind != ApiException.Kind.Http || e.status != 404) return null
        }
        return try {
            val text = get("/api/health", readTimeoutMs, signed = false)
            if (!Subnet.looksLikeNetmon(text)) return null
            val h = Parse.health(text)
            BoardId(h.version, h.mac, "netmon", login = false, signedIn = false)
        } catch (e: ApiException) {
            null
        }
    }

    // --- Signing in (firmware 0.13) ---------------------------------------------

    /** Who the board is and, with [token], whether that session is still good. */
    fun auth(): AuthInfo = Parse.auth(get("/api/auth"))

    /**
     * Signs in. [remember] asks for a session that lasts 30 days and outlives
     * restarts. The phone's clock goes along, which the board takes when it
     * has none. A wrong password is ApiException(Unauthorized); too many,
     * Http 429 with how long to wait in [ApiException.retryS].
     */
    fun login(password: String, remember: Boolean, unix: Long = System.currentTimeMillis() / 1000): LoginReply {
        val body = Parse.loginBody(password, remember, unix).toByteArray(Charsets.UTF_8)
        val text = try {
            post("/api/login", body, "application/json", signed = false)
        } catch (e: ApiException) {
            // Here a 401 can only mean the password. Some HttpURLConnections
            // drop a streamed request's 401 body, and with it the board's words.
            if (e.kind == ApiException.Kind.Unauthorized && e.message == DEFAULT_401) {
                throw ApiException(ApiException.Kind.Unauthorized, "That password is not right.", e.status)
            }
            throw e
        }
        return Parse.loginReply(text)
    }

    /** Ends this session on the board. */
    fun logout() {
        post("/api/logout", ByteArray(0), null)
    }
    fun devices(): List<Device> = Parse.devices(get("/api/devices"))
    fun events(): List<BoardEvent> = Parse.events(get("/api/events"))
    fun latency(): Latency = Parse.latency(get("/api/latency"))
    fun config(): BoardConfig = Parse.config(get("/api/config"))
    fun networks(): List<SavedNetwork> = Parse.networks(get("/api/networks"))
    fun dhcp(): DhcpStatus = Parse.dhcp(get("/api/dhcp"))

    /** The board asks an outside lookup service, which can take several seconds. */
    fun isp(force: Boolean): Isp = Parse.isp(get(if (force) "/api/isp?force=1" else "/api/isp", 16000))

    /** Blocks the board for a couple of seconds while its radio scans. */
    fun scan(): List<WifiNetwork> = Parse.scan(get("/api/scan", 20000))

    fun saveConfig(update: ConfigUpdate) {
        post("/api/config", Parse.configBody(update).toByteArray(Charsets.UTF_8), "application/json")
    }

    fun forget(ssid: String) {
        post("/api/networks/forget", Parse.forgetBody(ssid).toByteArray(Charsets.UTF_8), "application/json")
    }

    fun reboot() {
        post("/api/reboot", ByteArray(0), null)
    }

    // --- Nearby, Finder and Map (firmware 0.10 and 0.11) ------------------------

    /** Both radios' tables. Reading it counts as watching: the board scans quickly for the next 20 s. */
    fun nearby(): Nearby = Parse.nearby(get("/api/nearby", 12000))

    /** Asks for a scan of both radios as soon as the board's own work leaves the radio free. */
    fun nearbyScan() {
        post("/api/nearby/scan", ByteArray(0), null)
    }

    fun nearbyConfig(): NearbyConfig = Parse.nearbyConfig(get("/api/nearby/config"))

    fun setNearbyConfig(wifi: Boolean? = null, ble: Boolean? = null, backgroundS: Int? = null): NearbyConfig =
        Parse.nearbyConfig(post("/api/nearby/config",
            Parse.nearbyConfigBody(wifi, ble, backgroundS).toByteArray(Charsets.UTF_8), "application/json"))

    /** The Finder's state and the readings numbered after [after]. Asking keeps the Finder going. */
    fun find(after: Long): FindStatus = Parse.find(get("/api/nearby/find?after=$after"))

    /** Starts finding a device, or keeps finding it; [holdS] asks the sweep to wait for a turn (0 ends that). */
    fun findStart(type: String, addr: String, holdS: Int? = null): FindStatus =
        Parse.find(post("/api/nearby/find", Parse.findBody(type, addr, holdS).toByteArray(Charsets.UTF_8), "application/json"))

    fun findStop(): FindStatus =
        Parse.find(post("/api/nearby/find", Parse.FIND_STOP_BODY.toByteArray(Charsets.UTF_8), "application/json"))

    fun map(): MapInfo = Parse.map(get("/api/map"))

    // --- Bluetooth link (firmware 0.12) ------------------------------------------

    fun ble(): BleStatus = Parse.ble(get("/api/ble"))

    /** Switches the board's Bluetooth link on or off. */
    fun setBle(enabled: Boolean): BleStatus =
        Parse.ble(post("/api/ble", Parse.bleBody(enabled).toByteArray(Charsets.UTF_8), "application/json"))

    /** Opens a two-minute pairing window, or keeps the open one going; its code comes back. [stop] closes it. */
    fun blePair(stop: Boolean = false): BleStatus =
        Parse.ble(post("/api/ble/pair", (if (stop) Parse.STOP_BODY else "{}").toByteArray(Charsets.UTF_8), "application/json"))

    /** Forgets every paired phone, this one included. */
    fun bleForget(): BleStatus = Parse.ble(post("/api/ble/forget", ByteArray(0), null))

    // --- Saved reports and the board's clock (firmware 0.12) ------------------------

    fun reports(): ReportList = Parse.reports(get("/api/reports"))

    /** One network's report, up to some tens of kilobytes: over Bluetooth it takes a few seconds. */
    fun report(slot: Int): Report = Parse.report(get("/api/reports/get?slot=$slot", maxOf(readTimeoutMs, 20000)))

    /** Saves the report of the network the board is on now. */
    fun saveReport(): ReportList = Parse.reports(post("/api/reports/save", ByteArray(0), null))

    fun deleteReport(slot: Int): ReportList =
        Parse.reports(post("/api/reports/delete", Parse.slotBody(slot).toByteArray(Charsets.UTF_8), "application/json"))

    /** Tells the board the time, which it has no clock of its own to know. */
    fun setClock(unix: Long) {
        post("/api/clock", Parse.clockBody(unix).toByteArray(Charsets.UTF_8), "application/json")
    }

    /** True when the board accepts this update password. From 0.13 it takes a session as well. */
    fun checkUpdateKey(key: String): Boolean = try {
        post("/api/update/check", ByteArray(0), null, mapOf(KEY_HEADER to key))
        true
    } catch (e: ApiException) {
        if (e.kind == ApiException.Kind.Unauthorized) false else throw e
    }

    /**
     * Sends a firmware image the way the web page does: multipart/form-data,
     * one part named "firmware", the password in X-Netmon-Key. The body length
     * is fixed up front so progress reports bytes actually handed to the
     * socket, not bytes copied into a buffer.
     *
     * Returns normally when the board says it installed the image and is
     * restarting. A connection that drops after the last byte is reported as
     * ApiException(Unreachable): the board may have restarted before
     * answering, which only a later look at /api/health can settle.
     */
    fun uploadFirmware(image: ByteArray, fileName: String, key: String, progress: (sent: Long, total: Long) -> Unit) {
        if (viaBluetooth) {
            throw ApiException(ApiException.Kind.Http, "Firmware updates go over Wi-Fi. Join the monitor's Wi-Fi to update it.", 501)
        }
        val boundary = "netmon" + java.lang.Long.toHexString(System.nanoTime())
        val safeName = fileName.replace(Regex("[\"\\r\\n\\\\]"), "_").ifBlank { "firmware.bin" }
        val head = ("--$boundary\r\n" +
            "Content-Disposition: form-data; name=\"firmware\"; filename=\"$safeName\"\r\n" +
            "Content-Type: application/octet-stream\r\n\r\n").toByteArray(Charsets.UTF_8)
        val tail = "\r\n--$boundary--\r\n".toByteArray(Charsets.UTF_8)
        val total = (head.size + image.size + tail.size).toLong()

        val c = open("/api/update", "POST", 60000, signed = true)
        try {
            c.doOutput = true
            c.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
            c.setRequestProperty(KEY_HEADER, key)
            c.setFixedLengthStreamingMode(total)
            var sent = 0L
            progress(0L, total)
            c.outputStream.use { out ->
                out.write(head)
                sent += head.size
                var off = 0
                while (off < image.size) {
                    if (Thread.currentThread().isInterrupted) throw IOException("cancelled")
                    val n = minOf(CHUNK, image.size - off)
                    out.write(image, off, n)
                    off += n
                    sent += n
                    progress(sent, total)
                }
                out.write(tail)
                out.flush()
                sent += tail.size
                progress(sent, total)
            }
            finish(c)
        } catch (e: SocketTimeoutException) {
            throw ApiException(ApiException.Kind.Timeout, "The board stopped answering during the upload.")
        } catch (e: IOException) {
            throw ApiException(ApiException.Kind.Unreachable, "The connection dropped during the upload.")
        } finally {
            c.disconnect()
        }
    }

    // --- plumbing ----------------------------------------------------------

    private fun get(path: String, readTimeout: Int = readTimeoutMs, signed: Boolean = true): String =
        renewing(signed) { getOnce(path, readTimeout, signed) }

    private fun post(path: String, body: ByteArray, contentType: String?, headers: Map<String, String> = emptyMap(),
                     signed: Boolean = true): String =
        renewing(signed) { postOnce(path, body, contentType, headers, signed) }

    /**
     * Makes a request, and when the board says to sign in first and a new
     * session can be had without asking anybody, makes it once more with it.
     */
    private inline fun renewing(signed: Boolean, call: () -> String): String {
        try {
            return call()
        } catch (e: ApiException) {
            if (!signed || e.kind != ApiException.Kind.LoginRequired) throw e
            val fresh = renewSession?.invoke(this) ?: throw e
            token = fresh
            return call()
        }
    }

    private fun getOnce(path: String, readTimeout: Int, signed: Boolean): String {
        if (viaBluetooth) return overLink("GET", path, null, emptyMap(), readTimeout, signed)
        val c = open(path, "GET", readTimeout, signed)
        try {
            return finish(c)
        } catch (e: SocketTimeoutException) {
            throw ApiException(ApiException.Kind.Timeout, "The board did not answer in time.")
        } catch (e: IOException) {
            throw ApiException(ApiException.Kind.Unreachable, "Cannot reach the board at ${display(base)}.")
        } finally {
            c.disconnect()
        }
    }

    private fun postOnce(path: String, body: ByteArray, contentType: String?, headers: Map<String, String>, signed: Boolean): String {
        if (viaBluetooth) return overLink("POST", path, body, headers, readTimeoutMs, signed)
        val c = open(path, "POST", readTimeoutMs, signed)
        try {
            c.doOutput = true
            if (contentType != null) c.setRequestProperty("Content-Type", contentType)
            for ((k, v) in headers) c.setRequestProperty(k, v)
            c.setFixedLengthStreamingMode(body.size)
            c.outputStream.use { it.write(body) }
            return finish(c)
        } catch (e: SocketTimeoutException) {
            throw ApiException(ApiException.Kind.Timeout, "The board did not answer in time.")
        } catch (e: IOException) {
            throw ApiException(ApiException.Kind.Unreachable, "Cannot reach the board at ${display(base)}.")
        } finally {
            c.disconnect()
        }
    }

    private fun open(path: String, method: String, readTimeout: Int, signed: Boolean): HttpURLConnection {
        val c = try {
            URL(base + path).openConnection() as HttpURLConnection
        } catch (e: IOException) {
            throw ApiException(ApiException.Kind.Unreachable, "Cannot reach the board at ${display(base)}.")
        }
        c.requestMethod = method
        c.connectTimeout = connectTimeoutMs
        c.readTimeout = readTimeout
        c.useCaches = false
        c.instanceFollowRedirects = false
        c.setRequestProperty("Connection", "close")
        c.setRequestProperty("Accept", "application/json")
        val t = token
        if (signed && t != null) c.setRequestProperty(AUTH_HEADER, "Bearer $t")
        return c
    }

    /**
     * The same request over the Bluetooth link. The link itself says when the
     * board is out of reach or this phone is not paired; the board's answer
     * is read exactly as an HTTP one.
     */
    private fun overLink(method: String, path: String, body: ByteArray?, headers: Map<String, String>, timeout: Int,
                         signed: Boolean): String {
        val t = Link.transport ?: throw ApiException(ApiException.Kind.Unreachable, "Bluetooth is not available on this phone.")
        val tok = token
        val all = if (signed && tok != null) headers + (AUTH_HEADER to "Bearer $tok") else headers
        val reply = t.exchange(LinkCodec.address(base), LinkCodec.request(method, path, all, body), timeout)
        return answer(reply.status, reply.text)
    }

    /** Reads the answer; anything but 2xx becomes an ApiException carrying the board's own words. */
    private fun finish(c: HttpURLConnection): String {
        val code = c.responseCode
        val stream: InputStream? = if (code >= 400) c.errorStream else c.inputStream
        val text = stream?.use { readAll(it) } ?: ""
        // Over Wi-Fi the board marks "sign in first" in a header as well, which
        // survives where the body of a 401 to a streamed request may not.
        return answer(code, text, c.getHeaderField(LOGIN_HEADER) != null)
    }

    private fun answer(code: Int, text: String, loginMarked: Boolean = false): String {
        if (code in 200..299) return text
        val said = Parse.errorText(text)
        when {
            code == 401 && (loginMarked || Parse.loginRequired(text)) ->
                throw ApiException(ApiException.Kind.LoginRequired, said ?: "Sign in to the monitor first.", code)
            code == 401 -> throw ApiException(ApiException.Kind.Unauthorized, said ?: DEFAULT_401, code)
            code == 429 -> throw ApiException(ApiException.Kind.Http, said ?: "Too many wrong passwords. Try again later.",
                code, Parse.retryS(text))
            code == 302 -> throw ApiException(ApiException.Kind.Http,
                "The board is in setup mode and redirected the request. Join netmon-setup and use 192.168.4.1.", code)
            code == 404 -> throw ApiException(ApiException.Kind.Http,
                said ?: "This board's firmware does not have that feature. Update the firmware to use it.", code)
            else -> throw ApiException(ApiException.Kind.Http, said ?: "The board answered with HTTP $code.", code)
        }
    }

    private fun readAll(input: InputStream): String {
        val buf = ByteArrayOutputStream()
        val chunk = ByteArray(4096)
        while (true) {
            val n = input.read(chunk)
            if (n < 0) break
            buf.write(chunk, 0, n)
            if (buf.size() > MAX_REPLY) throw IOException("reply too large")
        }
        return String(buf.toByteArray(), Charsets.UTF_8)
    }

    companion object {
        const val KEY_HEADER = "X-Netmon-Key"
        const val AUTH_HEADER = "Authorization"
        const val LOGIN_HEADER = "X-Netmon-Login"
        private const val DEFAULT_401 = "Wrong update password."
        private const val CHUNK = 4096
        private const val MAX_REPLY = 512 * 1024

        private val BLE_ADDRESS = Regex("^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")

        /**
         * "192.168.2.30", "netmon.local:80", "http://192.168.2.30/settings"
         * all become a scheme and authority with no path. A board reached over
         * Bluetooth is "ble://" and its address, in capitals.
         */
        fun normalize(address: String): String {
            var s = address.trim()
            require(s.isNotEmpty()) { "Enter the board's address." }
            if (s.length >= LinkCodec.SCHEME.length && s.substring(0, LinkCodec.SCHEME.length).lowercase() == LinkCodec.SCHEME) {
                val mac = s.substring(LinkCodec.SCHEME.length).uppercase()
                require(BLE_ADDRESS.matches(mac)) { "That is not a Bluetooth address." }
                return LinkCodec.SCHEME + mac
            }
            if (!s.contains("://")) s = "http://$s"
            val u = try {
                URL(s)
            } catch (e: MalformedURLException) {
                throw IllegalArgumentException("That does not look like an address.")
            }
            require(u.protocol == "http" || u.protocol == "https") { "Use an http:// address." }
            val host = u.host
            require(!host.isNullOrEmpty()) { "That does not look like an address." }
            require(host.all { it.isLetterOrDigit() || it == '.' || it == '-' || it == ':' || it == '[' || it == ']' }) {
                "That does not look like an address."
            }
            val port = if (u.port == -1 || u.port == u.defaultPort) "" else ":${u.port}"
            return "${u.protocol}://$host$port"
        }

        /** "http://192.168.2.30" -> "192.168.2.30" for display; a board over Bluetooth is just "Bluetooth". */
        fun display(base: String): String =
            if (LinkCodec.isBle(base)) "Bluetooth" else base.removePrefix("http://").removePrefix("https://")
    }
}

/**
 * After an upload, waits for the board to come back and says what happened.
 * Same rule as the web page: the board answers, restarts half a second later,
 * so the first look is four seconds on. Its uptime says whether it really
 * restarted since the upload began; its version says whether the new image
 * is the one now running.
 */
object RestartWatch {

    sealed class Outcome {
        data class Updated(val version: String) : Outcome()
        data class SameVersion(val version: String) : Outcome()
        data class NotRestarted(val version: String) : Outcome()
        object NoAnswer : Outcome()
        object Cancelled : Outcome()
    }

    fun await(
        base: String,
        uploadStartedMs: Long,
        oldVersion: String,
        token: String? = null,
        now: () -> Long = { System.currentTimeMillis() },
        sleep: (Long) -> Unit = { Thread.sleep(it) },
        firstLookMs: Long = 4000,
        retryMs: Long = 2000,
        giveUpAfterMs: Long = 150_000,
        cancelled: () -> Boolean = { false },
    ): Outcome {
        // Short timeouts of its own: a board mid-restart should cost one quick
        // failed attempt, not the eight-second read the screens use.
        val probe = NetmonClient(base).apply {
            connectTimeoutMs = 2500
            readTimeoutMs = 3000
            // Sessions outlive a restart: the board keeps them in its flash.
            this.token = token
        }
        sleep(firstLookMs)
        while (true) {
            if (cancelled()) return Outcome.Cancelled
            try {
                val h = probe.health()
                val restarted = h.uptimeS * 1000 < now() - uploadStartedMs
                return when {
                    !restarted -> Outcome.NotRestarted(h.version)
                    h.version != oldVersion -> Outcome.Updated(h.version)
                    else -> Outcome.SameVersion(h.version)
                }
            } catch (e: ApiException) {
                if (now() - uploadStartedMs > giveUpAfterMs) return Outcome.NoAnswer
                sleep(retryMs)
            }
        }
    }
}
