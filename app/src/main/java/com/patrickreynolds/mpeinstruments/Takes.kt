package com.patrickreynolds.mpeinstruments

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import com.patrickreynolds.mpeinstruments.ui.Eink
import com.patrickreynolds.mpeinstruments.ui.Tokens
import org.json.JSONObject
import java.io.File

/**
 * Takes (docs/formats/TAKE_FORMAT.md): what is played, kept as the touches and the notes, or a
 * piece written by hand (TAKE_AUTHORING.md). Record keeps what the controller on screen plays;
 * Listen plays a take into it with ghost fingers showing how; Play along shows only the fingers.
 * The engine records, reads, expands and plays them (mpe::take, through Native.nativeTake*), as
 * odo for computers does; the files are in the app's own folder, takes/.
 */
object Takes {
    const val MIME = "text/vnd.odo.take"
    private const val MAX_BYTES = 32 shl 20

    class Take(val file: File, val name: String, val controller: String, val written: Boolean, val length: Double,
               val sections: List<String>, val settings: JSONObject, val sounds: List<String>)

    var list by mutableStateOf<List<Take>>(emptyList())
        private set
    /** The take chosen in the panel, by its file's name. */
    var chosen by mutableStateOf<String?>(null)
    /** What happened last, for the panel. */
    var note by mutableStateOf("")
    var speed by mutableDoubleStateOf(1.0)
        private set
    var loop by mutableIntStateOf(-1)
        private set
    /** ms since the first finger while recording, 0 before it; -1 when not recording. */
    var recording by mutableDoubleStateOf(-1.0)
        private set
    /** 0 nothing, 1 listening, 2 playing along. */
    var playing by mutableIntStateOf(0)
        private set
    /** The surface the controller is on, dp: where a take written by hand is laid out. */
    var surfaceW = 0f
    var surfaceH = 0f

    private lateinit var dir: File

    fun init(context: Context) {
        dir = File(context.filesDir, "takes").apply { mkdirs() }
        scan()
    }

    fun current(): Take? = list.firstOrNull { it.file.name == chosen }

    fun scan() {
        if (!::dir.isInitialized) return
        list = (dir.listFiles() ?: emptyArray()).filter { it.name.endsWith(".odotake") }.mapNotNull { f ->
            runCatching {
                val o = JSONObject(String(Native.nativeTakeInfo(f.readBytes()), Charsets.UTF_8))
                if (o.has("error")) null
                else Take(f, o.optString("name").ifEmpty { f.nameWithoutExtension }, o.optString("controller"), o.optBoolean("written"),
                          o.optDouble("length", 0.0), o.optJSONArray("sections")?.let { a -> List(a.length()) { a.optString(it) } } ?: emptyList(),
                          o.optJSONObject("settings") ?: JSONObject(),
                          o.optJSONArray("sounds")?.let { a -> List(a.length()) { a.optString(it) } } ?: emptyList())
            }.getOrNull()
        }.sortedBy { it.name }
        if (current() == null) chosen = null
    }

    fun clock(ms: Double): String { val s = Math.round(ms / 1000).toInt(); return "%d:%02d".format(s / 60, s % 60) }

    /** "0:38 · Grid · written by hand", as odo for computers says it. */
    fun sub(t: Take): String {
        val c = Controllers.all.firstOrNull { it.id == t.controller }
        val ctl = c?.name ?: t.controller.removePrefix("pkg:").ifEmpty { "any controller" }
        return "${clock(t.length)} · $ctl · " + if (t.written) "written by hand" else "recorded"
    }

    /** The rail's line: what is happening, or how many there are. */
    fun summary(): String = when {
        recording >= 0 -> if (recording > 0) "Recording ${clock(recording)}" else "Recording: play something"
        playing != 0 -> (if (playing == 1) "Listening: " else "Playing along: ") + (current()?.name ?: "")
        list.isEmpty() -> "Record what you play"
        else -> "${list.size} " + if (list.size == 1) "take" else "takes"
    }

    fun choose(t: Take) {
        if (t.file.name == chosen) return
        stop()
        chosen = t.file.name
        loop = -1
        note = ""
    }

    /** Record, from the first finger down; again: stop, and keep it as a take of yours. */
    fun toggleRecord() {
        if (recording >= 0) {
            val name = freeName()
            val text = String(Native.nativeTakeStopRecording(name.toByteArray(Charsets.UTF_8),
                "odo ${BuildConfig.VERSION_NAME} (Android)".toByteArray(Charsets.UTF_8)), Charsets.UTF_8)
            recording = -1.0
            if (text.isEmpty()) { note = "Nothing was played, so nothing was kept."; return }
            val f = freeFile(name)
            runCatching { f.writeText(text) }.onFailure { note = "Could not write ${f.name}."; return }
            scan()
            chosen = f.name
            note = "Kept as “$name”."
            return
        }
        stop()
        if (AppState.controller !is PackageController) { note = "Takes are recorded from a controller on the screen."; return }
        val sounds = (0 until AppState.partCount).mapNotNull { AppState.instrument(it)?.id }.joinToString("\n")
        if (!Native.nativeTakeRecord(sounds.toByteArray(Charsets.UTF_8))) { note = "Open a controller to record from."; return }
        recording = 0.0
        note = ""
    }

    /** Listen (mode 0) or play along (1): the controller, its settings and the sound as the take has
     *  them, then the take into the controller. The same again stops it. */
    fun play(mode: Int) {
        val t = current() ?: return
        if (playing == mode + 1) { stop(); return }
        stop()
        if (recording >= 0) toggleRecord()
        if (t.controller.isNotEmpty() && AppState.controller.id != t.controller) {
            val c = Controllers.all.firstOrNull { it.id == t.controller }
            if (c == null) { note = "It was played on ${t.controller.removePrefix("pkg:")}, which is not here."; return }
            AppState.switchController(c)
            note = "Its controller is on the screen now: tap ${if (mode == 0) "Listen" else "Play along"} again."
            return                               // its surface opens and loads it first
        }
        val c = AppState.controller as? PackageController ?: run { note = "Takes play on a controller on the screen."; return }
        for (k in c.settings.indices) {
            val id = c.settings[k].optString("id")
            if (t.settings.has(id)) c.change(k, t.settings.optDouble(id, c.values[k]))
        }
        t.sounds.firstOrNull()?.let { s -> if (AppState.perf.part(0).instrument != s && AppState.instruments.any { it.id == s }) AppState.chooseInstrument(0, s) }
        val centers = (0 until 2).map { AppState.instrument(if (AppState.partCount > 1) it else 0)?.centerNote ?: 60f }
        val r = String(Native.nativeTakePlay(t.file.readBytes(), mode, c.values.toDoubleArray(), surfaceW, surfaceH, centers[0], centers[1],
                                             speed, loop), Charsets.UTF_8)
        note = r.removePrefix("!")
        if (!r.startsWith("!")) playing = mode + 1
    }

    fun stop() {
        if (playing != 0) Native.nativeTakeStop()
        playing = 0
    }

    fun changeSpeed(s: Double) { speed = s; Native.nativeTakeSpeed(s) }
    fun changeLoop(k: Int) { loop = k; Native.nativeTakeLoop(k) }

    /** A frame, from the controller's surface: the take moves on; its ghosts' draw list, or null. */
    fun tick(w: Float, h: Float): ByteArray? {
        if (recording >= 0) {
            // The rail shows whole seconds: changed (and drawn again) once a second, not every frame.
            val now = Native.nativeTakeRecording()
            if (now < 0 || Math.floor(now / 1000) != Math.floor(recording / 1000) || (recording == 0.0) != (now == 0.0)) recording = now
        }
        if (playing == 0) return null
        val g = Native.nativeTakeTick(w, h, if (Eink) Tokens.Ghost.eink else Tokens.Ghost.dark, ghostSizes)
        if (g == null) playing = 0                // it came to its end
        return g
    }
    private val ghostSizes = floatArrayOf(Tokens.Ghost.radius, Tokens.Ghost.grow, Tokens.Ghost.trailMs, Tokens.Ghost.previewMs, Tokens.Ghost.textSize)

    fun delete(t: Take) {
        if (t.file.name == chosen) stop()
        t.file.delete()
        note = "Deleted “${t.name}”."
        chosen = null
        scan()
    }

    private fun freeName(): String = generateSequence(1) { it + 1 }.map { "Take $it" }.first { n -> list.none { it.name == n } }

    private fun freeFile(name: String): File {
        val base = String(Native.nativeTakeFileName(name.toByteArray(Charsets.UTF_8)), Charsets.UTF_8).removeSuffix(".odotake")
        var f = File(dir, "$base.odotake")
        var n = 2
        while (f.exists()) f = File(dir, "$base ${n++}.odotake")
        return f
    }

    /** Open a take file (from the file picker, or another app): one of yours, chosen. */
    fun open(context: Context, uri: Uri): String {
        val bytes = runCatching {
            context.contentResolver.openInputStream(uri)?.use { s ->
                val buf = java.io.ByteArrayOutputStream()
                val chunk = ByteArray(64 * 1024)
                while (buf.size() < MAX_BYTES) { val n = s.read(chunk); if (n < 0) break; buf.write(chunk, 0, n) }
                buf.toByteArray()
            }
        }.getOrNull() ?: run { note = "Could not read that file."; return note }
        val o = JSONObject(String(Native.nativeTakeInfo(bytes), Charsets.UTF_8))
        if (o.has("error")) { note = o.optString("error"); return note }
        val name = o.optString("name").ifEmpty { "Take" }
        val f = freeFile(name)
        runCatching { f.writeBytes(bytes) }.onFailure { note = "Could not keep it."; return note }
        scan()
        chosen = f.name
        note = "Opened “$name”."
        return note
    }

    /** Send a take as a file, through the share sheet. */
    fun share(context: Context, t: Take) {
        val out = File(context.cacheDir, "takes").apply { deleteRecursively(); mkdirs() }
        val f = File(out, t.file.name).apply { writeBytes(t.file.readBytes()) }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.presets", f)
        val send = Intent(Intent.ACTION_SEND).setType(MIME)
            .putExtra(Intent.EXTRA_STREAM, uri)
            .putExtra(Intent.EXTRA_SUBJECT, "odo take: ${t.name}")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        send.clipData = ClipData.newRawUri(t.name, uri)
        context.startActivity(Intent.createChooser(send, "Share “${t.name}”"))
        note = ""
    }
}
