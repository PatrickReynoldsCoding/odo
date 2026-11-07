package com.patrickreynolds.mpeinstruments.ui

import android.content.Context
import android.graphics.Typeface
import android.graphics.fonts.Font as PlatformFont
import android.graphics.fonts.FontFamily as PlatformFontFamily
import android.graphics.fonts.FontStyle
import androidx.compose.material3.Typography
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import com.patrickreynolds.mpeinstruments.R

/*
 * odo's font: Roboto, from ui/fonts (copied into res/font by the build), the same files odo for
 * computers carries in its binary. Roboto has no sharps or flats: Noto Sans Symbols (a few signs of
 * it, also in ui/fonts) draws those, as it does on the computer.
 */

/** Roboto for Compose text. Signs Roboto lacks fall back to the system's fonts. */
val Roboto = FontFamily(
    Font(R.font.roboto_regular, FontWeight.Normal),
    Font(R.font.roboto_medium, FontWeight.Medium),
    Font(R.font.roboto_bold, FontWeight.Bold),
)

/** Material's type scale, all in [family]. */
fun Typography.withFamily(family: FontFamily) = Typography(
    displayLarge = displayLarge.copy(fontFamily = family), displayMedium = displayMedium.copy(fontFamily = family),
    displaySmall = displaySmall.copy(fontFamily = family), headlineLarge = headlineLarge.copy(fontFamily = family),
    headlineMedium = headlineMedium.copy(fontFamily = family), headlineSmall = headlineSmall.copy(fontFamily = family),
    titleLarge = titleLarge.copy(fontFamily = family), titleMedium = titleMedium.copy(fontFamily = family),
    titleSmall = titleSmall.copy(fontFamily = family), bodyLarge = bodyLarge.copy(fontFamily = family),
    bodyMedium = bodyMedium.copy(fontFamily = family), bodySmall = bodySmall.copy(fontFamily = family),
    labelLarge = labelLarge.copy(fontFamily = family), labelMedium = labelMedium.copy(fontFamily = family),
    labelSmall = labelSmall.copy(fontFamily = family),
)

/** Typefaces for drawing a controller's draw list: Roboto regular and bold, then the bundled symbols. */
class OdoTypefaces(context: Context) {
    val regular: Typeface = build(context, R.font.roboto_regular, R.font.notosanssymbols_regular_subset, FontStyle.FONT_WEIGHT_NORMAL)
    val bold: Typeface = build(context, R.font.roboto_bold, R.font.notosanssymbols_bold_subset, FontStyle.FONT_WEIGHT_BOLD)

    private fun build(context: Context, main: Int, symbols: Int, weight: Int): Typeface {
        val style = FontStyle(weight, FontStyle.FONT_SLANT_UPRIGHT)
        fun family(res: Int) = PlatformFontFamily.Builder(PlatformFont.Builder(context.resources, res).setWeight(weight).build()).build()
        // Then the system's fonts, for whatever neither has.
        return Typeface.CustomFallbackBuilder(family(main)).addCustomFallback(family(symbols)).setSystemFallback("sans-serif")
            .setStyle(style).build()
    }
}
