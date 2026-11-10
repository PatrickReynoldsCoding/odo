package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.ARP_ORDERS
import com.patrickreynolds.mpeinstruments.Arp
import com.patrickreynolds.mpeinstruments.ARP_RATES
import com.patrickreynolds.mpeinstruments.RELEASE_MAX
import com.patrickreynolds.mpeinstruments.releaseName
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.DELAY_TIMES
import com.patrickreynolds.mpeinstruments.NOTE_NAMES
import com.patrickreynolds.mpeinstruments.SCALES
import com.patrickreynolds.mpeinstruments.packs.InstrumentPack
import com.patrickreynolds.mpeinstruments.packs.Packs
import kotlin.math.roundToInt

/** Play or arpeggiate, and the arp's settings, for one part. [latch]: the controller lets the arp latch. */
@Composable
fun ArpControls(p: Arp, c: Color, latch: Boolean = true, change: ((Arp) -> Arp) -> Unit) {
    Segmented(listOf("Play", "Arpeggiate"), if (p.on) 1 else 0, accent = c) { i -> change { it.copy(on = i == 1) } }
    if (p.on) {
        Spacer(Modifier.height(12.dp))
        Text("Order", color = TextDim, fontSize = 13.sp)
        Chips(ARP_ORDERS, p.order, accent = c) { i -> change { it.copy(order = i) } }
        Text("Rate", color = TextDim, fontSize = 13.sp, modifier = Modifier.padding(top = 6.dp))
        Chips(ARP_RATES.map { it.first }, ARP_RATES.indexOfFirst { it.second == p.rate }, accent = c) { i ->
            change { it.copy(rate = ARP_RATES[i].second) }
        }
        LabeledSlider("Gate", p.gate, 0.05f..1f, pct(p.gate), accent = c) { v -> change { it.copy(gate = v) } }
        Text("Octaves", color = TextDim, fontSize = 13.sp)
        Segmented(listOf("1", "2", "3", "4"), p.octaves - 1, accent = c) { i -> change { it.copy(octaves = i + 1) } }
        if (latch) SwitchRow("Latch", p.latch, "Keep playing after you lift your hand; a new chord replaces it", accent = c) { v ->
            change { it.copy(latch = v) }
        }
        Text("Each arp note follows the finger that holds it: press to swell, slide to change the tone, wiggle for vibrato.",
             color = TextDim, fontSize = 12.sp)
    }
}

/** The tempo, for the loops, the arp and the delay: a slider and tap tempo. */
@Composable
fun TempoControls(accent: Color = PartColors[0]) {
    val perf = AppState.perf
    val taps = remember { mutableStateListOf<Long>() }
    LabeledSlider("Tempo", perf.bpm, 40f..220f, "${perf.bpm.roundToInt()} bpm", accent = accent) { v ->
        AppState.update { it.copy(bpm = v.roundToInt().toFloat()) } }
    OutlinedButton(onClick = {
        val now = System.currentTimeMillis()
        if (taps.isNotEmpty() && now - taps.last() > 2000) taps.clear()
        taps.add(now)
        while (taps.size > 6) taps.removeAt(0)
        if (taps.size >= 3) {
            val bpm = 60000.0 * (taps.size - 1) / (taps.last() - taps.first())
            AppState.update { it.copy(bpm = bpm.roundToInt().coerceIn(40, 220).toFloat()) }
        }
    }, modifier = Modifier.fillMaxWidth()) { Text(if (taps.size in 1..2) "Tap again…" else "Tap tempo", color = accent) }
}

/**
 * The sound the rail is on: its instrument, its octave, and a second sound layered on every note
 * (when the controller does not split the notes between two).
 */
@Composable
fun SoundControls(k: Int, c: Color) {
    val perf = AppState.perf
    val split = AppState.shape.split
    if (!split) {
        SwitchRow("Layer a second sound", perf.layered, "Every note plays both sounds: pick each below", accent = c) { v ->
            AppState.update { it.copy(layered = v) }
            if (!v) AppState.side = 0
        }
    }
    val p = perf.part(k)
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("Octave", modifier = Modifier.weight(1f))
        TextButton(onClick = { AppState.updatePart(k) { it.copy(transpose = it.transpose - 12) } }) { Text("−") }
        Text(if (p.transpose == 0) "0" else "%+d".format(p.transpose / 12), fontWeight = FontWeight.SemiBold)
        TextButton(onClick = { AppState.updatePart(k) { it.copy(transpose = it.transpose + 12) } }) { Text("+") }
    }
}

/** Pitch assist: a resting finger drawn into tune. External MIDI's (the touch pad has its own scale and snap). */
@Composable
fun PitchAssistControls(scale: Int, root: Int, strength: Float, accent: Color, change: (Int, Int, Float) -> Unit) {
    Text("When a finger comes to rest it is drawn into tune: onto the note it is on, even one outside the scale, or, if it " +
         "stopped between two notes, onto the nearest note of the scale. Glides and vibrato are left alone.",
         color = TextDim, fontSize = 12.sp)
    Spacer(Modifier.height(8.dp))
    Chips(SCALES.map { it.name }, scale, accent) { i -> change(i, root, strength) }
    if (scale != 0) {
        Text("Key", color = TextDim, fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp))
        Chips(NOTE_NAMES, root, accent) { i -> change(scale, i, strength) }
        LabeledSlider("Strength", strength, 0f..1f, pct(strength), accent = accent) { v -> change(scale, root, v) }
    }
}

/** One part's sound: brightness, drive, touch, and how much of it goes to the reverb and delay. Also on the screen pad. */
@Composable
fun PartEffects(k: Int, c: Color) {
    val p = AppState.perf.part(k)
    // Past 100%: open, with air (a lift above about 6 kHz).
    LabeledSlider("Brightness", p.brightness, 0f..1.5f,
                  when { p.brightness > 1.01f -> "open, air ${pct(p.brightness - 1f)}"; p.brightness >= 0.99f -> "open"; else -> pct(p.brightness) },
                  accent = c) { v -> AppState.updatePart(k) { it.copy(brightness = if (kotlin.math.abs(v - 1f) < 0.02f) 1f else v) } }
    LabeledSlider("Width", p.width, 0f..1f, if (p.width < 0.01f) "mono" else pct(p.width), accent = c) { v ->
        AppState.updatePart(k) { it.copy(width = v) }
    }
    LabeledSlider("Drive", p.drive, 0f..1f, if (p.drive < 0.01f) "clean" else pct(p.drive), accent = c) { v ->
        AppState.updatePart(k) { it.copy(drive = v) }
    }
    // Slider left = sensitive (curve < 1), right = firm (curve > 1), on a log scale.
    val pos = (kotlin.math.ln(p.pressureCurve) / kotlin.math.ln(3f)).coerceIn(-1f, 1f)
    LabeledSlider("Touch", pos, -1f..1f,
                  when { pos < -0.15f -> "light"; pos > 0.15f -> "firm"; else -> "even" }, accent = c) { v ->
        AppState.updatePart(k) { it.copy(pressureCurve = Math.pow(3.0, v.toDouble()).toFloat()) }
    }
    ReleaseSlider(k, c)
    LabeledSlider("Reverb", p.reverb, 0f..1f, pct(p.reverb), accent = c) { v -> AppState.updatePart(k) { it.copy(reverb = v) } }
    LabeledSlider("Delay", p.delay, 0f..1f, pct(p.delay), accent = c) { v -> AppState.updatePart(k) { it.copy(delay = v) } }
}

/** The shared reverb. Also on the screen pad. */
@Composable
fun ReverbControls() {
    val perf = AppState.perf
    LabeledSlider("Size", perf.reverbSize, 0f..1f, pct(perf.reverbSize)) { v -> AppState.update { it.copy(reverbSize = v) } }
    LabeledSlider("Damping", perf.reverbDamp, 0f..1f, pct(perf.reverbDamp)) { v -> AppState.update { it.copy(reverbDamp = v) } }
}

/** The shared delay. Also on the screen pad. */
@Composable
fun DelayControls() {
    val perf = AppState.perf
    Text("Time, in beats of the tempo (${perf.bpm.roundToInt()} bpm)", color = TextDim, fontSize = 13.sp)
    Chips(DELAY_TIMES.map { it.first }, DELAY_TIMES.indexOfFirst { it.second == perf.delayBeats }) { i ->
        AppState.update { it.copy(delayBeats = DELAY_TIMES[i].second) }
    }
    LabeledSlider("Feedback", perf.delayFeedback, 0f..0.9f, pct(perf.delayFeedback)) { v -> AppState.update { it.copy(delayFeedback = v) } }
    SwitchRow("Ping-pong", perf.delayPingPong, "Echoes bounce between left and right") { v -> AppState.update { it.copy(delayPingPong = v) } }
}

/**
 * How long a note plays on and fades after the finger lifts, for part k. Squared, so the short
 * times, where a small change is easy to hear, get most of the slider.
 */
@Composable
fun ReleaseSlider(k: Int, c: Color) {
    val r = AppState.perf.part(k).release
    LabeledSlider("Release", kotlin.math.sqrt(r / RELEASE_MAX), 0f..1f, releaseName(r), accent = c) { v ->
        val s = v * v * RELEASE_MAX
        AppState.updatePart(k) { it.copy(release = if (s < 0.02f) 0f else s) }
    }
}

/**
 * The sounds of [only] (default: every pack), at a glance: a row of chips for each group, so none
 * is hidden below the fold. Picking one gives part [k] that instrument with its pack's effects.
 */
@Composable
fun SoundPicker(k: Int, accent: Color, only: InstrumentPack? = null, picked: () -> Unit = {}) {
    val chosen = AppState.perf.part(k).instrument
    for (pack in if (only != null) listOf(only) else Packs.all) {
        if (only == null && Packs.all.size > 1) Text(pack.name, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
        for (g in pack.groups) {
            Text(g.name.uppercase(), color = TextDim, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
            Chips(g.instruments.map { it.name }, g.instruments.indexOfFirst { it.id == chosen }, accent) { i ->
                AppState.chooseInstrument(k, g.instruments[i].id); picked()
            }
        }
    }
    Text("A sound comes with its pack's effects, as it is meant to be heard: change them under Effects.",
         color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
}

/** The instrument on part [k]: its pack and group, and what it is. */
@Composable
fun SoundDescription(k: Int) {
    val inst = AppState.instrument(k) ?: return
    val pack = Packs.packOf(inst.id)
    Text(inst.description, color = TextDim, fontSize = 12.sp)
    if (pack != null) Text("${pack.name} · ${Packs.groupOf(inst.id)?.name ?: ""}", color = TextDim, fontSize = 11.sp)
}
