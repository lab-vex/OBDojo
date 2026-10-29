package au.obdojo.app

/**
 * How the app talks to the unit. Both links carry the same commands as the web page's /api
 * and get the same JSON back, so every screen works the same over Bluetooth or Wi-Fi.
 */
interface ObdLink {
    /** Run one command (e.g. "codes", "sweep start") and return its JSON reply. */
    suspend fun cmd(c: String, timeoutMs: Int): String

    /** Latest live data JSON. */
    suspend fun live(): String

    /** The whole log as CSV text. */
    suspend fun logCsv(): String

    fun disconnect()
}
