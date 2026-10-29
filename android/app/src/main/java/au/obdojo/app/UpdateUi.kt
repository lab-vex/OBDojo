package au.obdojo.app

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** "Updates" card: version, check-on-launch switch, Check now, and the optional GitHub token. */
@Composable
fun UpdatesCard(vm: ObdojoViewModel) {
    var showToken by remember { mutableStateOf(vm.ghToken.value.isBlank()) }
    var token by remember { mutableStateOf(vm.ghToken.value) }

    Heading("App updates")
    CardBox {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("OBDojo app ${Updater.CURRENT}", fontWeight = FontWeight.Bold)
                if (vm.updStatus.value.isNotEmpty())
                    Text(vm.updStatus.value, color = C.Muted, fontSize = 12.sp)
            }
            if (vm.updChecking.value) CircularProgressIndicator(Modifier.size(20.dp), color = C.Accent, strokeWidth = 2.dp)
        }
        Spacer(Modifier.height(4.dp))
        Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Check for updates on launch", modifier = Modifier.weight(1f))
            Switch(
                vm.updAuto.value, { vm.updAuto.value = it; vm.setPref("upd_auto", it) },
                colors = SwitchDefaults.colors(checkedThumbColor = Color.Black, checkedTrackColor = C.Accent)
            )
        }
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            KButton("Check now", { vm.checkForUpdate(manual = true) }, primary = true,
                enabled = !vm.updChecking.value && vm.updProgress.value == null, modifier = Modifier.weight(1f))
            KButton(if (showToken) "Hide token" else "GitHub token", { showToken = !showToken }, modifier = Modifier.weight(1f))
        }
        if (showToken) {
            Spacer(Modifier.height(10.dp))
            PasswordField(token, { token = it }, "GitHub token (private repo only)", Modifier.fillMaxWidth())
            Text(
                "Only needed while the GitHub repo is private. Use a fine-grained token with read-only " +
                    "access to Contents on ${Updater.REPO}.",
                color = C.Muted, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp)
            )
            Spacer(Modifier.height(8.dp))
            KButton("Save token", {
                vm.saveGhToken(token)
                vm.toast.value = if (token.isBlank()) "Token cleared" else "Token saved"
                vm.checkForUpdate(manual = true)
            }, modifier = Modifier.fillMaxWidth())
        }
    }
}

/** Update-available, downloading and wrong-signature dialogs. Shown over any screen. */
@Composable
fun UpdateDialogs(vm: ObdojoViewModel) {
    val ctx = LocalContext.current
    val r = vm.update.value
    val progress = vm.updProgress.value

    if (r != null) AlertDialog(
        onDismissRequest = { if (progress == null) vm.laterUpdate() }, containerColor = C.Card,
        title = { Text("Update available", fontWeight = FontWeight.ExtraBold) },
        text = {
            Column {
                Text("Version ${r.version} is out (you have ${Updater.CURRENT}).")
                if (r.size > 0) Text("${"%.1f".format(r.size / 1048576.0)} MB", color = C.Muted, fontSize = 12.sp)
                if (r.notes.isNotBlank()) {
                    Spacer(Modifier.height(8.dp))
                    Column(Modifier.heightIn(max = 220.dp).verticalScroll(rememberScrollState())) {
                        Text(r.notes, color = C.Muted, fontSize = 12.sp)
                    }
                }
                if (progress != null) {
                    Spacer(Modifier.height(12.dp))
                    LinearProgressIndicator({ progress }, Modifier.fillMaxWidth(), color = C.Accent, trackColor = C.Card2)
                    Text("Downloading… ${(progress * 100).toInt()}%", color = C.Muted, fontSize = 12.sp,
                        modifier = Modifier.padding(top = 4.dp))
                }
            }
        },
        confirmButton = {
            TextButton({ vm.installUpdate() }, enabled = progress == null) {
                Text("UPDATE", color = if (progress == null) C.Accent else C.Muted, fontWeight = FontWeight.ExtraBold)
            }
        },
        dismissButton = {
            if (progress == null) TextButton({ vm.laterUpdate() }) { Text("LATER", color = C.Muted) }
        }
    )

    if (vm.updSignerMismatch.value) AlertDialog(
        onDismissRequest = { vm.updSignerMismatch.value = false }, containerColor = C.Card,
        title = { Text("Reinstall needed (once)", fontWeight = FontWeight.ExtraBold) },
        text = {
            Text(
                "This version is signed with OBDojo's new permanent release key, and your installed copy " +
                    "isn't, so Android won't install it over the top.\n\n" +
                    "Uninstall OBDojo, then install the new APK from the GitHub Releases page. Updates after " +
                    "this install straight from the app. Your app settings (hotspot, Bluetooth unit) will " +
                    "need entering again; the unit's own settings aren't affected."
            )
        },
        confirmButton = {
            TextButton({
                vm.updSignerMismatch.value = false
                try {
                    ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/${Updater.REPO}/releases/latest")))
                } catch (_: Exception) {}
            }) { Text("OPEN RELEASES", color = C.Accent, fontWeight = FontWeight.ExtraBold) }
        },
        dismissButton = { TextButton({ vm.updSignerMismatch.value = false }) { Text("LATER", color = C.Muted) } }
    )
}
