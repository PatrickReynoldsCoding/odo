package com.patrickreynolds.mpeinstruments.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
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
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.Region
import com.patrickreynolds.mpeinstruments.Splits
import com.patrickreynolds.mpeinstruments.Takes
import com.patrickreynolds.mpeinstruments.controllers.PackageController
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** One side's controller on screen: its draw list, why it can't run (if it can't), what it was started at. */
private class SideView {
    var list by mutableStateOf<ByteArray?>(null)
    var failed by mutableStateOf<String?>(null)
    var startedAt: Pair<Size, PackageController>? = null
}

/** The line between two sides (dp), and how near a touch must be to drag it, as odo for computers'. */
private const val GAP = 3f
private const val DIVIDE_GRAB = 14f

/** A controller package's play surface, the whole screen: its draw list drawn, the fingers sent to it, its time ticked. */
@Composable
fun PackageSurface(c: PackageController) = PackageSides(listOf(c))

/**
 * The play area with the sides of a split, as the touch pad's two pads: each side's controller is
 * the whole play area and shows only its own share of it. Between them a divider (a line and a
 * grip) is dragged to show more of one and less of another; the controllers keep their size, so
 * it slides. A touch goes to the side it lands on, at the same place as on the whole surface; one
 * within 14 dp of a divider drags it and plays nothing. A take plays on the first side. [cs]: each
 * side's controller, side k's in slot k.
 */
@Composable
fun PackageSides(cs: List<PackageController>) {
    val views = remember { List(4) { SideView() } }
    var ghosts by remember { mutableStateOf<ByteArray?>(null) }     // a take playing: its fingers (Takes)
    val n = cs.size
    val split = if (n > 1) AppState.split else 0
    val at = AppState.splitAt
    val regions = remember(split, at, n) { if (n > 1) Splits.regions(split, at) else listOf(Region(0f, 0f, 1f, 1f)) }
    for ((k, c) in cs.withIndex()) key(k, c) { SideSetup(k, c, views[k]) }
    LaunchedEffect(cs) {
        while (true) {
            withFrameNanos { }
            for ((k, c) in cs.withIndex()) {
                Native.nativeCtlTick(c.slot)?.let { views[k].list = it }
                Native.nativeCtlTakeSettings(c.slot)?.let { a -> for (i in 0 until a.size / 2) c.changedByIt(a[2 * i].toInt(), a[2 * i + 1]) }
            }
            ghosts = Takes.tick(Takes.surfaceW, Takes.surfaceH)
        }
    }
    var sized by remember { mutableStateOf(Size.Zero) }
    var dragging by remember { mutableStateOf(-1) }      // the divider being dragged, or -1
    val context = LocalContext.current
    val faces = remember { OdoTypefaces(context) }
    // The pointer handler lives as long as the surface: what it routes by is read fresh.
    val sidesNow by rememberUpdatedState(cs)
    val splitNow by rememberUpdatedState(split)
    val atNow by rememberUpdatedState(at)
    val regionsNow by rememberUpdatedState(regions)
    val outline = n > 1 && !AppState.railHidden
    val active = AppState.activeSide
    val ink = Fg.copy(alpha = 0x99 / 255f)
    val grip = PanelHigh
    BoxWithConstraints(Modifier.fillMaxSize()) {
        Canvas(Modifier.fillMaxSize().clipToBounds().systemGestureExclusion()
            .pointerInput(Unit) {
                val owner = HashMap<Long, Int>()          // a finger: the side it went down on
                var drag = -1L                            // the finger dragging a divider
                awaitPointerEventScope {
                    while (true) {
                        val e = awaitPointerEvent()
                        val w = size.width / density; val h = size.height / density
                        for (ch in e.changes) {
                            val phase = when {
                                ch.pressed && !ch.previousPressed -> 0
                                ch.pressed -> 1
                                ch.previousPressed -> 2
                                else -> -1
                            }
                            ch.consume()
                            if (phase < 0) continue
                            val id = ch.id.value
                            val x = ch.position.x / density; val y = ch.position.y / density
                            if (id == drag) {
                                if (phase == 1) AppState.dragDivider(dragging, x / w, y / h)
                                else { AppState.dragDivider(dragging, x / w, y / h, done = true); drag = -1L; dragging = -1 }
                                continue
                            }
                            val list = sidesNow
                            if (phase == 0) {
                                if (list.size > 1 && drag < 0) {
                                    val d = Splits.dividerAt(splitNow, atNow, x, y, w, h, DIVIDE_GRAB)
                                    if (d >= 0) { drag = id; dragging = d; continue }
                                }
                                val k = if (list.size > 1) regionsNow.indexOfFirst { it.contains(x / w, y / h) } else 0
                                if (k !in list.indices) continue
                                owner[id] = k
                            }
                            val k = owner[id] ?: continue
                            list.getOrNull(k)?.let { Native.nativeCtlTouch(it.slot, id.toInt(), phase, x, y, 0f, ch.pressure) }
                            if (phase == 2) owner.remove(id)
                        }
                    }
                }
            }) {
            val d = density
            val w = size.width / d; val h = size.height / d
            if (size != sized) {
                sized = size
                Takes.surfaceW = w
                Takes.surfaceH = h
            }
            for ((k, c) in cs.withIndex()) {
                val v = views[k]
                // Each side started at the whole play area's size (again when it changes), its settings given.
                if (v.startedAt != (size to c)) {
                    v.startedAt = size to c
                    Native.nativeCtlInit(c.slot, w, h, if (Eink) 1 else 0)
                    sideCenters(k, c).forEachIndexed { j, m -> Native.nativeCtlInstrument(c.slot, j, m) }
                    c.values.forEachIndexed { i, x -> Native.nativeCtlSetting(c.slot, i, x) }
                }
                val r = regions.getOrElse(k) { Region(0f, 0f, 1f, 1f) }
                clipRect(r.x * size.width, r.y * size.height, (r.x + r.w) * size.width, (r.y + r.h) * size.height) {
                    v.list?.let { bytes -> withTransform({ scale(d, d, Offset.Zero) }) { drawList(bytes, w, h, faces) } }
                    if (k == 0) ghosts?.let { bytes -> withTransform({ scale(d, d, Offset.Zero) }) { drawList(bytes, w, h, faces) } }
                }
            }
            if (n > 1) {
                val cols = Splits.of(split).cols
                for (k in 0 until n) {
                    val r = regions.getOrNull(k) ?: continue
                    val vx = r.x * size.width; val vy = r.y * size.height; val vw = r.w * size.width; val vh = r.h * size.height
                    val c = k % cols; val row = k / cols
                    for (across in listOf(true, false)) {
                        if (if (across) c == 0 else row == 0) continue
                        val index = if (across) c - 1 else cols - 1 + row - 1
                        val dragged = dragging == index
                        val line = if (dragged) sideColour(k) else ink
                        val g = GAP * d
                        if (across) drawRect(line, Offset(vx - g / 2, vy), Size(g, vh)) else drawRect(line, Offset(vx, vy - g / 2), Size(vw, g))
                        // The grip: in the middle of this side's stretch of the divider.
                        val gx = if (across) vx else vx + vw / 2
                        val gy = if (across) vy + vh / 2 else vy
                        val gw = (if (across) 28f else 56f) * d
                        val gh = (if (across) 56f else 28f) * d
                        val rad = CornerRadius(14f * d)
                        drawRoundRect(if (dragged) sideColour(k) else grip, Offset(gx - gw / 2, gy - gh / 2), Size(gw, gh), rad)
                        drawRoundRect(ink, Offset(gx - gw / 2, gy - gh / 2), Size(gw, gh), rad, style = Stroke(2f * d))
                        for (q in -1..1) {
                            if (across) drawRect(ink, Offset(gx + (q * 5 - 1) * d, gy - 10 * d), Size(2 * d, 20 * d))
                            else drawRect(ink, Offset(gx - 10 * d, gy + (q * 5 - 1) * d), Size(20 * d, 2 * d))
                        }
                    }
                }
                // With the rail out, the side it changes outlined in its own colour.
                if (outline) regions.getOrNull(active)?.let { r ->
                    drawRoundRect(sideColour(active).copy(alpha = 0xC0 / 255f), Offset(r.x * size.width + 2 * d, r.y * size.height + 2 * d),
                                  Size(r.w * size.width - 4 * d, r.h * size.height - 4 * d), CornerRadius(6f * d), style = Stroke(3f * d))
                }
            }
        }
        for ((k, _) in cs.withIndex()) views[k].failed?.let { why ->
            val r = regions.getOrElse(k) { Region(0f, 0f, 1f, 1f) }
            Box(Modifier.offset(maxWidth * r.x, maxHeight * r.y).size(maxWidth * r.w, maxHeight * r.h)) {
                Text("This controller can't run: $why", color = Warn, fontSize = 14.sp, modifier = Modifier.padding(16.dp))
            }
        }
    }
}

/** Each part side [k]'s controller plays: its instrument's middle (a split's further keyboards each centre on their own sound). */
private fun sideCenters(k: Int, c: PackageController): List<Float> {
    val first = AppState.instrumentOf(k, 0)?.centerNote ?: 60f
    return List(c.inUse.coerceIn(1, 4)) { j -> if (j == 0) first else AppState.instrumentOf(k, j)?.centerNote ?: first }
}

/** Side [k]'s controller started in its host (stopped when it goes), told its sounds' middles and which are drum kits. */
@Composable
private fun SideSetup(k: Int, c: PackageController, v: SideView) {
    DisposableEffect(c) {
        v.failed = if (c.start()) null else Native.nativeCtlError(c.slot)
        v.list = null
        v.startedAt = null
        onDispose { if (k == 0) Takes.stop(); Native.nativeCtlStop(c.slot) }
    }
    val centers = sideCenters(k, c)
    LaunchedEffect(c, centers) { centers.forEachIndexed { j, m -> Native.nativeCtlInstrument(c.slot, j, m) } }
    // A drum kit: the controller's "drumsFrom" switches follow each part's sound (the touch pad lays out drums).
    val drums = List(2) { AppState.instrumentOf(k, it)?.group == "drums" }
    LaunchedEffect(c, drums) { c.syncDrums(drums) }
}


private fun color(c: Int): Color {
    val u = c.toLong() and 0xFFFFFFFFL
    return Color(red = ((u shr 24) and 255).toInt(), green = ((u shr 16) and 255).toInt(), blue = ((u shr 8) and 255).toInt(),
                 alpha = (u and 255).toInt())
}

/** A draw list (docs/formats/CONTROLLER_FORMAT.md), in dp, its text in odo's Roboto. An operation it doesn't know ends the drawing. */
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
