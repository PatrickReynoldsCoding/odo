package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.ui.graphics.Color
import com.patrickreynolds.mpeinstruments.PadSettings

/*
 * The touch pad's looks, which the rail and the loop buttons take their colours from (the touch
 * pad itself is a controller package from 0.26, controllers/touchpad, which draws the same looks).
 */

/** How the pad looks. [flat] is the e-ink look: no gradients, glows, trails or ripples. */
class PadTheme(
    val name: String, val top: Color, val mid: Color, val bottom: Color, val line: Color, val label: Color,
    val strip: Color, val rail: Color, val railText: Color, val railDim: Color, val parts: List<Color>,
    val flat: Boolean = false,
)

private fun dark(name: String, top: Long, mid: Long, bottom: Long, a: Long, b: Long, strip: Long = 0xFF07080B) = PadTheme(
    name, Color(top), Color(mid), Color(bottom), Color.White, Color.White, Color(strip),
    Color.Black.copy(alpha = 0.35f), Color.White.copy(alpha = 0.85f), Color(0xFF9AA2B4), listOf(Color(a), Color(b)))

val PAD_THEMES = listOf(
    dark("Night", 0xFF1A1626, 0xFF0E1014, 0xFF0B1418, 0xFFFF8A65, 0xFF4DD0E1),
    dark("Sunset", 0xFF3A1430, 0xFF2A1022, 0xFF1A0C18, 0xFFFFB74D, 0xFFF06292, strip = 0xFF12070F),
    dark("Ocean", 0xFF0A2E3A, 0xFF07202B, 0xFF041318, 0xFF80DEEA, 0xFF69F0AE, strip = 0xFF020B0E),
    dark("Forest", 0xFF1B2A17, 0xFF131E11, 0xFF0C140B, 0xFFD4E157, 0xFFFFB74D, strip = 0xFF060B05),
    dark("Contrast", 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFEB3B, 0xFF00E5FF, strip = 0xFF1C1C1C),
    PadTheme("Daylight", Color(0xFFF7F3EA), Color(0xFFF1ECE1), Color(0xFFE9E3D6), Color(0xFF1B1B1B), Color(0xFF1B1B1B),
             Color(0xFFDCD4C3), Color(0xFFE2DBCB), Color(0xFF1B1B1B), Color(0xFF5A5A5A), listOf(Color(0xFFD84315), Color(0xFF00838F))),
)

private val EINK_PAD = PadTheme("E-ink", Color.White, Color.White, Color.White, Color.Black, Color.Black,
    Color.White, Color.White, Color.Black, Color(0xFF333333), listOf(Color.Black, Color(0xFF666666)), flat = true)

fun padTheme(s: PadSettings) = if (Eink) EINK_PAD else PAD_THEMES.getOrElse(s.theme) { PAD_THEMES[0] }
