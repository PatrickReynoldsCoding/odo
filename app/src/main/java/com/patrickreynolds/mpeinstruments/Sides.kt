package com.patrickreynolds.mpeinstruments

import com.patrickreynolds.mpeinstruments.controllers.Controller
import org.json.JSONArray
import org.json.JSONObject

/**
 * A split of the screen into sides, each its own controller playing its own sound(s): the engine's
 * (mpe/split.h), so the names, the sides' names and where the dividers may go are odo for
 * computers' too. Each side's controller is the whole play area and shows its own share of it, so a
 * divider slides between them, as the touch pad's two pads.
 */
data class Split(val name: String, val sides: Int, val cols: Int, val side: List<String>) {
    val rows get() = (sides + cols - 1) / cols
}

/** Where a side shows, as fractions of the play area. */
data class Region(val x: Float, val y: Float, val w: Float, val h: Float) {
    fun contains(fx: Float, fy: Float) = fx >= x && fx < x + w && fy >= y && fy < y + h
}

object Splits {
    /** The splits, by index (a preset's `split`): Whole, Side by side, Above and below, ... */
    val all: List<Split> by lazy {
        val a = JSONArray(Native.nativeSplits().decodeToString())
        List(a.length()) { i ->
            val o = a.getJSONObject(i)
            val names = o.getJSONArray("side")
            Split(o.getString("name"), o.getInt("sides"), o.getInt("cols"), List(names.length()) { names.getString(it) })
        }
    }
    fun of(n: Int) = all[n.coerceIn(all.indices)]

    /** Split [n]'s dividers from [at] (evenly if it has not as many), kept in order and apart. */
    fun dividers(n: Int, at: List<Double>): List<Double> = Native.nativeSplitDividers(n, at.toDoubleArray()).toList()

    /** Each side's region with the dividers [at]. */
    fun regions(n: Int, at: List<Double>): List<Region> {
        val r = Native.nativeSplitRegions(n, at.toDoubleArray())
        return List(r.size / 4) { k -> Region(r[4 * k].toFloat(), r[4 * k + 1].toFloat(), r[4 * k + 2].toFloat(), r[4 * k + 3].toFloat()) }
    }

    /** The divider within [grab] dp of (x, y) in a w x h dp play area, or -1. */
    fun dividerAt(n: Int, at: List<Double>, x: Float, y: Float, w: Float, h: Float, grab: Float) =
        Native.nativeSplitDividerAt(n, at.toDoubleArray(), x.toDouble(), y.toDouble(), w.toDouble(), h.toDouble(), grab.toDouble())

    /** Divider [i] dragged to (fx, fy), fractions of the play area: the dividers then. */
    fun dragged(n: Int, at: List<Double>, i: Int, fx: Float, fy: Float): List<Double> =
        Native.nativeSplitDragged(n, at.toDoubleArray(), i, fx.toDouble(), fy.toDouble()).toList()

    /** "1 side", "2 sides". */
    fun sidesWord(n: Int) = if (n == 1) "1 side" else "$n sides"
}

/** A side of the screen: its controller (its own instance, with its own settings) and its parts' sounds. */
data class Side(val controller: Controller, val parts: List<Part>)

/** A side after the first in a preset: its controller's id, its settings (JSON text) and its parts. */
data class PresetSide(val controller: String, val settings: String?, val parts: List<Part>) {
    fun json(): JSONObject = JSONObject().put("controller", controller).apply {
        settings?.let { put("settings", JSONObject(it)) }
        put("parts", partsJson(parts))
    }
    companion object {
        fun from(o: JSONObject) = PresetSide(o.optString("controller"), o.optJSONObject("settings")?.toString(),
                                             partsFrom(o.optJSONArray("parts")))
    }
}

/** Always [PARTS] parts: those given, then the defaults. */
fun fullParts(parts: List<Part>): List<Part> = List(PARTS) { k -> parts.getOrElse(k) { Perf.DEFAULT_PARTS[k] } }
fun partsJson(parts: List<Part>) = JSONArray().apply { for (p in parts) put(p.json()) }
fun partsFrom(a: JSONArray?): List<Part> =
    fullParts(if (a == null) emptyList() else (0 until a.length()).map { Part.from(a.optJSONObject(it), Perf.DEFAULT_PARTS[it.coerceAtMost(PARTS - 1)]) })
