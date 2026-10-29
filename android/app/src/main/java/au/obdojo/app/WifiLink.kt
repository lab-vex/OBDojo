package au.obdojo.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.util.Base64
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlin.coroutines.resume

/**
 * Wi-Fi link to the OBDojo hotspot. Talks to the same HTTP API as the web page:
 *   POST /api  c=<command>   -> one JSON object
 *   GET  /live               -> live data JSON
 *   GET  /log.csv            -> log as CSV
 *
 * The OBDojo hotspot has no internet, so Android would normally send our requests
 * over mobile data instead. We grab the Wi-Fi network explicitly and send every
 * request through it, which also keeps mobile data working for everything else.
 */
class WifiLink(ctx: Context) : ObdLink {

    class AuthException : Exception("Login required")
    class LinkException(msg: String) : Exception(msg)

    private val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private var network: Network? = null
    private var callback: ConnectivityManager.NetworkCallback? = null
    private val lock = Mutex()

    var host = "1.1.1.1"
    var user = ""
    var pass = ""
    /** Called when the unit answered on a different address than [host] (e.g. older firmware). */
    var onHostFound: (String) -> Unit = {}

    val connected get() = network != null

    // ---------------- connecting ----------------

    /** Join (or find) the OBDojo hotspot and check the unit answers. Returns the status JSON. */
    suspend fun connect(ssid: String, password: String): String {
        disconnect()

        // 1. Already on a Wi-Fi network (joined manually, or home Wi-Fi with OBDojo on it)?
        val existing = requestWifi(null, 4000)
        if (existing != null) {
            network = existing.first
            callback = existing.second
            tryHosts(4000)?.let { return it }
            disconnect()
        }

        // 2. Ask Android to join the hotspot for this app only (Android 10+, shows a system prompt)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val spec = WifiNetworkSpecifier.Builder().setSsid(ssid).apply {
                if (password.isNotEmpty()) setWpa2Passphrase(password)
            }.build()
            val joined = requestWifi(spec, 30000)
                ?: throw LinkException("Couldn't join \"$ssid\". Is OBDojo powered and in range?")
            network = joined.first
            callback = joined.second
            tryHosts(5000)?.let { return it }
            disconnect()
            throw LinkException("Joined \"$ssid\" but OBDojo didn't answer at $host")
        }
        throw LinkException("Join the \"$ssid\" Wi-Fi in your phone settings, then tap Connect")
    }

    /** Try the saved address, then the hotspot's gateway (the unit itself). Returns status JSON or null. */
    private suspend fun tryHosts(timeoutMs: Int): String? {
        try { return cmd("status", timeoutMs) } catch (e: AuthException) { throw e } catch (_: Exception) {}
        val gw = gateway() ?: return null
        if (gw == host) return null
        val old = host
        host = gw
        try {
            val s = cmd("status", timeoutMs)
            onHostFound(gw)
            return s
        } catch (e: AuthException) { onHostFound(gw); throw e } catch (_: Exception) {}
        host = old
        return null
    }

    private fun gateway(): String? {
        val n = network ?: return null
        val lp = cm.getLinkProperties(n) ?: return null
        return lp.routes.firstOrNull { it.isDefaultRoute && it.gateway != null }?.gateway?.hostAddress
            ?: lp.linkAddresses.firstOrNull { it.address is java.net.Inet4Address }?.address?.address?.let {
                // no default route: assume the unit is x.x.x.1 on its own subnet
                "${it[0].toInt() and 255}.${it[1].toInt() and 255}.${it[2].toInt() and 255}.1"
            }
    }

    private suspend fun requestWifi(
        spec: android.net.NetworkSpecifier?, timeoutMs: Long
    ): Pair<Network, ConnectivityManager.NetworkCallback>? = withTimeoutOrNull(timeoutMs) {
        suspendCancellableCoroutine<Pair<Network, ConnectivityManager.NetworkCallback>?> { cont ->
            val req = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .apply { if (spec != null) setNetworkSpecifier(spec) }
                .build()
            val cb = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(n: Network) {
                    if (cont.isActive) cont.resume(n to this)
                }
                override fun onUnavailable() {
                    if (cont.isActive) cont.resume(null)
                }
                override fun onLost(n: Network) {
                    if (n == network) network = null
                }
            }
            cm.requestNetwork(req, cb)
            cont.invokeOnCancellation { try { cm.unregisterNetworkCallback(cb) } catch (_: Exception) {} }
        }
    }

    override fun disconnect() {
        callback?.let { try { cm.unregisterNetworkCallback(it) } catch (_: Exception) {} }
        callback = null
        network = null
    }

    // ---------------- requests ----------------

    override suspend fun cmd(c: String, timeoutMs: Int): String =
        request("POST", "/api", "c=" + URLEncoder.encode(c, "UTF-8"), timeoutMs)

    override suspend fun live(): String = request("GET", "/live", null, 3000)

    override suspend fun logCsv(): String = request("GET", "/log.csv", null, 20000)

    private suspend fun request(method: String, path: String, body: String?, timeoutMs: Int): String =
        lock.withLock {
            withContext(Dispatchers.IO) {
                val net = network ?: throw LinkException("Not connected")
                val conn = net.openConnection(URL("http://$host$path")) as HttpURLConnection
                try {
                    conn.requestMethod = method
                    conn.connectTimeout = 4000
                    conn.readTimeout = timeoutMs
                    conn.useCaches = false
                    if (user.isNotEmpty() || pass.isNotEmpty()) {
                        val tok = Base64.encodeToString("$user:$pass".toByteArray(), Base64.NO_WRAP)
                        conn.setRequestProperty("Authorization", "Basic $tok")
                    }
                    if (body != null) {
                        conn.doOutput = true
                        conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                        conn.outputStream.use { it.write(body.toByteArray()) }
                    }
                    when (val code = conn.responseCode) {
                        200 -> conn.inputStream.bufferedReader().use { it.readText() }
                        401 -> throw AuthException()
                        else -> throw LinkException("OBDojo replied HTTP $code")
                    }
                } finally {
                    conn.disconnect()
                }
            }
        }
}
