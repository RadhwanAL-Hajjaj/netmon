package com.example.netmon

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.SystemClock
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * The Finder's beeps: they quicken and rise as the device gets closer, the
 * same tones as the web page's. One stream, fed by its own thread, so a beep
 * never waits on the network or the screen.
 */
class Beeper {

    private class Tone(val hz: Double, val ms: Int, val vol: Double)

    /** How close, 0 far to 1 here; null for silence (nothing heard lately). */
    @Volatile var level: Double? = null

    /** True while the beeps should stop, as during a turn, which has its own ticks. */
    @Volatile var paused = false

    /**
     * The thread that plays, or null. Each thread plays only while it is the
     * one here: a stop and a start in quick succession (picking another device)
     * would otherwise leave the old thread, still inside a write when the flag
     * came back on, playing alongside the new one.
     */
    private val thread = AtomicReference<Thread?>(null)
    private val queue = ConcurrentLinkedQueue<Tone>()

    val on: Boolean get() = thread.get() != null

    fun start() {
        if (thread.get() != null) return
        val t = Thread({ loop() }, "netmon-beeper")
        t.isDaemon = true
        thread.set(t)
        t.start()
    }

    fun stop() {
        // No interrupt: a write to the track does not heed one. The thread
        // sees it is no longer the one here after its current write, 50 ms at most.
        thread.set(null)
        queue.clear()
    }

    /** One tone now, ahead of the regular beeps: the turn's start, ticks and end. */
    fun tone(hz: Double, ms: Int, vol: Double) {
        if (thread.get() != null) queue.add(Tone(hz, ms, vol))
    }

    private fun mine(): Boolean = thread.get() === Thread.currentThread()

    private fun loop() {
        val rate = 22050
        val track = try {
            val min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
            // Media volume, as for the web page's beeps: sounds on the system
            // stream follow the ringer on most phones and go quiet with it.
            AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build())
                .setAudioFormat(AudioFormat.Builder()
                    .setSampleRate(rate)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build())
                .setBufferSizeInBytes(max(min, rate / 8 * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (e: Exception) {
            thread.compareAndSet(Thread.currentThread(), null)
            return
        }
        val silence = ShortArray(rate / 20)          // 50 ms
        var next = 0L
        try {
            track.play()
            var ok = true
            while (ok && mine()) {
                val t = queue.poll()
                if (t != null) {
                    ok = write(track, toneSamples(rate, t))
                    continue
                }
                val lv = level
                val now = SystemClock.elapsedRealtime()
                if (lv == null || paused) {
                    next = 0L
                    ok = write(track, silence)
                    continue
                }
                if (now >= next) {
                    ok = write(track, toneSamples(rate, Tone(330 + 990 * lv, 70, .18)))
                    next = now + (1500 - 1340 * lv).toLong()
                } else {
                    val left = min(50L, next - now).toInt()
                    ok = write(track, if (left >= 50) silence else ShortArray(max(1, rate * left / 1000)))
                }
            }
        } catch (e: Exception) {
            // Audio taken away by the system: the Finder carries on without sound.
        } finally {
            try { track.stop() } catch (e: Exception) {}
            track.release()
            thread.compareAndSet(Thread.currentThread(), null)
        }
    }

    /**
     * False when the track refuses the samples (the audio server restarted,
     * say): the beeps end there rather than retrying in a busy loop.
     */
    private fun write(track: AudioTrack, s: ShortArray): Boolean {
        var off = 0
        while (off < s.size && mine()) {
            val n = track.write(s, off, s.size - off)
            if (n <= 0) return false
            off += n
        }
        return true
    }

    // A sine with a 10 ms rise and an exponential fall, so it starts and ends without a click.
    private fun toneSamples(rate: Int, t: Tone): ShortArray {
        val n = rate * t.ms / 1000
        val out = ShortArray(n)
        val dur = t.ms / 1000.0
        for (i in 0 until n) {
            val s = i.toDouble() / rate
            val env = if (s < .01) s / .01 else exp(-5 * (s - .01) / max(.001, dur - .01))
            out[i] = (32767 * t.vol * env * sin(2 * PI * t.hz * s)).toInt().toShort()
        }
        return out
    }
}
