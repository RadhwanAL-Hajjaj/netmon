package com.example.netmon

/**
 * Catches the wrong file before a megabyte of upload, with the same rules as
 * the web settings page. The board checks again on its side: the updater
 * refuses an image without the ESP32 magic byte.
 */
object Firmware {

    /** The app partition in the "Default 4MB with spiffs" scheme the firmware is built for. */
    const val APP_PARTITION_BYTES = 1_310_720L
    const val MIN_BYTES = 262_144L
    const val MAGIC = 0xE9

    /** Why this file cannot be the netmon firmware, or null when it looks right. */
    fun problem(name: String, size: Long, firstByte: Int?): String? {
        val nm = name.lowercase()
        val part = Regex("bootloader|partitions|merged").find(nm)?.value
        return when {
            !nm.endsWith(".bin") -> "That is not a .bin file."
            part != null -> "That is the $part image, not the firmware. Choose the file ending in .ino.bin."
            size > APP_PARTITION_BYTES -> "Too large: the app partition holds 1,310,720 bytes."
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
