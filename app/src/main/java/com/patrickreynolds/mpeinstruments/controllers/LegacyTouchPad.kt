package com.patrickreynolds.mpeinstruments.controllers

import com.patrickreynolds.mpeinstruments.Arp
import com.patrickreynolds.mpeinstruments.Chords
import com.patrickreynolds.mpeinstruments.PadSettings
import org.json.JSONObject

/**
 * The touch pad built into odo until 0.26, in Kotlin. The touch pad package
 * (controllers/touchpad, the same file odo for computers plays) does all it did, so it took its
 * place: settings and presets saved for the Kotlin one become the package's here.
 *
 * Its settings were {"left": pad, "right": pad, "split": bool, "splitAt": 0..1}, each pad a
 * PadSettings; the package has one value per setting, the right pad's "Same as left" where it is.
 */
object LegacyTouchPad {
    /** Its id, kept in saved settings and presets. */
    const val ID = "touchpad"
    /** The package that took its place. */
    const val PACKAGE = "pkg:patrickreynolds/touchpad"

    /** Settings in its format (rather than the package's, which keeps them "byId"). */
    fun isLegacy(o: JSONObject) = !o.has("byId") && !o.has("values") &&
        (o.has("left") || o.has("right") || o.has("splitAt") || o.opt("split") is Boolean)

    /** A built-in preset's settings, in its format: the split given (off unless said), the pads only if given. */
    fun preset(split: Boolean = false, pads: List<PadSettings>? = null, splitAt: Float = 0.5f): JSONObject = JSONObject().apply {
        put("split", split)
        if (pads != null) { put("left", pads[0].json()); put("right", pads[1].json()); put("splitAt", splitAt.toDouble()) }
    }

    /**
     * Before 0.16 its settings were the app's: the pads (their own, or a preset's), and in the
     * performance the split (layout 1) and each part's arp.
     */
    fun legacy(left: JSONObject?, right: JSONObject?, splitAt: Float?, perf: JSONObject?): JSONObject = JSONObject().apply {
        if (left != null) {
            put("left", left); right?.let { put("right", it) }
            put("splitAt", (splitAt ?: 0.5f).toDouble())
        }
        put("split", perf?.optInt("layout", 0) == 1)
        put("arp0", Arp.from(perf?.optJSONObject("part0")).json())
        put("arp1", Arp.from(perf?.optJSONObject("part1")).json())
    }

    /** Until 0.21 each pad had its own arp: "arp0" and "arp1". */
    fun arps(o: JSONObject?) = o?.let { listOf(Arp.from(it.optJSONObject("arp0")), Arp.from(it.optJSONObject("arp1"))) }

    /**
     * Its settings as the package's, by the package's setting ids. Only what is there: a preset
     * without pads gives only the split, so the pads stay as they are. For a [preset] the look and
     * the lock stay as they are too, as they did.
     */
    fun flatten(o: JSONObject, preset: Boolean = false): JSONObject = JSONObject().apply {
        if (o.has("split")) put("split", if (o.optBoolean("split")) 1 else 0)
        if (o.has("splitAt")) put("splitAt", o.optDouble("splitAt", 0.5).coerceIn(0.2, 0.8))
        if (!o.has("left")) return@apply
        val l = PadSettings.from(o.optJSONObject("left"))
        val r = PadSettings.from(o.optJSONObject("right"), l.copy(shift = 0))
        val types = Chords.TYPES.keys.toList()
        fun type(t: String) = types.indexOf(t).coerceAtLeast(0)
        fun align(semis: Int) = Chords.ALIGN.indexOfFirst { it.first == semis }.coerceAtLeast(0)
        fun mod(m: String) = Chords.MODS.indexOf(m).coerceAtLeast(0)
        put("size", l.size); put("octave", l.octave); put("key", l.key); put("scale", l.scale)
        put("chordMode", l.chordMode)
        put("vertical", l.vertical); put("fade", l.fade); put("glide", l.glide.toDouble())
        put("vibrato", l.vibrato); put("vibSense", l.vibSense)
        if (!preset) { put("locked", if (l.locked) 1 else 0); put("theme", l.theme.coerceIn(0, 5)); put("lines", l.lines) }
        put("shift", l.shift)
        put("chordRoot", l.chordRoot); put("chordType", type(l.chordType)); put("chordMod", mod(l.chordMod))
        listOf("wheelOuter", "wheelMiddle", "wheelInner").forEachIndexed { i, id -> put(id, type(l.wheelRings.getOrElse(i) { "maj" })) }
        put("middleRoots", align(l.wheelAlign.getOrElse(1) { 9 })); put("innerRoots", align(l.wheelAlign.getOrElse(2) { 11 }))
        for (k in 0 until 7) put("gridRow${k + 1}", type(l.gridRows.getOrElse(k) { "maj" }))
        // The right pad: "Same as left" (0) where it is the same, else its own.
        put("rightNotes", if (r.chordMode == l.chordMode) 0 else r.chordMode + 1)
        put("rightSize", if (r.size == l.size) 0 else (r.size - 2).coerceIn(1, 22))
        put("rightOctave", (r.octave - l.octave).coerceIn(-4, 4))
        put("rightKey", if (r.key == l.key) 0 else r.key + 1)
        put("rightScale", if (r.scale == l.scale) 0 else r.scale + 1)
        put("shift2", r.shift)
        put("chordRoot2", r.chordRoot); put("chordType2", type(r.chordType)); put("chordMod2", mod(r.chordMod))
    }
}
