package com.example.netmon

import java.util.Locale

/**
 * Catches the wrong file before a megabyte of upload, with the same rules as
 * the web settings page. The board checks again on its side: the updater
 * refuses an image without the ESP32 magic byte.
 */
object Firmware {

    /**
     * The app partition of the "Default 4MB with spiffs" scheme, which builds
     * before 0.10 used. From 0.10 the board says what it takes (update_max in
     * /api/config): 1,966,080 bytes with "Minimal SPIFFS".
     */
    const val APP_PARTITION_BYTES = 1_310_720L
    /** The app partition of "Minimal SPIFFS", which 0.10 and later need: the largest netmon uses. */
    const val MINIMAL_SPIFFS_APP_BYTES = 1_966_080L
    /** Read no further than this from a chosen file: no ESP32 app partition is larger. */
    const val READ_LIMIT_BYTES = 4L * 1024 * 1024
    const val MIN_BYTES = 262_144L
    const val MAGIC = 0xE9

    /** What this board's updater takes: its own figure when it reports one. */
    fun limit(updateMax: Long): Long = if (updateMax > 0) updateMax else APP_PARTITION_BYTES

    /** Why this file cannot be the netmon firmware, or null when it looks right. */
    fun problem(name: String, size: Long, firstByte: Int?, maxBytes: Long = APP_PARTITION_BYTES): String? {
        val nm = name.lowercase()
        val part = Regex("bootloader|partitions|merged").find(nm)?.value
        return when {
            !nm.endsWith(".bin") -> "That is not a .bin file."
            part != null -> "That is the $part image, not the firmware. Choose the file ending in .ino.bin."
            size > maxBytes -> "Too large: this board takes firmware up to ${String.format(Locale.US, "%,d", maxBytes)} bytes."
            size < MIN_BYTES -> "Too small to be netmon firmware."
            firstByte != null && firstByte != MAGIC -> "That file is not an ESP32 firmware image."
            else -> null
        }
    }

    private val versionPattern = Regex("\u0000(\\d+\\.\\d+\\.\\d+-[a-z][a-z0-9-]{1,40})\u0000")

    /**
     * The netmon version string compiled into an image, such as
     * "0.9.6-status-hints", when exactly one is found. The ESP-IDF app
     * descriptor is no help here: an Arduino build fills it with the core's
     * own details, the same for every sketch.
     */
    fun versionIn(image: ByteArray): String? {
        val text = String(image, Charsets.ISO_8859_1)
        val found = versionPattern.findAll(text).map { it.groupValues[1] }.toList().distinct()
        return found.singleOrNull()
    }
}
