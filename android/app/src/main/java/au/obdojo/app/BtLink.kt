package au.obdojo.app

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID

/**
 * Bluetooth Classic (SPP) link to the OBDojo unit - the same Bluetooth that Torque Pro and the
 * tracker use. App commands are sent as "!<command>\r" and the unit answers with one line of
 * JSON, so the ELM327 side keeps working for Torque. Works with any OBDojo firmware.
 *
 * The unit takes one Bluetooth connection at a time: while the app is connected, Torque and
 * the tracker can't be, and the other way round.
 */
@SuppressLint("MissingPermission")   // MainActivity asks for BLUETOOTH_CONNECT before we're used
class BtLink(ctx: Context) : ObdLink {

    private val adapter: BluetoothAdapter? =
        (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
    private var socket: BluetoothSocket? = null
    private var out: OutputStream? = null
    private var readerJob: Job? = null
    private val lines = Channel<String>(Channel.UNLIMITED)
    private val lock = Mutex()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    @Volatile private var alive = false

    val hasBluetooth get() = adapter != null
    val isOn get() = try { adapter?.isEnabled == true } catch (_: SecurityException) { false }

    /** Paired devices as (name, address), OBDojo units first. */
    fun pairedDevices(): List<Pair<String, String>> = try {
        (adapter?.bondedDevices ?: emptySet())
            .map { (it.name ?: it.address) to it.address }
            .sortedWith(compareByDescending<Pair<String, String>> { it.first.contains("obdojo", true) }.thenBy { it.first })
    } catch (_: SecurityException) { emptyList() }

    /** Connect to the paired unit with this MAC address. Returns the status JSON. */
    suspend fun connect(address: String): String {
        disconnect()
        val a = adapter ?: throw WifiLink.LinkException("This phone has no Bluetooth")
        if (!isOn) throw WifiLink.LinkException("Turn Bluetooth on, then tap Connect")
        if (address.isBlank()) throw WifiLink.LinkException("Pick your OBDojo unit from the paired devices list")
        val dev = try { a.getRemoteDevice(address) } catch (_: IllegalArgumentException) {
            throw WifiLink.LinkException("Pick your OBDojo unit from the paired devices list")
        }
        val name = dev.name ?: address
        withContext(Dispatchers.IO) {
            try { a.cancelDiscovery() } catch (_: Exception) {}
            var s: BluetoothSocket? = null
            for (attempt in 0..1) {
                val tryS = try {
                    if (attempt == 0) dev.createRfcommSocketToServiceRecord(SPP)
                    else dev.createInsecureRfcommSocketToServiceRecord(SPP)
                } catch (_: Exception) { null } ?: continue
                try { tryS.connect(); s = tryS; break } catch (_: Exception) { try { tryS.close() } catch (_: Exception) {} }
            }
            val sock = s ?: throw WifiLink.LinkException(
                "Couldn't connect to $name. Is it powered, and is Torque or the tracker already connected to it?"
            )
            socket = sock
            out = sock.outputStream
            alive = true
            startReader(sock.inputStream)
        }
        return try {
            cmd("status", 5000)
        } catch (e: Exception) {
            disconnect()
            throw WifiLink.LinkException("Connected to $name but it didn't answer. Is it running the OBDojo firmware?")
        }
    }

    private fun startReader(ins: InputStream) {
        while (lines.tryReceive().isSuccess) { /* drop anything left from a previous connection */ }
        readerJob = scope.launch {
            val line = ByteArrayOutputStream()
            val buf = ByteArray(2048)
            try {
                while (isActive) {
                    val n = ins.read(buf)
                    if (n < 0) break
                    for (i in 0 until n) {
                        val b = buf[i]
                        if (b == '\n'.code.toByte() || b == '\r'.code.toByte()) {
                            if (line.size() > 0) { lines.trySend(line.toString("UTF-8")); line.reset() }
                        } else if (line.size() < 512 * 1024) line.write(b.toInt())
                    }
                }
            } catch (_: Exception) {}
            alive = false
        }
    }

    override suspend fun cmd(c: String, timeoutMs: Int): String = lock.withLock {
        val o = out
        if (o == null || !alive) throw WifiLink.LinkException("Bluetooth link to OBDojo dropped")
        while (lines.tryReceive().isSuccess) { /* stale replies from a request that timed out */ }
        try {
            withContext(Dispatchers.IO) {
                o.write(("!" + c.replace('\r', ' ').replace('\n', ' ') + "\r").toByteArray(Charsets.UTF_8))
                o.flush()
            }
        } catch (e: Exception) {
            alive = false
            throw WifiLink.LinkException("Bluetooth link to OBDojo dropped")
        }
        val deadline = System.currentTimeMillis() + timeoutMs
        while (true) {
            val left = deadline - System.currentTimeMillis()
            if (left <= 0) break
            val l = withTimeoutOrNull(left) { lines.receive() } ?: break
            if (l.startsWith("{")) return@withLock l       // skip any ELM327 leftovers like ">"
        }
        if (!alive) throw WifiLink.LinkException("Bluetooth link to OBDojo dropped")
        throw WifiLink.LinkException("OBDojo didn't reply")
    }

    override suspend fun live(): String = cmd("live", 3000)

    override suspend fun logCsv(): String {
        val sb = StringBuilder()
        var start = 0
        while (true) {
            val j = JSONObject(cmd("logrows $start 40", 10000))
            if (start == 0) sb.append(j.optString("header"))
            sb.append(j.optString("rows"))
            val n = j.optInt("n")
            start += n
            if (n == 0 || start >= j.optInt("total")) break
        }
        return sb.toString()
    }

    override fun disconnect() {
        alive = false
        readerJob?.cancel()
        readerJob = null
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        out = null
    }

    companion object {
        /** Standard Serial Port Profile UUID - what the ESP32's BluetoothSerial advertises. */
        val SPP: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }
}
