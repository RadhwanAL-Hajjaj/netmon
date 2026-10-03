package com.example.netmon

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.MalformedURLException
import java.net.SocketTimeoutException
import java.net.URL

class ApiException(val kind: Kind, message: String, val status: Int = 0) : Exception(message) {
    enum class Kind { Unreachable, Timeout, Unauthorized, Http, BadData }
}

/**
 * Talks to one board over plain HTTP. Blocking: call it from a worker thread.
 *
 * The board runs the Arduino WebServer, which serves one client at a time and
 * closes every connection after the answer, so every request asks for
 * "Connection: close" and nothing is ever pooled or pipelined.
 */
class NetmonClient(address: String) {

    val base: String = normalize(address)

    var connectTimeoutMs = 4000
    var readTimeoutMs = 8000

    fun health(): Health = Parse.health(get("/api/health"))

    /** The board's health when this address really is a netmon board, otherwise null. */
    fun identify(): Health? = try {
        val text = get("/api/health")
        if (Subnet.looksLikeNetmon(text)) Parse.health(text) else null
    } catch (e: ApiException) {
        null
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

    /** True when the board accepts this update password. */
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
        val boundary = "netmon" + java.lang.Long.toHexString(System.nanoTime())
        val safeName = fileName.replace(Regex("[\"\\r\\n\\\\]"), "_").ifBlank { "firmware.bin" }
        val head = ("--$boundary\r\n" +
            "Content-Disposition: form-data; name=\"firmware\"; filename=\"$safeName\"\r\n" +
            "Content-Type: application/octet-stream\r\n\r\n").toByteArray(Charsets.UTF_8)
        val tail = "\r\n--$boundary--\r\n".toByteArray(Charsets.UTF_8)
        val total = (head.size + image.size + tail.size).toLong()

        val c = open("/api/update", "POST", 60000)
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

    private fun get(path: String, readTimeout: Int = readTimeoutMs): String {
        val c = open(path, "GET", readTimeout)
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

    private fun post(path: String, body: ByteArray, contentType: String?, headers: Map<String, String> = emptyMap()): String {
        val c = open(path, "POST", readTimeoutMs)
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

    private fun open(path: String, method: String, readTimeout: Int): HttpURLConnection {
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
        return c
    }

    /** Reads the answer; anything but 2xx becomes an ApiException carrying the board's own words. */
    private fun finish(c: HttpURLConnection): String {
        val code = c.responseCode
        val stream: InputStream? = if (code >= 400) c.errorStream else c.inputStream
        val text = stream?.use { readAll(it) } ?: ""
        if (code in 200..299) return text
        val said = Parse.errorText(text)
        when {
            code == 401 -> throw ApiException(ApiException.Kind.Unauthorized, said ?: "Wrong update password.", code)
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
        private const val CHUNK = 4096
        private const val MAX_REPLY = 512 * 1024

        /**
         * "192.168.2.30", "netmon.local:80", "http://192.168.2.30/settings"
         * all become a scheme and authority with no path.
         */
        fun normalize(address: String): String {
            var s = address.trim()
            require(s.isNotEmpty()) { "Enter the board's address." }
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

        /** "http://192.168.2.30" -> "192.168.2.30" for display. */
        fun display(base: String): String = base.removePrefix("http://").removePrefix("https://")
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
