package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.GyroSettings
import com.patrickreynolds.mpeinstruments.PadSettings
import com.patrickreynolds.mpeinstruments.Part
import com.patrickreynolds.mpeinstruments.PhoneMods
import com.patrickreynolds.mpeinstruments.ARP_ORDERS
import com.patrickreynolds.mpeinstruments.controllers.ArpUse
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.Option
import com.patrickreynolds.mpeinstruments.controllers.LegacyTouchPad
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import com.patrickreynolds.mpeinstruments.releaseName

/** The rail's colours: the touch pad's look while it is in use, else the default one. */
/** The colour of part [k] on the rail: the controller's own, else the theme's, else the app's. */
fun partColor(t: PadTheme, k: Int): Color =
    AppState.controller.partColors?.getOrNull(k) ?: t.parts.getOrNull(k) ?: PartColors.getOrElse(k) { PartColors[0] }

fun hostTheme(): PadTheme {
    // The touch pad's look, while it is in use.
    val c = AppState.controller as? PackageController
    val look = if (c?.id == LegacyTouchPad.PACKAGE) c.valueOf("theme")?.toInt() ?: 0 else 0
    return padTheme(PadSettings(theme = look))
}

/**
 * The play screen, the same for every controller: the rail at the left (the controller to play
 * with, its own options, then what every controller shares), the controller's surface filling the
 * rest, and the loop buttons over it.
 */
@Composable
fun PlayHost(onClose: () -> Unit) {
    var panel by remember { mutableStateOf<String?>(null) }
    val c = AppState.controller
    val part = AppState.part
    val theme = hostTheme()
    LoopTicker()
    // The controller's options and the shared ones, built once for the rail and the open panel.
    val own = c.options(part)
    val shared = sharedOptions(part)
    val pickController = Option("Controller", c.name, perPart = false) { ControllerPicker { panel = null } }
    Box(Modifier.fillMaxSize().background(theme.bottom)) {
        // An upright controller (the grid) keeps its size and place: the rail lies over it rather than
        // squeezing it, hiding what is under it until it is tucked away again.
        val over = c.upright
        if (over) Box(Modifier.fillMaxSize()) {
            key(c.id) { c.Surface(part) }
            if (AppState.loopDock) LoopDock(theme, Modifier.align(Alignment.TopEnd).padding(6.dp))
        }
        Row(Modifier.fillMaxSize()) {
            if (!AppState.railHidden) Box(if (over) Modifier.background(theme.bottom) else Modifier) {
                Rail(pickController, own, shared, theme, panel, { panel = if (panel == it) null else it }, onClose)
            }
            if (!over) Box(Modifier.weight(1f).fillMaxHeight()) {
                // A new surface for each controller: the old one lets go of its notes as it goes.
                key(c.id) { c.Surface(part) }
                if (AppState.loopDock) LoopDock(theme, Modifier.align(Alignment.TopEnd).padding(6.dp))
            }
        }
        if (!AppState.railHidden) panel?.let { label ->
            (listOf(pickController) + own + shared).firstOrNull { it.label == label && it.panel != null }?.let { o ->
                SettingPanel(o, part, theme) { panel = null }
            }
        }
        // The rail tucked away: one small button in the corner brings it back.
        else Box(Modifier.padding(6.dp).size(44.dp)
                     .background(if (theme.flat) Color.White else theme.rail, RoundedCornerShape(12.dp))
                     .border(BorderStroke(if (theme.flat) 2.dp else 1.dp, if (theme.flat) Color.Black else theme.railDim), RoundedCornerShape(12.dp))
                     .clickable { AppState.changeRailHidden(false) },
                 contentAlignment = Alignment.Center) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Show the settings", tint = theme.railText)
        }
    }
}

/** What every controller shares: presets, the sound and its effects, release, the arp, loops, and the phone's gyro and breath. */
@Composable
private fun sharedOptions(k: Int): List<Option> {
    val part = AppState.perf.part(k)
    val accent = if (Eink) Color.Black else partColor(hostTheme(), k)
    val two = AppState.twoParts
    return listOfNotNull(
        Option("Presets", AppState.presetName.ifEmpty { "None" }, perPart = false) {
            Column(Modifier.verticalScroll(rememberScrollState())) { PresetLibrary(accent) }
        },
        Option("Sound", AppState.instrument(k)?.name ?: "") {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                SoundControls(k, accent)
                SoundPicker(k, accent)
            }
        },
        Option("Effects", effectsSummary(part)) {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                PartEffects(k, accent)
                TextButton(onClick = { AppState.resetEffects(k) }) {
                    Text("As the pack means it (${AppState.instrument(k)?.name ?: "this sound"})", color = accent) }
                LabeledSlider("Level", part.volume, 0f..1.5f, pct(part.volume), accent = accent) { v ->
                    AppState.updatePart(k) { it.copy(volume = v) } }
                Text(if (two) "Reverb (all the sounds)" else "Reverb", fontSize = 15.sp, fontWeight = FontWeight.SemiBold,
                     modifier = Modifier.padding(top = 12.dp))
                ReverbControls()
                Text(if (two) "Delay (all the sounds)" else "Delay", fontSize = 15.sp, fontWeight = FontWeight.SemiBold,
                     modifier = Modifier.padding(top = 12.dp))
                DelayControls()
            }
        },
        Option("Release", releaseName(part.release)) {
            ReleaseSlider(k, accent)
            Text(if (part.release < 0.01f) "A note ends the instrument's own way when you lift your finger."
                 else "When you lift your finger the note plays on as it was and fades out over this time, " +
                      "like holding a piano's sustain pedal. A new note on the same finger cuts it off.",
                 color = TextDim, fontSize = 12.sp)
        },
        // The app's arp, a modifier: on the rail unless the controller has none, or its own.
        if (AppState.controller.arpUse == ArpUse.App) {
            // Layered, both parts arpeggiate as the main one does, so that one is changed.
            val layered = AppState.perf.layout(AppState.shape) == 2
            val a = if (layered) 0 else k
            val arp = AppState.perf.part(a).arp
            Option("Arp", if (arp.on) ARP_ORDERS[arp.order.coerceIn(ARP_ORDERS.indices)] else "Off", perPart = !layered) {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    ArpControls(arp, accent, latch = AppState.shape.arpLatch) { c -> AppState.updatePart(a) { it.copy(arp = c(it.arp)) } }
                    if (arp.on) AppState.controller.arpNote?.let { Text(it, color = TextDim, fontSize = 12.sp) }
                    TempoControls(accent)
                }
            }
        } else null,
        Option("Loop", loopSummary(), perPart = false) {
            Column(Modifier.verticalScroll(rememberScrollState())) { LoopControls(accent) }
        },
        // Experimental: on the rail once turned on in Settings.
        if (AppState.mods.gyroOn) Option("Gyro", gyroSummary(AppState.mods), perPart = false) {
            // The gyro's tabs and meter stay at the top; the rest scrolls under them.
            Column { GyroPanel(liveModReading(), scroll = true) }
        } else null,
        if (AppState.mods.breath != 0) Option("Breath", PhoneMods.BREATH[AppState.mods.breath], perPart = false) {
            Column(Modifier.verticalScroll(rememberScrollState())) { BreathControls(liveModReading()) }
        } else null,
    )
}

/** The controllers there are, to play with another. */
@Composable
private fun ControllerPicker(done: () -> Unit) {
    Column(Modifier.verticalScroll(rememberScrollState())) {
        for (c in Controllers.all) {
            val on = c === AppState.controller
            Column(Modifier.fillMaxWidth().clickable { AppState.switchController(c); done() }.padding(vertical = 8.dp)) {
                Text(c.name, fontSize = 17.sp, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal,
                     color = if (on) PartColors[0] else Color.Unspecified)
                Text(c.description, color = TextDim, fontSize = 12.sp)
            }
        }
        Text("Each controller keeps its own settings. The sound, its arp, effects, loops and presets stay as they are when you switch.",
             color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 6.dp))
    }
}

/**
 * The rail at the left edge, Etherpad style: tap a setting to change it. At the top the
 * controller; with two sounds (a split, or a layer) a toggle for the one the settings below change.
 */
@Composable
private fun Rail(pick: Option, own: List<Option>, shared: List<Option>, t: PadTheme, open: String?,
                 tap: (String) -> Unit, onClose: () -> Unit) {
    val accent = partColor(t, AppState.part)
    Column(Modifier.fillMaxHeight().width(92.dp).background(t.rail)
               .then(if (t.flat) Modifier.border(BorderStroke(2.dp, Color.Black)) else Modifier)
               .verticalScroll(rememberScrollState()).padding(vertical = 4.dp),
           horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row {
            IconButton(onClick = onClose, Modifier.size(44.dp)) { Icon(Icons.Filled.Close, "Close", tint = t.railDim) }
            // Tuck the rail away for the whole width to play on; the corner button brings it back.
            IconButton(onClick = { AppState.changeRailHidden(true) }, Modifier.size(44.dp)) {
                Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Hide the settings", tint = t.railDim) }
        }
        // An upright controller keeps the screen still: turn the phone and only the labels turn, to stay readable.
        val turn = if (AppState.controller.upright) rememberPhoneTurn() else 0f
        RailItem(pick, open == pick.label, t, accent, tap, turn)
        if (AppState.twoParts) PartToggle(AppState.controller.splitNames ?: listOf("Main", "Layer"), t)
        for (o in own) {
            o.heading?.let { RailHeading(it, t, turn) }
            RailItem(o, open == o.label, t, accent, tap, turn)
        }
        Box(Modifier.padding(horizontal = 14.dp).fillMaxWidth().height(1.dp).background(t.railDim.copy(alpha = 0.4f)))
        for (o in shared) RailItem(o, open == o.label, t, accent, tap, turn)
    }
}

/** A group's name over its settings, in the section-header style (SectionCard's), sized for the rail. */
@Composable
private fun RailHeading(name: String, t: PadTheme, turn: Float) {
    // Turned (an upright controller's), it gets a square of its own as the items do.
    Box(if (turn != 0f) Modifier.size(86.dp).rotate(turn) else Modifier.padding(top = 4.dp), contentAlignment = Alignment.Center) {
        Text(name.uppercase(), fontSize = Tokens.Type.header, fontWeight = FontWeight.SemiBold, letterSpacing = 1.2.sp,
             color = t.railDim, maxLines = 1)
    }
}

@Composable
private fun RailItem(o: Option, open: Boolean, t: PadTheme, accent: Color, tap: (String) -> Unit, turn: Float = 0f) {
    val on = open || o.on
    // Turned, each item gets a square of its own, so the turned words do not run into the next.
    Column(Modifier.clickable { o.tap?.invoke() ?: tap(o.label) }
               .then(if (on && t.flat) Modifier.background(Color.Black) else Modifier)
               .then(if (turn != 0f) Modifier.size(86.dp).rotate(turn) else Modifier)
               .padding(horizontal = 4.dp, vertical = 2.dp),
           horizontalAlignment = Alignment.CenterHorizontally,
           verticalArrangement = if (turn != 0f) Arrangement.Center else Arrangement.Top) {
        Text(o.label, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
             color = when { on && t.flat -> Color.White; on -> accent; else -> t.railText })
        Text(o.value, fontSize = 10.sp, color = if (on && t.flat) Color.White else t.railDim, maxLines = 1)
    }
}

/**
 * Which sound the rail changes: the left or right pad of a split, the main sound or its layer, or
 * one of a grid's sections (more than two: stacked as on the grid, the top one at the top).
 */
@Composable
private fun PartToggle(names: List<String>, t: PadTheme) {
    val n = AppState.partCount
    @Composable
    fun cell(k: Int, m: Modifier) {
        val on = k == AppState.part
        val col = partColor(t, k)
        Box(m.background(when { !on -> Color.Transparent; t.flat -> Color.Black; else -> col.copy(alpha = 0.3f) }, RoundedCornerShape(8.dp))
                .clickable { AppState.side = k },
            contentAlignment = Alignment.Center) {
            Text(names.getOrElse(k) { "${k + 1}" }, fontSize = 12.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Normal,
                 maxLines = 1, color = when { on && t.flat -> Color.White; on -> col; else -> t.railText })
        }
    }
    val edge = Modifier.padding(horizontal = 6.dp).fillMaxWidth()
        .border(BorderStroke(if (t.flat) 2.dp else 1.dp, if (t.flat) Color.Black else t.railDim), RoundedCornerShape(8.dp))
    if (n <= 2) Row(edge.height(34.dp)) { for (k in 0 until n) cell(k, Modifier.weight(1f).fillMaxHeight()) }
    else Column(edge) { for (k in n - 1 downTo 0) cell(k, Modifier.fillMaxWidth().height(30.dp)) }
}

@Composable
private fun SettingPanel(o: Option, part: Int, t: PadTheme, close: () -> Unit) {
    val names = AppState.controller.splitNames ?: listOf("main", "layer")
    Surface(Modifier.padding(start = 100.dp, top = 12.dp, bottom = 12.dp).widthIn(max = 380.dp),
            shape = RoundedCornerShape(18.dp), color = if (t.flat) Color.White else Panel.copy(alpha = 0.96f), contentColor = Fg,
            border = if (t.flat) BorderStroke(Edge, Color.Black) else null) {
        Column(Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(if (AppState.twoParts && o.perPart) "${o.label} · ${names.getOrElse(part) { "${part + 1}" }.lowercase()}" else o.label,
                     fontSize = 17.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                TextButton(onClick = close) { Text("Done") }
            }
            o.panel?.invoke()
        }
    }
}

/**
 * Which way the phone is held, as the turn (degrees) that makes text upright on a screen that
 * does not rotate: 0 upright, -90 turned clockwise onto its side, 90 the other way. A little
 * hysteresis, so it does not flicker at 45 degrees.
 */
@Composable
internal fun rememberPhoneTurn(): Float {
    val context = androidx.compose.ui.platform.LocalContext.current
    var turn by remember { mutableStateOf(0f) }
    androidx.compose.runtime.DisposableEffect(Unit) {
        val l = object : android.view.OrientationEventListener(context) {
            override fun onOrientationChanged(deg: Int) {
                if (deg == ORIENTATION_UNKNOWN) return
                turn = when {
                    deg in 65..115 -> -90f
                    deg in 245..295 -> 90f
                    deg <= 25 || deg >= 335 || deg in 155..205 -> 0f
                    else -> turn
                }
            }
        }
        if (l.canDetectOrientation()) l.enable()
        onDispose { l.disable() }
    }
    return turn
}

/** A word or two for the rail: the effects that are on for this part. */
private fun effectsSummary(p: Part): String {
    val on = listOfNotNull("Verb".takeIf { p.reverb > 0.01f }, "Delay".takeIf { p.delay > 0.01f },
                           "Drive".takeIf { p.drive > 0.01f }, "Tone".takeIf { p.brightness < 0.999f })
    return when { on.isEmpty() -> "Dry"; on.size <= 2 -> on.joinToString(" + "); else -> "${on.size} on" }
}

/** A word or two for the rail: what the gyro's sources do ("Off", "Twist: Bend", "2 on"). */
internal fun gyroSummary(m: PhoneMods): String {
    val on = m.gyro.withIndex().filter { it.value.target != 0 }
    return when (on.size) {
        0 -> "Off"
        1 -> "${PhoneMods.SOURCES[on[0].index]}: ${GyroSettings.TARGETS[on[0].value.target]}"
        else -> "${on.size} on"
    }
}
