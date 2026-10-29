package au.obdojo.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontStyle
import kotlin.math.cos
import kotlin.math.sin

// =====================================================================
//  Connect screen
// =====================================================================
@Composable
fun ConnectScreen(vm: ObdojoViewModel, onAskBt: () -> Unit, onBtSettings: () -> Unit) {
    val st = vm.linkState.value
    val useBt = vm.transport.value == "bt"
    var showAdv by remember { mutableStateOf(false) }
    LaunchedEffect(vm.transport.value, vm.btAllowed.value) { if (useBt) vm.refreshBtDevices() }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        Image(
            painterResource(R.drawable.obdojo_icon), contentDescription = "OBDojo",
            modifier = Modifier.align(Alignment.CenterHorizontally).padding(top = 4.dp, bottom = 16.dp)
                .size(132.dp).clip(RoundedCornerShape(28.dp))
        )
        CardBox {
            Text(
                if (st == LinkState.CONNECTING) "Connecting…" else "Not connected",
                fontWeight = FontWeight.ExtraBold, fontSize = 18.sp
            )
            Text(
                vm.connectMsg.value.ifEmpty {
                    if (useBt) "Power the OBDojo unit, pick it below and tap Connect."
                    else "Power the OBDojo unit, then tap Connect. Your phone may ask to join the OBDojo Wi-Fi — allow it."
                },
                color = C.Muted, fontSize = 13.sp, modifier = Modifier.padding(top = 4.dp)
            )
            if (st == LinkState.CONNECTING)
                LinearProgressIndicator(Modifier.fillMaxWidth().padding(top = 12.dp), color = C.Accent, trackColor = C.Card2)
            Spacer(Modifier.height(12.dp))
            KButton(
                "Connect", { vm.connect() }, primary = true,
                enabled = st == LinkState.IDLE && (!useBt || (vm.btAllowed.value && vm.btAddr.value.isNotBlank())),
                modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(8.dp))
            KButton("Try demo mode", { vm.connectDemo() }, enabled = st == LinkState.IDLE, modifier = Modifier.fillMaxWidth())
            Text(
                "No unit handy? Demo mode plays a pretend Ninja 300 ride with a stored code, so you can try every screen.",
                color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp)
            )
        }

        Heading("Connect over")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton("Bluetooth", { vm.setTransport("bt") }, primary = useBt, modifier = Modifier.weight(1f), enabled = st == LinkState.IDLE)
            KButton("Wi-Fi", { vm.setTransport("wifi") }, primary = !useBt, modifier = Modifier.weight(1f), enabled = st == LinkState.IDLE)
        }

        if (useBt) {
            Heading("OBDojo unit")
            CardBox {
                when {
                    !vm.bt.hasBluetooth -> Text("This phone has no Bluetooth. Use Wi-Fi instead.", color = C.Muted)
                    !vm.btAllowed.value -> {
                        Text("OBDojo needs permission to use Bluetooth.", color = C.Muted)
                        Spacer(Modifier.height(8.dp))
                        KButton("Allow Bluetooth", onAskBt, primary = true)
                    }
                    else -> {
                        if (!vm.bt.isOn) {
                            Text("Bluetooth is off.", color = C.Warn, fontWeight = FontWeight.Bold)
                            Spacer(Modifier.height(8.dp))
                        }
                        if (vm.btDevices.isEmpty())
                            Text("No paired devices yet. Pair OBDojo first (below).", color = C.Muted)
                        vm.btDevices.forEach { (name, addr) ->
                            val sel = addr == vm.btAddr.value
                            Row(
                                Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { vm.pickBt(addr) }.padding(vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                RadioButton(sel, { vm.pickBt(addr) }, colors = RadioButtonDefaults.colors(selectedColor = C.Accent))
                                Column(Modifier.weight(1f)) {
                                    Text(name, fontWeight = if (sel) FontWeight.ExtraBold else FontWeight.Normal)
                                    Text(addr, color = C.Muted, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                                }
                            }
                        }
                        Spacer(Modifier.height(8.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            KButton("Pair new", onBtSettings, modifier = Modifier.weight(1f))
                            KButton("Refresh", { vm.refreshBtDevices() }, modifier = Modifier.weight(1f))
                        }
                    }
                }
            }
            Text(
                "First time: tap Pair new, pick OBDojo in your phone's Bluetooth list and enter PIN 1234, then come back. " +
                    "The unit takes one Bluetooth connection at a time, so disconnect Torque (and the tracker, if it's in range) first.",
                color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 8.dp)
            )
        } else {
            Heading("Hotspot")
            CardBox {
                OutlinedTextField(vm.ssid.value, { vm.ssid.value = it }, label = { Text("Wi-Fi name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                PasswordField(vm.wifiPass.value, { vm.wifiPass.value = it }, "Wi-Fi password", Modifier.fillMaxWidth())
                TextButton({ showAdv = !showAdv }) { Text(if (showAdv) "Hide advanced" else "Advanced", color = C.Accent) }
                if (showAdv) {
                    OutlinedTextField(vm.host.value, { vm.host.value = it }, label = { Text("Address") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(vm.webUser.value, { vm.webUser.value = it }, label = { Text("Web login user") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                    Spacer(Modifier.height(8.dp))
                    PasswordField(vm.webPass.value, { vm.webPass.value = it }, "Web login password (if set)", Modifier.fillMaxWidth())
                    Text("Address is 1.1.1.1 on the hotspot. On home Wi-Fi use the IP shown in the unit's Settings > About.",
                        color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
                }
            }
        }
        UpdatesCard(vm)
        Spacer(Modifier.height(24.dp))
    }
}

// =====================================================================
//  Dashboard
// =====================================================================
private data class Gauge(
    val key: String, val label: String, val unit: String, val max: Double, val dp: Int,
    val imperial: ((Double) -> Double)? = null, val impUnit: String = ""
)

// Tiles under the tacho, most-used first. RPM and gear live in the tacho itself.
private val GAUGES = listOf(
    Gauge("tps", "Throttle", "%", 100.0, 1),
    Gauge("spd", "Speed", "km/h", 180.0, 0, imperial = { it * 0.6214 }, impUnit = "mph"),
    Gauge("ect", "Coolant", "°C", 120.0, 0, imperial = { it * 1.8 + 32 }, impUnit = "°F"),
    Gauge("vbat", "Battery", "V", 15.0, 2),
    Gauge("iat", "Intake air", "°C", 80.0, 0, imperial = { it * 1.8 + 32 }, impUnit = "°F"),
    Gauge("map", "Intake pressure", "kPa", 110.0, 0),
    Gauge("tpsv", "Throttle sensor", "V", 5.0, 2),
    Gauge("sub", "Sub-throttle", "V", 5.0, 2),
    Gauge("baro", "Atmospheric", "kPa", 110.0, 1),
)

private const val TACHO_MAX = 14000.0

/** rpm past the shift warning where the shift light goes from flashing white to flashing red */
private const val SHIFT_RED_ABOVE = 300.0

/** Flips on/off every [periodMs] while it's non-null; steady on when null. */
@Composable
private fun rememberBlink(periodMs: Long?): Boolean {
    var on by remember { mutableStateOf(true) }
    LaunchedEffect(periodMs) {
        on = true
        if (periodMs != null) while (true) { kotlinx.coroutines.delay(periodMs); on = !on }
    }
    return on
}

@Composable
fun DashScreen(vm: ObdojoViewModel) {
    DisposableEffect(Unit) {
        vm.startLive()
        onDispose { vm.stopLive() }
    }
    val imp = vm.units.value == 1
    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        when {
            vm.liveMode.value != "live" -> {
                Spacer(Modifier.height(10.dp))
                CardBox(borderColor = C.Warn) {
                    Text(
                        (if (vm.liveMode.value == "sweep") "Register sweep" else "Register monitor") + " running: live values are paused. Stop it in Tools.",
                        color = C.Warn, fontWeight = FontWeight.ExtraBold, fontSize = 13.sp
                    )
                }
            }
            vm.fi.value == 1 -> {
                Spacer(Modifier.height(10.dp))
                CardBox(borderColor = C.Warn) {
                    Text("${vm.dtcCount.value} CURRENT CODE(S). See Codes.", color = C.Warn, fontWeight = FontWeight.ExtraBold, fontSize = 13.sp)
                }
            }
            !vm.ecu.value -> Text("Waiting for the ECU: turn the key on.", color = C.Muted, fontSize = 13.sp, modifier = Modifier.padding(top = 10.dp))
        }
        LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            contentPadding = PaddingValues(vertical = 10.dp)
        ) {
            item(span = { GridItemSpan(3) }) {
                TachoTile(vm.live["rpm"], vm.live["gear"], vm.rpmWarn.value, vm.peaks["rpm"])
            }
            val all = GAUGES + vm.sigDefs.map { Gauge(it.key, it.name, it.unit, 0.0, 2) }
            items(all) { g ->
                val raw = vm.live[g.key]
                val warn = raw != null && when (g.key) {
                    "ect" -> raw >= vm.tempWarn.value
                    "vbat" -> raw < vm.vbatLow.value
                    else -> false
                }
                val shown = raw?.let { if (imp && g.imperial != null) g.imperial.invoke(it) else it }
                val unit = if (imp && g.imperial != null) g.impUnit else g.unit
                val pct = if (g.max <= 0) 0.0 else raw?.let { (it / g.max).coerceIn(0.0, 1.0) } ?: 0.0
                val pk = vm.peaks[g.key]?.let { p -> "max %.0f".format(if (imp && g.imperial != null) g.imperial.invoke(p) else p) }
                val text = if (shown == null) "--" else "%.${g.dp}f".format(shown)
                // overtemp / low battery pulse red, 500 ms on / 500 ms off
                val on = rememberBlink(if (warn) 500L else null)
                Tile(g.label, text, unit, pct.toFloat(), warn && on, raw == null, pk)
            }
            item(span = { GridItemSpan(3) }) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    KButton("Reset peaks", { vm.send("peak reset") })
                    Spacer(Modifier.width(12.dp))
                    if (vm.ecu.value && vm.rate.value > 0)
                        Text("%.1f ECU replies a second".format(vm.rate.value), color = C.Muted, fontSize = 12.sp)
                }
            }
        }
    }
}

/** Tacho: 0-14k arc, red from the shift warning, gear (straight from the ECU) in the middle. */
@Composable
private fun TachoTile(rpm: Double?, gear: Double?, warnAt: Double, peak: Double?) {
    val frac = ((rpm ?: 0.0) / TACHO_MAX).toFloat().coerceIn(0f, 1f)
    val redFrac = (warnAt / TACHO_MAX).toFloat().coerceIn(0f, 1f)
    val stage = when {
        rpm == null || rpm < warnAt -> 0
        rpm < warnAt + SHIFT_RED_ABOVE -> 1
        else -> 2
    }
    val on = rememberBlink(when (stage) { 1 -> 350L; 2 -> 250L; else -> null })
    val lit = if (stage == 2) C.Bad else Color.White
    val warn = stage > 0
    val arcColor = when { !warn -> C.Accent; on -> lit; else -> Color(0xFF4A4A4A) }
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(C.Card)
            .border(if (warn && on) 2.dp else 1.dp, if (warn && on) lit else C.Line, RoundedCornerShape(14.dp))
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(Modifier.size(128.dp), contentAlignment = Alignment.Center) {
            Canvas(Modifier.fillMaxSize()) {
                val sw = 9.dp.toPx()
                val inset = sw / 2 + 1.dp.toPx()
                val arc = Size(size.width - 2 * inset, size.height - 2 * inset)
                val tl = Offset(inset, inset)
                val start = 135f
                val sweep = 270f
                drawArc(C.Track, start, sweep, false, tl, arc, style = Stroke(sw))
                drawArc(C.Bad.copy(alpha = 0.35f), start + sweep * redFrac, sweep * (1 - redFrac), false, tl, arc, style = Stroke(sw))
                if (frac > 0f) drawArc(arcColor, start, sweep * frac, false, tl, arc, style = Stroke(sw, cap = StrokeCap.Round))
                // a tick every 1000 rpm, longer every 2000
                val r1 = arc.width / 2 - sw / 2 - 3.dp.toPx()
                for (i in 0..14) {
                    val a = Math.toRadians((start + sweep * i / 14f).toDouble())
                    val r2 = r1 - (if (i % 2 == 0) 8.dp else 4.dp).toPx()
                    val red = i * 1000.0 >= warnAt
                    drawLine(
                        if (red) C.Bad else Color(0xFFBDBDBD),
                        Offset(center.x + r1 * cos(a).toFloat(), center.y + r1 * sin(a).toFloat()),
                        Offset(center.x + r2 * cos(a).toFloat(), center.y + r2 * sin(a).toFloat()),
                        strokeWidth = 2.dp.toPx()
                    )
                }
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    when {
                        gear == null -> "--"
                        gear >= 0.5 -> "%.0f".format(gear)
                        else -> "N"
                    },
                    fontSize = 44.sp, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic,
                    color = if (gear == null) Color(0xFF555555) else C.Accent
                )
                Text("GEAR", color = C.Muted, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text("RPM", color = C.Muted, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            Text(
                rpm?.let { "%.0f".format(it) } ?: "--", fontSize = 40.sp, fontWeight = FontWeight.ExtraBold,
                fontStyle = FontStyle.Italic,
                color = when { rpm == null -> Color(0xFF555555); !warn -> C.Text; on -> lit; else -> Color(0xFF4A4A4A) }
            )
            Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(C.Track)) {
                Box(Modifier.fillMaxWidth(frac).fillMaxHeight().background(if (warn) androidx.compose.ui.graphics.SolidColor(arcColor) else C.Glow))
            }
            if (peak != null) Text(
                "max %.0f".format(peak), color = C.Muted, fontSize = 11.sp, fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(top = 6.dp)
            )
        }
    }
}

@Composable
private fun Tile(label: String, value: String, unit: String, pct: Float, warn: Boolean, na: Boolean, peak: String? = null) {
    Box(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(C.Card)
            .border(1.dp, if (warn) C.Bad else C.Line, RoundedCornerShape(10.dp))
    ) {
        Column(Modifier.padding(start = 10.dp, end = 8.dp, top = 8.dp, bottom = 10.dp)) {
            Text(
                label.uppercase(), color = C.Muted, fontWeight = FontWeight.Bold, maxLines = 1, softWrap = false,
                fontSize = if (label.length > 12) 9.sp else 10.sp, letterSpacing = if (label.length > 12) 0.sp else 0.5.sp
            )
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    value, fontSize = 22.sp, fontWeight = FontWeight.ExtraBold, maxLines = 1,
                    color = when { na -> Color(0xFF555555); warn -> C.Bad; else -> C.Text }
                )
                Text(" $unit", color = C.Muted, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, modifier = Modifier.padding(bottom = 3.dp))
            }
            Text(peak ?: " ", color = C.Muted, fontSize = 10.sp, fontWeight = FontWeight.Bold, maxLines = 1)
        }
        Box(
            Modifier.align(Alignment.BottomStart).fillMaxWidth(pct).height(3.dp)
                .background(if (warn) androidx.compose.ui.graphics.SolidColor(C.Bad) else C.Glow)
        )
    }
}

// =====================================================================
//  Codes
// =====================================================================
@Composable
private fun CodeRow(d: Dtc) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(d.code, color = C.Accent, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.ExtraBold, fontSize = 20.sp, modifier = Modifier.width(48.dp))
        Column(Modifier.weight(1f)) {
            Text(d.desc.ifEmpty { "Unknown code" })
            if (d.st.isNotEmpty()) Text("status ${d.st}", color = C.Muted, fontSize = 11.sp)
        }
        Text(
            d.list.uppercase(), fontSize = 11.sp, fontWeight = FontWeight.Bold, color = C.Muted,
            modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(C.Card2).padding(horizontal = 8.dp, vertical = 3.dp)
        )
    }
}

@Composable
fun CodesScreen(vm: ObdojoViewModel) {
    var confirm by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { if (!vm.codesRead.value) vm.send("codes", "Reading codes") }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        CardBox {
            Row(verticalAlignment = Alignment.CenterVertically) {
                val cur = vm.dtcs.count { it.list == "Current" }
                val lampColor = when { !vm.codesRead.value -> Color(0xFF222222); cur > 0 -> C.Warn; else -> Color(0xFF1A3D22) }
                val ring = when { !vm.codesRead.value -> Color(0xFF333333); cur > 0 -> Color(0xFFFFE066); else -> C.Ok }
                Box(Modifier.size(46.dp).clip(CircleShape).background(lampColor).border(3.dp, ring, CircleShape))
                Spacer(Modifier.width(14.dp))
                Column {
                    Text(
                        when { !vm.codesRead.value -> "Codes: not read yet"; cur > 0 -> "$cur current code(s)"; else -> "No current codes" },
                        fontWeight = FontWeight.ExtraBold, fontSize = 18.sp
                    )
                    Text(vm.codesInfo.value.ifEmpty { "Key on, then tap Read" }, color = C.Muted, fontSize = 13.sp)
                }
            }
        }
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton("Read", { vm.send("codes", "Reading codes") }, primary = true, modifier = Modifier.weight(1f))
            KButton("Records", { vm.send("freeze", "Reading failure records") }, modifier = Modifier.weight(1f))
            KButton("Clear", { confirm = true }, danger = true, modifier = Modifier.weight(1f))
        }
        Spacer(Modifier.height(12.dp))
        CardBox {
            if (!vm.codesRead.value) Text("No codes read yet.", color = C.Muted)
            else if (vm.dtcs.isEmpty()) Text("No codes.", color = C.Ok, fontWeight = FontWeight.ExtraBold)
            vm.dtcs.forEachIndexed { i, d ->
                if (i > 0) HorizontalDivider(color = C.Line, modifier = Modifier.padding(vertical = 8.dp))
                CodeRow(d)
            }
            if (vm.codesRaw.value.isNotEmpty())
                Text(vm.codesRaw.value, color = C.Muted, fontSize = 11.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(top = 10.dp))
        }
        vm.freeze.value?.let { f ->
            Spacer(Modifier.height(12.dp))
            CardBox {
                if (!f.optBoolean("ok")) Text(f.optString("msg"), color = C.Muted)
                else {
                    Text("FAILURE RECORDS (${f.optInt("count")})", color = C.Accent, fontWeight = FontWeight.ExtraBold)
                    val a = f.optJSONArray("list")
                    if (a == null || a.length() == 0) Text("None stored.", color = C.Muted, modifier = Modifier.padding(top = 6.dp))
                    else for (i in 0 until a.length()) {
                        val o = a.getJSONObject(i)
                        Row(Modifier.padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(o.optString("code", "?"), color = C.Accent, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.ExtraBold, fontSize = 18.sp, modifier = Modifier.width(48.dp))
                            Text(o.optString("hex"), fontFamily = FontFamily.Monospace, fontSize = 13.sp, modifier = Modifier.weight(1f))
                            Text("#${o.optInt("n")}", color = C.Muted, fontSize = 11.sp)
                        }
                    }
                    Text("Raw replies to 12 n 01 54. The layout of these records is still being worked out.", color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 8.dp))
                }
            }
        }
        Text(
            "These are Kawasaki's two-digit DFI service codes, the same numbers dealer mode shows on the dash (key on, odometer screen, hold the left button 2 s). " +
                "\"Current\" and \"stored\" are the ECU's two code lists (13 01 and 13 02); which is which isn't confirmed on the EX300 yet.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp)
        )

        Heading("ABS (EX300B)")
        KButton("Try the ABS ECU", { vm.send("abs", "Asking the ABS ECU", 30000) })
        Text("Experimental: asks ECU address 28 on the same K-line. The 300's ABS may only talk on its own connector.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
        vm.abs.value?.let { a ->
            Spacer(Modifier.height(10.dp))
            CardBox {
                if (!a.optBoolean("ok")) Text(a.optString("msg"), color = C.Muted)
                else {
                    KV("ABS ECU", a.optString("id"))
                    val list = mutableListOf<Dtc>()
                    for ((key, name) in listOf("current" to "Current", "stored" to "Stored")) {
                        val arr = a.optJSONArray(key) ?: continue
                        for (i in 0 until arr.length()) arr.getJSONObject(i).let { list.add(Dtc(it.optString("code"), it.optString("desc"), name, it.optString("st"))) }
                    }
                    if (list.isEmpty()) Text("No ABS codes.", color = C.Ok, fontWeight = FontWeight.ExtraBold, modifier = Modifier.padding(top = 6.dp))
                    list.forEach { Spacer(Modifier.height(8.dp)); CodeRow(it.copy(desc = "")) }
                    Text(a.optString("msg"), color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 8.dp))
                }
            }
        }
        Spacer(Modifier.height(24.dp))
    }

    if (confirm) AlertDialog(
        onDismissRequest = { confirm = false },
        containerColor = C.Card,
        title = { Text("Clear the codes?", fontWeight = FontWeight.ExtraBold) },
        text = { Text("Asks the ECU to clear its stored codes. Key on, engine off works best. Fix the fault first or the code comes straight back.") },
        confirmButton = { TextButton({ confirm = false; vm.send("clear yes", "Clearing codes") }) { Text("CLEAR", color = C.Bad, fontWeight = FontWeight.ExtraBold) } },
        dismissButton = { TextButton({ confirm = false }) { Text("CANCEL", color = C.Muted) } }
    )
}

@Composable
fun KV(k: String, v: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(k, color = C.Muted, modifier = Modifier.weight(1f))
        Text(v, fontWeight = FontWeight.SemiBold)
    }
}

// =====================================================================
//  Tools
// =====================================================================
@Composable
fun ToolsScreen(vm: ObdojoViewModel) {
    var raw by remember { mutableStateOf("") }
    DisposableEffect(Unit) {
        vm.send("status")
        vm.pollSweep()
        onDispose { vm.monPause(); vm.watchPause() }
    }
    val rawAllowed = vm.status.value.optBoolean("raw")
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        Heading("K-line check", Modifier.padding(top = 0.dp))
        FlowButtons(listOf(
            "Run K-line test" to { vm.send("ktest", "Waking the ECU", 20000) },
            "Frame trace" to { vm.send("kline", "Reading the trace") },
            "ECU info" to { vm.send("ident", "Reading ECU info", 20000) },
        ))
        Text("Key on. Re-wakes the ECU and reports each step: the wake-up pulse, the transceiver echo, the ECU's answer and a register read.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(vertical = 6.dp))
        if (vm.kSteps.isNotEmpty() || vm.kInfo.value != null) CardBox {
            vm.kSteps.forEach { s ->
                Row(Modifier.padding(vertical = 5.dp), verticalAlignment = Alignment.Top) {
                    val (bg, mark) = when (s.ok) { true -> C.Ok to "✓"; false -> C.Bad to "✗"; null -> Color(0xFF444444) to "?" }
                    Box(Modifier.size(22.dp).clip(CircleShape).background(bg), contentAlignment = Alignment.Center) {
                        Text(mark, color = if (s.ok == true) Color.Black else Color.White, fontSize = 13.sp, fontWeight = FontWeight.Bold)
                    }
                    Spacer(Modifier.width(10.dp))
                    Column {
                        Text(s.name, fontWeight = FontWeight.Bold)
                        Text(s.msg, color = C.Muted, fontSize = 12.sp)
                    }
                }
            }
            vm.kInfo.value?.let { k ->
                if (vm.kSteps.isNotEmpty()) HorizontalDivider(color = C.Line, modifier = Modifier.padding(vertical = 8.dp))
                KV("Session", if (k.optBoolean("session")) "open, tester ${k.optString("tester")} → ECU ${k.optString("target")}" else "closed")
                KV("Wake-up pulse", "${k.optDouble("init_ms")} ms")
                KV("Requests", "${k.optInt("req")} sent, ${k.optInt("okc")} answered")
                KV("Problems", "${k.optInt("tmo")} no reply, ${k.optInt("cks")} bad checksum, ${k.optInt("echo_bad")} no echo")
                KV("Wake-ups", "${k.optInt("init_ok")} of ${k.optInt("inits")} answered")
                KV("Speed", "${k.optDouble("rate")} replies/s")
                if (k.optString("err").isNotEmpty()) Text(k.optString("err"), color = C.Warn, fontSize = 12.sp, modifier = Modifier.padding(top = 4.dp))
            }
        }

        Heading("Register sweep")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton(if (vm.sweepOn.value) "Stop" else "Start sweep", { vm.sweepToggle() }, primary = true, modifier = Modifier.weight(1f))
            KButton("CSV", { vm.exportSweep() }, modifier = Modifier.weight(1f))
            KButton("Monitor", { vm.monFromSweep() }, modifier = Modifier.weight(1f))
        }
        Text("Reads every register 00-FF once (about 35 s, key on, engine off is fine) and lists the ones the ECU answers. Live data pauses while it runs.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(vertical = 6.dp))
        val pos = vm.sweepPos.value.coerceIn(0, 256)
        LinearProgressIndicator({ pos / 256f }, Modifier.fillMaxWidth().padding(vertical = 4.dp), color = C.Accent, trackColor = C.Track)
        Text(vm.sweepInfo.value, color = C.Muted, fontSize = 12.sp)
        if (vm.sweepItems.isNotEmpty()) MonoBox {
            vm.sweepItems.forEach { i ->
                Row(Modifier.padding(vertical = 3.dp)) {
                    Text(i.reg, color = C.Accent, fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace, fontSize = 13.sp, modifier = Modifier.width(34.dp))
                    Column(Modifier.weight(1f)) {
                        Text(i.hex, fontFamily = FontFamily.Monospace, fontSize = 13.sp, color = Color(0xFFDDDDDD))
                        if (i.name.isNotEmpty()) Text(i.name, color = C.Muted, fontSize = 11.sp)
                    }
                    Text(i.hex.split(" ").filter { it.isNotEmpty() }.joinToString(" ") { (it.toIntOrNull(16) ?: 0).toString() },
                        fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = C.Muted)
                }
            }
        }

        Heading("Register monitor")
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(vm.monReq.value, { vm.monReq.value = it }, Modifier.weight(1f), placeholder = { Text("all  or  04 05 07 5B") }, singleLine = true)
            Spacer(Modifier.width(8.dp))
            KButton(if (vm.monOn.value) "Stop" else "Start", { vm.monToggle() }, primary = true)
        }
        Row(Modifier.padding(top = 6.dp)) { KButton("Mark", { vm.send("mon mark"); vm.toast.value = "Marked. Now move something." }) }
        Text("Re-reads the registers over and over. Green box = changed just now, green text = changed since Mark. Tap Mark, then twist the throttle, warm the engine or pull the clutch, and watch what moves. Fewer registers = faster updates.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(vertical = 6.dp))
        MonoBox {
            if (vm.monMsg.value.isNotEmpty()) Text(vm.monMsg.value, color = C.Muted, fontSize = 12.sp)
            vm.monRows.forEach { ByteRowView(it) }
        }
        Text("Found one? Point a live value at it in Settings → Live data, or add a Custom signal, e.g. 5E:0:1:b:0.01953:0:Tilt sensor:V",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))

        Heading("Watch a request")
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(vm.watchReq.value, { vm.watchReq.value = it }, Modifier.weight(1f), placeholder = { Text("21 04  or  @28 1A") }, singleLine = true)
            Spacer(Modifier.width(8.dp))
            KButton(if (vm.watchOn.value) "Stop" else "Watch", { vm.watchToggle() }, primary = true)
        }
        Row(Modifier.padding(top = 6.dp)) { KButton("Mark", { vm.watchMark() }) }
        Text("Sends one read request over and over and highlights bytes that move. Byte 0 is the reply code (61 = OK, 7F = refused); for 21 xx the data starts at byte 2. Read-only requests only.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(vertical = 6.dp))
        MonoBox {
            if (vm.watchMsg.value.isNotEmpty()) Text(vm.watchMsg.value, color = C.Muted, fontSize = 12.sp)
            vm.watchRow.value?.let { ByteRowView(it) }
        }

        Heading("Throttle calibration")
        Text("Engine off, key on. Throttle shut: Learn closed. Hold it wide open: Learn open. Sets 0% and 100% on the Dash.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(bottom = 6.dp))
        FlowButtons(listOf(
            "Learn closed" to { vm.send("tps closed", "Reading throttle") },
            "Learn open" to { vm.send("tps open", "Reading throttle") },
            "Defaults" to { vm.send("tps reset") },
        ))
        val t = vm.tps.value
        if (t.has("min")) Text("raw now ${t.optInt("raw")}   closed ${t.optInt("min")}   open ${t.optInt("max")}",
            fontFamily = FontFamily.Monospace, color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))

        Heading("Raw command")
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(raw, { raw = it }, Modifier.weight(1f), placeholder = { Text("e.g. 21 04  or  @28 13 01") }, singleLine = true)
            Spacer(Modifier.width(8.dp))
            KButton("Send", { vm.send("raw $raw", "Sending") }, enabled = rawAllowed && raw.isNotBlank())
        }
        Text(if (!rawAllowed) "Turn on \"Allow raw ECU commands\" in Settings → Show advanced → K-line / ECU." else "Services that could reprogram the ECU are always blocked.",
            color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
        Heading("Output")
        Text(
            vm.toolOut.value, fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = Color(0xFFDDDDDD),
            modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(Color.Black)
                .border(1.dp, C.Line, RoundedCornerShape(10.dp)).padding(12.dp)
        )
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun MonoBox(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(Color.Black)
            .border(1.dp, C.Line, RoundedCornerShape(10.dp)).padding(10.dp),
        content = content
    )
}

/** Register (or request), then each byte: green box = changed just now, green text = changed since Mark. */
@Composable
private fun ByteRowView(r: ByteRow) {
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp).alpha(if (r.stale) 0.45f else 1f)) {
        Text(
            buildAnnotatedString {
                if (r.name.isNotEmpty() || r.bytes.isEmpty()) {
                    withStyle(SpanStyle(color = C.Accent, fontWeight = FontWeight.ExtraBold)) { append(r.id.padEnd(4)) }
                }
                r.bytes.forEachIndexed { i, b ->
                    val now = i < 31 && ((r.chg shr i) and 1) == 1
                    val mk = i < 31 && ((r.mk shr i) and 1) == 1
                    when {
                        now -> withStyle(SpanStyle(background = C.Accent, color = Color.Black, fontWeight = FontWeight.Bold)) { append(b) }
                        mk -> withStyle(SpanStyle(color = C.Accent, fontWeight = FontWeight.Bold)) { append(b) }
                        else -> withStyle(SpanStyle(color = Color(0xFFDDDDDD))) { append(b) }
                    }
                    append(" ")
                }
            },
            fontFamily = FontFamily.Monospace, fontSize = 13.sp
        )
        if (r.name.isNotEmpty()) Text(r.name, fontSize = 11.sp, color = C.Muted)
        if (r.ranges.isNotEmpty()) Text(r.ranges, fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = C.Muted)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FlowButtons(items: List<Pair<String, () -> Unit>>) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items.forEach { (t, f) -> KButton(t, f) }
    }
}

// =====================================================================
//  Log
// =====================================================================
@Composable
fun LogScreen(vm: ObdojoViewModel, onExport: () -> Unit) {
    LaunchedEffect(Unit) { vm.send("log status") }
    val l = vm.logInfo.value
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        CardBox {
            val on = l.optBoolean("on")
            Row { Text("Status", color = C.Muted, modifier = Modifier.weight(1f)); Text(if (on) "LOGGING" else "Stopped", color = if (on) C.Ok else C.Muted, fontWeight = FontWeight.ExtraBold) }
            KV("Samples", "${l.optInt("n")} / ${l.optInt("max")}")
            KV("Interval", "${l.optInt("ms")} ms")
            KV("Capacity", "${l.optInt("max") * l.optInt("ms") / 60000} min")
        }
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton("Start", { vm.send("log start") }, primary = true, modifier = Modifier.weight(1f))
            KButton("Stop", { vm.send("log stop") }, modifier = Modifier.weight(1f))
            KButton("Clear", { vm.send("log clear") }, modifier = Modifier.weight(1f))
        }
        Spacer(Modifier.height(8.dp))
        KButton("Export CSV", onExport, modifier = Modifier.fillMaxWidth())
        Text(
            "Logs RPM, throttle (% and volts), sub-throttle, MAP, atmospheric pressure, speed, temps, battery, gear and your custom signals into the unit's memory. Lost on power off, so export before you unplug.",
            color = C.Muted, fontSize = 13.sp, modifier = Modifier.padding(top = 12.dp)
        )
    }
}

// =====================================================================
//  Settings
// =====================================================================
@Composable
fun SettingsScreen(vm: ObdojoViewModel) {
    LaunchedEffect(Unit) { vm.send("settings"); vm.send("status") }
    val edits = remember { mutableStateMapOf<String, String>() }
    var askReboot by remember { mutableStateOf(false) }
    var askFactory by remember { mutableStateOf(false) }
    var showAdv by remember { mutableStateOf(false) }
    var askBtOff by remember { mutableStateOf(false) }

    // The unit's settings, regrouped for the app: everyday ones up top, the rest under Advanced.
    val tidy = vm.fields.map { f ->
        f.copy(
            group = SETTING_GROUP[f.group] ?: f.group,
            label = SETTING_LABEL[f.key] ?: f.label
        )
    }
    val advKeys = ADVANCED.flatMap { it.second }.toSet()
    fun onEditFor(f: Field): (String) -> Unit = { v ->
        // switching Bluetooth off cuts the app (and Torque) off, so make sure first
        if (f.key == "trk_on" && v == "0" && (edits[f.key] ?: f.value) == "1") askBtOff = true
        else edits[f.key] = v
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
        Heading("App", Modifier.padding(top = 0.dp))
        CardBox {
            SwitchRow("Auto-connect to last unit", vm.autoConnect.value) { vm.autoConnect.value = it; vm.setPref("auto", it) }
            SwitchRow("Keep screen on", vm.keepScreenOn.value) { vm.keepScreenOn.value = it; vm.setPref("screen", it) }
            Spacer(Modifier.height(8.dp))
            KV("Connected over", vm.linkName)
            Spacer(Modifier.height(8.dp))
            KButton("Disconnect", { vm.disconnect() })
        }
        UpdatesCard(vm)

        tidy.filter { it.key !in advKeys }.groupBy { it.group }.forEach { (group, list) ->
            Heading(group)
            list.forEach { SettingField(it, edits[it.key], onEditFor(it)) }
        }
        if (vm.fields.isEmpty()) Text("Loading settings…", color = C.Muted, modifier = Modifier.padding(top = 12.dp))
        else {
            Spacer(Modifier.height(10.dp))
            KButton(if (showAdv) "Hide advanced" else "Show advanced", { showAdv = !showAdv }, modifier = Modifier.fillMaxWidth())
            if (showAdv) {
                Text(
                    "Home Wi-Fi, hotspot extras, Bluetooth on/off, K-line timing and addresses, register map, custom signals, wiring and sleep. Most people never need these.",
                    color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 8.dp)
                )
                ADVANCED.forEach { (title, keys) ->
                    val list = keys.mapNotNull { k -> tidy.firstOrNull { it.key == k } }
                    if (list.isNotEmpty()) {
                        Heading(title)
                        list.forEach { SettingField(it, edits[it.key], onEditFor(it)) }
                    }
                }
            }
        }
        Text("↻ = takes effect after reboot", color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 10.dp))

        Spacer(Modifier.height(14.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton("Save", {
                var reboot = false
                edits.forEach { (k, v) ->
                    val f = vm.fields.firstOrNull { it.key == k } ?: return@forEach
                    if (f.type != "pass" && v == f.value) return@forEach
                    if (f.type == "pass" && v.isEmpty()) return@forEach
                    vm.send("set $k $v")
                    if (f.reboot) reboot = true
                }
                edits.clear()
                vm.send("settings"); vm.send("status")
                vm.toast.value = "Saved"
                if (reboot) askReboot = true
            }, primary = true, modifier = Modifier.weight(1f))
            KButton("Reboot", { vm.send("reboot") }, modifier = Modifier.weight(1f))
            KButton("Reset", { askFactory = true }, danger = true, modifier = Modifier.weight(1f))
        }

        Heading("About")
        CardBox {
            val s = vm.status.value
            val kl = s.optJSONObject("kl")
            KV("Firmware", s.optString("fw", "—"))
            KV("ECU", if (s.optBoolean("ecu")) "Connected" + s.optString("ecu_id").let { if (it.isNotEmpty()) " ($it)" else "" } else "Not connected")
            if (kl != null) KV("K-line", if (kl.optBoolean("session")) "tester ${kl.optString("tester")}, ${kl.optDouble("rate")} replies/s" else kl.optString("err").ifEmpty { "no session" })
            KV("Supported registers", if (s.isNull("supp") || !s.has("supp")) "not read yet" else s.optString("supp"))
            KV("Bluetooth (Torque / tracker)", if (s.optBoolean("tracker")) "Connected" else "Not connected")
            KV("Hotspot address", s.optString("ap_ip", "—"))
            KV("Phones on hotspot", s.optInt("wifi_clients").toString())
            KV("Bluetooth MAC", s.optString("bt_mac", "—"))
            KV("Home Wi-Fi IP", s.optString("sta_ip").ifEmpty { "—" })
            KV("Uptime", "${s.optLong("uptime") / 60} min")
            KV("Free memory", "${s.optLong("heap") / 1024} KB")
            KV("App", BuildConfig.VERSION_NAME)
        }
        Spacer(Modifier.height(24.dp))
    }

    if (askReboot) AlertDialog(
        onDismissRequest = { askReboot = false }, containerColor = C.Card,
        title = { Text("Reboot now?", fontWeight = FontWeight.ExtraBold) },
        text = { Text("Some changes only take effect after a reboot. If you changed the hotspot name or password, update them on the Connect screen too.") },
        confirmButton = { TextButton({ askReboot = false; vm.send("reboot") }) { Text("REBOOT", color = C.Accent, fontWeight = FontWeight.ExtraBold) } },
        dismissButton = { TextButton({ askReboot = false }) { Text("LATER", color = C.Muted) } }
    )
    if (askBtOff) AlertDialog(
        onDismissRequest = { askBtOff = false }, containerColor = C.Card,
        title = { Text("Turn Bluetooth off?", fontWeight = FontWeight.ExtraBold) },
        text = {
            Text(
                "This switches the unit's Bluetooth off completely. The OBDojo app, Torque Pro and the tracker won't be able to connect over Bluetooth." +
                    (if (vm.transport.value == "bt" && !vm.demo.value) " You're connected over Bluetooth now, so the app will lose its connection when you save." else "") +
                    "\n\nTo turn it back on, join the OBDojo Wi-Fi and open the web page at ${vm.status.value.optString("ap_ip", "1.1.1.1")} (or use this app over Wi-Fi), then switch it on in Settings and reboot."
            )
        },
        confirmButton = { TextButton({ askBtOff = false; edits["trk_on"] = "0" }) { Text("TURN OFF", color = C.Bad, fontWeight = FontWeight.ExtraBold) } },
        dismissButton = { TextButton({ askBtOff = false }) { Text("CANCEL", color = C.Muted) } }
    )
    if (askFactory) AlertDialog(
        onDismissRequest = { askFactory = false }, containerColor = C.Card,
        title = { Text("Factory reset?", fontWeight = FontWeight.ExtraBold) },
        text = { Text("All settings go back to defaults and the unit reboots.") },
        confirmButton = { TextButton({ askFactory = false; vm.send("factory yes") }) { Text("RESET", color = C.Bad, fontWeight = FontWeight.ExtraBold) } },
        dismissButton = { TextButton({ askFactory = false }) { Text("CANCEL", color = C.Muted) } }
    )
}

/** The firmware's groups, renamed for the app. */
private val SETTING_GROUP = mapOf("Web page" to "Wi-Fi", "Bluetooth ELM327" to "Bluetooth")

private val SETTING_LABEL = mapOf("trk_on" to "Bluetooth (OBDojo app, Torque Pro, tracker)")

/** Settings shown only under Show advanced, by section. Anything not listed stays in the main list. */
private val ADVANCED = listOf(
    "Home Wi-Fi" to listOf("wifi_mode", "sta_ssid", "sta_pass", "hostname"),
    "Hotspot extras" to listOf("ap_ch", "ap_hidden", "captive"),
    "Bluetooth" to listOf("trk_on", "trk_virt"),
    "K-line / ECU" to listOf("kl_tester", "kl_ecu", "kl_p4", "kl_p3", "allow_raw", "kl_tx", "kl_rx", "kl_cs", "kl_init"),
    "Live data" to listOf("regs", "rpm_form", "tps_min", "tps_max"),
    "Logging" to listOf("log_ms"),
    "Custom signals" to listOf("sig1", "sig2", "sig3", "sig4"),
    "Hardware" to listOf("vbat_pin", "vbat_mult", "led_pin"),
    "Power" to listOf("sleep_min", "sleep_s"),
)

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f))
        Switch(
            checked, onChange,
            colors = SwitchDefaults.colors(checkedThumbColor = Color.Black, checkedTrackColor = C.Accent)
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SettingField(f: Field, edit: String?, onEdit: (String) -> Unit) {
    val label = f.label + if (f.reboot) " ↻" else ""
    val value = edit ?: if (f.type == "pass") "" else f.value
    Box(Modifier.padding(bottom = 8.dp)) {
        when (f.type) {
            "bool" -> CardBox { SwitchRow(label, value == "1") { onEdit(if (it) "1" else "0") } }
            "sel" -> {
                var open by remember { mutableStateOf(false) }
                ExposedDropdownMenuBox(open, { open = it }) {
                    OutlinedTextField(
                        f.options.firstOrNull { it.first == value }?.second ?: value, {}, readOnly = true,
                        label = { Text(label) },
                        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(open) },
                        modifier = Modifier.menuAnchor().fillMaxWidth()
                    )
                    ExposedDropdownMenu(open, { open = false }) {
                        f.options.forEach { (v, l) -> DropdownMenuItem({ Text(l) }, { onEdit(v); open = false }) }
                    }
                }
            }
            "pass" -> PasswordField(
                value, onEdit, label, Modifier.fillMaxWidth(),
                placeholder = if (f.isSet) "•••••• (unchanged)" else "not set"
            )
            else -> OutlinedTextField(
                value, onEdit, label = { Text(label) }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = if (f.type == "int" || f.type == "float") KeyboardType.Decimal else KeyboardType.Text),
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}
