package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The app's colours. Dark: a dark stage where the pad and the two part colours stand out. */
class Palette(
    val ink: Color, val panel: Color, val panelHigh: Color, val line: Color, val text: Color, val textDim: Color,
    val parts: List<Color>, val warn: Color, val danger: Color, val eink: Boolean,
)

/** The colours themselves live in ui/tokens.json (shared with odo for computers), written into Tokens.kt. */
val DarkPalette = Tokens.dark

/**
 * For e-ink screens (as in focus-tracker): black on white, no tints, gradients, glows or motion,
 * thick borders, the chosen option filled black. The two parts are told apart by black and grey.
 */
val EinkPalette = Tokens.eink

/** The palette in use; read in composition, so changing it recolours everything. */
var palette by mutableStateOf(DarkPalette)

val Ink get() = palette.ink
val Panel get() = palette.panel
val PanelHigh get() = palette.panelHigh
val Line get() = palette.line
val Fg get() = palette.text
val TextDim get() = palette.textDim
/** Part 0 (main, left of a split), part 1 (layer, right of a split), and a grid's third and fourth sections. */
val PartColors get() = palette.parts
val Warn get() = palette.warn
val Danger get() = palette.danger
val Eink get() = palette.eink
/** Border width of cards and controls: e-ink needs them thick to read. */
val Edge get() = if (palette.eink) 3.dp else 0.dp
/** A border for filled buttons, which on e-ink are white on white without one. */
val ButtonEdge get() = if (palette.eink) BorderStroke(2.dp, Color.Black) else null

private fun scheme(p: Palette) = if (p.eink) lightColorScheme(
    primary = Color.Black, onPrimary = Color.White, secondary = Color.Black, onSecondary = Color.White,
    background = Color.White, onBackground = Color.Black, surface = Color.White, onSurface = Color.Black,
    surfaceVariant = Color.White, onSurfaceVariant = Color.Black,
    surfaceContainer = Color.White, surfaceContainerHigh = Color.White, surfaceContainerHighest = Color.White,
    surfaceContainerLow = Color.White, surfaceContainerLowest = Color.White,
    outline = Color.Black, outlineVariant = Color.Black, secondaryContainer = Color.Black, onSecondaryContainer = Color.White,
) else darkColorScheme(
    primary = p.parts[0], onPrimary = p.ink,
    secondary = p.parts[1], onSecondary = p.ink,
    background = p.ink, onBackground = p.text,
    surface = p.ink, onSurface = p.text,
    surfaceVariant = p.panel, onSurfaceVariant = p.textDim,
    surfaceContainer = p.panel, surfaceContainerHigh = p.panelHigh, surfaceContainerHighest = p.panelHigh,
    surfaceContainerLow = p.panel, surfaceContainerLowest = p.ink,
    outline = p.line, outlineVariant = p.line,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MpeTheme(content: @Composable () -> Unit) {
    val p = palette
    // E-ink: no ripples (they redraw the screen for nothing) and a little bigger text.
    // All of it in Roboto from ui/fonts, the file odo for computers draws with.
    val base = Typography().withFamily(Roboto)
    val typography = if (p.eink) base.copy(bodyLarge = base.bodyLarge.copy(fontSize = 17.sp), bodyMedium = base.bodyMedium.copy(fontSize = 15.sp))
                     else base
    MaterialTheme(colorScheme = scheme(p), typography = typography) {
        if (p.eink) CompositionLocalProvider(LocalRippleConfiguration provides null, content = content) else content()
    }
}

@Composable
fun SectionCard(title: String, accent: Color? = null, trailing: (@Composable () -> Unit)? = null, content: @Composable () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
        shape = RoundedCornerShape(Tokens.Radius.card),
        colors = CardDefaults.cardColors(containerColor = Panel),
        border = if (Eink) BorderStroke(Edge, Line) else null,
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title.uppercase(), color = accent ?: TextDim, fontSize = Tokens.Type.header, fontWeight = FontWeight.SemiBold,
                     letterSpacing = 1.2.sp, modifier = Modifier.weight(1f))
                trailing?.invoke()
            }
            Spacer(Modifier.padding(top = 8.dp))
            content()
        }
    }
}

@Composable
fun LabeledSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, valueText: String,
                  accent: Color = PartColors[0], steps: Int = 0, onChange: (Float) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Row {
            Text(label, modifier = Modifier.weight(1f), fontSize = 14.sp)
            Text(valueText, color = TextDim, fontSize = 14.sp)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range, steps = steps,
               colors = SliderDefaults.colors(thumbColor = accent, activeTrackColor = accent,
                                              inactiveTrackColor = if (Eink) Color(0xFFBBBBBB) else Line))
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Segmented(options: List<String>, selected: Int, accent: Color = PartColors[0], onSelect: (Int) -> Unit) {
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected, onClick = { onSelect(i) },
                shape = SegmentedButtonDefaults.itemShape(i, options.size),
                colors = if (Eink) SegmentedButtonDefaults.colors(activeContainerColor = Color.Black, activeContentColor = Color.White,
                                                                  activeBorderColor = Color.Black, inactiveBorderColor = Color.Black,
                                                                  inactiveContainerColor = Color.White, inactiveContentColor = Color.Black)
                         else SegmentedButtonDefaults.colors(activeContainerColor = accent.copy(alpha = 0.22f),
                                                             activeContentColor = accent, activeBorderColor = accent,
                                                             inactiveBorderColor = Line),
                border = if (Eink) BorderStroke(2.dp, Color.Black) else SegmentedButtonDefaults.borderStroke(if (i == selected) accent else Line),
                icon = {},
            ) { Text(label, fontSize = 13.sp, maxLines = 1) }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun Chips(options: List<String>, selected: Int, accent: Color = PartColors[0], onSelect: (Int) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEachIndexed { i, label ->
            FilterChip(
                selected = i == selected, onClick = { onSelect(i) }, label = { Text(label) },
                colors = if (Eink) FilterChipDefaults.filterChipColors(selectedContainerColor = Color.Black, selectedLabelColor = Color.White,
                                                                       labelColor = Color.Black)
                         else FilterChipDefaults.filterChipColors(selectedContainerColor = accent.copy(alpha = 0.22f),
                                                                  selectedLabelColor = accent),
                border = if (Eink) BorderStroke(2.dp, Color.Black) else FilterChipDefaults.filterChipBorder(true, i == selected),
            )
        }
    }
}

@Composable
fun SwitchRow(label: String, checked: Boolean, detail: String? = null, accent: Color = PartColors[0], onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, fontSize = 14.sp)
            if (detail != null) Text(detail, color = TextDim, fontSize = 12.sp)
        }
        Spacer(Modifier.width(12.dp))
        Switch(checked = checked, onCheckedChange = onChange,
               colors = androidx.compose.material3.SwitchDefaults.colors(checkedTrackColor = accent, checkedThumbColor = Ink,
                   uncheckedBorderColor = if (Eink) Color.Black else Color.Unspecified,
                   uncheckedThumbColor = if (Eink) Color.Black else Color.Unspecified))
    }
}

fun pct(v: Float) = "${(v * 100).toInt()}%"
