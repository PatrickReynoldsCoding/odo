package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.controllers.Option
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

/** A controller package's settings on the rail, from its manifest. */
@Composable
fun packageOptions(c: PackageController): List<Option> {
    val accent = if (Eink) Color.Black else PartColors[0]
    // A "hidden" setting is state the controller keeps itself (its editor's), not for the rail.
    return c.settings.withIndex().filter { !it.value.optBoolean("hidden") }.map { (k, s) ->
        val v = c.values.getOrElse(k) { 0.0 }
        val label = s.optString("name", s.optString("id"))
        when (s.optString("type")) {
            "switch" -> Option(label, if (v != 0.0) "On" else "Off", perPart = false, on = v != 0.0, tap = { c.change(k, if (v != 0.0) 0.0 else 1.0) })
            "choice" -> {
                val opts = s.optJSONArray("options")?.let { a -> List(a.length()) { a.optString(it) } } ?: emptyList()
                Option(label, opts.getOrElse(v.roundToInt()) { "" }, perPart = false) {
                    Chips(opts, v.roundToInt(), accent) { i -> c.change(k, i.toDouble()) }
                }
            }
            else -> {
                val min = s.optDouble("min", 0.0); val max = s.optDouble("max", 1.0)
                val step = s.optDouble("step", 0.0)
                fun show(x: Double) = if (step >= 1.0 || step == 0.0 && x == x.roundToInt().toDouble()) "${x.roundToInt()}" else "%.2f".format(x)
                Option(label, show(v), perPart = false) {
                    val count = if (step > 0) ((max - min) / step).roundToInt() + 1 else 0
                    if (count in 2..16) Chips(List(count) { show(min + it * step) }, ((v - min) / step).roundToInt(), accent) { i ->
                        c.change(k, min + i * step) }
                    else LabeledSlider(label, v.toFloat(), min.toFloat()..max.toFloat(), show(v), accent = accent) { x ->
                        c.change(k, if (step > 0) min + ((x - min) / step).roundToInt() * step else x.toDouble()) }
                }
            }
        }
    }
}

/** A controller package's play surface: its draw list drawn, the fingers sent to it, its time ticked. */
@Composable
fun PackageSurface(c: PackageController) {
    var list by remember { mutableStateOf<ByteArray?>(null) }
    var failed by remember { mutableStateOf<String?>(null) }
    DisposableEffect(c) {
        if (!c.start()) failed = Native.nativeCtlError()
        onDispose { Native.nativeCtlStop() }
    }
    val center = AppState.instrument(0)?.centerNote ?: 60f
    // A split (the Grid's): the second part's instrument too, so its half centres on its own sound.
    val center1 = if (c.inUse > 1) AppState.instrument(1)?.centerNote else null
    LaunchedEffect(center) { Native.nativeCtlInstrument(0, center) }
    LaunchedEffect(center1) { center1?.let { Native.nativeCtlInstrument(1, it) } }
    LaunchedEffect(c) {
        while (true) {
            withFrameNanos { }
            Native.nativeCtlTick()?.let { list = it }
            Native.nativeCtlTakeSettings()?.let { a -> for (i in 0 until a.size / 2) c.changedByIt(a[2 * i].toInt(), a[2 * i + 1]) }
        }
    }
    failed?.let { Box(Modifier.fillMaxSize()) { Text("This controller can't run: $it", color = Warn, fontSize = 14.sp, modifier = Modifier.padding(16.dp)) }; return }
    var sized by remember { mutableStateOf(Size.Zero) }
    val context = LocalContext.current
    val faces = remember { OdoTypefaces(context) }
    Canvas(Modifier.fillMaxSize().clipToBounds().systemGestureExclusion()
        .pointerInput(c) {
            awaitPointerEventScope {
                while (true) {
                    val e = awaitPointerEvent()
                    for (ch in e.changes) {
                        val phase = when {
                            ch.pressed && !ch.previousPressed -> 0
                            ch.pressed -> 1
                            ch.previousPressed -> 2
                            else -> -1
                        }
                        if (phase >= 0) Native.nativeCtlTouch(ch.id.value.toInt(), phase, ch.position.x / density, ch.position.y / density,
                                                              0f, ch.pressure)
                        ch.consume()
                    }
                }
            }
        }) {
        if (size != sized) {
            sized = size
            Native.nativeCtlInit(size.width / density, size.height / density, if (Eink) 1 else 0)
            Native.nativeCtlInstrument(0, center)
            center1?.let { Native.nativeCtlInstrument(1, it) }
            c.values.forEachIndexed { k, v -> Native.nativeCtlSetting(k, v) }
        }
        val d = density
        list?.let { bytes -> withTransform({ scale(d, d, Offset.Zero) }) { drawList(bytes, size.width / d, size.height / d, faces) } }
    }
}

private fun color(c: Int): Color {
    val u = c.toLong() and 0xFFFFFFFFL
    return Color(red = ((u shr 24) and 255).toInt(), green = ((u shr 16) and 255).toInt(), blue = ((u shr 8) and 255).toInt(),
                 alpha = (u and 255).toInt())
}

/** A draw list (CONTROLLER_FORMAT.md), in dp, its text in odo's Roboto. An operation it doesn't know ends the drawing. */
private fun DrawScope.drawList(bytes: ByteArray, w: Float, h: Float, faces: OdoTypefaces) {
    val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    val text = android.graphics.Paint().apply { isAntiAlias = true }
    while (b.remaining() >= 4) {
        when (b.int) {
            1 -> drawRect(color(b.int), Offset.Zero, Size(w, h))
            2 -> {
                val x = b.float; val y = b.float; val ww = b.float; val hh = b.float; val r = b.float
                val fill = color(b.int); val stroke = color(b.int); val sw = b.float
                if (fill.alpha > 0) drawRoundRect(fill, Offset(x, y), Size(ww, hh), CornerRadius(r, r))
                if (sw > 0 && stroke.alpha > 0) drawRoundRect(stroke, Offset(x, y), Size(ww, hh), CornerRadius(r, r), style = Stroke(sw))
            }
            3 -> {
                val x = b.float; val y = b.float; val r = b.float
                val fill = color(b.int); val stroke = color(b.int); val sw = b.float
                if (fill.alpha > 0) drawCircle(fill, r, Offset(x, y))
                if (sw > 0 && stroke.alpha > 0) drawCircle(stroke, r, Offset(x, y), style = Stroke(sw))
            }
            4 -> {
                val x1 = b.float; val y1 = b.float; val x2 = b.float; val y2 = b.float
                val c = color(b.int); val sw = b.float
                drawLine(c, Offset(x1, y1), Offset(x2, y2), strokeWidth = sw)
            }
            5 -> {
                val n = b.int
                val fill = color(b.int); val stroke = color(b.int); val sw = b.float
                if (n < 0 || n > 4096 || b.remaining() < n * 8) return
                val path = Path()
                for (k in 0 until n) { val x = b.float; val y = b.float; if (k == 0) path.moveTo(x, y) else path.lineTo(x, y) }
                path.close()
                if (fill.alpha > 0) drawPath(path, fill)
                if (sw > 0 && stroke.alpha > 0) drawPath(path, stroke, style = Stroke(sw))
            }
            6 -> {
                val x = b.float; val y = b.float; val size = b.float
                val c = color(b.int); val flags = b.int; val n = b.int
                val padded = (n + 3) and 3.inv()
                if (n < 0 || n > 4096 || b.remaining() < padded) return
                val raw = ByteArray(n); b.get(raw); b.position(b.position() + padded - n)
                text.textSize = size
                text.color = c.toArgb()
                text.typeface = if (flags and 4 != 0) faces.bold else faces.regular
                text.textAlign = when (flags and 3) { 1 -> android.graphics.Paint.Align.CENTER; 2 -> android.graphics.Paint.Align.RIGHT
                                                      else -> android.graphics.Paint.Align.LEFT }
                drawContext.canvas.nativeCanvas.drawText(raw.decodeToString(), x, y, text)
            }
            7 -> {
                val x = b.float; val y = b.float; val ww = b.float; val hh = b.float
                val top = color(b.int); val bottom = color(b.int)
                drawRect(Brush.verticalGradient(listOf(top, bottom), startY = y, endY = y + hh), Offset(x, y), Size(ww, hh))
            }
            8 -> {
                val x = b.float; val y = b.float; val r = b.float
                val inner = color(b.int); val outer = color(b.int)
                if (r > 0) drawCircle(Brush.radialGradient(listOf(inner, outer), center = Offset(x, y), radius = r), r, Offset(x, y))
            }
            else -> return
        }
    }
}
