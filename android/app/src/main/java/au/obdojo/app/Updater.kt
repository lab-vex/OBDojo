package au.obdojo.app

import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/**
 * Self-update from GitHub Releases.
 *
 * Looks at the latest release of [REPO], and if its tag (v1.2.3) is newer than this app,
 * downloads the attached obdojo-app.apk and hands it to Android's installer.
 *
 * Works with a public repo as-is. For a private repo, a GitHub token with read access to
 * the repo's contents must be entered in the app (Updates > GitHub token).
 *
 * The phone is often joined to the OBDojo hotspot (no internet), so requests go out over
 * whichever network actually has working internet, usually mobile data.
 */
class Updater(private val ctx: Context) {

    data class Release(
        val version: String, val notes: String, val assetUrl: String, val size: Long, val pageUrl: String
    )

    class UpdateException(msg: String) : Exception(msg)

    companion object {
        const val REPO = "lab-vex/obdojo"
        const val APK_NAME = "obdojo-app.apk"
        val CURRENT: String get() = BuildConfig.VERSION_NAME

        /** true if [a] is a newer version than [b] (1.10.0 > 1.9.3, 1.2.0 > 1.2.0-beta) */
        fun newer(a: String, b: String): Boolean {
            fun parts(v: String) = v.trim().removePrefix("v").removePrefix("V")
                .split('-', limit = 2).let { p -> p[0].split('.').map { it.toIntOrNull() ?: 0 } to p.getOrNull(1) }
            val (na, pa) = parts(a)
            val (nb, pb) = parts(b)
            for (i in 0 until maxOf(na.size, nb.size)) {
                val x = na.getOrElse(i) { 0 }
                val y = nb.getOrElse(i) { 0 }
                if (x != y) return x > y
            }
            // same numbers: a release beats a pre-release
            return pa == null && pb != null
        }
    }

    private val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    /** A network with validated internet, preferring the phone's default one. */
    private fun internet(): Network? {
        fun ok(n: Network?) = n != null && cm.getNetworkCapabilities(n)?.let {
            it.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                it.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
        } == true
        val def = cm.activeNetwork
        if (ok(def)) return def
        @Suppress("DEPRECATION")
        return cm.allNetworks.firstOrNull { ok(it) }
    }

    private fun open(url: String, token: String, accept: String, auth: Boolean = true): HttpURLConnection {
        val net = internet() ?: throw UpdateException("No internet connection")
        return (net.openConnection(URL(url)) as HttpURLConnection).apply {
            connectTimeout = 8000
            readTimeout = 15000
            instanceFollowRedirects = false
            setRequestProperty("Accept", accept)
            setRequestProperty("User-Agent", "OBDojo-app/$CURRENT")
            setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
            if (auth && token.isNotBlank()) setRequestProperty("Authorization", "Bearer ${token.trim()}")
        }
    }

    /** The newest release on GitHub, or null if this app is already up to date. */
    suspend fun check(token: String): Release? = withContext(Dispatchers.IO) {
        val c = open("https://api.github.com/repos/$REPO/releases/latest", token, "application/vnd.github+json")
        try {
            when (c.responseCode) {
                200 -> {}
                401 -> throw UpdateException("GitHub token was rejected. Check it in Updates.")
                403, 429 -> throw UpdateException("GitHub is rate-limiting update checks. Try again later.")
                404 -> throw UpdateException(
                    if (token.isBlank()) "Can't see releases: the repo is private. Add a GitHub token in Updates."
                    else "No releases published yet (or the token can't read $REPO)."
                )
                else -> throw UpdateException("GitHub replied HTTP ${c.responseCode}")
            }
            val j = JSONObject(c.inputStream.bufferedReader().use { it.readText() })
            val tag = j.optString("tag_name")
            if (!newer(tag, CURRENT)) return@withContext null
            val assets = j.optJSONArray("assets")
            var asset: JSONObject? = null
            for (i in 0 until (assets?.length() ?: 0)) {
                val a = assets!!.getJSONObject(i)
                if (a.optString("name") == APK_NAME) { asset = a; break }
            }
            if (asset == null) throw UpdateException("Release $tag has no $APK_NAME attached yet")
            Release(
                version = tag.removePrefix("v"),
                notes = j.optString("body").trim(),
                // the API asset URL works for public and private repos alike
                assetUrl = asset.optString("url"),
                size = asset.optLong("size"),
                pageUrl = j.optString("html_url"),
            )
        } finally { c.disconnect() }
    }

    /** Download the APK to the app's cache. [progress] gets 0..1. */
    suspend fun download(r: Release, token: String, progress: (Float) -> Unit): File = withContext(Dispatchers.IO) {
        val dir = File(ctx.cacheDir, "updates").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }
        val out = File(dir, "obdojo-${r.version}.apk")

        var c = open(r.assetUrl, token, "application/octet-stream")
        // GitHub redirects the download to its file storage. Follow it ourselves and
        // drop the token, which the storage server would reject.
        var hops = 0
        while (c.responseCode in 300..399 && hops++ < 5) {
            val loc = c.getHeaderField("Location") ?: break
            c.disconnect()
            c = open(loc, token, "application/octet-stream", auth = false)
        }
        try {
            if (c.responseCode != 200) throw UpdateException("Download failed: HTTP ${c.responseCode}")
            val total = c.contentLengthLong.takeIf { it > 0 } ?: r.size
            var done = 0L
            c.inputStream.use { inp ->
                out.outputStream().use { o ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = inp.read(buf)
                        if (n < 0) break
                        o.write(buf, 0, n)
                        done += n
                        if (total > 0) progress((done.toFloat() / total).coerceIn(0f, 1f))
                    }
                }
            }
            if (r.size > 0 && out.length() != r.size) throw UpdateException("Download was cut short. Try again.")
        } catch (e: IOException) {
            out.delete()
            throw UpdateException("Download failed: ${e.message}")
        } finally { c.disconnect() }
        out
    }

    // ---------------- installing ----------------

    /** Android asks the user once to allow OBDojo to install apps. */
    fun canInstall(): Boolean = ctx.packageManager.canRequestPackageInstalls()

    fun openInstallPermission() {
        ctx.startActivity(
            Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${ctx.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }

    /**
     * false if the downloaded APK is signed with a different key than this app.
     * Android refuses those as an update; the old app has to be uninstalled first.
     */
    fun sameSigner(apk: File): Boolean {
        val pm = ctx.packageManager
        val fresh = archiveSigners(pm, apk) ?: return true // can't tell: let the installer decide
        val mine = installedSigners(pm) ?: return true
        return fresh.isNotEmpty() && fresh == mine
    }

    fun install(apk: File) {
        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.updates", apk)
        ctx.startActivity(
            Intent(Intent.ACTION_VIEW)
                .setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }

    private fun digests(sigs: Array<android.content.pm.Signature>?): Set<String>? =
        sigs?.map { s -> MessageDigest.getInstance("SHA-256").digest(s.toByteArray()).joinToString("") { "%02x".format(it) } }?.toSet()

    @Suppress("DEPRECATION")
    private fun signersOf(pi: PackageInfo?): Set<String>? {
        pi ?: return null
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) digests(pi.signingInfo?.apkContentsSigners)
        else digests(pi.signatures)
    }

    @Suppress("DEPRECATION")
    private fun flags() =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) PackageManager.GET_SIGNING_CERTIFICATES
        else PackageManager.GET_SIGNATURES

    private fun archiveSigners(pm: PackageManager, apk: File): Set<String>? = try {
        signersOf(pm.getPackageArchiveInfo(apk.path, flags()))
    } catch (_: Exception) { null }

    private fun installedSigners(pm: PackageManager): Set<String>? = try {
        signersOf(pm.getPackageInfo(ctx.packageName, flags()))
    } catch (_: Exception) { null }
}
