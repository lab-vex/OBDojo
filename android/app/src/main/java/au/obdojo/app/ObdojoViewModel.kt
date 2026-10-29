package au.obdojo.app

import android.app.Application
import android.content.Context
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject

/** A Kawasaki DFI service code. [list] = "Current" or "Stored", [st] = the status byte after it. */
data class Dtc(val code: String, val desc: String, val list: String, val st: String)

data class SigDef(val key: String, val name: String, val unit: String)

/** One row of the register monitor or watch view. chg = changed just now, mk = changed since Mark. */
data class ByteRow(
    val id: String, val name: String, val bytes: List<String>, val chg: Int, val mk: Int,
    val ranges: String, val stale: Boolean
)

/** A register that answered the sweep */
data class SweepItem(val reg: String, val name: String, val hex: String)

/** One line of the K-line test. ok = null means "couldn't tell" */
data class KStep(val name: String, val ok: Boolean?, val msg: String)

data class Field(
    val key: String, val label: String, val group: String, val type: String,
    val value: String, val reboot: Boolean, val options: List<Pair<String, String>>,
    val isSet: Boolean
)

enum class LinkState { IDLE, CONNECTING, CONNECTED }

class ObdojoViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences("obdojo", Context.MODE_PRIVATE)
    val wifi = WifiLink(app)
    val bt = BtLink(app)
    val demoLink = DemoLink()
    /** True while connected to the pretend unit (no hardware needed). */
    val demo = mutableStateOf(false)

    /** "bt" = Bluetooth (default), "wifi" = the OBDojo hotspot. */
    val transport = mutableStateOf(prefs.getString("transport", "bt")!!)
    val btAddr = mutableStateOf(prefs.getString("btaddr", "")!!)
    val btDevices = mutableStateListOf<Pair<String, String>>()
    /** Set by MainActivity once the Bluetooth permission is granted (always true below Android 12). */
    val btAllowed = mutableStateOf(false)

    val link: ObdLink get() = when { demo.value -> demoLink; transport.value == "bt" -> bt; else -> wifi }
    val linkName get() = when { demo.value -> "Demo mode (simulated unit)"; transport.value == "bt" -> "Bluetooth"; else -> "Wi-Fi" }

    // connection
    val linkState = mutableStateOf(LinkState.IDLE)
    val connectMsg = mutableStateOf("")
    val needLogin = mutableStateOf(false)
    val toast = mutableStateOf<String?>(null)
    val busy = mutableStateOf<String?>(null)

    // app-side settings (how to reach the unit)
    val ssid = mutableStateOf(prefs.getString("ssid", "OBDojo")!!)
    val wifiPass = mutableStateOf(prefs.getString("wpass", "obdojo1234")!!)
    val host = mutableStateOf(prefs.getString("host", DEFAULT_HOST)!!)
    val webUser = mutableStateOf(prefs.getString("wuser", "admin")!!)
    val webPass = mutableStateOf(prefs.getString("wpw", "")!!)
    val autoConnect = mutableStateOf(prefs.getBoolean("auto", true))
    val keepScreenOn = mutableStateOf(prefs.getBoolean("screen", true))

    // device status
    val status = mutableStateOf(JSONObject())
    val live = mutableStateMapOf<String, Double?>()
    val ecu = mutableStateOf(false)
    val fi = mutableStateOf(-1)
    val dtcCount = mutableStateOf(-1)
    val liveMode = mutableStateOf("live")
    val rate = mutableStateOf(0.0)
    val units = mutableStateOf(0)
    val tempWarn = mutableStateOf(105.0)
    val rpmWarn = mutableStateOf(12500.0)
    val vbatLow = mutableStateOf(11.8)
    val peaks = mutableStateMapOf<String, Double>()
    val sigDefs = mutableStateListOf<SigDef>()
    val tps = mutableStateOf(JSONObject())
    private var liveMs = 250L

    // codes
    val codesRead = mutableStateOf(false)
    val dtcs = mutableStateListOf<Dtc>()
    val codesInfo = mutableStateOf("")
    val codesRaw = mutableStateOf("")
    val freeze = mutableStateOf<JSONObject?>(null)
    val abs = mutableStateOf<JSONObject?>(null)

    // K-line test + frame trace
    val kSteps = mutableStateListOf<KStep>()
    val kInfo = mutableStateOf<JSONObject?>(null)

    // register sweep
    val sweepOn = mutableStateOf(false)
    val sweepPos = mutableStateOf(-1)
    val sweepInfo = mutableStateOf("Not run yet.")
    val sweepItems = mutableStateListOf<SweepItem>()
    private var sweepJob: Job? = null

    // register monitor + watch
    val monOn = mutableStateOf(false)
    val monReq = mutableStateOf("all")
    val monRows = mutableStateListOf<ByteRow>()
    val monMsg = mutableStateOf("Not running.")
    private var monJob: Job? = null
    val watchOn = mutableStateOf(false)
    val watchReq = mutableStateOf("21 04")
    val watchRow = mutableStateOf<ByteRow?>(null)
    val watchMsg = mutableStateOf("Not running.")
    private var watchPrev: IntArray? = null
    private var watchLo: IntArray? = null
    private var watchHi: IntArray? = null
    private var watchJob: Job? = null

    // tools / log / settings
    val toolOut = mutableStateOf("Pick a tool.")
    val logInfo = mutableStateOf(JSONObject())
    val fields = mutableStateListOf<Field>()
    /** MainActivity saves the text with the file picker: (csv, file name). */
    var onCsvReady: (String, String) -> Unit = { _, _ -> }

    private var liveJob: Job? = null
    private var failStreak = 0

    init { applyLinkSettings() }

    private fun applyLinkSettings() {
        wifi.host = host.value.trim()
        wifi.user = webUser.value.trim()
        wifi.pass = webPass.value
        wifi.onHostFound = { h ->
            host.value = h
            prefs.edit().putString("host", h).apply()
        }
    }

    fun saveLinkSettings() {
        prefs.edit()
            .putString("ssid", ssid.value).putString("wpass", wifiPass.value)
            .putString("host", host.value).putString("wuser", webUser.value).putString("wpw", webPass.value)
            .putString("transport", transport.value).putString("btaddr", btAddr.value)
            .apply()
        applyLinkSettings()
    }

    fun setPref(key: String, v: Boolean) { prefs.edit().putBoolean(key, v).apply() }

    // ---------------- app updates (GitHub Releases) ----------------

    val updater = Updater(app)
    val updAuto = mutableStateOf(prefs.getBoolean("upd_auto", true))
    val ghToken = mutableStateOf(prefs.getString("gh_token", "")!!)
    /** Newer release found: shows the update dialog while non-null. */
    val update = mutableStateOf<Updater.Release?>(null)
    /** Download progress 0..1 while downloading, else null. */
    val updProgress = mutableStateOf<Float?>(null)
    val updChecking = mutableStateOf(false)
    /** One-line result of the last check, shown in the Updates card. */
    val updStatus = mutableStateOf("")
    /** Shows the "uninstall once" dialog: the new APK is signed with a different key. */
    val updSignerMismatch = mutableStateOf(false)
    private var updApk: java.io.File? = null
    private var updWaitingForPermission = false

    fun saveGhToken(t: String) {
        ghToken.value = t.trim()
        prefs.edit().putString("gh_token", ghToken.value).apply()
    }

    /** [manual] = the user tapped Check now; launch checks stay quiet unless there's an update. */
    fun checkForUpdate(manual: Boolean) {
        if (updChecking.value || updProgress.value != null) return
        if (!manual && !updAuto.value) return
        updChecking.value = true
        if (manual) updStatus.value = "Checking…"
        viewModelScope.launch {
            try {
                val r = updater.check(ghToken.value)
                if (r == null) updStatus.value = "Up to date (${Updater.CURRENT})"
                else {
                    updStatus.value = "Version ${r.version} available"
                    // on launch, don't nag about a version the user already said "Later" to
                    if (manual || prefs.getString("upd_skip", "") != r.version) update.value = r
                }
                prefs.edit().putLong("upd_last", System.currentTimeMillis()).apply()
            } catch (e: Exception) {
                updStatus.value = e.message ?: "Update check failed"
                if (manual) toast.value = updStatus.value
            } finally {
                updChecking.value = false
            }
        }
    }

    fun laterUpdate() {
        update.value?.let { prefs.edit().putString("upd_skip", it.version).apply() }
        update.value = null
    }

    fun installUpdate() {
        val r = update.value ?: return
        if (updProgress.value != null) return
        updProgress.value = 0f
        viewModelScope.launch {
            try {
                val apk = updater.download(r, ghToken.value) { updProgress.value = it }
                updApk = apk
                if (!updater.sameSigner(apk)) {
                    update.value = null
                    updSignerMismatch.value = true
                    return@launch
                }
                update.value = null
                launchInstaller()
            } catch (e: Exception) {
                toast.value = e.message ?: "Update failed"
            } finally {
                updProgress.value = null
            }
        }
    }

    private fun launchInstaller() {
        val apk = updApk ?: return
        if (!updater.canInstall()) {
            // Android needs a one-off "allow from this source"; we carry on in onAppResumed()
            updWaitingForPermission = true
            toast.value = "Allow OBDojo to install apps, then come back"
            updater.openInstallPermission()
            return
        }
        updWaitingForPermission = false
        updater.install(apk)
    }

    /** Called from MainActivity.onResume: finish an install that was waiting on the permission. */
    fun onAppResumed() {
        if (updWaitingForPermission) {
            if (updater.canInstall()) launchInstaller()
            else { updWaitingForPermission = false; toast.value = "Update not installed: permission wasn't given" }
        }
    }

    fun setTransport(t: String) {
        transport.value = t
        prefs.edit().putString("transport", t).apply()
        if (t == "bt") refreshBtDevices()
    }

    fun refreshBtDevices() {
        btDevices.clear()
        if (!btAllowed.value) return
        btDevices.addAll(bt.pairedDevices())
        // first time: pick the paired OBDojo unit automatically
        if (btDevices.none { it.second == btAddr.value }) {
            btDevices.firstOrNull { it.first.contains("obdojo", true) }?.let { btAddr.value = it.second }
        }
    }

    fun pickBt(address: String) {
        btAddr.value = address
        prefs.edit().putString("btaddr", address).apply()
    }

    fun btPermissionResult(granted: Boolean) {
        btAllowed.value = granted
        if (granted) refreshBtDevices()
    }

    // ---------------- connection ----------------
    fun connect() {
        if (linkState.value == LinkState.CONNECTING) return
        saveLinkSettings()
        linkState.value = LinkState.CONNECTING
        val useBt = transport.value == "bt"
        connectMsg.value = if (useBt) "Connecting over Bluetooth…" else "Looking for ${ssid.value}…"
        viewModelScope.launch {
            try {
                if (useBt && !btAllowed.value) throw WifiLink.LinkException("Allow the Bluetooth permission first")
                val s = if (useBt) bt.connect(btAddr.value) else wifi.connect(ssid.value, wifiPass.value)
                handle(s)
                failStreak = 0
                linkState.value = LinkState.CONNECTED
                connectMsg.value = ""
            } catch (e: WifiLink.AuthException) {
                linkState.value = LinkState.IDLE
                needLogin.value = true
                connectMsg.value = "OBDojo wants the web login."
            } catch (e: Exception) {
                link.disconnect()
                linkState.value = LinkState.IDLE
                connectMsg.value = e.message ?: "Couldn't connect"
            }
        }
    }

    /** Connect to the built-in pretend unit, to try the app without hardware. */
    fun connectDemo() {
        if (linkState.value != LinkState.IDLE) return
        demo.value = true
        try {
            handle(demoLink.connect())
            failStreak = 0
            linkState.value = LinkState.CONNECTED
            connectMsg.value = ""
            toast.value = "Demo mode: simulated Ninja 300, nothing is real"
        } catch (e: Exception) {
            demoLink.disconnect()
            demo.value = false
            connectMsg.value = e.message ?: "Couldn't start demo mode"
        }
    }

    fun tryAutoConnect() {
        if (!autoConnect.value || linkState.value != LinkState.IDLE) return
        if (transport.value == "bt" && (!btAllowed.value || btAddr.value.isBlank() || !bt.isOn)) return
        connect()
    }

    fun disconnect() {
        stopLive()
        sweepJob?.cancel(); sweepOn.value = false
        monJob?.cancel(); monOn.value = false
        watchJob?.cancel(); watchOn.value = false
        wifi.disconnect()
        bt.disconnect()
        demoLink.disconnect()
        demo.value = false
        linkState.value = LinkState.IDLE
        ecu.value = false
        busy.value = null
    }

    private fun lost(msg: String) {
        disconnect()
        connectMsg.value = msg
    }

    fun login(user: String, pass: String) {
        webUser.value = user
        webPass.value = pass
        needLogin.value = false
        saveLinkSettings()
        connect()
    }

    // ---------------- commands ----------------
    fun send(cmd: String, busyLabel: String? = null, timeoutMs: Int = 15000) {
        if (busyLabel != null) busy.value = busyLabel
        viewModelScope.launch {
            try {
                handle(link.cmd(cmd, timeoutMs))
                failStreak = 0
            } catch (e: WifiLink.AuthException) {
                needLogin.value = true
            } catch (e: Exception) {
                toast.value = e.message ?: "Request failed"
                if (++failStreak >= 3) lost("Lost connection to OBDojo")
            } finally {
                if (busyLabel != null) busy.value = null
            }
        }
    }

    fun startLive() {
        if (liveJob?.isActive == true) return
        liveJob = viewModelScope.launch {
            while (isActive && linkState.value == LinkState.CONNECTED) {
                val t0 = System.currentTimeMillis()
                try {
                    handle(link.live())
                    failStreak = 0
                } catch (e: WifiLink.AuthException) {
                    needLogin.value = true
                    break
                } catch (e: Exception) {
                    if (++failStreak >= 4) { lost("Lost connection to OBDojo"); break }
                }
                delay((liveMs.coerceAtLeast(100) - (System.currentTimeMillis() - t0)).coerceAtLeast(10))
            }
        }
    }

    fun stopLive() { liveJob?.cancel(); liveJob = null }

    fun downloadCsv() {
        busy.value = "Downloading log"
        viewModelScope.launch {
            try { onCsvReady(link.logCsv(), "obdojo_log_${System.currentTimeMillis() / 1000}.csv") }
            catch (e: Exception) { toast.value = e.message ?: "Download failed" }
            finally { busy.value = null }
        }
    }

    /** The sweep results as CSV, built in the app so it works over Bluetooth too. */
    fun exportSweep() {
        if (sweepItems.isEmpty()) { toast.value = "Run the sweep first"; return }
        val sb = StringBuilder("register,name,hex,decimal\n")
        sweepItems.forEach { i ->
            val dec = i.hex.split(" ").filter { it.isNotEmpty() }.joinToString(" ") { (it.toIntOrNull(16) ?: 0).toString() }
            sb.append(i.reg).append(',').append(i.name.replace(",", " ")).append(',').append(i.hex).append(',').append(dec).append('\n')
        }
        onCsvReady(sb.toString(), "obdojo_sweep_${System.currentTimeMillis() / 1000}.csv")
    }

    // ---------------- incoming JSON ----------------
    private fun handle(text: String) {
        val j = try { JSONObject(text) } catch (e: Exception) { return }
        val t = j.optString("t")

        if (!j.optBoolean("ok", true)) {
            val msg = j.optString("msg")
            if (t in TOOL_TYPES) toolOut.value = "Error: $msg"
            if (t == "abs") abs.value = j
            if (t == "freeze") freeze.value = j
            toast.value = msg
            return
        }

        when (t) {
            "status" -> {
                status.value = j
                ecu.value = j.optBoolean("ecu")
                fi.value = j.optInt("fi", -1)
                dtcCount.value = j.optInt("dtc", -1)
                units.value = j.optInt("units", 0)
                tempWarn.value = j.optDouble("temp_warn", 105.0)
                rpmWarn.value = j.optDouble("rpm_warn", 12500.0)
                vbatLow.value = j.optDouble("vbat_low", 11.8)
                liveMs = j.optLong("live_ms", 250)
                j.optJSONObject("tps")?.let { tps.value = it }
                j.optJSONObject("kl")?.let { kInfo.value = it }
                val sg = j.optJSONArray("sigs")
                sigDefs.clear()
                if (sg != null) for (i in 0 until sg.length()) {
                    val o = sg.getJSONObject(i)
                    sigDefs.add(SigDef(o.optString("k"), o.optString("n"), o.optString("u")))
                }
            }
            "live" -> {
                ecu.value = j.optBoolean("ecu")
                fi.value = j.optInt("fi", -1)
                dtcCount.value = j.optInt("dtc", -1)
                liveMode.value = j.optString("mode", "live")
                rate.value = j.optDouble("rate", 0.0)
                for (k in LIVE_KEYS) live[k] = if (!j.has(k) || j.isNull(k)) null else j.optDouble(k)
                j.optJSONObject("pk")?.let { p ->
                    for (k in listOf("rpm", "spd", "ect")) if (p.has(k) && !p.isNull(k)) peaks[k] = p.optDouble(k) else peaks.remove(k)
                }
            }
            "codes" -> {
                codesRead.value = true
                dtcs.clear()
                parseDtcs(j.optJSONArray("current"), "Current")
                parseDtcs(j.optJSONArray("stored"), "Stored")
                fi.value = if (j.optBoolean("fi")) 1 else 0
                val cur = j.optJSONArray("current")?.length() ?: 0
                val sto = j.optJSONArray("stored")?.length() ?: 0
                codesInfo.value = "$cur current · $sto stored" + if (!j.isNull("count") && j.has("count")) " · failure count ${j.optInt("count")}" else ""
                codesRaw.value = "13 01 → ${j.optString("raw_cur").ifEmpty { "no reply" }}\n13 02 → ${j.optString("raw_sto").ifEmpty { "no reply" }}"
            }
            "clear" -> { toast.value = j.optString("msg", "Codes cleared"); send("codes", "Reading codes") }
            "freeze" -> freeze.value = j
            "abs" -> abs.value = j
            "ktest" -> {
                kSteps.clear()
                val a = j.optJSONArray("steps") ?: JSONArray()
                for (i in 0 until a.length()) {
                    val o = a.getJSONObject(i)
                    kSteps.add(KStep(o.optString("k"), if (o.isNull("ok")) null else o.optBoolean("ok"), o.optString("msg")))
                }
                kInfo.value = j
            }
            "kline" -> {
                kInfo.value = j
                val a = j.optJSONArray("trace") ?: JSONArray()
                toolOut.value = if (a.length() == 0) "Nothing yet." else (0 until a.length()).joinToString("\n") {
                    val o = a.getJSONObject(it)
                    o.optLong("ms").toString().padStart(8) + "  " + when (o.optString("d")) { "T" -> "→ "; "R" -> "← "; else -> "· " } + o.optString("x")
                }
            }
            "sweep" -> parseSweep(j)
            "mon" -> parseMon(j)
            "tps" -> { tps.value = j; toast.value = "Throttle range: ${j.optInt("min")} – ${j.optInt("max")}" }
            in TOOL_TYPES -> toolOut.value = formatTool(j)
            "settings" -> parseSettings(j.optJSONArray("fields"))
            "set" -> {}
            "log" -> logInfo.value = j
            "peak" -> { peaks.clear(); toast.value = j.optString("msg") }
            "reboot", "factory" -> { toast.value = j.optString("msg"); lost("OBDojo is rebooting. Tap Connect in a few seconds.") }
        }
    }

    private fun parseDtcs(a: JSONArray?, list: String) {
        if (a == null) return
        for (i in 0 until a.length()) {
            val o = a.getJSONObject(i)
            dtcs.add(Dtc(o.optString("code"), o.optString("desc"), list, o.optString("st")))
        }
    }

    private fun parseSettings(a: JSONArray?) {
        if (a == null) return
        fields.clear()
        for (i in 0 until a.length()) {
            val o = a.getJSONObject(i)
            val opts = o.optString("o").split("|").filter { it.contains(":") }
                .map { val p = it.split(":", limit = 2); p[0] to p[1] }
            fields.add(
                Field(
                    o.optString("k"), o.optString("l"), o.optString("g"), o.optString("t"),
                    o.optString("v"), o.optBoolean("r"), opts, o.optBoolean("set")
                )
            )
        }
    }

    private fun formatTool(j: JSONObject): String {
        return when (j.optString("t")) {
            "raw" -> "Sent: ${j.optString("req")}\nGot:  ${j.optString("hex")}\n      |${j.optString("ascii")}|"
            else -> {
                val a = j.optJSONArray("items") ?: JSONArray()
                if (a.length() == 0) "Nothing answered." else (0 until a.length()).joinToString("\n") {
                    val o = a.getJSONObject(it)
                    o.optString("k") + "\n  " + o.optString("v")
                }
            }
        }
    }

    // ---------------- register sweep ----------------
    fun sweepToggle() {
        if (sweepOn.value) { send("sweep stop"); return }
        send("sweep start")
        pollSweep()
    }

    fun pollSweep() {
        sweepJob?.cancel()
        sweepJob = viewModelScope.launch {
            delay(400)
            while (isActive && linkState.value == LinkState.CONNECTED) {
                try { handle(link.cmd("sweep", 5000)) } catch (_: Exception) {}
                if (!sweepOn.value) break
                delay(1000)
            }
        }
    }

    private fun parseSweep(j: JSONObject) {
        sweepOn.value = j.optBoolean("on")
        val pos = j.optInt("pos", -1)
        sweepPos.value = pos
        sweepItems.clear()
        val a = j.optJSONArray("items") ?: JSONArray()
        for (i in 0 until a.length()) {
            val o = a.getJSONObject(i)
            sweepItems.add(SweepItem(o.optString("r"), o.optString("nm"), o.optString("hex")))
        }
        val counts = "${j.optInt("found")} answered, ${j.optInt("refused")} refused, ${j.optInt("silent")} no reply."
        sweepInfo.value = when {
            pos < 0 -> "Not run yet."
            j.optBoolean("on") -> "Reading register %02X… $counts".format(pos.coerceIn(0, 255))
            j.optBoolean("done") -> "Done in ${j.optLong("ms") / 1000} s. $counts"
            else -> "Stopped. $counts"
        }
    }

    // ---------------- register monitor ----------------
    fun monToggle() {
        if (monOn.value) { monOn.value = false; monJob?.cancel(); send("mon stop"); return }
        monOn.value = true
        monMsg.value = "Starting…"
        monRows.clear()
        monJob?.cancel()
        monJob = viewModelScope.launch {
            var first = true
            while (isActive && monOn.value && linkState.value == LinkState.CONNECTED) {
                try {
                    val r = JSONObject(link.cmd(if (first) "mon start ${monReq.value.ifBlank { "all" }}" else "mon", 5000))
                    if (!r.optBoolean("ok")) { monMsg.value = r.optString("msg"); monOn.value = false; break }
                    parseMon(r)
                    if (!r.optBoolean("on") && !first) { monOn.value = false; break }
                    first = false
                } catch (e: WifiLink.AuthException) { needLogin.value = true; break }
                catch (_: Exception) {}
                delay(700)
            }
        }
    }

    fun monPause() { monJob?.cancel(); if (monOn.value) { monOn.value = false; send("mon stop") } }

    fun monFromSweep() { monReq.value = "all"; if (!monOn.value) monToggle() }

    private fun parseMon(j: JSONObject) {
        val a = j.optJSONArray("regs") ?: JSONArray()
        val rows = mutableListOf<ByteRow>()
        for (i in 0 until a.length()) {
            val o = a.getJSONObject(i)
            val mk = o.optInt("m")
            if (o.optInt("n") == 0) {
                rows.add(ByteRow(o.optString("r"), o.optString("nm"), emptyList(), 0, 0, o.optString("nrc", "waiting…"), true))
                continue
            }
            val lo = o.optString("lo").split(" ")
            val hi = o.optString("hi").split(" ")
            val rg = lo.indices.filter { ((mk shr it) and 1) == 1 }.joinToString("\n") {
                val l = lo[it]; val h = hi.getOrElse(it) { "" }
                "byte $it: $l–$h (${l.toIntOrNull(16) ?: 0}–${h.toIntOrNull(16) ?: 0})"
            }
            rows.add(
                ByteRow(o.optString("r"), o.optString("nm"), o.optString("d").split(" ").filter { it.isNotEmpty() },
                    o.optInt("c"), mk, rg, o.optLong("age") > 5000)
            )
        }
        monRows.clear()
        monRows.addAll(rows)
        monMsg.value = if (rows.isEmpty()) "Nothing to monitor." else "${rows.size} registers, ${j.optDouble("rate")} reads/s"
    }

    // ---------------- watch a request ----------------
    fun watchToggle() {
        if (watchOn.value) { watchOn.value = false; watchJob?.cancel(); return }
        watchOn.value = true
        watchPrev = null
        watchMark()
        watchJob?.cancel()
        watchJob = viewModelScope.launch {
            while (isActive && watchOn.value && linkState.value == LinkState.CONNECTED) {
                try {
                    val j = JSONObject(link.cmd("peek ${watchReq.value}", 5000))
                    if (!j.optBoolean("ok")) watchMsg.value = j.optString("msg")
                    else parseWatch(j)
                } catch (e: WifiLink.AuthException) { needLogin.value = true; break }
                catch (_: Exception) {}
                delay(250)
            }
        }
    }

    fun watchPause() { watchJob?.cancel(); watchOn.value = false }

    fun watchMark() { watchLo = null; watchHi = null }

    private fun parseWatch(j: JSONObject) {
        val hex = j.optString("hex").split(" ").filter { it.isNotEmpty() }
        val b = hex.map { it.toInt(16) }.toIntArray()
        if (watchLo == null || watchLo!!.size != b.size) { watchLo = b.copyOf(); watchHi = b.copyOf() }
        val lo = watchLo!!
        val hi = watchHi!!
        var chg = 0
        var mk = 0
        val prev = watchPrev
        for (i in b.indices) {
            if (prev != null && i < prev.size && prev[i] != b[i]) chg = chg or (1 shl minOf(i, 30))
            if (b[i] < lo[i]) lo[i] = b[i]
            if (b[i] > hi[i]) hi[i] = b[i]
            if (lo[i] != hi[i]) mk = mk or (1 shl minOf(i, 30))
        }
        watchPrev = b
        val rg = b.indices.filter { lo[it] != hi[it] }
            .joinToString("\n") { "byte $it: %02X-%02X (%d-%d)".format(lo[it], hi[it], lo[it], hi[it]) }
        watchRow.value = ByteRow(j.optString("req"), "", hex, chg, mk, rg + (if (j.has("nrc")) "\nECU says: ${j.optString("nrc")}" else ""), false)
        watchMsg.value = ""
    }

    override fun onCleared() { wifi.disconnect(); bt.disconnect(); demoLink.disconnect() }

    companion object {
        const val DEFAULT_HOST = "1.1.1.1"
        val LIVE_KEYS = listOf("rpm", "tps", "tpsv", "sub", "map", "baro", "spd", "ect", "iat", "vbat", "gear",
            "c1", "c2", "c3", "c4")
        val TOOL_TYPES = setOf("ident", "raw")
    }
}
