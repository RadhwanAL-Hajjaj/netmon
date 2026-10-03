package com.example.netmon

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.net.Inet4Address

/** Which local network the phone is on, and making sure the app's traffic goes there. */
object NetRoute {

    data class Local(val network: Network, val ip: Int, val prefix: Int, val hasInternet: Boolean) {
        val ipText: String get() = Subnet.format(ip)
    }

    /** The phone's Wi-Fi (or Ethernet) network with an IPv4 address, if it has one. */
    fun local(context: Context): Local? {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return null
        @Suppress("DEPRECATION")
        val all = cm.allNetworks
        for (n in all) {
            val caps = cm.getNetworkCapabilities(n) ?: continue
            val lan = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
            if (!lan) continue
            val lp = cm.getLinkProperties(n) ?: continue
            for (la in lp.linkAddresses) {
                val a = la.address
                if (a !is Inet4Address || a.isLoopbackAddress || a.isLinkLocalAddress) continue
                val ip = Subnet.parse(a.hostAddress ?: continue) ?: continue
                return Local(n, ip, la.prefixLength, caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED))
            }
        }
        return null
    }

    /**
     * On a Wi-Fi network with no internet — the board's own netmon-setup
     * network, for one — Android keeps mobile data as the default network, so
     * a request to 192.168.4.1 would leave over mobile data and fail. In that
     * case the app is pinned to the Wi-Fi network; otherwise it follows the
     * default, which keeps a VPN (for reaching the board from away) working.
     */
    fun pinIfNeeded(context: Context) {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return
        try {
            val active = cm.activeNetwork
            val caps = active?.let { cm.getNetworkCapabilities(it) }
            val activeIsLocal = caps != null && (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) ||
                caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN))
            val lan = local(context)
            val want: Network? = if (!activeIsLocal && lan != null && !lan.hasInternet) lan.network else null
            if (cm.boundNetworkForProcess != want) cm.bindProcessToNetwork(want)
        } catch (e: SecurityException) {
            // Without ACCESS_NETWORK_STATE there is nothing to decide; leave routing alone.
        }
    }
}
