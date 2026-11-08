package com.patrickreynolds.mpeinstruments.packs

import android.content.Context
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.Part
import org.json.JSONObject

/**
 * An instrument pack: instruments, the groups they are shown in, and the effects each is meant
 * to be heard with. Described by a manifest (assets/packs/<id>.json); the sounds themselves are
 * the engine's (built into it from faust-mpe-instruments). A downloaded pack will bring its
 * manifest and its sounds the same way.
 */
data class InstrumentPack(
    val id: String,
    val name: String,
    val description: String,
    val source: String,
    val defaults: SoundDefaults,
    val groups: List<Group>,
    val overrides: Map<String, SoundDefaults> = emptyMap(),
) {
    data class Group(val id: String, val name: String, val instruments: List<Native.Instrument>)

    val instruments get() = groups.flatMap { it.instruments }

    /** The effects instrument [id] is meant to be heard with. */
    fun defaultsFor(id: String) = overrides[id] ?: defaults
}

/** An instrument's effects, as it says it is meant to be heard (a pack can give the ones it leaves out): its part's sends, tone and touch, and the room it plays in. */
data class SoundDefaults(
    val reverb: Float = 0.15f,
    val reverbSize: Float = 0.5f,
    val reverbDamp: Float = 0.4f,
    val delay: Float = 0f,
    val brightness: Float = 1f,
    val drive: Float = 0f,
    val pressureCurve: Float = 1f,
    val release: Float = 0f,
) {
    /** A part playing [instrument] (its newest version, if it is another instrument), with these effects. */
    fun applyTo(p: Part, instrument: String = p.instrument) = p.copy(instrument = instrument,
        version = if (instrument == p.instrument) p.version else "", reverb = reverb, delay = delay,
        brightness = brightness, drive = drive, pressureCurve = pressureCurve, release = release)

    companion object {
        fun from(o: JSONObject?, d: SoundDefaults = SoundDefaults()) = if (o == null) d else SoundDefaults(
            f(o, "reverb", d.reverb), f(o, "reverbSize", d.reverbSize), f(o, "reverbDamp", d.reverbDamp), f(o, "delay", d.delay),
            f(o, "brightness", d.brightness), f(o, "drive", d.drive), f(o, "pressureCurve", d.pressureCurve), f(o, "release", d.release))
        /** An instrument's own `declare sound "reverb=0.3 delay=0 ..."`; what it leaves out comes from [d]. */
        fun declared(text: String, d: SoundDefaults = SoundDefaults()): SoundDefaults {
            val m = text.trim().split(Regex("\\s+")).mapNotNull { it.split("=").takeIf { p -> p.size == 2 } }
                .mapNotNull { p -> p[1].toFloatOrNull()?.let { p[0] to it } }.toMap()
            return SoundDefaults(m["reverb"] ?: d.reverb, m["reverbSize"] ?: d.reverbSize, m["reverbDamp"] ?: d.reverbDamp,
                m["delay"] ?: d.delay, m["brightness"] ?: d.brightness, m["drive"] ?: d.drive,
                m["pressureCurve"] ?: d.pressureCurve, m["release"] ?: d.release)
        }
        private fun f(o: JSONObject, k: String, d: Float) = o.optDouble(k, d.toDouble()).toFloat()
    }
}

/** The instrument packs there are: every manifest in assets/packs/, in the order they ask for. */
object Packs {
    var all: List<InstrumentPack> = emptyList()
        private set

    /** The effects each instrument says it is meant to be heard with (its `declare sound`). */
    private var own: Map<String, String> = emptyMap()

    /**
     * Read the manifests and match them to the engine's instruments. A pack holds the instruments
     * its manifest lists (each in one pack only: the first to list it). An instrument the engine
     * has that no pack lists (a new one in faust-mpe-instruments) goes to a "More sounds" pack,
     * in the group of its folder, so none is ever left out.
     */
    fun load(context: Context, instruments: List<Native.Instrument>) {
        val manifests = (runCatching { context.assets.list("packs") }.getOrNull() ?: emptyArray())
            .filter { it.endsWith(".json") }
            .mapNotNull { f -> runCatching { JSONObject(context.assets.open("packs/$f").bufferedReader().use { it.readText() }) }.getOrNull() }
            .sortedWith(compareBy({ it.optInt("order", 1000) }, { it.optString("name") }))
        own = instruments.filter { it.sound.isNotBlank() }.associate { it.id to it.sound }
        val listed = mutableSetOf<String>()
        val packs = manifests.map { parse(it, instruments, listed) }.filter { it.instruments.isNotEmpty() }.toMutableList()
        val rest = instruments.filter { it.id !in listed }
        if (rest.isNotEmpty()) packs += InstrumentPack("more-sounds", "More sounds", "Instruments no pack lists.", "", SoundDefaults(),
            rest.groupBy { it.group }.map { (g, l) -> InstrumentPack.Group(g, g.replaceFirstChar { it.uppercase() }, l) })
        all = packs
    }

    fun packOf(id: String) = all.firstOrNull { p -> p.instruments.any { it.id == id } }
    fun groupOf(id: String) = all.firstNotNullOfOrNull { p -> p.groups.firstOrNull { g -> g.instruments.any { it.id == id } } }
    /** The instrument's own effects; what it leaves out, and instruments that declare none, get the pack's. */
    fun defaultsFor(id: String): SoundDefaults {
        val pack = packOf(id)?.defaultsFor(id) ?: SoundDefaults()
        return own[id]?.let { SoundDefaults.declared(it, pack) } ?: pack
    }

    /** One manifest: its groups, with the instruments the engine has that no earlier pack took ([listed] is updated). */
    private fun parse(o: JSONObject, instruments: List<Native.Instrument>, listed: MutableSet<String>): InstrumentPack {
        val byId = instruments.associateBy { it.id }
        val groups = mutableListOf<InstrumentPack.Group>()
        val ga = o.optJSONArray("groups")
        for (k in 0 until (ga?.length() ?: 0)) {
            val g = ga!!.getJSONObject(k)
            val ids = g.optJSONArray("instruments")
            val list = (0 until (ids?.length() ?: 0)).mapNotNull { byId[ids!!.getString(it)] }.filter { listed.add(it.id) }
            groups += InstrumentPack.Group(g.getString("id"), g.optString("name", g.getString("id")), list)
        }
        val defaults = SoundDefaults.from(o.optJSONObject("defaults"))
        val ov = o.optJSONObject("overrides")
        val overrides = ov?.keys()?.asSequence()?.associateWith { SoundDefaults.from(ov.getJSONObject(it), defaults) } ?: emptyMap()
        return InstrumentPack(o.optString("id"), o.optString("name", o.optString("id")), o.optString("description", ""),
                              o.optString("source", ""), defaults, groups.filter { it.instruments.isNotEmpty() }, overrides)
    }
}
