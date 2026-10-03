// Beeper.kt against a fake AudioTrack: one track at a time however quickly the
// Finder stops and starts the beeps (picking another device does both in one
// go), and a track that refuses samples ends the beeps rather than spinning.
import android.media.AudioTrack
import com.example.netmon.Beeper

private var passed = 0
private var failed = 0

private fun check(ok: Boolean, what: String) {
    if (ok) passed++ else { failed++; println("FAIL $what") }
}

fun main() {
    val b = Beeper()
    b.level = 0.5
    // Stopped and started while the first thread is still making its track.
    b.start(); Thread.sleep(5); b.stop(); b.start()
    Thread.sleep(400)
    check(AudioTrack.playing().size == 1, "one track after a stop and start while the track is made: ${AudioTrack.playing()}")
    // And while a thread is inside a blocking write.
    b.stop(); b.start()
    Thread.sleep(400)
    check(AudioTrack.playing().size == 1, "one track after a stop and start during a write: ${AudioTrack.playing()}")
    val now = AudioTrack.all.last { !it.released }
    val w = now.writes
    Thread.sleep(300)
    check(now.writes > w, "the track playing is still fed")
    check(b.on, "on while playing")
    b.stop()
    Thread.sleep(300)
    check(AudioTrack.playing().isEmpty(), "stop releases the track: ${AudioTrack.playing()}")
    check(!b.on, "off after stop")

    AudioTrack.nextFailAfter = 3
    b.start()
    Thread.sleep(400)
    val dead = AudioTrack.all.last()
    check(dead.released, "a track that refuses samples is released")
    check(dead.writes <= 5, "and not written to in a loop (${dead.writes} writes)")
    check(!b.on, "the beeper then says it is off")
    AudioTrack.nextFailAfter = -1
    b.start()
    Thread.sleep(200)
    check(AudioTrack.playing().size == 1 && b.on, "and starts again afterwards")
    b.stop()
    Thread.sleep(200)

    println("beeper: passed $passed, failed $failed")
    if (failed > 0) System.exit(1)
}
