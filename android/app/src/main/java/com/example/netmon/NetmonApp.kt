package com.example.netmon

import android.app.Application

class NetmonApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CrashLog.install(this)
        AppState.init(this)
        BleLink.init(this)
        Board.init(this)
        Alerts.ensureChannel(this)
        Alerts.schedule(this)
    }
}
