package au.obdojo.app

import kotlinx.coroutines.delay
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * A pretend OBDojo unit for trying the app without hardware. It answers the same commands with
 * the same JSON as the firmware and plays a looping 45 s Ninja 300 ride: idle, a run up through
 * the gears, a cruise, then slowing back down.
 *
 * Underneath it keeps a pretend ECU register map (the same KDS registers and scaling the firmware
 * decodes), so the register sweep, monitor, watch and custom signals all work on real-looking
 * bytes. One code is stored so the Codes screen has something on it, and settings changes are kept
 * until you leave demo mode.
 */
class DemoLink : ObdLink {

    private var t0 = System.currentTimeMillis()
    private var connected = false

    // ---------------- pretend unit state ----------------
    private val settings = LinkedHashMap<String, String>()
    private var current = mutableListOf<String>()
    private var stored = mutableListOf("13")
    private var absStored = mutableListOf("42")
    private var pkRpm = 0.0
    private var pkSpd = 0.0
    private var pkEct = -1000.0
    private var logOn = false
    private var logStart = 0L
    private var logFrozenN = 0

    // register sweep / monitor
    private var swStart = 0L
    private var swOn = false
    private var swDone = false
    private var swStopPos = -1
    private var monOn = false
    private var monStart = 0L
    private val monRegs = mutableListOf<Int>()
    private val monLo = HashMap<Int, IntArray>()
    private val monHi = HashMap<Int, IntArray>()
    private val monPrev = HashMap<Int, IntArray>()
    private var frozen: Double? = null          // ride time the live values stopped at during a sweep / monitor

    init { factoryReset() }

    private fun factoryReset() {
        settings.clear()
        for (d in DEFS) settings[d.key] = d.def
        // two custom signals, so the dashboard shows what they look like
        settings["sig1"] = "5E:0:1:b:0.01953:0:Tilt sensor:V"
        settings["sig2"] = "44:0:4:b:0.0002778:0:Engine hours:h"
        current = mutableListOf(); stored = mutableListOf("13"); absStored = mutableListOf("42")
    }

    fun connect(): String {
        connected = true
        t0 = System.currentTimeMillis()
        return statusJson().toString()
    }

    override fun disconnect() { connected = false; swOn = false; monOn = false; frozen = null }

    private fun now() = (System.currentTimeMillis() - t0) / 1000.0

    private fun mode() = when { swOn -> "sweep"; monOn -> "mon"; else -> "live" }

    // ---------------- the ride ----------------
    /** Engine rpm per km/h in each gear: primary 3.087, final 42/14, 140/70-17 rear. */
    private val ratio = listOf(217.0, 143.0, 113.0, 93.0, 80.0, 71.4)

    /** Engine values at [t] seconds since connecting. */
    private fun sample(t: Double): Map<String, Double> {
        val p = t % 45.0
        val jit = sin(t * 7.3) * 25 + sin(t * 3.1) * 15
        var rpm: Double
        var spd: Double
        var tps: Double
        var gear: Int
        when {
            p < 6 -> { rpm = 1350 + jit; spd = 0.0; tps = 0.0; gear = 0 }
            p < 24 -> {                                    // 3 s in each gear
                val gi = ((p - 6) / 3).toInt().coerceIn(0, 5)
                val f = ((p - 6) % 3) / 3
                // shift at 12,800 rpm (past the 12,500 warning, so it shows red); the revs drop by
                // the ratio change. 6th eases off into the cruise.
                val top = if (gi < 5) 12800.0 else 7000.0
                val bottom = if (gi == 0) 4000.0 else 12800.0 * ratio[gi] / ratio[gi - 1]
                rpm = bottom + (top - bottom) * f
                spd = rpm / ratio[gi]
                tps = if (gi < 5) 90 + 10 * f else 32.0
                gear = gi + 1
            }
            p < 34 -> { rpm = 7000 + jit; spd = rpm / ratio[5]; tps = 26 + sin(t) * 3; gear = 6 }
            else -> {                                      // braking, changing down
                val f = (p - 34) / 11
                spd = 98 * (1 - f)
                if (spd < 10) { rpm = 1350 + jit; tps = 0.0; gear = 0 }
                else {
                    val gi = (5 downTo 0).firstOrNull { spd * ratio[it] >= 3500 } ?: 0
                    rpm = spd * ratio[gi]; tps = 0.0; gear = gi + 1
                }
            }
        }
        val warm = min(1.0, t / 300.0)                     // 5 min warm-up
        // the sub-throttle (ECU-driven) lags the main one and stops short of fully open
        val subPct = min(tps * 0.9, 85.0)
        return mapOf(
            "rpm" to rpm, "tps" to tps, "spd" to spd, "gear" to gear.toDouble(),
            "map" to if (tps == 0.0 && spd > 10) 26.0 else 36 + tps * 0.62,
            "baro" to 100.9,
            "ect" to 45 + 47 * warm + sin(t / 9) * 1.5,
            "iat" to 31 + sin(t / 20) * 2,
            "vbat" to if (rpm > 2500) 14.1 + sin(t) * 0.05 else 13.3,
            "sub" to 0.62 + subPct / 100 * 3.3,
            "tilt" to 2.5 + sin(t * 1.9) * 0.04,
            "hours" to 512.3 * 3600 + t,
        )
    }

    private fun tpsRaw(tps: Double): Int {
        val lo = settings["tps_min"]!!.toInt()
        val hi = settings["tps_max"]!!.toInt()
        return (lo + (hi - lo) * tps / 100).roundToInt().coerceIn(0, 1023)
    }

    /** The bytes the pretend ECU sends back for 21 [reg], or null for 7F 21 10. */
    private fun regBytes(reg: Int, s: Map<String, Double>): IntArray? {
        if (reg % 0x20 == 0) {                             // supported-register bitmaps
            if (reg > 0x60) return null
            val b = IntArray(4)
            for (r in reg + 1..reg + 0x20) if (r in SUPP || (r == reg + 0x20 && r <= 0x60)) {
                val i = r - reg - 1
                b[i / 8] = b[i / 8] or (0x80 shr (i % 8))
            }
            return b
        }
        if (reg !in SUPP) return null
        fun two(v: Int) = intArrayOf(v shr 8 and 0xFF, v and 0xFF)
        return when (reg) {
            0x02 -> intArrayOf(0x00)
            0x04 -> two(tpsRaw(s["tps"]!!))
            0x05 -> two((s["map"]!! / (950.0 / 65536 * 0.133322)).roundToInt().coerceIn(0, 65535))
            0x06 -> intArrayOf((s["ect"]!! * 1.6 + 48).roundToInt().coerceIn(0, 255))
            0x07 -> intArrayOf((s["iat"]!! * 1.6 + 48).roundToInt().coerceIn(0, 255))
            0x08 -> intArrayOf((s["baro"]!! / (950.0 / 256 * 0.133322)).roundToInt().coerceIn(0, 255))
            0x09 -> { val r = s["rpm"]!!.toInt(); intArrayOf(r / 100, (r % 100) * 256 / 100) }
            0x0A -> intArrayOf((s["vbat"]!! * 256 / 20).roundToInt().coerceIn(0, 255))
            0x0B -> intArrayOf(s["gear"]!!.toInt())
            0x0C -> two((s["spd"]!! * 2).roundToInt())
            0x44 -> { val v = s["hours"]!!.toLong(); intArrayOf((v shr 24 and 0xFF).toInt(), (v shr 16 and 0xFF).toInt(), (v shr 8 and 0xFF).toInt(), (v and 0xFF).toInt()) }
            0x56 -> intArrayOf(stored.size)
            0x5B -> intArrayOf((s["sub"]!! * 256 / 5).roundToInt().coerceIn(0, 255))
            0x5E -> intArrayOf((s["tilt"]!! * 256 / 5).roundToInt().coerceIn(0, 255))
            0x5F -> intArrayOf(0xCC)
            0x61 -> intArrayOf(0xCB)
            0x62 -> intArrayOf(0x01)
            else -> intArrayOf(0x00)
        }
    }

    private fun sigOn(i: Int) = (settings["sig$i"] ?: "").split(":").size >= 6

    /** Decodes custom signal [i] exactly as the firmware does: REG:byte:length:b/l:scale:offset:name:unit. */
    private fun sigValue(i: Int, s: Map<String, Double>): Double? {
        val f = (settings["sig$i"] ?: "").split(":")
        if (f.size < 6) return null
        val reg = f[0].toIntOrNull(16) ?: return null
        val at = f[1].toIntOrNull() ?: return null
        val len = (f[2].toIntOrNull() ?: 1).coerceIn(1, 4)
        val d = regBytes(reg, s) ?: return null
        if (at + len > d.size) return null
        var v = 0L
        for (k in 0 until len) {
            val b = d[if (f[3] == "l") at + len - 1 - k else at + k]
            v = (v shl 8) or b.toLong()
        }
        return v * (f[4].toDoubleOrNull() ?: 1.0) + (f[5].toDoubleOrNull() ?: 0.0)
    }

    /** Live values decoded from the register bytes, the way the firmware's defaults decode them. */
    private fun values(t: Double): Map<String, Double?> {
        val s = sample(t)
        val tr = tpsRaw(s["tps"]!!)
        val lo = settings["tps_min"]!!.toDouble()
        val hi = settings["tps_max"]!!.toDouble()
        val rb = regBytes(0x09, s)!!
        return mapOf(
            "rpm" to rb[0] * 100.0 + rb[1] * 100.0 / 256,
            "tps" to ((tr - lo) / (hi - lo) * 100).coerceIn(0.0, 100.0),
            "tpsv" to tr * 5.0 / 1024,
            "sub" to regBytes(0x5B, s)!![0] * 5.0 / 256,
            "map" to regBytes(0x05, s)!!.let { (it[0] * 256 + it[1]) * 950.0 / 65536 * 0.133322 },
            "baro" to regBytes(0x08, s)!![0] * 950.0 / 256 * 0.133322,
            "spd" to regBytes(0x0C, s)!!.let { (it[0] * 256 + it[1]) / 2.0 },
            "ect" to (regBytes(0x06, s)!![0] - 48) / 1.6,
            "iat" to (regBytes(0x07, s)!![0] - 48) / 1.6,
            "vbat" to regBytes(0x0A, s)!![0] * 20.0 / 256,
            "gear" to s["gear"],
            "c1" to if (sigOn(1)) sigValue(1, s) else null,
            "c2" to if (sigOn(2)) sigValue(2, s) else null,
            "c3" to if (sigOn(3)) sigValue(3, s) else null,
            "c4" to if (sigOn(4)) sigValue(4, s) else null,
        )
    }

    private fun round(v: Double, dp: Int): Double {
        var m = 1.0; repeat(dp) { m *= 10 }
        return (v * m).roundToInt() / m
    }

    private fun liveJson(): JSONObject {
        val t = frozen ?: now()
        val v = values(t)
        if (frozen == null) {
            pkRpm = max(pkRpm, v["rpm"]!!); pkSpd = max(pkSpd, v["spd"]!!); pkEct = max(pkEct, v["ect"]!!)
        }
        val o = JSONObject().put("t", "live").put("ms", (now() * 1000).toLong()).put("ecu", true)
        for ((k, dp) in LIVE_DP) {
            val x = v[k]
            o.put(k, if (x == null || x.isNaN()) JSONObject.NULL else round(x, dp))
        }
        o.put("fi", if (current.isEmpty()) 0 else 1).put("dtc", current.size)
        o.put("pk", JSONObject().put("rpm", pkRpm.toInt()).put("spd", pkSpd.toInt()).put("ect", pkEct.toInt()))
        o.put("mode", mode()).put("rate", if (mode() == "live") 15.2 else 16.8)
        o.put("log", logOn)
        return o
    }

    // ---------------- commands ----------------
    override suspend fun live(): String {
        check()
        delay(30)
        return liveJson().toString()
    }

    override suspend fun cmd(c: String, timeoutMs: Int): String {
        check()
        val line = c.trim()
        val sp = line.indexOf(' ')
        val name = if (sp < 0) line else line.substring(0, sp)
        val a = if (sp < 0) "" else line.substring(sp + 1).trim()
        delay(SLOW[name] ?: 60L)
        return when (name) {
            "status", "hello" -> statusJson()
            "live" -> liveJson()
            "codes" -> codesJson()
            "clear" -> if (a != "yes") err("clear", "Send 'clear yes' to confirm") else {
                current.clear(); stored.clear()
                JSONObject().put("t", "clear").put("ok", true).put("msg", "Codes cleared. Cycle the key and check dealer mode.")
            }
            "freeze" -> {
                val l = JSONArray()
                stored.forEachIndexed { i, code ->
                    l.put(JSONObject().put("n", i + 1).put("hex", "52 ${hex(i + 1)} 01 $code 00 5E 31 7A 02").put("code", code))
                }
                JSONObject().put("t", "freeze").put("ok", true).put("count", stored.size).put("list", l)
            }
            "ident" -> {
                val hrs = sample(now())["hours"]!!
                items(listOf(
                    "ECU part number (1A)" to "DEMO-ECU-300",
                    "Keywords" to "EA 8F",
                    "Tester address" to "F1",
                    "ECU address" to "11",
                    "Supported registers" to suppList(),
                    "Engine run time (21 44)" to "%.1f h (%d s, unconfirmed)".format(hrs / 3600, hrs.toLong()),
                ))
            }
            "sweep" -> sweep(a)
            "mon" -> mon(a)
            "peek" -> peek(a, "peek")
            "raw" -> if (settings["allow_raw"] != "1") err("raw", "Raw commands are off (Settings > K-line / ECU)") else peek(a, "raw")
            "kline" -> klineJson()
            "ktest" -> ktestJson()
            "abs" -> {
                val st = JSONArray().apply { absStored.forEach { put(JSONObject().put("code", it).put("desc", "").put("st", "08")) } }
                JSONObject().put("t", "abs").put("ok", true).put("id", "DEMO-ABS").put("current", JSONArray()).put("raw_cur", "53 00")
                    .put("stored", st).put("raw_sto", "53 ${hex(absStored.size)}" + absStored.joinToString("") { " $it 08" })
                    .put("msg", "ABS code numbers aren't the engine's DFI codes; check the ABS section of the manual.")
            }
            "tps" -> tps(a)
            "peak" -> { pkRpm = 0.0; pkSpd = 0.0; pkEct = -1000.0; JSONObject().put("t", "peak").put("ok", true).put("msg", "Peaks reset") }
            "settings" -> settingsJson()
            "set" -> set(a)
            "log" -> log(a)
            "logrows" -> logRows(a)
            "reboot" -> JSONObject().put("t", "reboot").put("ok", true).put("msg", "Rebooting (demo: leaving demo mode)")
            "factory" -> if (a != "yes") err("factory", "Send 'factory yes' to confirm") else {
                factoryReset(); JSONObject().put("t", "factory").put("ok", true).put("msg", "Settings reset (demo: leaving demo mode)")
            }
            "help" -> JSONObject().put("t", "help").put("ok", true).put("msg", "status live codes clear freeze ident sweep mon peek kline ktest abs tps peak raw settings set log logrows reboot factory")
            else -> err(name, "Unknown command $name")
        }.toString()
    }

    override suspend fun logCsv(): String {
        check()
        delay(400)
        val n = logCount()
        val ms = logMs()
        val sb = StringBuilder(logHeader())
        for (i in 0 until n) sb.append(logRow((logStart - t0) / 1000.0 + i * ms / 1000.0, i * ms / 1000.0))
        return sb.toString()
    }

    private fun check() { if (!connected) throw WifiLink.LinkException("Demo mode is off") }

    // ---------------- replies ----------------
    private fun err(t: String, msg: String) = JSONObject().put("t", t).put("ok", false).put("msg", msg)

    private fun suppList() = (1..0xFF).filter { it % 0x20 != 0 && it in SUPP }.joinToString(" ") { hex(it) }

    private fun klJson(o: JSONObject = JSONObject()): JSONObject {
        val reqs = (now() * 15.2).toInt() + 40
        return o.put("session", true).put("tester", "F1").put("target", "11").put("kb", "EA 8F")
            .put("init_ms", 25.0).put("echo", 1).put("req", reqs).put("okc", reqs - 3).put("tmo", 1).put("nrc", 2)
            .put("cks", 0).put("echo_bad", 0).put("inits", 1).put("init_ok", 1).put("rate", 15.2)
            .put("err", "").put("mode", mode())
    }

    private fun statusJson(): JSONObject {
        val sigs = JSONArray()
        for (i in 1..4) if (sigOn(i)) {
            val f = settings["sig$i"]!!.split(":")
            sigs.put(JSONObject().put("k", "c$i").put("n", f.getOrElse(6) { "Signal $i" }).put("u", f.getOrElse(7) { "" }))
        }
        val tr = tpsRaw(sample(now())["tps"]!!)
        return JSONObject().put("t", "status").put("ok", true).put("fw", "1.0.0 (demo)").put("bike", "EX300")
            .put("ecu", true).put("ecu_id", "DEMO-ECU-300").put("kl", klJson())
            .put("fi", if (current.isEmpty()) 0 else 1).put("dtc", current.size)
            .put("uptime", (now() + 600).toLong()).put("heap", 151552).put("tracker", false)
            .put("bt_mac", "DE:30:00:00:00:01").put("wifi_clients", 0).put("ap_ip", settings["ap_ip"]).put("sta_ip", "")
            .put("log", logOn).put("log_n", logCount()).put("log_max", LOG_MAX)
            .put("units", settings["units"]!!.toInt()).put("live_ms", settings["live_ms"]!!.toInt())
            .put("temp_warn", settings["temp_warn"]!!.toInt()).put("rpm_warn", settings["rpm_warn"]!!.toInt())
            .put("vbat_low", settings["vbat_low"]!!.toDouble()).put("raw", settings["allow_raw"] == "1")
            .put("bt_client", false).put("bt_mode", "none")
            .put("tps", JSONObject().put("min", settings["tps_min"]!!.toInt()).put("max", settings["tps_max"]!!.toInt()).put("raw", tr))
            .put("regs", settings["regs"]).put("supp", suppList()).put("sweep", sweepPos())
            .put("sigs", sigs)
    }

    private fun codesJson(): JSONObject {
        fun list(c: List<String>) = JSONArray().apply { c.forEach { put(JSONObject().put("code", it).put("desc", desc(it)).put("st", "08")) } }
        fun raw(c: List<String>) = "53 ${hex(c.size)}" + c.joinToString("") { " $it 08" }
        return JSONObject().put("t", "codes").put("ok", true).put("count", stored.size)
            .put("current", list(current)).put("raw_cur", raw(current))
            .put("stored", list(stored)).put("raw_sto", raw(stored))
            .put("fi", current.isNotEmpty())
    }

    private fun items(list: List<Pair<String, String>>): JSONObject {
        val a = JSONArray()
        for ((k, v) in list) a.put(JSONObject().put("k", k).put("v", v))
        return JSONObject().put("t", "ident").put("ok", true).put("items", a)
    }

    private fun parseHex(s: String) = s.uppercase().split(" ").filter { it.isNotEmpty() }.mapNotNull { it.toIntOrNull(16) }

    /** peek (read-only requests) and raw (anything but the reprogramming services), with an optional @XX target. */
    private fun peek(arg: String, t: String): JSONObject {
        var a = arg.trim()
        var tgt = 0x11
        if (a.startsWith("@")) {
            tgt = a.substring(1).substringBefore(' ').toIntOrNull(16) ?: return err(t, "Use @XX then the request, e.g. @28 1A")
            a = a.substringAfter(' ', "")
        }
        val rq = parseHex(a)
        if (rq.isEmpty()) return err(t, if (t == "peek") "Give a request in hex, e.g. 21 04" else "Give hex bytes, e.g. raw 21 04")
        val s = rq[0]
        if (t == "peek" && s !in listOf(0x21, 0x1A, 0x12, 0x13, 0x17, 0x18, 0x3E))
            return err(t, "Watch only allows read requests (21 1A 12 13 17 18 3E)")
        if (t == "raw" && (s in listOf(0x11, 0x27, 0x2E, 0x3B, 0x3D, 0x85) || s in 0x31..0x3A || (s == 0x10 && rq.getOrNull(1) == 0x85)))
            return err(t, "Blocked: that service can write to or reprogram the ECU")
        if (tgt != 0x11 && tgt != 0x28) return err(t, "No reply")
        val smp = sample(now())
        val reply: List<Int> = when {
            s == 0x21 && tgt == 0x11 -> regBytes(rq.getOrElse(1) { 0 }, smp)?.let { listOf(0x61, rq.getOrElse(1) { 0 }) + it.toList() }
                ?: listOf(0x7F, 0x21, 0x10)
            s == 0x1A -> listOf(0x5A) + (if (tgt == 0x28) "DEMO-ABS" else "DEMO-ECU-300").map { it.code }
            s == 0x13 -> {
                val l = if (tgt == 0x28) absStored else if (rq.getOrNull(1) == 0x02) stored else current
                listOf(0x53, l.size) + l.flatMap { listOf(it.toInt(16), 0x08) }
            }
            s == 0x3E -> listOf(0x7E)
            s == 0x10 -> listOf(0x50) + rq.drop(1)
            else -> listOf(0x7F, s, 0x11)
        }
        val o = JSONObject().put("t", t).put("ok", true).put("req", rq.joinToString(" ") { hex(it) })
            .put("hex", reply.joinToString(" ") { hex(it) })
        if (reply[0] == 0x7F) o.put("nrc", if (reply[2] == 0x10) "general reject" else "service not supported")
        if (t == "raw") o.put("ascii", reply.joinToString("") { if (it in 0x20..0x7E) it.toChar().toString() else "." })
        return o
    }

    private fun klineJson(): JSONObject {
        val tr = JSONArray()
        val base = max((now() * 1000).toLong() - 500, 650L)
        val s = sample(now())
        var ms = base
        tr.put(JSONObject().put("ms", 412).put("d", "I").put("x", "fast init, tester F1"))
        tr.put(JSONObject().put("ms", 463).put("d", "T").put("x", "81 11 F1 81 04"))
        tr.put(JSONObject().put("ms", 491).put("d", "R").put("x", "83 F1 11 C1 EA 8F C0"))
        tr.put(JSONObject().put("ms", 552).put("d", "T").put("x", "82 11 F1 10 80 14"))
        tr.put(JSONObject().put("ms", 580).put("d", "R").put("x", "82 F1 11 50 80 54"))
        for (reg in listOf(0x09, 0x04, 0x0C, 0x09, 0x04, 0x0C, 0x06)) {
            val d = regBytes(reg, s)!!
            val req = listOf(0x82, 0x11, 0xF1, 0x21, reg)
            val rsp = listOf(0x80 + 2 + d.size, 0xF1, 0x11, 0x61, reg) + d.toList()
            tr.put(JSONObject().put("ms", ms).put("d", "T").put("x", (req + (req.sum() and 0xFF)).joinToString(" ") { hex(it) }))
            tr.put(JSONObject().put("ms", ms + 26).put("d", "R").put("x", (rsp + (rsp.sum() and 0xFF)).joinToString(" ") { hex(it) }))
            ms += 66
        }
        return klJson(JSONObject().put("t", "kline").put("ok", true)).put("trace", tr)
    }

    private fun ktestJson(): JSONObject {
        val d = regBytes(0x09, sample(now()))!!
        fun step(k: String, msg: String) = JSONObject().put("k", k).put("ok", true).put("msg", msg)
        val steps = JSONArray()
            .put(step("Wake-up pulse on the bus", "25.0 ms low"))
            .put(step("Transceiver echo", "Our bytes came back on RX"))
            .put(step("ECU answered", "Keywords EA 8F, tester F1"))
            .put(step("Read a register", "21 09 -> 61 09 " + d.joinToString(" ") { hex(it) }))
        return klJson(JSONObject().put("t", "ktest").put("ok", true).put("steps", steps))
    }

    // ---------------- register sweep ----------------
    /** Like the real one: about 35 s for all 256 registers. */
    private fun sweepPos(): Int = when {
        swStart == 0L -> -1
        swOn -> {
            val p = ((System.currentTimeMillis() - swStart) / SWEEP_MS_PER_REG).toInt()
            if (p > 255) { swOn = false; swDone = true; frozen = null; 256 } else p
        }
        swDone -> 256
        else -> swStopPos
    }

    private fun sweep(a: String): JSONObject {
        when (a) {
            "start" -> { swStart = System.currentTimeMillis(); swOn = true; swDone = false; monOn = false; frozen = now() }
            "stop" -> if (swOn) { swStopPos = sweepPos(); swOn = false; frozen = null }
        }
        val pos = sweepPos()
        val s = sample(frozen ?: now())
        val items = JSONArray()
        var found = 0
        var refused = 0
        for (reg in 0 until min(max(pos, 0), 256)) {
            val d = regBytes(reg, s)
            if (d == null) { refused++; continue }
            found++
            items.put(JSONObject().put("r", hex(reg)).put("nm", REG_NAMES[reg] ?: "").put("hex", d.joinToString(" ") { hex(it) }))
        }
        val ms = if (pos < 0) 0L else if (swDone) 256 * SWEEP_MS_PER_REG else System.currentTimeMillis() - swStart
        return JSONObject().put("t", "sweep").put("ok", true).put("on", swOn).put("pos", pos).put("done", pos > 255)
            .put("ms", ms).put("found", found).put("refused", refused).put("silent", 0).put("items", items)
    }

    // ---------------- register monitor ----------------
    private fun mon(arg: String): JSONObject {
        val a = arg.trim()
        if (a.startsWith("start")) {
            val rest = a.removePrefix("start").trim()
            monRegs.clear(); monLo.clear(); monHi.clear(); monPrev.clear()
            swOn = false
            if (rest.isEmpty() || rest == "all") {
                monRegs.addAll((1..0xFF).filter { it % 0x20 != 0 && it in SUPP })
            } else {
                val r = parseHex(rest)
                if (r.isEmpty()) return err("mon", "Give registers in hex, e.g. mon start 04 05 07 5B")
                r.distinct().take(48).forEach { monRegs.add(it) }
            }
            monOn = true; monStart = System.currentTimeMillis(); frozen = now()
        } else if (a == "stop") { monOn = false; frozen = null }
        else if (a == "mark") { monLo.clear(); monHi.clear() }

        val s = sample(now())
        val regs = JSONArray()
        val secs = (System.currentTimeMillis() - monStart) / 1000.0
        for (reg in monRegs) {
            val o = JSONObject().put("r", hex(reg)).put("nm", REG_NAMES[reg] ?: "")
            val d = regBytes(reg, s)
            if (d == null) { regs.put(o.put("n", 0).put("nrc", "general reject")); continue }
            val lo = monLo.getOrPut(reg) { d.copyOf() }
            val hi = monHi.getOrPut(reg) { d.copyOf() }
            val prev = monPrev[reg]
            var chg = 0
            var mk = 0
            for (i in d.indices) {
                if (d[i] < lo[i]) lo[i] = d[i]
                if (d[i] > hi[i]) hi[i] = d[i]
                if (lo[i] != hi[i]) mk = mk or (1 shl i)
                if (prev != null && prev[i] != d[i]) chg = chg or (1 shl i)
            }
            monPrev[reg] = d
            regs.put(o.put("n", max(1, (secs * 16.8 / monRegs.size).toInt())).put("d", d.joinToString(" ") { hex(it) })
                .put("lo", lo.joinToString(" ") { hex(it) }).put("hi", hi.joinToString(" ") { hex(it) })
                .put("c", chg).put("m", mk).put("age", 40))
        }
        return JSONObject().put("t", "mon").put("ok", true).put("on", monOn)
            .put("ms", if (monOn) System.currentTimeMillis() - monStart else 0).put("rate", 16.8).put("regs", regs)
    }

    // ---------------- throttle calibration ----------------
    private fun tps(a: String): JSONObject {
        val raw = tpsRaw(sample(now())["tps"]!!)
        when (a) {
            "closed" -> {
                if (raw >= settings["tps_max"]!!.toInt()) return err("tps", "Closed reading is above the open value. Learn open first or reset")
                settings["tps_min"] = raw.toString()
            }
            "open" -> {
                if (raw <= settings["tps_min"]!!.toInt() + 20) return err("tps", "That's barely above closed. Hold the throttle wide open (engine off)")
                settings["tps_max"] = raw.toString()
            }
            "reset" -> { settings["tps_min"] = "201"; settings["tps_max"] = "895" }
        }
        return JSONObject().put("t", "tps").put("ok", true).put("min", settings["tps_min"]!!.toInt())
            .put("max", settings["tps_max"]!!.toInt()).put("raw", tpsRaw(sample(now())["tps"]!!))
    }

    // ---------------- settings ----------------
    private fun settingsJson(): JSONObject {
        val a = JSONArray()
        for (d in DEFS) {
            val o = JSONObject().put("k", d.key).put("l", d.label).put("g", d.group).put("t", d.type)
                .put("r", d.reboot).put("min", d.min).put("max", d.max)
            if (d.type == "pass") o.put("v", "").put("set", settings[d.key]!!.isNotEmpty()) else o.put("v", settings[d.key])
            if (d.type == "sel") o.put("o", d.opts)
            a.put(o)
        }
        return JSONObject().put("t", "settings").put("ok", true).put("fields", a)
    }

    private fun set(args: String): JSONObject {
        val key = args.substringBefore(' ')
        var v = if (args.contains(' ')) args.substringAfter(' ').trim() else ""
        val d = DEFS.firstOrNull { it.key == key } ?: return err("set", "Unknown setting $key")
        when (d.type) {
            "int", "sel" -> {
                val n = v.toLongOrNull() ?: return err("set", "${d.label}: whole number needed")
                if (n < d.min || n > d.max) return err("set", "${d.label}: must be ${d.min.toInt()}-${d.max.toInt()}")
                v = n.toString()
            }
            "float" -> {
                val f = v.toDoubleOrNull()
                if (f == null || f < d.min || f > d.max) return err("set", "${d.label}: must be ${d.min}-${d.max}")
                v = String.format(java.util.Locale.US, "%.2f", f)
            }
            "bool" -> v = if (v.lowercase() in listOf("1", "true", "on", "yes")) "1" else "0"
            else -> {
                if (v.length > d.max) return err("set", "${d.label}: max ${d.max.toInt()} characters")
                if (v.length < d.min) return err("set", "${d.label}: at least ${d.min.toInt()} characters")
                if (key.startsWith("sig") && v.isNotEmpty() && v.split(":").size < 6)
                    return err("set", "Signal ${key.last()}: Format: REG:byte:length:b/l:scale:offset:name:unit, e.g. 5E:0:1:b:0.01953:0:Tilt sensor:V")
                if (key == "kl_ecu" && v.toIntOrNull(16) == null) return err("set", "${d.label}: two hex digits, e.g. 11")
            }
        }
        settings[key] = v
        return JSONObject().put("t", "set").put("ok", true).put("k", key).put("reboot", d.reboot)
    }

    // ---------------- logging ----------------
    private fun logMs() = (settings["log_ms"]?.toIntOrNull() ?: 500).coerceAtLeast(200)

    private fun logCount(): Int =
        if (!logOn) logFrozenN else min(LOG_MAX, ((System.currentTimeMillis() - logStart) / logMs()).toInt() + 1)

    private fun log(a: String): JSONObject {
        when (a) {
            "start" -> if (!logOn) { logOn = true; logStart = System.currentTimeMillis() }
            "stop" -> { logFrozenN = logCount(); logOn = false }
            "clear" -> { logFrozenN = 0; if (logOn) logStart = System.currentTimeMillis() }
        }
        return JSONObject().put("t", "log").put("ok", true).put("on", logOn).put("n", logCount()).put("max", LOG_MAX).put("ms", logMs())
    }

    private fun logHeader(): String {
        val sb = StringBuilder("seconds,rpm,tps_pct,tps_v,subthrottle_v,map_kpa,baro_kpa,speed_kmh,coolant_c,intake_c,battery_v,gear")
        for (i in 1..4) {
            if (sigOn(i)) {
                val f = settings["sig$i"]!!.split(":")
                sb.append(",").append(f.getOrElse(6) { "signal$i" }.replace(",", " "))
                f.getOrNull(7)?.takeIf { it.isNotEmpty() }?.let { sb.append("_").append(it) }
            } else sb.append(",signal$i")
        }
        return sb.append("\n").toString()
    }

    private fun logRow(t: Double, secs: Double): String {
        val v = values(t)
        val sb = StringBuilder(String.format(java.util.Locale.US, "%.1f", secs))
        for ((k, dp) in LIVE_DP) {
            sb.append(',')
            val x = v[k]
            if (x != null && !x.isNaN()) sb.append(String.format(java.util.Locale.US, "%.${dp}f", x))
        }
        return sb.append('\n').toString()
    }

    private fun logRows(a: String): JSONObject {
        val parts = a.split(" ")
        val start = parts.getOrNull(0)?.toIntOrNull()?.coerceAtLeast(0) ?: 0
        val cnt = (parts.getOrNull(1)?.toIntOrNull() ?: 40).coerceIn(1, 80)
        val total = logCount()
        val ms = logMs()
        val rows = StringBuilder()
        var n = 0
        var i = start
        while (i < total && n < cnt) { rows.append(logRow((logStart - t0) / 1000.0 + i * ms / 1000.0, i * ms / 1000.0)); i++; n++ }
        val o = JSONObject().put("t", "logrows").put("ok", true).put("total", total).put("start", start).put("n", n)
        if (start == 0) o.put("header", logHeader())
        return o.put("rows", rows.toString())
    }

    private class Def(
        val key: String, val label: String, val group: String, val type: String, val def: String,
        val reboot: Boolean, val opts: String, val min: Double, val max: Double
    )

    companion object {
        private const val LOG_MAX = 1500
        private const val SWEEP_MS_PER_REG = 135L

        private fun hex(b: Int) = "%02X".format(b and 0xFF)

        /** Registers the pretend ECU answers (the bitmaps at 00 20 40 60 answer too). */
        private val SUPP = setOf(0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
            0x44, 0x56, 0x5B, 0x5E, 0x5F, 0x61, 0x62)

        /** Live keys and decimal places, in the firmware's order. */
        private val LIVE_DP = listOf(
            "rpm" to 0, "tps" to 1, "tpsv" to 3, "sub" to 3, "map" to 1, "baro" to 1, "spd" to 1, "ect" to 1,
            "iat" to 1, "vbat" to 2, "gear" to 0, "c1" to 2, "c2" to 2, "c3" to 2, "c4" to 2
        )

        /** Slower replies for the commands that take a while on the real unit. */
        private val SLOW = mapOf("codes" to 700L, "freeze" to 600L, "ident" to 900L, "ktest" to 1800L, "abs" to 1500L,
            "clear" to 800L, "tps" to 200L, "raw" to 200L)

        /** Same names as the firmware's table (Z750 / Z1000 captures, unconfirmed on the EX300). */
        private val REG_NAMES = mapOf(
            0x00 to "Supported registers 01-20", 0x02 to "Starter switch", 0x04 to "Main throttle sensor",
            0x05 to "Intake air pressure", 0x06 to "Coolant temp", 0x07 to "Intake air temp",
            0x08 to "Atmospheric pressure", 0x09 to "Engine RPM", 0x0A to "Battery voltage", 0x0B to "Gear position",
            0x0C to "Vehicle speed", 0x20 to "Supported registers 21-40", 0x40 to "Supported registers 41-60",
            0x44 to "Engine run time (s)", 0x56 to "Stored failure count", 0x5B to "Sub-throttle sensor",
            0x5E to "Vehicle-down sensor", 0x5F to "Internal voltage 2", 0x60 to "Supported registers 61-80",
            0x61 to "Internal voltage 3", 0x62 to "Engine stop switch",
        )

        private val DESCS = mapOf(
            "11" to "Main throttle sensor", "12" to "Intake air pressure sensor", "13" to "Intake air temp sensor",
            "14" to "Water temp sensor", "15" to "Atmospheric pressure sensor", "21" to "Crankshaft sensor",
            "24" to "Speed sensor", "25" to "Speed sensor / gear position", "31" to "Vehicle-down sensor",
            "32" to "Sub-throttle sensor", "62" to "Sub-throttle valve actuator",
        )

        private fun desc(code: String) = DESCS[code] ?: ""

        /** Same settings as the firmware (keys, labels, groups, defaults, limits). tools/check_demo_defs.py checks this. */
        private val DEFS = listOf(
            Def("wifi_mode", "Wi-Fi mode", "Wi-Fi", "sel", "0", true, "0:Hotspot only|1:Hotspot + home Wi-Fi|2:Wi-Fi off", 0.0, 2.0),
            Def("ap_ssid", "Hotspot name", "Wi-Fi", "str", "OBDojo", true, "", 1.0, 32.0),
            Def("ap_pass", "Hotspot password (8+ chars, blank = open)", "Wi-Fi", "pass", "obdojo1234", true, "", 0.0, 63.0),
            Def("ap_ip", "Hotspot address (web page IP)", "Wi-Fi", "str", "1.1.1.1", true, "", 7.0, 15.0),
            Def("ap_ch", "Hotspot channel", "Wi-Fi", "int", "6", true, "", 1.0, 13.0),
            Def("ap_hidden", "Hide hotspot name", "Wi-Fi", "bool", "0", true, "", 0.0, 1.0),
            Def("sta_ssid", "Home Wi-Fi name", "Wi-Fi", "str", "", true, "", 0.0, 32.0),
            Def("sta_pass", "Home Wi-Fi password", "Wi-Fi", "pass", "", true, "", 0.0, 63.0),
            Def("hostname", "Network name (.local)", "Wi-Fi", "str", "obdojo", true, "", 1.0, 24.0),
            Def("web_user", "Web login user (blank = no login)", "Web page", "str", "admin", false, "", 0.0, 24.0),
            Def("web_pass", "Web login password (blank = no login)", "Web page", "pass", "", false, "", 0.0, 32.0),
            Def("captive", "Open page automatically on join", "Web page", "bool", "1", true, "", 0.0, 1.0),
            Def("bt_name", "Bluetooth name (Torque / tracker see this)", "Bluetooth ELM327", "str", "OBDojo", true, "", 1.0, 24.0),
            Def("trk_on", "Bluetooth ELM327 link (Torque Pro / tracker)", "Bluetooth ELM327", "bool", "1", true, "", 0.0, 1.0),
            Def("trk_pin", "Bluetooth pairing PIN", "Bluetooth ELM327", "str", "1234", true, "", 4.0, 16.0),
            Def("trk_virt", "Extra Torque PIDs (22F0xx: gear, sub-throttle, custom signals)", "Bluetooth ELM327", "bool", "1", false, "", 0.0, 1.0),
            Def("kl_tx", "K-line TX pin (to MCP2003A TXD)", "K-line / ECU", "int", "17", true, "", 0.0, 33.0),
            Def("kl_rx", "K-line RX pin (to MCP2003A RXD)", "K-line / ECU", "int", "16", true, "", 0.0, 39.0),
            Def("kl_cs", "Transceiver CS pin (0 = CS wired high)", "K-line / ECU", "int", "4", true, "", 0.0, 33.0),
            Def("kl_init", "Init MOSFET pin (0 = pulse through TXD)", "K-line / ECU", "int", "0", true, "", 0.0, 33.0),
            Def("kl_tester", "Tester address", "K-line / ECU", "sel", "0", false, "0:Auto (F1, then F2)|1:F1|2:F2", 0.0, 2.0),
            Def("kl_ecu", "Engine ECU address (hex)", "K-line / ECU", "str", "11", false, "", 2.0, 2.0),
            Def("kl_p4", "Gap between request bytes (ms)", "K-line / ECU", "int", "5", false, "", 0.0, 20.0),
            Def("kl_p3", "Gap after each reply (ms)", "K-line / ECU", "int", "55", false, "", 25.0, 500.0),
            Def("allow_raw", "Allow raw ECU commands (advanced)", "K-line / ECU", "bool", "0", false, "", 0.0, 1.0),
            Def("regs", "Live value registers (name=hex)", "Live data", "str", "rpm=09 tps=04 spd=0C gear=0B ect=06 iat=07 map=05 baro=08 vbat=0A sub=5B", false, "", 0.0, 120.0),
            Def("rpm_form", "RPM formula", "Live data", "sel", "0", false, "0:A×100 + B×100/256|1:A×100 + B", 0.0, 1.0),
            Def("tps_min", "Throttle closed (raw)", "Live data", "int", "201", false, "", 0.0, 1023.0),
            Def("tps_max", "Throttle fully open (raw)", "Live data", "int", "895", false, "", 1.0, 1023.0),
            Def("units", "Units", "Display", "sel", "0", false, "0:Metric|1:Imperial", 0.0, 1.0),
            Def("live_ms", "Screen refresh (ms)", "Display", "int", "250", false, "", 100.0, 5000.0),
            Def("temp_warn", "Coolant warning (C)", "Display", "int", "105", false, "", 60.0, 130.0),
            Def("rpm_warn", "Shift / RPM warning", "Display", "int", "12500", false, "", 1000.0, 15000.0),
            Def("sig1", "Signal 1 (REG:byte:length:b/l:scale:offset:name:unit)", "Custom signals", "str", "", false, "", 0.0, 63.0),
            Def("sig2", "Signal 2", "Custom signals", "str", "", false, "", 0.0, 63.0),
            Def("sig3", "Signal 3", "Custom signals", "str", "", false, "", 0.0, 63.0),
            Def("sig4", "Signal 4", "Custom signals", "str", "", false, "", 0.0, 63.0),
            Def("vbat_pin", "Battery sense pin (0 = off, use 32-39)", "Battery", "int", "0", true, "", 0.0, 39.0),
            Def("vbat_mult", "Battery divider ratio", "Battery", "float", "5.7", false, "", 1.0, 20.0),
            Def("vbat_low", "Low battery warning (V)", "Battery", "float", "11.8", false, "", 9.0, 14.0),
            Def("log_ms", "Log interval (ms)", "Logging", "int", "500", false, "", 200.0, 60000.0),
            Def("log_auto", "Start logging on power-up", "Logging", "bool", "0", false, "", 0.0, 1.0),
            Def("led_pin", "Status LED pin (0 = off)", "System", "int", "2", true, "", 0.0, 33.0),
            Def("sleep_min", "Deep sleep after N min with no ECU and nobody connected (0 = never)", "Power", "int", "0", false, "", 0.0, 240.0),
            Def("sleep_s", "While asleep, check for the ECU every N seconds", "Power", "int", "60", false, "", 10.0, 3600.0),
        )
    }
}
