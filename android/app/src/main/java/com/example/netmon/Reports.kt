package com.example.netmon

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Saved reports as files, and the words for them. No Android types, so the
 * tests can run it. The CSV has the same columns, in the same order, as the
 * one the board's Settings page makes (pages.h, tocsv), so a spreadsheet
 * built on one takes the other.
 */
object ReportFiles {

    val CSV_HEADER = listOf(
        "network", "subnet", "saved", "mac", "ip", "hostname", "vendor", "status", "private_mac", "online",
        "last_seen", "last_seen_s_before_save", "first_seen", "uptime_s", "carried_over",
    )

    /** The report as CSV, times in [zone], with a byte-order mark so spreadsheets read it as UTF-8. */
    fun csv(r: Report, zone: TimeZone = TimeZone.getDefault()): String {
        val sb = StringBuilder("﻿")
        line(sb, CSV_HEADER)
        for (d in r.devices) {
            val v = d.device
            line(sb, listOf(
                r.ssid, r.subnet, time(r.savedUnix, zone), v.mac, v.ip, v.hostname, v.vendor, v.status,
                if (v.randomised) "yes" else "no", if (v.online) "yes" else "no",
                time(d.seenAt(r), zone), v.lastSeenS.toString(), time(d.firstUnix, zone), v.upS.toString(),
                if (d.carried) "yes" else "no",
            ))
        }
        return sb.toString()
    }

    private fun line(sb: StringBuilder, cells: List<String>) {
        cells.forEachIndexed { i, c ->
            if (i > 0) sb.append(',')
            sb.append(cell(c))
        }
        sb.append("\r\n")
    }

    /**
     * One CSV cell. A value a spreadsheet would run as a formula is written
     * as text: host names are whatever devices broadcast about themselves.
     */
    fun cell(value: String): String {
        var v = value
        if (v.isNotEmpty() && v[0] in "=+-@\t\r") v = "'$v"
        return if (v.any { it == '"' || it == ',' || it == '\r' || it == '\n' }) "\"" + v.replace("\"", "\"\"") + "\"" else v
    }

    /** "2026-10-04 21:01:02", or "" when not known. */
    fun time(unix: Long, zone: TimeZone = TimeZone.getDefault()): String {
        if (unix <= 0) return ""
        val f = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT)
        f.timeZone = zone
        return f.format(Date(unix * 1000))
    }

    /** "netmon-HOME-2.4-2026-10-04.csv": the same name the Settings page gives the file. */
    fun fileName(ssid: String, savedUnix: Long, ext: String): String {
        val name = ssid.replace(Regex("[^A-Za-z0-9._-]+"), "_").ifEmpty { "network" }
        val day = if (savedUnix > 0) {
            val f = SimpleDateFormat("yyyy-MM-dd", Locale.ROOT)
            f.timeZone = TimeZone.getTimeZone("UTC")
            f.format(Date(savedUnix * 1000))
        } else {
            "report"
        }
        return "netmon-$name-$day.$ext"
    }

    /** When a report was saved, in words: "Oct 4, 21:11 · 5 min ago", or why it cannot be told. */
    fun savedText(r: ReportInfo, zone: TimeZone = TimeZone.getDefault()): String {
        val ago = when {
            r.ageS < 0 -> "before the board last restarted"
            else -> Format.ago(r.ageS)
        }
        if (r.savedUnix <= 0) return "Saved $ago"
        val f = SimpleDateFormat("d MMM, HH:mm", Locale.getDefault())
        f.timeZone = zone
        return "${f.format(Date(r.savedUnix * 1000))} · $ago"
    }

    /** The reports, the network the board is on first, then the newest. */
    fun ordered(list: List<ReportInfo>): List<ReportInfo> = list.sortedWith(Comparator { a, b ->
        when {
            a.current != b.current -> if (a.current) -1 else 1
            else -> b.savedUnix.compareTo(a.savedUnix)
        }
    })

    /** A device in a report, last seen when: "online", "3 h before the save", "before the save, at least 2 days". */
    fun seenText(d: ReportDevice): String = when {
        d.device.online -> "online at the save"
        d.carried -> "${agoBefore(d.device.lastSeenS)}, kept from an earlier report"
        else -> agoBefore(d.device.lastSeenS)
    }

    private fun agoBefore(s: Long): String = when {
        s < 60 -> "seen just before the save"
        s < 3600 -> "seen ${s / 60} min before the save"
        s < 86400 -> "seen ${s / 3600} h before the save"
        else -> "seen ${s / 86400} days before the save"
    }
}
