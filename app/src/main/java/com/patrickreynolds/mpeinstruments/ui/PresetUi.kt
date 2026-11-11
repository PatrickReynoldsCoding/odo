package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.Preset

/** Asks for a new name for one of your presets. */
@Composable
fun RenameDialog(p: Preset, onDone: () -> Unit) {
    // The old name starts selected, so typing replaces it.
    var name by remember { mutableStateOf(TextFieldValue(p.name, TextRange(0, p.name.length))) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    AlertDialog(onDismissRequest = onDone, containerColor = Panel,
        title = { Text("Rename") },
        text = { OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true, label = { Text("Name") },
                                   modifier = Modifier.focusRequester(focus)) },
        confirmButton = { TextButton(onClick = { AppState.rename(p, name.text); onDone() }, enabled = name.text.isNotBlank()) { Text("Rename") } },
        dismissButton = { TextButton(onClick = onDone) { Text("Cancel") } })
}

/** What can be done to a preset: rename, duplicate, move, delete (yours), or copy it to yours (built-in). */
@Composable
fun PresetMenu(p: Preset, expanded: Boolean, onDismiss: () -> Unit, onRename: () -> Unit) {
    var confirm by remember { mutableStateOf(false) }
    DropdownMenu(expanded = expanded, onDismissRequest = { confirm = false; onDismiss() }) {
        if (p.builtIn) {
            DropdownMenuItem(text = { Text("Copy to your presets") }, onClick = { AppState.duplicate(p); onDismiss() })
            return@DropdownMenu
        }
        DropdownMenuItem(text = { Text("Rename") }, onClick = { onDismiss(); onRename() })
        DropdownMenuItem(text = { Text("Duplicate") }, onClick = { AppState.duplicate(p); onDismiss() })
        DropdownMenuItem(text = { Text("Move earlier") }, onClick = { AppState.move(p, -1); onDismiss() })
        DropdownMenuItem(text = { Text("Move later") }, onClick = { AppState.move(p, 1); onDismiss() })
        DropdownMenuItem(text = { Text(if (confirm) "Tap again to delete" else "Delete", color = if (confirm) Warn else Color.Unspecified) },
                         onClick = { if (confirm) { AppState.delete(p); confirm = false; onDismiss() } else confirm = true })
    }
}

/**
 * The preset library. A preset is a whole touch pad: its sounds and effects, and the pads'
 * notes, scale, glide, split... A save point: what you change after is not kept in it until you
 * save it. The starting points are the app's own: change one and keep it as yours.
 */
@Composable
fun PresetLibrary(accent: Color, onLoaded: () -> Unit = {}) {
    var renaming by remember { mutableStateOf<Preset?>(null) }
    val current = AppState.presetName
    val builtInOn = AppState.allPresets.firstOrNull { it.name == current }?.builtIn == true
    Text(when {
        current.isEmpty() -> "Not a preset yet: keep what is playing as one of yours."
        builtInOn && AppState.presetEdited -> "$current, changed: keep it as yours, or the changes are not saved."
        builtInOn -> "$current, a starting point."
        AppState.presetEdited -> "$current, changed since saved: save the changes, or they are not kept."
        else -> "$current, as saved."
    }, color = TextDim, fontSize = 12.sp)
    Row(Modifier.padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = { AppState.newPreset(if (builtInOn) current else AppState.soundName()) }) {
            Text(if (builtInOn && AppState.presetEdited) "Keep as mine" else "New from what is playing") }
    }
    Text("YOURS", color = TextDim, fontSize = 12.sp, letterSpacing = 1.2.sp, modifier = Modifier.padding(top = 6.dp, bottom = 2.dp))
    if (AppState.userPresets.isEmpty()) Text("None yet.", color = TextDim, fontSize = 13.sp)
    for (p in AppState.userPresets) PresetRow(p, p.name == current, accent, onLoaded) { renaming = p }
    HorizontalDivider(color = Line, modifier = Modifier.padding(vertical = 8.dp))
    Text("STARTING POINTS", color = TextDim, fontSize = 12.sp, letterSpacing = 1.2.sp, modifier = Modifier.padding(bottom = 2.dp))
    for (p in AppState.allPresets.filter { it.builtIn }) PresetRow(p, p.name == current, accent, onLoaded) { }
    renaming?.let { RenameDialog(it) { renaming = null } }
}

@Composable
private fun PresetRow(p: Preset, on: Boolean, accent: Color, onLoaded: () -> Unit, onRename: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().clickable { AppState.load(p); onLoaded() }.padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(p.name, fontSize = 16.sp, color = if (on) accent else Color.Unspecified,
                 fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal)
            Text(presetSummary(p), color = TextDim, fontSize = 12.sp, maxLines = 1)
        }
        Box {
            TextButton(onClick = { menu = true }) { Text("⋮", fontSize = 18.sp, color = TextDim) }
            PresetMenu(p, menu, { menu = false }, onRename)
        }
    }
}
