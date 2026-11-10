package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.Preset
import com.patrickreynolds.mpeinstruments.controllers.Controller
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import com.patrickreynolds.mpeinstruments.controllers.RailGroup
import com.patrickreynolds.mpeinstruments.controllers.RailWidget
import com.patrickreynolds.mpeinstruments.controllers.RailRow
import com.patrickreynolds.mpeinstruments.packs.Packs

/*
 * The panel beside the rail, as odo for computers' (desktop/main.cpp panelLayout): its name, what
 * it is for and Done, then its rows, tiles, chips, switches and sliders. The pickers (presets,
 * controllers, sounds), a group of the controller's settings, the sound's own groups and the app's.
 */

/** The panel for [p], [start] from the left edge and [width] wide; [go] opens another, [close] closes it. */
@Composable
fun RailPanel(p: Pane, start: Dp, width: Dp, go: (Pane?) -> Unit, close: () -> Unit) {
    val k = AppState.part
    val two = AppState.twoParts
    val c = AppState.controller
    when (p) {
        Pane.Presets -> PanelFrame(start, width, "Presets", "The sound, the controller and its settings", close) { PresetsPanel() }
        Pane.Controller -> PanelFrame(start, width, "Controller", "What you play on", close) { ControllersPanel() }
        is Pane.Sound -> {
            if (p.part >= AppState.partCount) { SideEffect { go(null) }; return }
            PanelFrame(start, width, if (two) "Sound: ${partLabel(p.part)}" else "Sound", "Type to find one", close) { SoundsPanel(p.part) }
        }
        is Pane.Group -> {
            if (c is PackageController) {
                val g = c.railGroups().firstOrNull { it.name == p.name }
                if (g == null) { SideEffect { go(null) }; return }
                PanelFrame(start, width, groupLabel(g.name), c.name, close) { GroupPanel(c, g) }
            } else {
                val o = c.options(k).firstOrNull { it.label == p.name && it.panel != null }
                if (o == null) { SideEffect { go(null) }; return }
                PanelFrame(start, width, o.label, c.name, close) { o.panel?.invoke() }
            }
        }
        is Pane.Own -> {
            val forPart = if (two) "For the ${partLabel(k).lowercase()} sound" else ""
            val accent = if (Eink) Color.Black else PartColors[0]
            when (p.name) {
                "Octave and layer" -> PanelFrame(start, width, p.name, forPart.ifEmpty { AppState.instrument(k)?.name ?: "" }, close) {
                    SoundControls(k, accent)
                }
                "Effects" -> PanelFrame(start, width, p.name, forPart.ifEmpty { "Kept in presets" }, close) { EffectsPanel(k, accent) }
                "Arp" -> PanelFrame(start, width, p.name, if (arpPart(k) == k) forPart.ifEmpty { "Kept in presets" } else "Both sounds, as the main one",
                                    close) {
                    val a = arpPart(k)
                    val arp = AppState.perf.part(a).arp
                    ArpControls(arp, accent, latch = AppState.shape.arpLatch) { f -> AppState.updatePart(a) { it.copy(arp = f(it.arp)) } }
                    if (arp.on) AppState.controller.arpNote?.let { PNote(it) }
                    TempoControls(accent)
                }
                "Loop" -> PanelFrame(start, width, p.name, "For all of odo, not kept in presets", close) { LoopControls(accent) }
                "Takes" -> PanelFrame(start, width, p.name, "Record what you play; play a take back to learn it", close) { TakesPanel() }
                "Gyro" -> PanelFrame(start, width, p.name, "The phone as a controller, not kept in presets", close) {
                    GyroPanel(liveModReading(), scroll = false)
                }
                "Breath" -> PanelFrame(start, width, p.name, "The phone as a controller, not kept in presets", close) {
                    BreathControls(liveModReading())
                }
                else -> SideEffect { go(null) }
            }
        }
        Pane.App -> PanelFrame(start, width, "App", "For all of odo, not kept in presets", close) { AppPanel() }
    }
}

/** The panel's frame: its title, what it is for, Done; under them its content, which scrolls. */
@Composable
private fun PanelFrame(start: Dp, width: Dp, title: String, sub: String, close: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    Surface(Modifier.padding(start = start, top = 12.dp, bottom = 12.dp).width(width), shape = RoundedCornerShape(Tokens.Radius.card),
            color = Panel, contentColor = Fg, border = BorderStroke(if (Eink) 2.dp else 1.dp, if (Eink) Line else Line.copy(alpha = 0.75f))) {
        Column {
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp), verticalAlignment = Alignment.Top) {
                Column(Modifier.weight(1f).padding(top = 6.dp)) {
                    Text(title, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (sub.isNotEmpty()) Text(sub, color = TextDim, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
                TextButton(onClick = close, modifier = Modifier.focusProperties { canFocus = false }) { Text("Done", color = PartColors[0], fontSize = 14.sp, fontWeight = FontWeight.Bold) }
            }
            Column(Modifier.verticalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 12.dp),
                   content = content)
        }
    }
}

// ---- the panel's parts ---------------------------------------------------------------------------

@Composable
fun PHeading(text: String, colour: Color? = null) =
    Text(text, color = if (colour != null && !Eink) colour else TextDim, fontSize = 11.sp, fontWeight = FontWeight.Bold,
         letterSpacing = 0.4.sp, modifier = Modifier.padding(top = 12.dp, bottom = 6.dp))

@Composable
fun PSub(text: String) = Text(text, color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(top = 2.dp, bottom = 4.dp))

@Composable
fun PNote(text: String) = Text(text, color = TextDim, fontSize = 12.sp, modifier = Modifier.padding(vertical = 4.dp))

@Composable
private fun PRule() = HorizontalDivider(color = Line.copy(alpha = 0.56f))

/** A row to tap: an optional drawing, its name (in the accent when it is the one on), and a line under it. */
@Composable
private fun PRow(text: String, sub: String = "", on: Boolean = false, rule: Boolean = true, danger: Boolean = false,
                 pic: (@Composable () -> Unit)? = null, onClick: () -> Unit) {
    if (rule) PRule()
    Row(Modifier.fillMaxWidth().heightIn(min = if (sub.isEmpty()) 44.dp else 50.dp).clickable(onClick = onClick).padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (pic != null) { Box(Modifier.size(width = 40.dp, height = 30.dp)) { pic() }; Spacer(Modifier.width(12.dp)) }
        Column(Modifier.weight(1f)) {
            Text(text, color = when { on -> PartColors[0]; danger -> Warn; else -> Fg }, fontSize = 14.sp, fontWeight = FontWeight.Bold,
                 maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (sub.isNotEmpty()) Text(sub, color = TextDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

/** A setting's name, and its value at the right. */
@Composable
private fun PLabel(label: String, value: String, rule: Boolean) {
    if (rule) PRule()
    Row(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        Text(value, color = TextDim, fontSize = 12.sp, maxLines = 1, modifier = Modifier.padding(start = 8.dp))
    }
}

/** A switch: its name, its value under it, the switch at the right. A tap anywhere on it flips it. */
@Composable
private fun PSwitch(label: String, sub: String, on: Boolean, rule: Boolean, onClick: () -> Unit) {
    if (rule) PRule()
    Row(Modifier.fillMaxWidth().heightIn(min = 50.dp).clickable(onClick = onClick).padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 2)
            if (sub.isNotEmpty()) Text(sub, color = TextDim, fontSize = 12.sp, maxLines = 2)
        }
        Spacer(Modifier.width(12.dp))
        OdoSwitch(on)
    }
}

/** A long list of choices, folded: its name and value, a chevron; tapped, the chips open under it. */
@Composable
private fun PFold(label: String, value: String, open: Boolean, rule: Boolean, onClick: () -> Unit) {
    if (rule) PRule()
    Row(Modifier.fillMaxWidth().heightIn(min = 50.dp).clickable(onClick = onClick).padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(value, color = if (open) PartColors[0] else TextDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        val dim = TextDim
        Canvas(Modifier.size(12.dp)) {
            val w = 2.dp.toPx()
            val cx = size.width / 2; val cy = size.height / 2; val d = 4.dp.toPx()
            if (open) { drawLine(dim, Offset(cx - d, cy + d / 2), Offset(cx, cy - d / 2), w); drawLine(dim, Offset(cx, cy - d / 2), Offset(cx + d, cy + d / 2), w) }
            else { drawLine(dim, Offset(cx - d, cy - d / 2), Offset(cx, cy + d / 2), w); drawLine(dim, Offset(cx, cy + d / 2), Offset(cx + d, cy - d / 2), w) }
        }
    }
}

/** Chips that wrap, as wide as their words: the chosen one filled in the accent. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PChips(names: List<String>, chosen: Int, onPick: (Int) -> Unit) {
    FlowRow(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)) {
        names.forEachIndexed { i, name ->
            val on = i == chosen
            val shape = RoundedCornerShape(8.dp)
            Box(Modifier.height(34.dp).clip(shape)
                    .then(if (on) Modifier.background(if (Eink) Fg else PartColors[0].copy(alpha = 0x38 / 255f)) else Modifier)
                    .border(if (Eink) 2.dp else 1.dp, if (Eink) Line else if (on) PartColors[0] else Line, shape)
                    .clickable { onPick(i) }.padding(horizontal = 12.dp),
                contentAlignment = Alignment.Center) {
                Text(name, color = when { on && Eink -> Ink; on -> PartColors[0]; else -> Fg }, fontSize = 13.sp, maxLines = 1)
            }
        }
    }
}

@Composable
private fun PSlider(fraction: Float, onChange: (Float) -> Unit) = OdoSlider(fraction, PartColors[0], onChange)

/** A box to type in: the accent round it, [hint] while it is empty. */
@Composable
private fun PField(text: String, hint: String, onChange: (String) -> Unit, focus: Boolean = false, onDone: (() -> Unit)? = null) {
    val shape = RoundedCornerShape(10.dp)
    // A new name is typed at once (the keyboard comes up), as on odo for computers; a search waits for a tap.
    val asked = remember { androidx.compose.ui.focus.FocusRequester() }
    if (focus) androidx.compose.runtime.LaunchedEffect(Unit) { asked.requestFocus() }
    // A name to change starts all selected, so typing replaces it.
    var value by remember { mutableStateOf(androidx.compose.ui.text.input.TextFieldValue(text,
        androidx.compose.ui.text.TextRange(if (focus) 0 else text.length, text.length))) }
    if (value.text != text) value = value.copy(text = text, selection = androidx.compose.ui.text.TextRange(text.length))
    BasicTextField(value, { v -> value = v; if (v.text != text) onChange(v.text) }, singleLine = true, textStyle = TextStyle(color = Fg, fontSize = 14.sp),
                   cursorBrush = SolidColor(PartColors[0]),
                   keyboardOptions = KeyboardOptions(imeAction = if (onDone != null) ImeAction.Done else ImeAction.Search),
                   keyboardActions = KeyboardActions(onDone = { onDone?.invoke() }),
                   modifier = Modifier.padding(vertical = 4.dp).fillMaxWidth().then(
                       if (focus) Modifier.focusRequester(asked) else Modifier),
                   decorationBox = { inner ->
                       Box(Modifier.fillMaxWidth().height(40.dp).clip(shape).background(Ink)
                               .border(if (Eink) 2.dp else 1.5.dp, PartColors[0], shape).padding(horizontal = 12.dp),
                           contentAlignment = Alignment.CenterStart) {
                           if (text.isEmpty()) Text(hint, color = TextDim, fontSize = 14.sp)
                           inner()
                       }
                   })
}

// ---- the pickers -----------------------------------------------------------------------------------

/** Presets: the built-in ones, then yours, each with its controller drawn; then what can be done to the one on view. */
@Composable
private fun PresetsPanel() {
    val current = AppState.presetName
    var renaming by remember { mutableStateOf(false) }
    var name by remember { mutableStateOf("") }
    // Done typing: nothing keeps the focus, so a key after it reaches nothing on the rail.
    val focus = androidx.compose.ui.platform.LocalFocusManager.current
    var confirmDelete by remember(current) { mutableStateOf(false) }
    val dim = TextDim
    @Composable
    fun row(p: Preset) {
        val on = p.name == current
        PRow(p.name, presetSummary(p), on = on,
             pic = { p.controller?.let { ControllerPic(Controllers.byId(it).id, Controllers.byId(it).name, if (on) PartColors[0] else dim, Modifier.size(38.dp, 30.dp)) } }) {
            AppState.load(p); renaming = false; confirmDelete = false
        }
    }
    PHeading("BUILT IN")
    for (p in AppState.allPresets.filter { it.builtIn }) row(p)
    PHeading("YOURS")
    if (AppState.userPresets.isEmpty()) PNote("None yet: keep what is playing as one, below.")
    for (p in AppState.userPresets) row(p)
    PHeading("THIS PRESET")
    val on = AppState.allPresets.firstOrNull { it.name == current }
    PRow("+ Keep what is playing as a new preset") {
        AppState.newPreset(if (on?.builtIn == true) current else AppState.soundName()); renaming = false; confirmDelete = false
    }
    // As a file, to another odo (docs/formats/PRESET_FORMAT.md): the same rows as odo for computers'.
    val context = androidx.compose.ui.platform.LocalContext.current
    val openFile = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) com.patrickreynolds.mpeinstruments.PresetFiles.open(context, uri)
    }
    PRow(if (current.isEmpty()) "Share what is playing as a file…" else "Share “$current” as a file…") {
        com.patrickreynolds.mpeinstruments.PresetFiles.share(context); renaming = false; confirmDelete = false
    }
    PRow("Open a preset file…") { openFile.launch(arrayOf("*/*")); renaming = false; confirmDelete = false }
    com.patrickreynolds.mpeinstruments.PresetFiles.note.takeIf { it.isNotEmpty() }?.let { PNote(it) }
    if (on != null && on.builtIn) {
        PRow("Copy “${on.name}” to yours") { AppState.duplicate(on) }
    } else if (on != null) {
        if (renaming) {
            PField(name, "A name", { name = it.take(60) }, focus = true) { focus.clearFocus(); AppState.rename(on, name); renaming = false }
            PRow("Save the name") { focus.clearFocus(); AppState.rename(on, name); renaming = false }
        } else PRow("Rename “${on.name}”") { renaming = true; name = on.name; confirmDelete = false }
        PRow("Duplicate “${on.name}”") { AppState.duplicate(on); confirmDelete = false }
        PRow("Move it earlier") { AppState.move(on, -1); confirmDelete = false }
        PRow("Move it later") { AppState.move(on, 1); confirmDelete = false }
        PRow((if (confirmDelete) "Tap again to delete “" else "Delete “") + on.name + "”", danger = confirmDelete) {
            if (confirmDelete) { AppState.delete(on); confirmDelete = false } else confirmDelete = true
        }
    }
    PNote(when {
        current.isEmpty() -> "Not a preset yet: keep what is playing as one of yours."
        on?.builtIn == true -> "A starting point: what you change is not kept in it. Keep it as one of yours."
        else -> "Yours: what you change is kept in it as you play."
    })
}

/** The controllers, two to a row: each drawn, its name, and what it is. */
@Composable
private fun ControllersPanel() {
    val all = Controllers.all
    for (row in all.chunked(2)) {
        Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (c in row) ControllerCard(c, Modifier.weight(1f))
            if (row.size == 1) Spacer(Modifier.weight(1f))
        }
    }
    PNote("Each controller keeps its own settings. The sound, its arp, effects, loops and presets stay as they are when you switch.")
}

@Composable
private fun ControllerCard(c: Controller, modifier: Modifier) {
    val on = c === AppState.controller
    val shape = RoundedCornerShape(12.dp)
    val about = (if (c is PackageController) c.manifest.optString("description") else c.description).let { d ->
        val cut = d.indexOfFirst { it == ':' || it == ';' || it == ',' }
        if (cut >= 0) d.substring(0, cut) else d
    }
    Column(modifier.height(100.dp).clip(shape).background(Ink).border(if (on) 2.dp else 1.dp, if (on) PartColors[0] else Line, shape)
               .clickable { AppState.switchController(c) }.padding(10.dp)) {
        ControllerPic(c.id, c.name, if (on) PartColors[0] else TextDim, Modifier.size(58.dp, 40.dp))
        Spacer(Modifier.weight(1f))
        Text(c.name, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(about, color = TextDim, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** The sound picker's filter, kept while the app runs: all, sustained or fading sounds. */
private var soundFilter by mutableIntStateOf(0)

/**
 * The sounds by pack and group, as chips; a search, and All / Sustained / Fading (mpe::sustains).
 * A pick gives part [k] that sound with its pack's effects.
 */
@Composable
private fun SoundsPanel(k: Int) {
    var search by rememberSaveable { mutableStateOf("") }
    val chosen = AppState.perf.part(k).instrument
    // The sound's versions, when it has more than one: the newest, and those kept to go back to.
    val versions = AppState.versions(k)
    if (versions.size > 1) {
        val playing = AppState.instrument(k)?.version
        PHeading("VERSION")
        PChips(versions.mapIndexed { i, v -> v.version + if (i == 0) " · newest" else "" },
               versions.indexOfFirst { it.version == playing }) { i -> AppState.chooseVersion(k, versions[i].version) }
        PNote("${versions[0].name}: older versions are kept as they were, to go back to.")
    }
    PField(search, "Find a sound", { search = it.take(60) })
    Spacer(Modifier.height(6.dp))
    PChips(listOf("All", "Sustained", "Fading"), soundFilter) { soundFilter = it }
    val q = search.trim().lowercase()
    var any = false
    for (pack in Packs.all) {
        val groups = pack.groups.map { g ->
            g to g.instruments.filter { s ->
                (q.isEmpty() || "${s.name} ${pack.name} ${g.name}".lowercase().contains(q)) &&
                    (soundFilter == 0 || s.sustains == (soundFilter == 1))
            }
        }.filter { it.second.isNotEmpty() }
        if (groups.isEmpty()) continue
        any = true
        PHeading(pack.name.uppercase(), packColour(pack.name))
        for ((g, list) in groups) {
            PSub(g.name)
            PChips(list.map { it.name }, list.indexOfFirst { it.id == chosen }) { i -> AppState.chooseInstrument(k, list[i].id) }
        }
    }
    if (!any) PNote(if (q.isEmpty()) "None here." else "No sound matches “$search”.")
    if (soundFilter != 0) PNote(if (soundFilter == 1) "Sustained: a held note keeps sounding." else "Fading: a held note dies away on its own.")
    PNote("A sound comes with its pack's effects, as it is meant to be heard: change them under Effects.")
}

/** A group of the controller's settings, all of them: switches, chips (a long list folded), sliders. */
@Composable
private fun GroupPanel(c: PackageController, g: RailGroup) {
    var folded by remember(g.name) { mutableIntStateOf(-1) }
    var first = true
    for (r in g.rows) {
        if (r.panel) continue
        val k = r.index
        when (r.widget) {
            RailWidget.Switch -> PSwitch(r.label, r.display, r.value != 0.0, !first) { c.change(k, c.stepped(k, r.value, 1)) }
            RailWidget.Chips -> {
                val fold = r.choices.size > 8 || r.choices.sumOf { it.length } > 56
                if (fold) {
                    PFold(r.label, r.display, folded == k, !first) { folded = if (folded == k) -1 else k }
                    if (folded == k) PChips(r.choices, r.chosen) { i -> c.change(k, c.chipValue(k, i)) }
                } else {
                    PLabel(r.label, r.display, !first)
                    PChips(r.choices, r.chosen) { i -> c.change(k, c.chipValue(k, i)) }
                }
            }
            RailWidget.Slider -> {
                PLabel(r.label, r.display, !first)
                PSlider(c.toFraction(k, r.value).toFloat()) { t -> c.change(k, c.fromFraction(k, t.toDouble())) }
            }
            RailWidget.Scala -> ScalaRow(c, r, !first)
        }
        first = false
    }
    if (g.rows.isEmpty()) PNote("Nothing to set in this mode.")
    if (g.hiddenNow > 0) PNote("${g.hiddenNow} " + (if (g.hiddenNow == 1) "option" else "options") + " hidden in this mode")
}

/** What the last Scala file did ("Playing “…”." or why not), shown under its row. */
private var scalaNote by mutableStateOf("")

/** A "scala" setting: what tuning it holds, and a row that opens a Scala (.scl) file for it, as odo
 *  for computers' (desktop/main.cpp, openScalaFile). */
@Composable
private fun ScalaRow(c: PackageController, r: RailRow, rule: Boolean) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val openFile = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        val bytes = runCatching {
            context.contentResolver.openInputStream(uri)?.use { s ->
                val buf = java.io.ByteArrayOutputStream()
                val chunk = ByteArray(16 * 1024)
                while (buf.size() < 1 shl 20) { val n = s.read(chunk); if (n < 0) break; buf.write(chunk, 0, n) }
                buf.toByteArray()
            }
        }.getOrNull()
        val name = uri.lastPathSegment?.substringAfterLast('/') ?: "tuning"
        scalaNote = if (bytes == null) "Could not open that file." else c.openScala(r.index, bytes, name)
    }
    PLabel(r.label, r.display, rule)
    PRow("Open a Scala file…") { openFile.launch(arrayOf("*/*")) }
    if (scalaNote.isNotEmpty()) PNote(scalaNote)
}

/** The sound's effects (Android's): its tone, drive, touch, release and sends; its level; the shared reverb and delay. */
@Composable
private fun EffectsPanel(k: Int, accent: Color) {
    val part = AppState.perf.part(k)
    val two = AppState.twoParts
    PartEffects(k, accent)
    LabeledSlider("Level", part.volume, 0f..1.5f, pct(part.volume), accent = accent) { v -> AppState.updatePart(k) { it.copy(volume = v) } }
    PRow("As the pack means it (${AppState.instrument(k)?.name ?: "this sound"})") { AppState.resetEffects(k) }
    PHeading(if (two) "REVERB (ALL THE SOUNDS)" else "REVERB")
    ReverbControls()
    PHeading(if (two) "DELAY (ALL THE SOUNDS)" else "DELAY")
    DelayControls()
}

/** For all of odo: the volume and the look. */
@Composable
private fun AppPanel() {
    PLabel("Volume", "${masterPercent()}%", rule = false)
    // 0 to 200%, 100% the engine's usual level, in 5% steps (as odo for computers').
    PSlider(AppState.volume / 0.8f / 2f) { t -> AppState.changeVolume(Math.round(t * 40) / 20f * 0.8f) }
    PLabel("Look", if (Eink) "E-ink" else "Dark", rule = false)
    PChips(listOf("Dark", "E-ink"), if (Eink) 1 else 0) { AppState.changeEink(it == 1) }
    PRule()
    PRow("Panic: stop every note", rule = false) { Native.nativePanic() }
}

// ---- drawings, as odo for computers draws them --------------------------------------------------

/** Each sound pack's colour (ui/tokens.json). */
fun packColour(pack: String): Color = Tokens.packs[pack] ?: Tokens.packOther

/** A sound's badge: its pack's colour, the first letters of its name. */
@Composable
fun SoundBadge(inst: Native.Instrument?, size: Dp) {
    val pack = inst?.let { Packs.packOf(it.id)?.name } ?: ""
    val shape = RoundedCornerShape(8.dp)
    Box(Modifier.size(size).clip(shape).background(if (Eink) Color.White else packColour(pack))
            .then(if (Eink) Modifier.border(2.dp, Line, shape) else Modifier),
        contentAlignment = Alignment.Center) {
        Text((inst?.name ?: "?").take(2), color = if (Eink) Color.Black else Color(0xFF0E1014), fontSize = 11.sp, fontWeight = FontWeight.Bold,
             style = TextStyle(platformStyle = androidx.compose.ui.text.PlatformTextStyle(includeFontPadding = false)))
    }
}

/** A chevron pointing left (dir -1) or right (dir 1), centred on (cx, cy), [size] high. */
fun DrawScope.chevron(cx: Float, cy: Float, size: Float, dir: Int, c: Color) {
    val hw = size * 0.3f * dir; val hh = size / 2; val w = maxOf(1.5.dp.toPx(), size / 7)
    drawLine(c, Offset(cx - hw, cy - hh), Offset(cx + hw, cy), w, StrokeCap.Round)
    drawLine(c, Offset(cx + hw, cy), Offset(cx - hw, cy + hh), w, StrokeCap.Round)
}

/** A controller, drawn small: the Grid, the touch pad, the Tonnetz, the Hex keyboard; one odo does not know, its first letter in a box. */
@Composable
fun ControllerPic(id: String, name: String, c: Color, modifier: Modifier) {
    val known = id.removePrefix("pkg:")
    Box(modifier, contentAlignment = Alignment.Center) {
        Canvas(Modifier.matchParentSize()) {
            val s = minOf(size.width / 40f, size.height / 28f)
            val ox = (size.width - 40 * s) / 2; val oy = (size.height - 28 * s) / 2
            fun p(x: Float, y: Float) = Offset(ox + x * s, oy + y * s)
            val line = 1.6.dp.toPx()
            fun box(x: Float, y: Float, w: Float, h: Float, r: Float, fill: Boolean) =
                if (fill) drawRoundRect(c, p(x, y), Size(w * s, h * s), CornerRadius(r * s))
                else drawRoundRect(c, p(x, y), Size(w * s, h * s), CornerRadius(r * s), style = Stroke(line))
            when (known) {
                "patrickreynolds/grid" -> for (r in 0 until 2) for (k in 0 until 3) box(5 + k * 10.5f, 3 + r * 11f, 8.5f, 9f, 2f, k == 1 && r == 1)
                "patrickreynolds/touchpad" -> {
                    box(3f, 3f, 34f, 22f, 3f, false)
                    for (k in 1 until 6) drawLine(c, p(3 + k * 34f / 6, 3f), p(3 + k * 34f / 6, 25f), 1.dp.toPx())
                    drawCircle(c, 3 * s, p(20f, 12f))
                }
                "patrickreynolds/tonnetz" -> {
                    val px = floatArrayOf(4f, 12f, 20f, 28f, 36f); val py = floatArrayOf(24f, 6f, 24f, 6f, 24f)
                    for (k in 0 until 4) drawLine(c, p(px[k], py[k]), p(px[k + 1], py[k + 1]), line)
                    drawLine(c, p(4f, 24f), p(36f, 24f), line)
                    drawLine(c, p(8f, 15f), p(32f, 15f), 1.2.dp.toPx())
                    for (k in 1..3) drawCircle(c, 2.4f * s, p(px[k], py[k]))
                }
                "patrickreynolds/hex" -> {
                    // Five hexagons, two rows nested, a dot in the middle one of the lower row.
                    val cx = floatArrayOf(9f, 20f, 31f, 14.5f, 25.5f); val cy = floatArrayOf(18.5f, 18.5f, 18.5f, 9f, 9f); val r = 5.77f
                    val hx = floatArrayOf(0f, 5f, 5f, 0f, -5f, -5f); val hy = floatArrayOf(-r, -r / 2, r / 2, r, r / 2, -r / 2)
                    for (k in 0 until 5) for (i in 0 until 6)
                        drawLine(c, p(cx[k] + hx[i], cy[k] + hy[i]), p(cx[k] + hx[(i + 1) % 6], cy[k] + hy[(i + 1) % 6]), 1.4.dp.toPx())
                    drawCircle(c, 2.4f * s, p(20f, 18.5f))
                }
                else -> box(6f, 3f, 28f, 22f, 4f, false)
            }
        }
        if (known != "patrickreynolds/grid" && known != "patrickreynolds/touchpad" && known != "patrickreynolds/tonnetz" && known != "patrickreynolds/hex")
            Text(name.take(1), color = c, fontSize = 12.sp, fontWeight = FontWeight.Bold)
    }
}

// ---- takes ------------------------------------------------------------------------------------------

/**
 * Takes (Takes.kt): record what you play; your takes; the one chosen played back. The same rows,
 * words and order as odo for computers' Takes panel (desktop/main.cpp panelLayout).
 */
@Composable
private fun TakesPanel() {
    val t = com.patrickreynolds.mpeinstruments.Takes
    val context = androidx.compose.ui.platform.LocalContext.current
    var confirmDelete by remember(t.chosen) { mutableStateOf(false) }
    val openFile = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) t.open(context, uri)
    }
    androidx.compose.runtime.LaunchedEffect(Unit) { t.scan() }
    PHeading("RECORD")
    if (t.recording >= 0) PRow(if (t.recording > 0) "Stop recording · ${t.clock(t.recording)}" else "Stop recording (play something first)", on = true) { t.toggleRecord() }
    else PRow("Record") { t.toggleRecord() }
    if (t.note.isNotEmpty()) PNote(t.note)
    PHeading("YOURS")
    for (take in t.list) PRow(take.name, t.sub(take), on = take.file.name == t.chosen) { t.choose(take); confirmDelete = false }
    if (t.list.isEmpty()) PNote("None yet: record one, or open a take file.")
    PRow("Open a take file…") { openFile.launch(arrayOf("*/*")); confirmDelete = false }
    val on = t.current() ?: return
    PHeading("THIS TAKE")
    PRow(if (t.playing == 1) "Stop" else "Listen", on = t.playing == 1) { t.play(0); confirmDelete = false }
    PRow(if (t.playing == 2) "Stop" else "Play along", on = t.playing == 2) { t.play(1); confirmDelete = false }
    val speeds = listOf(0.5, 0.75, 1.0)
    PLabel("Speed", "${Math.round(t.speed * 100)}%", rule = true)
    PChips(listOf("50%", "75%", "100%"), speeds.indexOfFirst { Math.abs(it - t.speed) < 1e-6 }.let { if (it < 0) 2 else it }) {
        t.changeSpeed(speeds[it]); confirmDelete = false
    }
    PLabel("Fingers", t.looks[t.look], rule = true)
    PChips(t.looks, t.look) { t.changeLook(it); confirmDelete = false }
    if (on.sections.isNotEmpty()) {
        PLabel("Loop", if (t.loop < 0 || t.loop >= on.sections.size) "All of it" else on.sections[t.loop], rule = true)
        PChips(listOf("All of it") + on.sections, t.loop + 1) { t.changeLoop(it - 1); confirmDelete = false }
    }
    PRow("Use its settings") { t.useSettings(); confirmDelete = false }
    PRow("Keep its settings as a new preset") { t.keepSettings(); confirmDelete = false }
    PRow("Share “${on.name}” as a file…") { t.share(context, on); confirmDelete = false }
    PRow((if (confirmDelete) "Tap again to delete “" else "Delete “") + on.name + "”", danger = confirmDelete) {
        if (confirmDelete) { t.delete(on); confirmDelete = false } else confirmDelete = true
    }
}
