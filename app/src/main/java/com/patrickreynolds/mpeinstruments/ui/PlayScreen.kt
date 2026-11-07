package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.Stats
import com.patrickreynolds.mpeinstruments.MidiConnector
import com.patrickreynolds.mpeinstruments.Preset
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.LegacyTouchPad

/**
 * Play: the controller you play with (and the way into it, full screen), the others to switch
 * to, and what is loaded: the preset, its sounds, the volume.
 */
@Composable
fun PlayScreen(stats: Stats?, openPlay: () -> Unit, openSetup: () -> Unit) {
    Column(Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp)) {
        UpdateBanner(openSetup)
        ControllerCard(openPlay)
        Column(Modifier.padding(horizontal = 16.dp)) { OctaveWarning(stats) }
        SectionCard("Controllers") {
            for (c in Controllers.all) {
                val on = c === AppState.controller
                Row(Modifier.fillMaxWidth().clickable { AppState.switchController(c) }.padding(vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(c.name, fontSize = 17.sp, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal,
                             color = if (on) PartColors[0] else Color.Unspecified)
                        Text(c.description, color = TextDim, fontSize = 12.sp)
                    }
                    Text(if (on) "In use" else "Use", color = if (on) TextDim else PartColors[0], fontSize = 13.sp,
                         modifier = Modifier.padding(start = 12.dp))
                }
            }
            Text("Built in. Each keeps its own settings; the sound, its arp, effects, loops and presets are shared.",
                 color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 4.dp))
        }
        val parts = (0 until AppState.partCount).toList()
        SectionCard(AppState.presetName.ifEmpty { "Playing" }) {
            for (k in parts) InstrumentRow(k, label = partLabel(k))
            LabeledSlider("Volume", AppState.volume, 0f..1f, pct(AppState.volume)) { AppState.changeVolume(it) }
            Spacer(Modifier.height(8.dp))
            Button(onClick = { Native.nativePanic() }, modifier = Modifier.fillMaxWidth(),
                   colors = ButtonDefaults.buttonColors(containerColor = PanelHigh, contentColor = Fg), border = ButtonEdge) {
                Text("Panic: stop every note")
            }
        }
    }
}

/** The controller in use, and the way into it. */
@Composable
private fun ControllerCard(open: () -> Unit) {
    val c = AppState.controller
    val pad = c.id == LegacyTouchPad.PACKAGE
    Card(Modifier.fillMaxWidth().padding(16.dp).height(150.dp).clickable(onClick = open), shape = RoundedCornerShape(22.dp),
         colors = CardDefaults.cardColors(containerColor = Panel),
         border = if (Eink) androidx.compose.foundation.BorderStroke(Edge, Line) else null) {
        Box(Modifier.fillMaxSize()) {
            Canvas(Modifier.fillMaxSize()) {
                if (!Eink) drawRect(Brush.linearGradient(listOf(Color(0xFF2A1F3D), Color(0xFF0E1014), Color(0xFF0F2A30))))
                val lines = if (pad) 8 else 25
                for (i in 0 until lines) {
                    val x = (i + 0.5f) * size.width / lines
                    drawLine(if (Eink) Color.Black.copy(alpha = 0.3f) else Color.White.copy(alpha = if (pad || i % 12 == 0) 0.08f else 0.04f),
                             Offset(x, 0f), Offset(x, size.height), strokeWidth = 2f)
                }
                if (!Eink) for ((k, col) in PartColors.withIndex()) {
                    val at = Offset(size.width * (0.62f + 0.2f * k), size.height * (0.38f + 0.25f * k))
                    drawCircle(Brush.radialGradient(listOf(col.copy(alpha = 0.6f), col.copy(alpha = 0f)), center = at, radius = 90f), 90f, at)
                    drawCircle(Color.White.copy(alpha = 0.9f), 10f, at)
                }
            }
            Column(Modifier.align(Alignment.CenterStart).padding(20.dp)) {
                Text(c.name, fontSize = 24.sp, fontWeight = FontWeight.SemiBold)
                Text(if (c === com.patrickreynolds.mpeinstruments.controllers.ExternalMidi) MidiConnector.connectedName.ifEmpty { "Nothing connected yet" }
                     else if (pad) "Play with your fingers, full screen" else c.description, color = TextDim, fontSize = 13.sp, maxLines = 1,
                     overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis, modifier = Modifier.padding(end = 8.dp))
                Spacer(Modifier.height(10.dp))
                Button(onClick = open, colors = ButtonDefaults.buttonColors(containerColor = PartColors[0], contentColor = Ink)) {
                    Text("Play")
                }
            }
        }
    }
}

/** A part's name, when there are two: the split's ("Left", "Right"), or "Main" and "Layer". */
fun partLabel(k: Int) = if (!AppState.twoParts) "" else (AppState.controller.splitNames ?: listOf("Main", "Layer")).getOrElse(k) { "${k + 1}" }

/** Library: your presets and the built-in ones, the instrument packs and their sounds, the controllers. */
@Composable
fun LibraryScreen() {
    Column(Modifier.verticalScroll(rememberScrollState()).padding(vertical = 10.dp)) {
        SectionCard("Presets") { PresetLibrary(PartColors[0]) }
        for (pack in com.patrickreynolds.mpeinstruments.packs.Packs.all) SectionCard(pack.name) {
            Text(pack.description, color = TextDim, fontSize = 12.sp)
            Text("Pick one for " + (if (AppState.twoParts) "the ${partLabel(AppState.part).lowercase()} sound." else "the sound in use."),
                 color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 4.dp))
            SoundPicker(AppState.part, PartColors[AppState.part], only = pack)
        }
        SectionCard("Controllers") {
            for (c in Controllers.all) {
                Text(c.name, fontSize = 16.sp, modifier = Modifier.padding(top = 6.dp))
                Text(c.description, color = TextDim, fontSize = 12.sp)
            }
            Text("Built in. Choose one on the Play tab or at the top of the play screen's rail.", color = TextDim, fontSize = 12.sp,
                 modifier = Modifier.padding(top = 6.dp))
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InstrumentRow(part: Int, label: String) {
    var open by remember { mutableStateOf(false) }
    val inst = AppState.instrument(part)
    Row(Modifier.fillMaxWidth().clickable { open = true }.padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.width(10.dp).height(36.dp)) { drawRoundRect(PartColors[part], cornerRadius = CornerRadius(8f)) }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            if (label.isNotEmpty()) Text(label, color = TextDim, fontSize = 12.sp)
            Text(inst?.name ?: "?", fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
            Text(inst?.description ?: "", color = TextDim, fontSize = 12.sp, maxLines = 1)
        }
        Text("Change", color = PartColors[part], fontSize = 13.sp)
    }
    if (open) ModalBottomSheet(onDismissRequest = { open = false }, containerColor = Panel) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 28.dp).verticalScroll(rememberScrollState())) {
            SoundPicker(part, PartColors[part]) { open = false }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PresetSheet(onClose: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onClose, containerColor = Panel) {
        Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 28.dp).verticalScroll(rememberScrollState())) {
            Text("Presets", fontSize = 22.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(6.dp))
            PresetLibrary(PartColors[0], onLoaded = onClose)
        }
    }
}

internal fun presetSummary(p: Preset): String {
    fun name(id: String) = AppState.instruments.firstOrNull { it.id == id }?.name ?: id
    val s = p.settings?.let { runCatching { org.json.JSONObject(it) }.getOrNull() }
    val pad = p.controller == LegacyTouchPad.ID || p.controller == LegacyTouchPad.PACKAGE
    // Its split: in the Kotlin touch pad's settings, or the package's.
    val split = pad && (s?.optBoolean("split") == true || s?.optJSONObject("byId")?.optDouble("split", 0.0) == 1.0)
    fun desc(k: Int) = name(p.perf.part(k).instrument) +
        if (p.perf.part(k).arp.on) " arp" else ""
    val gridParts = if (p.controller == com.patrickreynolds.mpeinstruments.controllers.KeyGrid.id)
        (s?.optJSONArray("splits")?.length() ?: 0) + 1 else 1
    val sound = when {
        split -> "${desc(0)} | ${desc(1)}"
        gridParts > 1 -> (gridParts - 1 downTo 0).joinToString(" / ") { name(p.perf.part(it).instrument) }
        p.perf.layered -> "${desc(0)} + ${name(p.perf.part(1).instrument)}"
        else -> desc(0)
    }
    return sound + (p.controller?.let { " · " + Controllers.byId(it).name } ?: "")
}
