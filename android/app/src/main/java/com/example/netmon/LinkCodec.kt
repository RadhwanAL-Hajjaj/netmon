package com.example.netmon

import java.io.ByteArrayOutputStream

/**
 * The Bluetooth link's frames, as the board's firmware defines them
 * (firmware/netmon/src/core/ble_link.h). Plain Kotlin, no Android, so the
 * tests can run it against the same byte-for-byte examples the firmware's own
 * tests use.
 *
 * Every value written to or notified by the board is a frame: a flags byte
 * (0x01 first, 0x02 last), the request's number, then payload. A request's
 * payloads together are a cut-down HTTP request; an answer's are the status,
 * two bytes least significant first, then the body, exactly as the same
 * request over Wi-Fi would have had it.
 */
object LinkCodec {

    const val FIRST = 0x01
    const val LAST = 0x02
    const val HEADER = 2

    /** The link's version in the firmware's advertisement and GET /api/ble. */
    const val VERSION = 1

    const val SERVICE = "4e4d0001-9c2b-4d8e-a1f3-6e65746d6f6e"
    const val RX = "4e4d0002-9c2b-4d8e-a1f3-6e65746d6f6e"     // the app writes requests here
    const val TX = "4e4d0003-9c2b-4d8e-a1f3-6e65746d6f6e"     // the board notifies answers here
    const val CCCD = "00002902-0000-1000-8000-00805f9b34fb"

    /** The company identifier in the board's advertisement: 0xFFFF, for testing and private use. */
    const val MANUFACTURER = 0xFFFF

    /** The longest request the board takes (kLinkRequestMax). */
    const val REQUEST_MAX = 2048

    /** The text of a request: method and target, headers, a blank line, the body. */
    fun request(method: String, path: String, headers: Map<String, String> = emptyMap(), body: ByteArray? = null): ByteArray {
        require(method == "GET" || method == "POST") { "GET or POST only" }
        require(path.startsWith("/") && path.none { it <= ' ' || it.code >= 0x7f }) { "bad path: $path" }
        val head = StringBuilder()
        head.append(method).append(' ').append(path).append('\n')
        for ((k, v) in headers) {
            require(k.none { it == ':' || it == '\n' || it == '\r' } && v.none { it == '\n' || it == '\r' }) { "bad header" }
            head.append(k).append(": ").append(v).append('\n')
        }
        head.append('\n')
        val out = ByteArrayOutputStream()
        out.write(head.toString().toByteArray(Charsets.UTF_8))
        if (body != null) out.write(body)
        return out.toByteArray()
    }

    /** [message] cut into frames of at most [cap] bytes, numbered [id]. */
    fun frames(id: Int, message: ByteArray, cap: Int): List<ByteArray> {
        require(cap > HEADER) { "frames need room for a payload" }
        val room = cap - HEADER
        val out = ArrayList<ByteArray>()
        var off = 0
        do {
            val n = minOf(room, message.size - off)
            val f = ByteArray(HEADER + n)
            var flags = 0
            if (off == 0) flags = flags or FIRST
            if (off + n == message.size) flags = flags or LAST
            f[0] = flags.toByte()
            f[1] = (id and 0xFF).toByte()
            System.arraycopy(message, off, f, HEADER, n)
            out.add(f)
            off += n
        } while (off < message.size)
        return out
    }

    /**
     * An answer being put back together. Frames for any other request are
     * ignored: they belong to one this app has given up on.
     */
    class Answer(val id: Int) {
        var status = -1
            private set
        var done = false
            private set
        private var started = false
        private val buf = ByteArrayOutputStream()

        val body: ByteArray get() = buf.toByteArray()

        /** True when the frame was part of this answer. */
        fun feed(frame: ByteArray): Boolean {
            if (frame.size < HEADER || done) return false
            val flags = frame[0].toInt() and 0xFF
            if (flags and (FIRST or LAST).inv() != 0) return false
            if ((frame[1].toInt() and 0xFF) != (id and 0xFF)) return false
            var from = HEADER
            if (flags and FIRST != 0) {
                if (frame.size < HEADER + 2) return false
                buf.reset()
                status = (frame[2].toInt() and 0xFF) or ((frame[3].toInt() and 0xFF) shl 8)
                started = true
                from += 2
            } else if (!started) {
                return false
            }
            buf.write(frame, from, frame.size - from)
            if (buf.size() > MAX_ANSWER) throw ApiException(ApiException.Kind.BadData, "The board's answer was too large.")
            if (flags and LAST != 0) done = true
            return true
        }
    }

    /** The link's own errors, apart from the board's answers. */
    class Closed(message: String) : Exception(message)

    /** One connection's frames, as the radio carries them. */
    interface Pipe {
        /** The largest value one write or notification can carry: the ATT MTU less 3. */
        val frameCap: Int

        /** Writes one frame, waiting until the board's stack has taken it. Throws [Closed]. */
        fun send(frame: ByteArray)

        /** The next notification, or null after [timeoutMs]. Throws [Closed]. */
        fun receive(timeoutMs: Long): ByteArray?
    }

    /**
     * Sends a request as frames and waits for its whole answer. Throws
     * [Closed] when the connection goes, and ApiException(Timeout) when the
     * answer does not finish within [timeoutMs].
     */
    fun exchange(pipe: Pipe, id: Int, message: ByteArray, timeoutMs: Long,
                 now: () -> Long = { System.currentTimeMillis() }): LinkReply {
        if (message.size > REQUEST_MAX) throw ApiException(ApiException.Kind.BadData, "That request is too large to send over Bluetooth.")
        for (f in frames(id, message, pipe.frameCap)) pipe.send(f)
        val answer = Answer(id)
        val until = now() + timeoutMs
        while (!answer.done) {
            val left = until - now()
            if (left <= 0) throw ApiException(ApiException.Kind.Timeout, "The board did not answer in time over Bluetooth.")
            val frame = pipe.receive(left) ?: continue
            answer.feed(frame)
        }
        return LinkReply(answer.status, answer.body)
    }

    /** What the board's advertisement says about itself, or null when it is not a netmon advertisement. */
    data class Advert(val version: Int, val pairing: Boolean)

    /** Reads the manufacturer data that follows the company identifier: "NM", version, flags. */
    fun advert(data: ByteArray?): Advert? {
        if (data == null || data.size < 4) return null
        if (data[0] != 'N'.code.toByte() || data[1] != 'M'.code.toByte()) return null
        return Advert(data[2].toInt() and 0xFF, (data[3].toInt() and 0x01) != 0)
    }

    /** "ble://D4:E9:F4:A3:B8:AE", the form a board reached over Bluetooth takes in the app. */
    const val SCHEME = "ble://"

    fun isBle(base: String?): Boolean = base != null && base.startsWith(SCHEME)

    /** The Bluetooth address in a ble:// base. */
    fun address(base: String): String = base.removePrefix(SCHEME)

    private const val MAX_ANSWER = 512 * 1024
}

/** The board's answer, as the status and body the same request over Wi-Fi would have had. */
class LinkReply(val status: Int, val body: ByteArray) {
    val text: String get() = String(body, Charsets.UTF_8)
}

/**
 * How [NetmonClient] reaches a board over Bluetooth. The app sets this to
 * its Bluetooth connection when it starts (BleLink); the tests set a stand-in.
 */
interface LinkTransport {
    fun exchange(address: String, message: ByteArray, timeoutMs: Int): LinkReply
}

object Link {
    @Volatile var transport: LinkTransport? = null
}
