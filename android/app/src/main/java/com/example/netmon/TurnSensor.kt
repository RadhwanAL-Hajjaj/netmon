package com.example.netmon

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager

/**
 * Which way the phone faces, from its own rotation sensor, for the Finder's
 * turn on the spot. The gyroscope-based "game" rotation vector is preferred:
 * it has no compass in it, so the board, a power bank and steel furniture
 * cannot pull it off, and over the minute a turn takes it hardly drifts. Its
 * north is arbitrary, which is fine: only how far the phone turned matters.
 * Phones without a gyroscope fall back to the compass-based vector; phones
 * with neither get the timed turn the web page uses.
 */
class TurnSensor(context: Context) : SensorEventListener {

    private val sm: SensorManager? = context.getSystemService(SensorManager::class.java)
    private val sensor: Sensor? = sm?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
        ?: sm?.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
    private val matrix = FloatArray(9)
    private val vec = FloatArray(4)
    private var running = false

    /** The latest heading, radians clockwise from the sensor's own north; null until one arrives. */
    var heading: Double? = null
        private set

    /** Called on the main thread with each heading and the phone clock when it came. */
    var listener: ((Double, Long) -> Unit)? = null

    val available: Boolean get() = sensor != null

    fun start(): Boolean {
        val m = sm ?: return false
        val s = sensor ?: return false
        if (running) return true
        running = m.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)
        return running
    }

    fun stop() {
        if (!running) return
        sm?.unregisterListener(this)
        running = false
        heading = null
    }

    override fun onSensorChanged(e: SensorEvent) {
        // Some phones send five values, and older Android versions refuse more
        // than four in getRotationMatrixFromVector.
        val n = minOf(4, e.values.size)
        for (i in 0 until 4) vec[i] = if (i < n) e.values[i] else 0f
        try {
            SensorManager.getRotationMatrixFromVector(matrix, if (n == 4) vec else e.values)
        } catch (x: IllegalArgumentException) {
            return
        }
        val h = FinderMath.headingOf(matrix) ?: return
        heading = h
        listener?.invoke(h, System.currentTimeMillis())
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
}
