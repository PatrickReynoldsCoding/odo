package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.min
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.ARP_ORDERS
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.BuildConfig
import com.patrickreynolds.mpeinstruments.GyroSettings
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.PadSettings
import com.patrickreynolds.mpeinstruments.Part
import com.patrickreynolds.mpeinstruments.PhoneMods
import com.patrickreynolds.mpeinstruments.Stats
import com.patrickreynolds.mpeinstruments.Updater
import com.patrickreynolds.mpeinstruments.controllers.ArpUse
import com.patrickreynolds.mpeinstruments.controllers.LegacyTouchPad
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import com.patrickreynolds.mpeinstruments.controllers.RailGroup
import com.patrickreynolds.mpeinstruments.releaseName
import kotlinx.coroutines.delay

/** The colour of part [k] on the rail: the controller's own, else the theme's, else the app's. */
fun partColor(t: PadTheme, k: Int): Color =
    AppState.controller.partColors?.getOrNull(k) ?: t.parts.getOrNull(k) ?: PartColors.getOrElse(k) { PartColors[0] }

/** The touch pad's look while it is in use (the loop buttons take their colours from it), else the default one. */
fun hostTheme(): PadTheme {
    val c = AppState.controller as? PackageController
    val look = if (c?.id == LegacyTouchPad.PACKAGE) c.valueOf("theme")?.toInt() ?: 0 else 0
    return padTheme(PadSettings(theme = look))
}

/**
 * The play screen, as odo for computers': the rail at the left, the controller's surface filling
 * the rest, and the loop buttons over it. The rail (RailUi.kt): tiles for the preset, the
 * controller and the sound, each opening its picker beside the rail; the controller's settings by
 * group, each opening a panel with all of them; then the sound's own (effects, arp: Android's), and
 * the app's. Hidden, a button in the corner brings it back.
 */
@Composable
fun PlayHost(onClose: () -> Unit) {
    var pane by remember { mutableStateOf<Pane?>(null) }
    val c = AppState.controller
    val part = AppState.part
    val theme = hostTheme()
    LoopTicker()
    // The controller's own panel open (the Grid's Touch): the loop buttons keep out of its way.
    val ownPanel = (c as? PackageController)?.let { pc ->
        remember(pc, pc.values) { pc.railGroups().any { g -> g.rows.any { it.panel && it.value != 0.0 } } }
    } ?: false
    val dock = AppState.loopDock && !ownPanel
    BoxWithConstraints(Modifier.fillMaxSize().background(theme.bottom)) {
        // A phone gets a narrower rail than a tablet or a computer's 300.
        val railW = if (maxWidth >= 1000.dp) 300.dp else 256.dp
        // An upright controller keeps its size and place: the rail lies over it.
        val over = c.upright
        if (over) Box(Modifier.fillMaxSize()) {
            key(c.id) { c.Surface(part) }
            if (dock) LoopDock(theme, Modifier.align(Alignment.TopEnd).padding(6.dp))
        }
        Row(Modifier.fillMaxSize()) {
            if (!AppState.railHidden) Rail(railW, pane, { p -> pane = if (pane == p) null else p }, onClose)
            if (!over) Box(Modifier.weight(1f).fillMaxHeight()) {
                // A new surface for each controller: the old one lets go of its notes as it goes.
                key(c.id) { c.Surface(part) }
                if (dock) LoopDock(theme, Modifier.align(Alignment.TopEnd).padding(6.dp))
            }
        }
        if (!AppState.railHidden) pane?.let { p ->
            // Beside the rail, over the play area; on a narrow screen over the rail too.
            val start = if (maxWidth - railW >= 300.dp) railW + 8.dp else 8.dp
            RailPanel(p, start, min(440.dp, maxWidth - start - 8.dp), { pane = it }) { pane = null }
        }
        // The rail tucked away: one small button in the corner brings it back.
        else Box(Modifier.padding(6.dp).size(44.dp)
                     .background(if (Eink) Color.White else Panel, RoundedCornerShape(12.dp))
                     .border(BorderStroke(if (Eink) 2.dp else 1.dp, if (Eink) Color.Black else Line), RoundedCornerShape(12.dp))
                     .clickable { AppState.changeRailHidden(false) },
                 contentAlignment = Alignment.Center) {
            Canvas(Modifier.size(20.dp)) {
                chevron(size.width / 2 - 3.dp.toPx(), size.height / 2, size.height * 0.6f, 1, Fg)
                chevron(size.width / 2 + 3.dp.toPx(), size.height / 2, size.height * 0.6f, 1, Fg)
            }
        }
    }
}

/** What the panel beside the rail shows. */
sealed interface Pane {
    data object Presets : Pane
    data object Controller : Pane
    /** The sound picker for part [part]. */
    data class Sound(val part: Int) : Pane
    /** A group of the controller's settings, by name. */
    data class Group(val name: String) : Pane
    /** One of the app's own groups (Effects, Arp, Loop...), by name. */
    data class Own(val name: String) : Pane
    data object App : Pane
}

/** A group of settings on the rail: its name, a few of its values, and how many settings it has. */
class RailEntry(val name: String, val summary: String, val count: Int?, val pane: Pane?, val on: Boolean = false,
                val tap: (() -> Unit)? = null)

/**
 * The rail. Under its top row (odo, close, hide), scrolled: the preset, the controller and the
 * sound tiles; the controller's groups; the sound's own (the part the rail is on); the app's.
 * At the bottom, the version (and an update) and how the engine is doing.
 */
@Composable
private fun Rail(width: Dp, pane: Pane?, open: (Pane) -> Unit, onClose: () -> Unit) {
    val c = AppState.controller
    val accent = PartColors[0]
    Column(Modifier.width(width).fillMaxHeight().background(Ink)
               .then(if (Eink) Modifier.border(BorderStroke(2.dp, Line)) else Modifier)) {
        Row(Modifier.fillMaxWidth().height(46.dp).padding(start = 12.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("odo", color = accent, fontSize = Tokens.Type.title, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            TopButton("Close") { onClose() }
            TopButton("Hide the rail") { AppState.changeRailHidden(true) }
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 10.dp, vertical = 2.dp),
               verticalArrangement = Arrangement.spacedBy(8.dp)) {
            PresetTile(pane == Pane.Presets) { open(Pane.Presets) }
            ControllerTile(pane == Pane.Controller) { open(Pane.Controller) }
            SoundTile(pane) { k -> AppState.side = k; open(Pane.Sound(k)) }
            Column {
                Eyebrow(c.name.uppercase())
                for (e in controllerEntries()) GroupRow(e, e.on || (e.pane != null && pane == e.pane)) { e.tap?.invoke() ?: e.pane?.let(open) }
                Eyebrow(if (AppState.twoParts) "SOUND · ${partLabel(AppState.part).uppercase()}" else "SOUND")
                for (e in soundEntries()) GroupRow(e, pane == e.pane) { e.pane?.let(open) }
                Eyebrow("APP")
                for (e in appEntries()) GroupRow(e, pane == e.pane) { e.pane?.let(open) }
            }
        }
        RailFoot()
    }
}

/** The top row's buttons: close the play screen (an X), hide the rail (two chevrons). */
@Composable
private fun TopButton(what: String, onClick: () -> Unit) {
    val shape = RoundedCornerShape(Tokens.Radius.control)
    Box(Modifier.padding(horizontal = 2.dp).size(width = 38.dp, height = 36.dp).clip(shape).background(PanelHigh)
            .then(if (Eink) Modifier.border(2.dp, Line, shape) else Modifier)
            // Not a key's to press: Enter after typing a name must not close the play screen.
            .focusProperties { canFocus = false }.clickable(onClick = onClick),
        contentAlignment = Alignment.Center) {
        val ink = Fg
        Canvas(Modifier.size(16.dp)) {
            val w = 2.dp.toPx()
            if (what == "Close") {
                drawLine(ink, Offset(2f, 2f), Offset(size.width - 2f, size.height - 2f), w, StrokeCap.Round)
                drawLine(ink, Offset(size.width - 2f, 2f), Offset(2f, size.height - 2f), w, StrokeCap.Round)
            } else {
                chevron(size.width / 2 - 4.dp.toPx(), size.height / 2, size.height * 0.75f, -1, ink)
                chevron(size.width / 2 + 4.dp.toPx(), size.height / 2, size.height * 0.75f, -1, ink)
            }
        }
    }
}

/** A tile on the rail: the panel colour, rounded; outlined in the accent while its picker is open. */
@Composable
private fun Tile(open: Boolean, modifier: Modifier = Modifier, onClick: (() -> Unit)?, content: @Composable ColumnScope.() -> Unit) {
    val shape = RoundedCornerShape(12.dp)
    Column(modifier.fillMaxWidth().clip(shape).background(if (open) PanelHigh else Panel)
               .then(if (open || Eink) Modifier.border(if (open) 2.dp else 1.5.dp, if (open && !Eink) PartColors[0] else Line, shape) else Modifier)
               .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
               .padding(horizontal = 12.dp, vertical = 8.dp), content = content)
}

@Composable
private fun Eyebrow(text: String, modifier: Modifier = Modifier) =
    Text(text, color = TextDim, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.4.sp, maxLines = 1,
         overflow = TextOverflow.Ellipsis, modifier = modifier.padding(start = 4.dp, top = 10.dp, bottom = 4.dp))

@Composable
private fun TileEyebrow(text: String) = Text(text, color = TextDim, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.4.sp)

/** The preset: its name, and what it is (or that it changed since it was loaded). */
@Composable
private fun PresetTile(open: Boolean, onClick: () -> Unit) = Tile(open, onClick = onClick) {
    val name = AppState.presetName
    val p = AppState.allPresets.firstOrNull { it.name == name }
    val edited = p != null && AppState.presetEdited
    TileEyebrow("PRESET")
    Text(name.ifEmpty { "None" }, color = Fg, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
    Text(when { edited -> if (p?.builtIn == true) "Changed since loaded" else "Changed since saved"; p == null -> "Keep what plays as a preset"; else -> presetSummary(p) },
         color = if (edited) PartColors[0] else TextDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
}

/** The controller: its drawing and its name. */
@Composable
private fun ControllerTile(open: Boolean, onClick: () -> Unit) = Tile(open, onClick = onClick) {
    val c = AppState.controller
    TileEyebrow("CONTROLLER")
    ControllerPic(c.id, c.name, if (Eink) Fg else PartColors[0], Modifier.padding(vertical = 4.dp).size(46.dp, 32.dp))
    Text(c.name, color = TextDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
}

/** The sound, or each part's (a split's, or the main sound and its layer): a badge, the name, its pack and group. */
@Composable
private fun SoundTile(pane: Pane?, pick: (Int) -> Unit) = Tile(pane is Pane.Sound, onClick = null) {
    val n = AppState.partCount
    TileEyebrow(if (n > 1) "SOUNDS" else "SOUND")
    for (k in 0 until n) {
        val inst = AppState.instrument(k)
        val open = pane == Pane.Sound(k)
        // With two, the one the rail's sound settings change is marked.
        val on = n > 1 && k == AppState.part
        Row(Modifier.padding(top = 4.dp).fillMaxWidth().height(44.dp).clip(RoundedCornerShape(8.dp))
                .then(if (on) Modifier.background(if (Eink) Color.White else Ink.copy(alpha = 0.6f)) else Modifier)
                .then(if (on && Eink) Modifier.border(1.5.dp, Line, RoundedCornerShape(8.dp)) else Modifier)
                .clickable { pick(k) }.padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            SoundBadge(inst, 30.dp)
            Column(Modifier.padding(start = 10.dp).weight(1f)) {
                Row {
                    if (n > 1) Text(partLabel(k) + " ", color = TextDim, fontSize = 14.sp, maxLines = 1)
                    Text((inst?.name ?: "?") + if (inst != null && !inst.newest) " ${inst.version}" else "", color = if (open) PartColors[0] else Fg, fontSize = 14.sp, fontWeight = FontWeight.Bold,
                         maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                val id = inst?.id ?: ""
                val pack = com.patrickreynolds.mpeinstruments.packs.Packs.packOf(id)
                val group = com.patrickreynolds.mpeinstruments.packs.Packs.groupOf(id)
                if (pack != null) Text("${pack.name} · ${group?.name ?: ""}", color = TextDim, fontSize = 11.sp, maxLines = 1,
                                       overflow = TextOverflow.Ellipsis)
            }
        }
    }
}

/** A group's row: its name, its first few values, how many settings it has, and a chevron. */
@Composable
private fun GroupRow(e: RailEntry, open: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(10.dp)
    val nameColour = when { open && Eink -> Ink; open -> PartColors[0]; else -> Fg }
    val dim = if (open && Eink) Ink else TextDim
    Row(Modifier.fillMaxWidth().height(50.dp).padding(bottom = 2.dp).clip(shape)
            .then(if (open) Modifier.background(if (Eink) Fg else PanelHigh) else Modifier)
            .clickable(onClick = onClick).padding(start = 12.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(e.name, color = nameColour, fontSize = 14.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (e.summary.isNotEmpty()) Text(e.summary, color = dim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        e.count?.let { Text("$it", color = dim, fontSize = 11.sp, modifier = Modifier.padding(horizontal = 6.dp)) }
        Canvas(Modifier.size(12.dp)) { chevron(size.width / 2, size.height / 2, size.height * 0.9f, 1, dim) }
    }
}

/** The bottom of the rail: the version, an update if there is one, the latency and the load. */
@Composable
private fun RailFoot() {
    val context = LocalContext.current
    var stats by remember { mutableStateOf<Stats?>(null) }
    LaunchedEffect(Unit) { while (true) { stats = Stats.from(Native.nativeStats()); delay(500) } }
    val s = Updater.state
    val (msg, act) = when (s) {
        is Updater.State.Checking -> "checking for updates" to null
        is Updater.State.UpToDate -> "up to date" to null
        is Updater.State.Available -> "Update to ${s.release.version}: tap here" to { Updater.downloadAndInstall(context, s.release) }
        is Updater.State.Downloading -> "Downloading ${s.release.version}… ${(s.progress * 100).toInt()}%" to null
        is Updater.State.NeedsPermission -> "Allow odo to install its update: tap here" to
            { context.startActivity(Updater.permissionSettings(context)) }
        is Updater.State.Installing -> "Installing ${s.release.version}…" to null
        is Updater.State.Failed -> s.message to s.release?.let { r -> { Updater.downloadAndInstall(context, r) } }
        else -> "" to null
    }
    // Two lines, where odo for computers has four: a phone's rail is short.
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)) {
        Text("odo ${BuildConfig.VERSION_NAME}" + if (msg.isNotEmpty() && act == null) " · $msg" else "", color = TextDim,
             fontSize = Tokens.Type.caption, maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (act != null) Text(msg, color = PartColors[0], fontSize = Tokens.Type.header, fontWeight = FontWeight.Bold, maxLines = 1,
                              overflow = TextOverflow.Ellipsis, modifier = Modifier.clickable(onClick = act))
        stats?.let { st ->
            Text("Latency %.1f ms, max %.1f · Load %.0f%% · Voices %d".format(st.eventAgeAvgMs, st.eventAgeMaxMs, 100 * st.loadAvg, st.voices),
                 color = TextDim, fontSize = Tokens.Type.caption, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

// ---- what the groups on the rail are ----------------------------------------------------------

/**
 * The controller's groups: a package's from the engine's rail model (mpe/rail.h), as odo for
 * computers shows them; a group with a "panel" switch (the Grid's Touch) flips it, opening the
 * controller's own panel. A built-in controller's options (External MIDI's), each a group of one.
 */
@Composable
private fun controllerEntries(): List<RailEntry> {
    val c = AppState.controller
    if (c is PackageController) {
        val groups = remember(c, c.values) { c.railGroups() }
        return groups.map { g ->
            val own = g.rows.firstOrNull { it.panel }
            if (own != null) RailEntry(groupLabel(g.name), groupSummary(g), null, null, on = own.value != 0.0,
                                       tap = { c.change(own.index, if (own.value != 0.0) 0.0 else 1.0) })
            else RailEntry(groupLabel(g.name), groupSummary(g), g.rows.size, Pane.Group(g.name))
        }
    }
    return c.options(AppState.part).map { o ->
        if (o.tap != null) RailEntry(o.label, o.value, null, null, on = o.on, tap = o.tap)
        else RailEntry(o.label, o.value, null, Pane.Group(o.label))
    }
}

fun groupLabel(name: String) = name.ifEmpty { "Settings" }

/** A group's row on the rail: its first few values (a switch as its name, on or off). */
fun groupSummary(g: RailGroup): String {
    val shown = g.rows.filter { !it.panel }.take(3).map { r ->
        if (r.widget == com.patrickreynolds.mpeinstruments.controllers.RailWidget.Switch) r.label + if (r.value != 0.0) " on" else " off"
        else r.display
    }
    return if (g.rows.isEmpty()) "Nothing to set in this mode" else shown.joinToString(" · ")
}

/** The sound's own (Android's): its octave and layer, its effects, the arp. For the part the rail is on. */
private fun soundEntries(): List<RailEntry> {
    val k = AppState.part
    val p = AppState.perf.part(k)
    return listOfNotNull(
        RailEntry("Octave and layer", "Octave " + (if (p.transpose == 0) "0" else "%+d".format(p.transpose / 12)) +
                  if (AppState.shape.split) "" else " · layer " + if (AppState.perf.layered) "on" else "off", null, Pane.Own("Octave and layer")),
        RailEntry("Effects", effectsSummary(p) + " · release " + releaseName(p.release).lowercase(), null, Pane.Own("Effects")),
        if (AppState.controller.arpUse == ArpUse.App) {
            val arp = AppState.perf.part(arpPart(k)).arp
            RailEntry("Arp", if (arp.on) ARP_ORDERS[arp.order.coerceIn(ARP_ORDERS.indices)] else "Off", null, Pane.Own("Arp"))
        } else null,
    )
}

/** The app's: the volume and look, the loops, the phone's gyro and breath (once turned on in Settings). None in presets. */
private fun appEntries(): List<RailEntry> = listOfNotNull(
    RailEntry("App", "Volume ${masterPercent()}% · " + if (Eink) "E-ink" else "Dark", null, Pane.App),
    RailEntry("Takes", com.patrickreynolds.mpeinstruments.Takes.summary(), null, Pane.Own("Takes")),
    RailEntry("Loop", loopSummary(), null, Pane.Own("Loop")),
    if (AppState.mods.gyroOn) RailEntry("Gyro", gyroSummary(AppState.mods), null, Pane.Own("Gyro")) else null,
    if (AppState.mods.breath != 0) RailEntry("Breath", PhoneMods.BREATH[AppState.mods.breath], null, Pane.Own("Breath")) else null,
)

/** Layered, both parts arpeggiate as the main one does, so that one is changed. */
internal fun arpPart(k: Int) = if (AppState.perf.layout(AppState.shape) == 2) 0 else k

/** The volume as odo for computers shows it: 100% is the engine's usual level. */
internal fun masterPercent() = Math.round(AppState.volume / 0.8f * 100)

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

/**
 * Which way the phone is held, as the turn (degrees) that makes text upright on a screen that
 * does not rotate: 0 upright, -90 turned clockwise onto its side, 90 the other way. A little
 * hysteresis, so it does not flicker at 45 degrees.
 */
@Composable
internal fun rememberPhoneTurn(): Float {
    val context = LocalContext.current
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
