package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.MidiConnector
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.SCALES
import com.patrickreynolds.mpeinstruments.Stats
import com.patrickreynolds.mpeinstruments.controllers.ExternalMidi
import com.patrickreynolds.mpeinstruments.controllers.Option
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.roundToInt

/** Finding and connecting MIDI devices (the activity's Bluetooth scan), for Settings and External MIDI's rail. */
val LocalDevices = compositionLocalOf<SetupActions?> { null }

/** External MIDI's options on the rail: the device, its bend range, a transpose and pitch assist. */
@Composable
fun externalMidiOptions(@Suppress("UNUSED_PARAMETER") part: Int): List<Option> {
    val s = ExternalMidi.settings
    val accent = if (Eink) Color.Black else PartColors[1]
    val name = MidiConnector.connectedName
    return listOf(
        Option("Device", name.ifEmpty { "None" }, perPart = false) {
            MidiDevices(); LinkWatchdog()
        },
        Option("Bend", "±${s.bendRange.roundToInt()}", perPart = false) {
            val ranges = listOf(48f, 24f, 12f, 2f)
            Text("Pitch bend range the controller sends (the Seaboard and most MPE controllers ±48)", color = TextDim, fontSize = 13.sp)
            Chips(ranges.map { "±${it.toInt()}" }, ranges.indexOf(s.bendRange), accent) { k -> ExternalMidi.update { it.copy(bendRange = ranges[k]) } }
        },
        Option("Transpose", if (s.transpose == 0) "0" else "%+d".format(s.transpose), perPart = false) {
            TransposeRow()
            Text("Moves every note you play. The sound's own octave is under Sound.", color = TextDim, fontSize = 12.sp)
        },
        Option("Assist", if (s.scale == 0) "Off" else SCALES[s.scale].name, perPart = false) {
            PitchAssistControls(s.scale, s.scaleRoot, s.assist, accent) { sc, root, st ->
                ExternalMidi.update { it.copy(scale = sc, scaleRoot = root, assist = st) } }
        },
    )
}

/**
 * External MIDI's surface: there is nothing to touch, so it shows what is played, each finger a
 * light along the notes (pitch across, slide up, pressure its size), and the device's state.
 */
@Composable
fun ExternalMidiSurface(@Suppress("UNUSED_PARAMETER") part: Int) {
    val buf = remember { FloatArray(16 * 5) }
    var n by remember { mutableIntStateOf(0) }
    var frame by remember { mutableIntStateOf(0) }
    var stats by remember { mutableStateOf<Stats?>(null) }
    LaunchedEffect(Unit) {
        while (true) {
            withFrameNanos { }
            n = Native.nativeFingers(buf)
            frame++
        }
    }
    LaunchedEffect(Unit) { while (true) { stats = Stats.from(Native.nativeStats()); delay(500) } }
    val theme = hostTheme()
    val center = AppState.instrument(0)?.centerNote ?: 60f
    val low = center - 24f; val span = 48f
    Box(Modifier.fillMaxSize().then(if (theme.flat) Modifier.background(Color.White)
                                     else Modifier.background(Brush.verticalGradient(listOf(theme.top, theme.mid, theme.bottom))))) {
        Canvas(Modifier.fillMaxSize()) {
            frame.let { }
            val w = size.width; val h = size.height
            for (k in 0..span.toInt()) {
                val note = (low + k).roundToInt()
                val x = k / span * w
                val c = ((note % 12) + 12) % 12 == 0
                drawLine(theme.line.copy(alpha = if (c) 0.25f else 0.07f), Offset(x, 0f), Offset(x, h), strokeWidth = if (c) 2f else 1f)
            }
            for (i in 0 until n) {
                val b = 5 * i
                val x = ((buf[b + 2] - low) / span).coerceIn(0f, 1f) * w
                val y = (1f - buf[b + 4]) * h
                val r = 18f + 60f * buf[b + 3]
                val col = theme.parts[0]
                if (!theme.flat) drawCircle(Brush.radialGradient(listOf(col.copy(alpha = 0.55f), col.copy(alpha = 0f)), center = Offset(x, y), radius = r * 2.2f),
                                            r * 2.2f, Offset(x, y))
                drawCircle(if (theme.flat) Color.Black else Color.White.copy(alpha = 0.9f), r * 0.35f, Offset(x, y))
            }
        }
        Column(Modifier.align(Alignment.Center).padding(24.dp).widthIn(max = 420.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            val name = MidiConnector.connectedName
            val waiting = MidiConnector.reconnecting
            frame.let { }
            if (name.isNotEmpty()) {
                if (n == 0) Text("$name: play", color = theme.railDim, fontSize = 15.sp, textAlign = TextAlign.Center)
            } else {
                Text(if (waiting.isNotEmpty()) "Waiting for $waiting…" else "No MIDI controller connected",
                     color = theme.railText, fontSize = 18.sp, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center)
                Spacer(Modifier.height(8.dp))
                Card(colors = CardDefaults.cardColors(containerColor = Panel.copy(alpha = 0.92f)), shape = RoundedCornerShape(18.dp)) {
                    Column(Modifier.padding(14.dp)) { MidiDevices() }
                }
            }
            OctaveWarning(stats)
        }
    }
}

/** The MIDI devices there are, connected or to connect: USB by itself, Bluetooth found here once. */
@Composable
fun MidiDevices() {
    val actions = LocalDevices.current
    val connected = MidiConnector.connectedName
    val waiting = MidiConnector.reconnecting
    Text(when {
        connected.isNotEmpty() -> connected
        waiting.isNotEmpty() -> "Waiting for $waiting (Bluetooth)…"
        else -> "Nothing connected"
    }, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = if (connected.isNotEmpty()) PartColors[1] else Color.Unspecified)
    if (connected.isNotEmpty()) OutlinedButton(onClick = { MidiConnector.disconnect() }, modifier = Modifier.padding(top = 6.dp)) {
        Text("Disconnect")
    }
    for (info in MidiConnector.devices().filter { it.id != MidiConnector.connectedId })
        OutlinedButton(onClick = { MidiConnector.connect(info) }, modifier = Modifier.fillMaxWidth().padding(top = 6.dp)) {
            Text("Connect ${MidiConnector.name(info)}")
        }
    if (actions != null) {
        for (d in actions.bleFound)
            OutlinedButton(onClick = { MidiConnector.connectBluetooth(d) }, modifier = Modifier.fillMaxWidth().padding(top = 6.dp)) {
                Text("Connect ${d.name ?: d.address} (Bluetooth)")
            }
        Button(onClick = actions.scan, modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
               colors = ButtonDefaults.buttonColors(containerColor = PanelHigh, contentColor = PartColors[1]), border = ButtonEdge) {
            Text(if (actions.scanning) "Looking for Bluetooth controllers…" else "Find a Bluetooth controller")
        }
    }
    Text("USB: plug the controller in and it connects by itself. Bluetooth: switch it on, then find it here once.",
         color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
    SwitchRow("Reconnect by itself", MidiConnector.autoBluetooth,
              "Plays the moment the Bluetooth controller is on, also with the app in the background", accent = PartColors[1]) {
        MidiConnector.autoBluetooth = it
    }
}

/** When a silent link counts as lost and its notes are let go. */
@Composable
fun LinkWatchdog() {
    val dogs = listOf(-1, 0, 500, 1500, 3000)
    Text("Release notes when the link goes silent", color = TextDim, fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp))
    Chips(dogs.map { when (it) { -1 -> "Auto"; 0 -> "Off"; else -> "${it} ms" } }, dogs.indexOf(MidiConnector.watchdogSetting)) {
        MidiConnector.watchdogSetting = dogs[it]
    }
    Text("Auto: 1.5 s on Bluetooth, where a lost link is not reported for up to 30 s; off on USB.",
         color = TextDim, fontSize = 12.sp)
}

/** External MIDI's transpose: every note it plays moves by this. */
@Composable
fun TransposeRow() {
    val t = ExternalMidi.settings.transpose
    fun set(v: Int) = ExternalMidi.update { it.copy(transpose = v) }
    Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        for ((label, d) in listOf("−oct" to -12, "−1" to -1)) TextButton(onClick = { set(t + d) }) { Text(label) }
        Text(if (t == 0) "0" else "%+d".format(t), modifier = Modifier.width(44.dp), fontWeight = FontWeight.SemiBold,
             textAlign = TextAlign.Center)
        for ((label, d) in listOf("+1" to 1, "+oct" to 12)) TextButton(onClick = { set(t + d) }) { Text(label) }
    }
}

/**
 * A Seaboard's notes can come in octaves away from the instrument (its octave buttons, or a
 * setting in ROLI's app): then everything sounds strained or at one pitch. Offer the fix.
 */
@Composable
fun OctaveWarning(stats: Stats?) {
    if (AppState.controller !== ExternalMidi) return
    val inst = AppState.instrument(0) ?: return
    val center = stats?.noteCenter ?: return
    if (center < 0) return
    val t = ExternalMidi.settings.transpose
    val playing = center + t + AppState.perf.part(0).transpose
    val octaves = ((inst.centerNote - playing) / 12.0).roundToInt()
    if (abs(octaves) < 2) return
    Card(Modifier.fillMaxWidth().padding(vertical = 6.dp), shape = RoundedCornerShape(18.dp),
         colors = CardDefaults.cardColors(containerColor = Warn.copy(alpha = if (Eink) 0f else 0.14f)),
         border = if (Eink) androidx.compose.foundation.BorderStroke(Edge, Line) else null) {
        Column(Modifier.padding(16.dp)) {
            Text("Your notes are ${abs(octaves)} octaves ${if (octaves < 0) "above" else "below"} the ${inst.name.lowercase()}",
                 color = Warn, fontWeight = FontWeight.SemiBold)
            Text("Check the octave buttons on the controller, or transpose here.", color = TextDim, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            Button(onClick = { ExternalMidi.update { it.copy(transpose = t + 12 * octaves) } },
                   colors = ButtonDefaults.buttonColors(containerColor = Warn, contentColor = Ink)) {
                Text("Transpose ${if (octaves > 0) "+" else "−"}${abs(octaves)} octaves")
            }
        }
    }
}
