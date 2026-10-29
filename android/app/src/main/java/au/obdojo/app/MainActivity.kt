package au.obdojo.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.core.content.ContextCompat
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

class MainActivity : ComponentActivity() {

    private val vm: ObdojoViewModel by viewModels()
    private var pendingCsv: String? = null

    private val saveCsv = registerForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val data = pendingCsv ?: return@registerForActivityResult
        pendingCsv = null
        if (uri == null) return@registerForActivityResult
        try {
            contentResolver.openOutputStream(uri)?.use { it.write(data.toByteArray()) }
            Toast.makeText(this, "Saved", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Couldn't save: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    // Android 12+ asks the user before an app may use Bluetooth
    private val btPerm = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        vm.btPermissionResult(ok)
        if (ok) vm.tryAutoConnect()
    }

    private fun hasBtPerm() = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private fun askBt() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) btPerm.launch(Manifest.permission.BLUETOOTH_CONNECT)
        else vm.btPermissionResult(true)
    }

    private fun openBtSettings() {
        try { startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS)) }
        catch (_: Exception) { Toast.makeText(this, "Open Bluetooth in your phone's settings", Toast.LENGTH_LONG).show() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        vm.onCsvReady = { csv, name ->
            pendingCsv = csv
            saveCsv.launch(name)
        }
        vm.btPermissionResult(hasBtPerm())
        if (savedInstanceState == null) {
            vm.checkForUpdate(manual = false)
            if (vm.transport.value == "bt" && !hasBtPerm()) askBt()
            else vm.tryAutoConnect()
        }
        setContent { ObdojoTheme { App() } }
    }

    override fun onResume() {
        super.onResume()
        // back from the phone's Bluetooth settings: pick up a newly paired unit
        vm.btPermissionResult(hasBtPerm())
        // back from "Install unknown apps": carry on with a pending update
        vm.onAppResumed()
    }

    @Composable
    private fun App() {
        val connected = vm.linkState.value == LinkState.CONNECTED
        var tab by remember { mutableStateOf(0) }

        LaunchedEffect(vm.keepScreenOn.value, connected) {
            if (vm.keepScreenOn.value && connected) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        val msg = vm.toast.value
        LaunchedEffect(msg) {
            if (msg != null) { Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show(); vm.toast.value = null }
        }

        Scaffold(
            containerColor = C.Bg,
            topBar = {
                Column {
                    Row(
                        Modifier.fillMaxWidth().background(Color.Black).statusBarsPadding().padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Logo()
                        Spacer(Modifier.weight(1f))
                        StatusPill(connected)
                    }
                    Box(Modifier.fillMaxWidth().height(3.dp).background(C.Glow))
                    vm.busy.value?.let {
                        LinearProgressIndicator(Modifier.fillMaxWidth(), color = C.Accent, trackColor = C.Card2)
                        Text(it, color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(start = 16.dp, top = 4.dp))
                    }
                }
            },
            bottomBar = {
                if (connected) NavigationBar(containerColor = Color.Black) {
                    TABS.forEachIndexed { i, (label, icon) ->
                        NavigationBarItem(
                            selected = tab == i, onClick = { tab = i },
                            icon = { Icon(icon, label) }, label = { Text(label, fontSize = 11.sp) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = C.Accent, selectedTextColor = C.Accent,
                                unselectedIconColor = C.Muted, unselectedTextColor = C.Muted,
                                indicatorColor = C.Card2
                            )
                        )
                    }
                }
            }
        ) { pad ->
            Box(Modifier.padding(pad).fillMaxSize()) {
                if (!connected) ConnectScreen(vm, ::askBt, ::openBtSettings)
                else when (tab) {
                    0 -> DashScreen(vm)
                    1 -> CodesScreen(vm)
                    2 -> ToolsScreen(vm)
                    3 -> LogScreen(vm) { vm.downloadCsv() }
                    else -> SettingsScreen(vm)
                }
            }
        }

        if (vm.needLogin.value) LoginDialog()
        UpdateDialogs(vm)
    }

    @Composable
    private fun StatusPill(connected: Boolean) {
        val (text, color) = when {
            !connected -> "Offline" to C.Muted
            vm.demo.value -> "Demo" to C.Accent
            vm.ecu.value -> "ECU OK" to C.Ok
            else -> "No ECU" to C.Bad
        }
        Text(
            text.uppercase(), color = color, fontSize = 12.sp, fontWeight = FontWeight.Bold,
            modifier = Modifier.background(color.copy(alpha = 0.15f), RoundedCornerShape(20.dp))
                .padding(horizontal = 10.dp, vertical = 5.dp)
        )
    }

    @Composable
    private fun LoginDialog() {
        var user by remember { mutableStateOf(vm.webUser.value) }
        var pass by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = {}, containerColor = C.Card,
            title = { Text("OBDojo login", fontWeight = FontWeight.ExtraBold) },
            text = {
                Column {
                    Text("This unit has a web login set.", color = C.Muted, fontSize = 13.sp)
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(user, { user = it }, label = { Text("User") }, singleLine = true)
                    Spacer(Modifier.height(8.dp))
                    PasswordField(pass, { pass = it }, "Password")
                }
            },
            confirmButton = { TextButton({ vm.login(user, pass) }) { Text("LOG IN", color = C.Accent, fontWeight = FontWeight.ExtraBold) } },
            dismissButton = { TextButton({ vm.needLogin.value = false; vm.disconnect() }) { Text("CANCEL", color = C.Muted) } }
        )
    }

    companion object {
        val TABS: List<Pair<String, ImageVector>> = listOf(
            "Dash" to Icons.Filled.Speed,
            "Codes" to Icons.Filled.Warning,
            "Tools" to Icons.Filled.Build,
            "Log" to Icons.Filled.ShowChart,
            "Settings" to Icons.Filled.Settings,
        )
    }
}
