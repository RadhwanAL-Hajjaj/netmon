// A stand-in for the parts of android.media and android.os that Beeper.kt
// uses, for BeeperTest on the JVM. The track "plays" in real time: a write
// blocks for as long as its samples last, as a streaming AudioTrack's does.
@file:Suppress("unused", "UNUSED_PARAMETER")
package android.media

import java.util.Collections
import java.util.concurrent.atomic.AtomicInteger

class AudioAttributes private constructor() {
    class Builder {
        fun setUsage(u: Int) = this
        fun setContentType(c: Int) = this
        fun build() = AudioAttributes()
    }
    companion object {
        const val USAGE_MEDIA = 1
        const val CONTENT_TYPE_SONIFICATION = 4
    }
}

class AudioFormat private constructor() {
    class Builder {
        fun setSampleRate(r: Int) = this
        fun setEncoding(e: Int) = this
        fun setChannelMask(m: Int) = this
        fun build() = AudioFormat()
    }
    companion object {
        const val CHANNEL_OUT_MONO = 4
        const val ENCODING_PCM_16BIT = 2
    }
}

class AudioTrack private constructor(val id: Int, private val failAfter: Int) {
    @Volatile var writes = 0
    @Volatile var released = false

    class Builder {
        fun setAudioAttributes(a: AudioAttributes) = this
        fun setAudioFormat(f: AudioFormat) = this
        fun setBufferSizeInBytes(n: Int) = this
        fun setTransferMode(m: Int) = this
        fun build(): AudioTrack {
            Thread.sleep(30)                         // making a track takes a moment
            val t = AudioTrack(ids.incrementAndGet(), nextFailAfter)
            all.add(t)
            return t
        }
    }

    fun play() {}
    fun stop() {}
    fun release() { released = true }

    fun write(s: ShortArray, off: Int, n: Int): Int {
        writes++
        if (failAfter in 0 until writes) return -6   // ERROR_DEAD_OBJECT, as after an audio server restart
        Thread.sleep(maxOf(1L, n * 1000L / 22050))
        return n
    }

    companion object {
        const val MODE_STREAM = 1
        private val ids = AtomicInteger()
        val all: MutableList<AudioTrack> = Collections.synchronizedList(ArrayList())
        /** Tracks made from now on refuse samples after this many writes; -1 never. */
        @Volatile var nextFailAfter = -1
        fun getMinBufferSize(r: Int, c: Int, e: Int) = 2048
        fun playing(): List<Int> = synchronized(all) { all.filter { !it.released }.map { it.id } }
    }
}
