package com.patrickreynolds.mpeinstruments

import org.json.JSONObject
import kotlin.math.roundToInt

/**
 * The arpeggiator of one part: a modifier of the app's, so it works with every controller that
 * lets it (Controller.arpUse). Kept in the part, so it goes with the sound into presets. Mirrors mpe::ArpConfig.
 */
data class Arp(
    val on: Boolean = false,
    val order: Int = 0,            // ArpOrder: 0 up, 1 down, 2 up-down, 3 as played, 4 random, 5 chord
    val rate: Float = 4f,          // steps per beat
    val gate: Float = 0.6f,
    val octaves: Int = 1,
    val latch: Boolean = false,
) {
    fun json() = JSONObject().apply {
        put("arp", on); put("order", order); put("rate", rate.toDouble()); put("gate", gate.toDouble())
        put("octaves", octaves); put("latch", latch)
    }
    companion object {
        /** Also reads a part of before 0.16, which kept its arp in it. */
        fun from(o: JSONObject?, d: Arp = Arp()) = if (o == null) d else Arp(
            o.optBoolean("arp", d.on), o.optInt("order", d.order).coerceIn(ARP_ORDERS.indices),
            o.optDouble("rate", d.rate.toDouble()).toFloat(), o.optDouble("gate", d.gate.toDouble()).toFloat(),
            o.optInt("octaves", d.octaves).coerceIn(1, 4), o.optBoolean("latch", d.latch))
    }
}

/** One part, a sound and its arp: mirrors mpe::PartConfig. */
data class Part(
    val instrument: String = "strings/cello",
    val version: String = "",      // the instrument's version it plays ("1.0.0"); "": the newest
    val transpose: Int = 0,
    val volume: Float = 1f,
    val brightness: Float = 1f,
    val drive: Float = 0f,
    val pressureCurve: Float = 1f,
    val reverb: Float = 0.15f,
    val delay: Float = 0f,
    val release: Float = 0f,       // s the note plays on and fades after the finger lifts; 0 = the instrument's own
    val arp: Arp = Arp(),
) {
    fun json() = JSONObject().apply {
        put("instrument", instrument); if (version.isNotEmpty()) put("version", version); put("transpose", transpose)
        put("volume", volume.toDouble()); put("brightness", brightness.toDouble()); put("drive", drive.toDouble())
        put("pressureCurve", pressureCurve.toDouble()); put("reverb", reverb.toDouble()); put("delay", delay.toDouble())
        put("release", release.toDouble()); put("arpeggio", arp.json())
    }

    companion object {
        fun from(o: JSONObject?, d: Part = Part()): Part = if (o == null) d else Part(
            o.optString("instrument", d.instrument), o.optString("version", ""), o.optInt("transpose", d.transpose),
            o.optDouble("volume", d.volume.toDouble()).toFloat(), o.optDouble("brightness", d.brightness.toDouble()).toFloat(),
            o.optDouble("drive", d.drive.toDouble()).toFloat(), o.optDouble("pressureCurve", d.pressureCurve.toDouble()).toFloat(),
            o.optDouble("reverb", d.reverb.toDouble()).toFloat(), o.optDouble("delay", d.delay.toDouble()).toFloat(),
            o.optDouble("release", d.release.toDouble()).toFloat().coerceIn(0f, RELEASE_MAX),
            // Before 0.16 the arp was in the part itself; from 0.16 to 0.20 in the controller (Controller.legacyArps).
            o.optJSONObject("arpeggio")?.let { Arp.from(it) } ?: Arp.from(o))
    }
}

/**
 * What the controller in use sets in the engine, beside the sound: a split (two parts, each
 * finger told its part), whether the app's arps play, pitch assist, transpose and the pitch
 * bend range of the notes it sends.
 */
data class EngineShape(
    val split: Boolean = false,
    val parts: Int = 2,            // split: how many parts (sounds) it divides between, 2..PARTS
    val appArp: Boolean = true,    // the app's arps play (Controller.arpUse is App)
    val arpLatch: Boolean = true,  // an arp may latch (the grid latches notes itself, so not there)
    val scale: Int = 0,            // pitch assist: index into SCALES; 0 = off
    val scaleRoot: Int = 0,
    val assist: Float = 0.6f,
    val transpose: Int = 0,
    val bendRange: Float = 48f,
    val splitNote: Int = 60,       // a split's divide for notes that do not say their part
)

/** The most sounds at once: a split's sections, each its own part (mpe::kParts). */
const val PARTS = 4

/** The sound, which every controller plays: the parts, a second one layered or not, tempo and effects. */
data class Perf(
    val layered: Boolean = false,  // every note plays both parts (unless the controller splits them)
    val bpm: Float = 110f,
    val reverbSize: Float = 0.5f,
    val reverbDamp: Float = 0.4f,
    val delayBeats: Float = 0.75f,
    val delayFeedback: Float = 0.35f,
    val delayPingPong: Boolean = true,
    val parts: List<Part> = DEFAULT_PARTS,
) {
    /** Part [i]; a perf made with fewer (most presets give two) has the defaults for the rest. */
    fun part(i: Int) = parts.getOrElse(i) { DEFAULT_PARTS[i] }
    fun withPart(i: Int, f: (Part) -> Part) = copy(parts = List(PARTS) { k -> if (k == i) f(part(k)) else part(k) })

    /** The engine's layout: 0 one part, 1 split, 2 layered. */
    fun layout(shape: EngineShape) = if (shape.split) 1 else if (layered) 2 else 0
    /** How many parts sound: one, a split's, or two layered. */
    fun partCount(shape: EngineShape) = when (layout(shape)) { 1 -> shape.parts.coerceIn(2, PARTS); 2 -> 2; else -> 1 }

    /** The layout nativeSetPerformance (jni.cpp) reads. Keep the two in step. */
    fun toArray(shape: EngineShape): FloatArray {
        val a = FloatArray(11 + PARTS * 14)
        val layout = layout(shape)
        a[0] = layout.toFloat(); a[1] = shape.splitNote.toFloat(); a[2] = bpm
        a[3] = SCALES[shape.scale.coerceIn(SCALES.indices)].mask.toFloat(); a[4] = shape.scaleRoot.toFloat()
        a[5] = if (shape.scale == 0) 0f else shape.assist
        a[6] = reverbSize; a[7] = reverbDamp; a[8] = delayBeats; a[9] = delayFeedback; a[10] = if (delayPingPong) 1f else 0f
        for (k in 0 until PARTS) {
            val p = part(k)
            // Layered, every note plays both parts: both arpeggiate as the main one does.
            val r = when {
                !shape.appArp -> Arp()
                layout == 2 -> part(0).arp
                else -> p.arp
            }.let { if (shape.arpLatch) it else it.copy(latch = false) }
            val b = 11 + 14 * k
            a[b] = if (r.on) 1f else 0f; a[b + 1] = r.order.toFloat(); a[b + 2] = r.rate; a[b + 3] = r.gate
            a[b + 4] = r.octaves.toFloat(); a[b + 5] = if (r.latch) 1f else 0f; a[b + 6] = p.transpose.toFloat()
            a[b + 7] = p.volume; a[b + 8] = p.brightness; a[b + 9] = p.drive; a[b + 10] = p.pressureCurve
            a[b + 11] = p.reverb; a[b + 12] = p.delay; a[b + 13] = p.release
        }
        return a
    }

    fun json(): JSONObject = JSONObject().apply {
        put("layered", layered); put("bpm", bpm.toDouble()); put("reverbSize", reverbSize.toDouble())
        put("reverbDamp", reverbDamp.toDouble()); put("delayBeats", delayBeats.toDouble())
        put("delayFeedback", delayFeedback.toDouble()); put("delayPingPong", delayPingPong)
        put("v", 2)                // 2: the arps are in the parts (0.21)
        for (k in 0 until PARTS) put("part$k", part(k).json())
    }

    /** Each part's arp from [arps] (a controller's of before 0.21), the rest off. */
    fun withArps(arps: List<Arp>) = copy(parts = List(PARTS) { k -> part(k).copy(arp = arps.getOrElse(k) { Arp() }) })

    companion object {
        /** Saved before 0.21, when (from 0.16) the arps were the controller's: see Controller.legacyArps. */
        fun arpsInController(o: JSONObject?) = o != null && o.optInt("v", 1) < 2 && !o.has("layout")

        /** The sounds a part starts with: the main one, the layer (or right of a split), then a grid's third and fourth sections. */
        val DEFAULT_PARTS = listOf(Part(), Part(instrument = "voices/soprano"), Part(instrument = "plucked/bass-guitar"),
                                   Part(instrument = "keys/rhodes"))
        fun from(o: JSONObject?): Perf {
            val d = Perf()
            if (o == null) return d
            return Perf(
                // Before 0.16 the layout was 0 single, 1 split (now the touch pad's), 2 layered.
                if (o.has("layered")) o.optBoolean("layered") else o.optInt("layout", 0) == 2,
                o.optDouble("bpm", d.bpm.toDouble()).toFloat(),
                o.optDouble("reverbSize", d.reverbSize.toDouble()).toFloat(), o.optDouble("reverbDamp", d.reverbDamp.toDouble()).toFloat(),
                o.optDouble("delayBeats", d.delayBeats.toDouble()).toFloat(), o.optDouble("delayFeedback", d.delayFeedback.toDouble()).toFloat(),
                o.optBoolean("delayPingPong", d.delayPingPong),
                List(PARTS) { k -> Part.from(o.optJSONObject("part$k"), d.parts[k]) })
        }
    }
}

/**
 * One on-screen pad: columns of notes left to right, up and down for loudness and tone. With the
 * performance split, the screen has two, left and right, each with its own settings; theme,
 * lines and locked belong to the screen as a whole and are kept in the left one.
 */
data class PadSettings(
    val size: Int = 8,             // columns (notes) across the screen
    val octave: Int = 0,           // 0 = the middle of the instrument's range
    val key: Int = 0,              // 0 = C
    val scale: Int = 1,            // index into SCALES; 0 (Off) plays every semitone
    val vertical: Int = 0,         // up/down: 0 loudness and tone, 1 tone, 2 loudness
    val glide: Float = 0f,         // 0 = snap: each column plays its note, straight from one to the next;
                                   // 1 = free: pitch follows the finger exactly; between, glides near the column edges
    val shift: Int = 0,            // columns moved by dragging the note strip (scale steps)
    val locked: Boolean = false,   // the note strip plays notes instead of moving them
    val theme: Int = 0,            // index into PAD_THEMES (the e-ink look overrides it)
    val lines: Int = 1,            // how strongly the columns are drawn: 0 soft, 1 clear, 2 bold
    val vibrato: Int = 2,          // a small wiggle of the finger made bigger: index into VIBRATO
    val vibSense: Int = 1,         // how small a wiggle makes the full vibrato: index into VIB_SENSE
    val fade: Int = 2,             // how much of the bottom of the pad fades to silence: index into FADE
    // Chord modes: instead of a scale, the pad holds only the notes of one chord, picked on a chord
    // wheel (circle of fifths) or a chord grid that takes the left part of the pad.
    val chordMode: Int = 0,        // 0 a scale, 1 the chord wheel, 2 the chord grid
    val chordRoot: Int = 0,        // the chord in hand: its root (0 = C) ..
    val chordType: String = "maj", // .. and type (Chords.TYPES)
    val chordMod: String = "",     // a quick change on top (Chords.MODS), from the wheel
    val wheelRings: List<String> = listOf("maj", "min", "dim"),   // outer, middle, inner ring: each ring's chord type
    val wheelAlign: List<Int> = listOf(0, 9, 11),                 // each ring's roots, in semitones from the outer ring's
    val gridRows: List<String> = listOf("maj", "min", "7", "maj7", "m7", "sus4", "dim"),
) {
    /** The chord the pad plays now. */
    val chord get() = Chords.modify(chordType, chordMod)
    fun json() = JSONObject().apply {
        put("size", size); put("octave", octave); put("key", key); put("scale", scale)
        put("vertical", vertical); put("glide", glide.toDouble()); put("shift", shift); put("locked", locked)
        put("theme", theme); put("lines", lines); put("vibrato", vibrato)
        put("vibSense", vibSense); put("fade", fade)
        put("chordMode", chordMode); put("chordRoot", chordRoot); put("chordType", chordType); put("chordMod", chordMod)
        put("wheelRings", org.json.JSONArray(wheelRings)); put("wheelAlign", org.json.JSONArray(wheelAlign))
        put("gridRows", org.json.JSONArray(gridRows))
    }

    companion object {
        val VERTICAL = listOf("Loudness + tone", "Tone", "Loudness")
        val LINES = listOf("Soft", "Clear", "Bold")
        fun glideName(g: Float) = when { g <= 0.005f -> "Snap"; g >= 0.995f -> "Free"; else -> "${(g * 100).roundToInt()}%" }
        val VIBRATO = listOf("Off", "Light", "Normal", "Wide")
        /** Widest vibrato (semitones each way) of each VIBRATO setting. */
        val VIBRATO_DEPTH = listOf(0f, 0.25f, 0.5f, 1f)
        val VIB_SENSE = listOf("Low", "Medium", "High", "Very high")
        /** Vibrato per dp of wiggle, times the Medium amount. */
        val VIB_SENSE_GAIN = listOf(0.5f, 1f, 2f, 4f)
        val FADE = listOf("None", "Short", "Medium", "Long", "Whole pad")
        /** Fraction of the pad's height, from the bottom, over which a note fades to silence. */
        val FADE_HEIGHT = listOf(0f, 0.1f, 0.25f, 0.5f, 1f)
        fun from(o: JSONObject?, d: PadSettings = PadSettings()) = if (o == null) d else PadSettings(
            o.optInt("size", d.size).coerceIn(3, 24), o.optInt("octave", d.octave).coerceIn(-4, 4),
            o.optInt("key", d.key).coerceIn(0, 11), o.optInt("scale", d.scale).coerceIn(SCALES.indices),
            o.optInt("vertical", d.vertical).coerceIn(0, 2),
            // Before 0.9 glide was Snap or Free.
            (if (o.has("glide")) o.optDouble("glide", 0.0).toFloat() else if (o.optBoolean("snap", true)) 0f else 1f).coerceIn(0f, 1f),
            o.optInt("shift", d.shift).coerceIn(-128, 128), o.optBoolean("locked", d.locked),
            o.optInt("theme", d.theme).coerceIn(0, 9), o.optInt("lines", d.lines).coerceIn(0, 2),
            o.optInt("vibrato", d.vibrato).coerceIn(VIBRATO.indices),
            o.optInt("vibSense", d.vibSense).coerceIn(VIB_SENSE.indices), o.optInt("fade", d.fade).coerceIn(FADE.indices),
            o.optInt("chordMode", d.chordMode).coerceIn(0, 2), o.optInt("chordRoot", d.chordRoot).coerceIn(0, 11),
            o.optString("chordType", d.chordType).takeIf { it in Chords.TYPES } ?: d.chordType,
            o.optString("chordMod", d.chordMod).takeIf { it in Chords.MODS } ?: "",
            strings(o.optJSONArray("wheelRings"), d.wheelRings), ints(o.optJSONArray("wheelAlign"), d.wheelAlign),
            strings(o.optJSONArray("gridRows"), d.gridRows))
        private fun strings(a: org.json.JSONArray?, d: List<String>) =
            if (a == null || a.length() != d.size) d else List(d.size) { k -> a.optString(k).takeIf { it in Chords.TYPES } ?: d[k] }
        private fun ints(a: org.json.JSONArray?, d: List<Int>) =
            if (a == null || a.length() != d.size) d else List(d.size) { k -> ((a.optInt(k, d[k]) % 12) + 12) % 12 }
    }
}

data class Scale(val name: String, val mask: Int)

/** Bit n = the note n semitones above the root is in the scale. */
val SCALES = listOf(
    Scale("Off", 0),
    Scale("Major", 0b101010110101),
    Scale("Minor", 0b010110101101),
    Scale("Dorian", 0b011010101101),
    Scale("Mixolydian", 0b011010110101),
    Scale("Harmonic minor", 0b100110101101),
    Scale("Pentatonic major", 0b001010010101),
    Scale("Pentatonic minor", 0b010010101001),
    Scale("Blues", 0b010011101001),
    Scale("Chromatic", 0b111111111111),
)

val NOTE_NAMES = listOf("C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B")
fun noteName(n: Int) = NOTE_NAMES[((n % 12) + 12) % 12] + (n / 12 - 1)

/** The longest release, seconds. */
const val RELEASE_MAX = 8f
fun releaseName(s: Float) = when { s < 0.01f -> "Off"; s < 1f -> "%.2f s".format(s); else -> "%.1f s".format(s) }

val ARP_ORDERS = listOf("Up", "Down", "Up-down", "As played", "Random", "Chord")
/** Arp rates as (label, steps per beat). */
val ARP_RATES = listOf("1/4" to 1f, "1/8" to 2f, "1/8T" to 3f, "1/16" to 4f, "1/16T" to 6f, "1/32" to 8f)
/** Delay times as (label, beats). */
val DELAY_TIMES = listOf("1/16" to 0.25f, "1/8" to 0.5f, "1/8." to 0.75f, "1/4" to 1f, "1/4." to 1.5f, "1/2" to 2f)
