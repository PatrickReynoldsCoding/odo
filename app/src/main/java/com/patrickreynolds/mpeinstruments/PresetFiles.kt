package com.patrickreynolds.mpeinstruments

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import java.io.File

/**
 * Presets as files (docs/formats/PRESET_FORMAT.md), to give one to another odo: Share sends the preset on view
 * through the share sheet; a file opened in the app, or from another app, becomes one of yours.
 * AppState turns them to and from text (with mpe::preset, as odo for computers does).
 */
object PresetFiles {
    const val MIME = "application/vnd.odo.preset+json"
    private const val MAX_BYTES = (1 shl 20) + 1     // one more than mpe::preset takes, so it says "too big"

    /** What the last share or open did, for the presets panel: "" until then. */
    var note by mutableStateOf("")

    fun share(context: Context) {
        val p = AppState.presetToShare()
        val text = AppState.presetFileText(p)
        if (text.isEmpty()) { note = "Could not make a file of “${p.name}”."; return }
        val dir = File(context.cacheDir, "presets").apply { deleteRecursively(); mkdirs() }
        val file = File(dir, AppState.presetFileName(p)).apply { writeText(text) }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.presets", file)
        val send = Intent(Intent.ACTION_SEND).setType(MIME)
            .putExtra(Intent.EXTRA_STREAM, uri)
            .putExtra(Intent.EXTRA_SUBJECT, "odo preset: ${p.name}")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        send.clipData = ClipData.newRawUri(p.name, uri)
        context.startActivity(Intent.createChooser(send, "Share “${p.name}”"))
        note = ""
    }

    /** Read the file at [uri] in as one of yours; what happened is the [note]. */
    fun open(context: Context, uri: Uri): String {
        val bytes = runCatching {
            context.contentResolver.openInputStream(uri)?.use { s ->
                val buf = java.io.ByteArrayOutputStream()
                val chunk = ByteArray(16 * 1024)
                while (buf.size() < MAX_BYTES) { val n = s.read(chunk); if (n < 0) break; buf.write(chunk, 0, n) }
                buf.toByteArray()
            }
        }.getOrNull()
        note = if (bytes == null) "Could not read that file." else AppState.openPresetFile(String(bytes, Charsets.UTF_8))
        return note
    }

    /** A file another app opened with odo, or shared to it: its uri, if [intent] carries one. */
    fun uriIn(intent: Intent?): Uri? = when (intent?.action) {
        Intent.ACTION_VIEW -> intent.data
        Intent.ACTION_SEND -> if (android.os.Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
                              else @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_STREAM)
        else -> null
    }
}
