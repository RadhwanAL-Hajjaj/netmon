package com.example.netmon

import android.content.Context
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter

/**
 * Keeps the stack trace of a crash so the next start can show it. There is no
 * crash reporting service behind this app; the text is for you to read or
 * paste into a message.
 */
object CrashLog {

    private const val FILE = "last-crash.txt"

    fun install(context: Context) {
        val app = context.applicationContext
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            try {
                val sw = StringWriter()
                error.printStackTrace(PrintWriter(sw))
                val text = "netmon ${BuildInfo.VERSION_NAME} on Android ${Build.VERSION.RELEASE} " +
                    "(API ${Build.VERSION.SDK_INT}), ${Build.MANUFACTURER} ${Build.MODEL}\n" +
                    "thread ${thread.name}\n\n$sw"
                File(app.filesDir, FILE).writeText(text)
            } catch (e: Throwable) {
                // Nothing more can be done from inside a crash.
            }
            previous?.uncaughtException(thread, error)
        }
    }

    /** The last crash report, removed as it is read. */
    fun take(context: Context): String? {
        val f = File(context.filesDir, FILE)
        if (!f.exists()) return null
        return try {
            f.readText().also { f.delete() }
        } catch (e: Exception) {
            null
        }
    }
}

object BuildInfo {
    const val VERSION_NAME = "1.3.1"
    const val VERSION_CODE = 5
}
