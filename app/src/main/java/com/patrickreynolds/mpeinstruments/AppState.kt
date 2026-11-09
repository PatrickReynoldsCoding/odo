package com.patrickreynolds.mpeinstruments

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.patrickreynolds.mpeinstruments.controllers.ArpUse
import com.patrickreynolds.mpeinstruments.controllers.Controller
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.ExternalMidi
import com.patrickreynolds.mpeinstruments.controllers.LegacyTouchPad
import com.patrickreynolds.mpeinstruments.ui.DarkPalette
import com.patrickreynolds.mpeinstruments.ui.EinkPalette
import com.patrickreynolds.mpeinstruments.ui.palette
import org.json.JSONArray
import org.json.JSONObject

/**
 * A preset: the sound (parts and their arps, layer, tempo, effects), the controller it is played
 * with and that controller's settings (the touch pad's notes and split, say). [controller] null (some
 * built-in presets): any controller, the one in use, with what is per preset turned off.
 * [settings] is kept as JSON text, so two presets compare by value.
 */
data class Preset(val name: String, val perf: Perf, val builtIn: Boolean = false,
                  val controller: String? = LegacyTouchPad.ID, val settings: String? = null) {
    fun json(): JSONObject = JSONObject().put("name", name).put("perf", perf.json()).apply {
        controller?.let { put("controller", it) }
        settings?.let { put("settings", JSONObject(it)) }
    }
    companion object {
        fun from(o: JSONObject): Preset {
            val perf = o.getJSONObject("perf")
            if (o.has("controller")) {
                val settings = o.optJSONObject("settings")
                var p = Perf.from(perf)
                // Saved by 0.16 to 0.20: the arps were the controller's.
                if (Perf.arpsInController(perf)) Controllers.byId(o.getString("controller")).legacyArps(settings)?.let { p = p.withArps(it) }
                return Preset(o.getString("name"), p, controller = o.getString("controller"), settings = settings?.toString())
            }
            // Before 0.16: a touch pad preset, the pads (if it had them) beside the performance.
            val pad = o.optJSONObject("pad")
            val s = LegacyTouchPad.legacy(pad?.optJSONObject("left"), pad?.optJSONObject("right"),
                                    pad?.optDouble("splitAt", 0.5)?.toFloat(), perf)
            return Preset(o.getString("name"), Perf.from(perf), settings = s.toString())
        }
    }
}

/**
 * The app around the controller: what the engine plays and the screen shows. Every change goes
 * to the engine at once and is saved, so the app (and the background service) start where they
 * left off.
 */
object AppState {
    private lateinit var prefs: SharedPreferences
    private lateinit var appContext: Context
    private var started = false

    /** The sounds to pick from: each instrument's newest version. */
    var instruments: List<Native.Instrument> = emptyList()
        private set
    /** Every version of every instrument: older ones are kept, to go back to (the sound picker's Version). */
    var allInstruments: List<Native.Instrument> = emptyList()
        private set
    var perf by mutableStateOf(Perf())
        private set
    var volume by mutableStateOf(0.8f)
        private set
    var presetName by mutableStateOf("")
        private set
    var userPresets by mutableStateOf<List<Preset>>(emptyList())
        private set

    /** The controller in use: its options are on the rail and its surface is what you play. */
    var controller by mutableStateOf<Controller>(ExternalMidi)
        private set
    /** What the controller sets in the engine beside the sound; the app's arps play if it lets them. */
    val shape get() = controller.shape.let { it.copy(appArp = it.appArp && controller.arpUse == ArpUse.App) }
    /** How many parts sound: 1, a split's sections (2 to 4), or 2 layered. */
    val partCount get() = perf.partCount(shape)
    /** More than one part sounds: split by the controller, or layered. */
    val twoParts get() = partCount > 1
    /** Which part (sound) the rail is changing: 0, or another (a section of a split, the layer). */
    var side by mutableStateOf(0)
    val part get() = side.coerceIn(0, partCount - 1)
    /** The rail tucked away, the whole screen to play on. */
    var railHidden by mutableStateOf(false)
        private set

    /** The built-in preset on view was changed since it was loaded (a built-in is not changed itself). */
    var presetEdited by mutableStateOf(false)
        private set
    /** The loop buttons on the play screen, and whether a loop's length snaps to beats. */
    var loopDock by mutableStateOf(true)
        private set
    var loopSnap by mutableStateOf(true)
        private set
    /** Each loop's layers' volume and mute (the loops themselves are not kept). */
    var loopLayers by mutableStateOf(List(Looper.LOOPS) { List(Looper.MAX_LAYERS) { 1f to false } })
        private set

    /** Gyro and breath: the phone itself as a controller, beside any; not part of a preset. */
    var mods by mutableStateOf(PhoneMods())
        private set

    /** E-ink look (black on white, no motion); not part of a preset. */
    var eink by mutableStateOf(false)
        private set

    fun init(context: Context) {
        if (started) return
        started = true
        appContext = context.applicationContext
        prefs = appContext.getSharedPreferences("mpe", Context.MODE_PRIVATE)
        allInstruments = Native.instruments()
        instruments = allInstruments.filter { it.newest }
        com.patrickreynolds.mpeinstruments.packs.Packs.load(appContext, instruments)
        Takes.init(appContext)
        val perfJson = runCatching { prefs.getString("perf", null)?.let { JSONObject(it) } }.getOrNull()
        perf = runCatching { Perf.from(perfJson) }.getOrDefault(Perf())
        // Before presets existed the instrument was saved as an index.
        if (!prefs.contains("perf") && prefs.contains("instrument"))
            allInstruments.getOrNull(prefs.getInt("instrument", -1))?.let { inst -> perf = perf.withPart(0) { it.copy(instrument = inst.id) } }
        volume = prefs.getFloat("volume", 0.8f)
        presetName = prefs.getString("preset", "") ?: ""
        eink = prefs.getBoolean("eink", false)
        palette = if (eink) EinkPalette else DarkPalette
        mods = runCatching { PhoneMods.from(prefs.getString("mods", null)?.let { JSONObject(it) }) }.getOrDefault(PhoneMods())
        loopDock = prefs.getBoolean("loopDock", true)
        loopSnap = prefs.getBoolean("loopSnap", true)
        if (!prefs.contains("ctl.${LegacyTouchPad.ID}")) migrateToControllers(perfJson)
        Controllers.packaged = com.patrickreynolds.mpeinstruments.controllers.PackageController.builtIn(appContext)
        for (c in Controllers.all) runCatching {
            val saved = prefs.getString("ctl.${c.id}", null)
                ?: Controllers.formerIds(c.id).firstNotNullOfOrNull { prefs.getString("ctl.$it", null) }
            c.load(saved?.let { JSONObject(it) })
        }
        controller = Controllers.byId(prefs.getString("controller", LegacyTouchPad.ID))
        // From 0.16 to 0.20 the arps were each controller's: the one in use brings its own into the sound.
        if (Perf.arpsInController(perfJson)) {
            val saved = runCatching { prefs.getString("ctl.${controller.id}", null)?.let { JSONObject(it) } }.getOrNull()
            controller.legacyArps(saved)?.let { perf = perf.withArps(it) }
            prefs.edit().putString("perf", perf.json().toString()).apply()
        }
        railHidden = prefs.getBoolean("railHidden", false)
        userPresets = runCatching {
            val a = JSONArray(prefs.getString("presets", "[]"))
            (0 until a.length()).map { Preset.from(a.getJSONObject(it)) }
        }.getOrDefault(emptyList())
        if (!prefs.getBoolean("presetsStarted", false)) startPresets()
        // Until 0.15.1 pitch assist dragged notes outside its scale out of tune: turn it off once.
        if (!prefs.getBoolean("assistOff", false)) {
            ExternalMidi.update { it.copy(scale = 0) }
            if (presetName == "Violin, in tune") presetName = ""
            prefs.edit().putBoolean("assistOff", true).apply()
        }
        applyAll()
    }

    /**
     * Before 0.16 the touch pad's settings and the Seaboard's were the app's: the pads, the split
     * and the arps go to the touch pad; the bend range, transpose and pitch assist to External MIDI.
     */
    private fun migrateToControllers(perfJson: JSONObject?) {
        fun json(key: String) = runCatching { prefs.getString(key, null)?.let { JSONObject(it) } }.getOrNull()
        val left = json("pad")
        val pad = LegacyTouchPad.legacy(left, json("pad2"), prefs.getFloat("padSplitAt", 0.5f), perfJson)
        val midi = ExternalMidi.legacy(prefs.getFloat("bend", 48f), prefs.getInt("transpose", 0), perfJson)
        prefs.edit().putString("ctl.${LegacyTouchPad.ID}", pad.toString()).putString("ctl.${ExternalMidi.id}", midi.toString())
            .putBoolean("railHidden", left?.optBoolean("hideRail", false) ?: false)
            .putString("controller", LegacyTouchPad.ID).apply()
    }

    /** Send everything to the engine (at start, and after the audio engine was rebuilt). */
    fun applyAll() {
        Native.nativeSetVolume(volume)
        built.fill(false)
        applyEngine(null)
        PhoneControl.apply(appContext, mods)
        Looper.snap(loopSnap)
    }

    /** Instrument [id] at [version] ("": the newest; one not here: the newest too), else the cello. */
    private fun find(id: String, version: String = ""): Native.Instrument? =
        (if (version.isEmpty()) null else allInstruments.firstOrNull { it.id == id && it.version == version })
            ?: instruments.firstOrNull { it.id == id } ?: instruments.firstOrNull { it.id == "strings/cello" } ?: instruments.firstOrNull()

    private fun index(p: Part) = find(p.instrument, p.version)?.index ?: 0

    fun instrument(part: Int) = find(perf.part(part).instrument, perf.part(part).version)

    /** Part [k]'s instrument's versions, newest first (one: it has no others). */
    fun versions(k: Int): List<Native.Instrument> {
        val id = perf.part(k).instrument
        return allInstruments.filter { it.id == id }.sortedWith { a, b -> -Native.compareVersions(a.version, b.version) }
    }

    /** Play [version] of part [k]'s instrument ("" or the newest's: the newest). Its effects stay as they are. */
    fun chooseVersion(k: Int, version: String) {
        val newest = versions(k).firstOrNull()?.version ?: ""
        updatePart(k) { it.copy(version = if (version == newest) "" else version) }
    }

    /** Parts after the first are only built when a split or layer needs them: 15 more copies of an instrument each. */
    private val built = BooleanArray(PARTS)

    /** The sound and the controller's shape to the engine; instruments only where they changed. */
    private fun applyEngine(old: Perf?) {
        val s = shape
        Native.nativeSetTranspose(s.transpose)
        Native.nativeSetBendRange(s.bendRange)
        fun changed(k: Int) = old == null || old.part(k).instrument != perf.part(k).instrument || old.part(k).version != perf.part(k).version
        if (changed(0)) Native.nativeSetPartInstrument(0, index(perf.part(0)))
        for (k in 1 until partCount)
            if (!built[k] || changed(k)) {
                Native.nativeSetPartInstrument(k, index(perf.part(k)))
                built[k] = true
            }
        Native.nativeSetPerformance(perf.toArray(s))
    }

    fun update(change: (Perf) -> Perf) {
        val old = perf
        perf = change(perf)
        if (perf == old) return
        applyEngine(old)
        touched()
        save()
    }

    fun updatePart(i: Int, change: (Part) -> Part) = update { it.withPart(i, change) }

    /**
     * Play instrument [id] on part [k], with the effects its pack means it to be heard with (the
     * room too, unless the other part is sounding and keeps it).
     */
    fun chooseInstrument(k: Int, id: String, keepVersion: Boolean = false) {
        val d = com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(id)
        update { p ->
            val q = p.withPart(k) { d.applyTo(it, id).let { r -> if (keepVersion) r else r.copy(version = "") } }   // a sound picked: its newest
            if (twoParts && k >= 1) q else q.copy(reverbSize = d.reverbSize, reverbDamp = d.reverbDamp)
        }
    }

    /** Part [k]'s effects back to its pack's, for its instrument. */
    fun resetEffects(k: Int) = chooseInstrument(k, perf.part(k).instrument, keepVersion = true)

    fun changeVolume(v: Float) { volume = v; Native.nativeSetVolume(v); prefs.edit().putFloat("volume", v).apply() }

    fun changeEink(on: Boolean) {
        eink = on
        palette = if (on) EinkPalette else DarkPalette
        prefs.edit().putBoolean("eink", on).apply()
    }

    fun changeRailHidden(on: Boolean) { railHidden = on; prefs.edit().putBoolean("railHidden", on).apply() }

    fun updateMods(change: (PhoneMods) -> PhoneMods) {
        val old = mods
        mods = change(mods)
        if (mods == old) return
        PhoneControl.apply(appContext, mods)
        // Breath with the screen off: the service has to say it uses the microphone.
        if ((mods.breath != 0) != (old.breath != 0)) SynthService.refresh(appContext)
        saver.removeCallbacks(saveMods)
        saver.postDelayed(saveMods, 400)
    }
    private val saveMods = Runnable { prefs.edit().putString("mods", mods.json().toString()).apply() }

    // ---- controllers --------------------------------------------------------------------------

    /**
     * A controller's settings changed: save them, keep them in the preset on view if it is in use,
     * and if [engine], what it sets in the engine changed too.
     */
    fun controllerChanged(c: Controller, engine: Boolean = true) {
        if (!started) return
        if (c === controller && engine && !loading) applyEngine(perf)
        pendingSave += c
        saver.removeCallbacks(saveControllers)
        saver.postDelayed(saveControllers, 400)
        if (c === controller) touched()
    }
    private val pendingSave = mutableSetOf<Controller>()
    private val saveControllers = Runnable {
        val e = prefs.edit()
        for (c in pendingSave) e.putString("ctl.${c.id}", c.save().toString())
        pendingSave.clear()
        e.apply()
    }

    /** Play with another controller: the sound stays; its own settings come back as you left them. */
    fun switchController(c: Controller) {
        if (c === controller) return
        controller.stop()
        controller = c
        side = 0
        prefs.edit().putString("controller", c.id).apply()
        applyEngine(perf)
        touched()
    }

    // ---- the loops ---------------------------------------------------------------------------

    fun changeLoopDock(on: Boolean) { loopDock = on; prefs.edit().putBoolean("loopDock", on).apply() }
    fun changeLoopSnap(on: Boolean) { loopSnap = on; Looper.snap(on); prefs.edit().putBoolean("loopSnap", on).apply() }
    fun changeLoopLayer(t: Int, i: Int, volume: Float, muted: Boolean) {
        loopLayers = loopLayers.mapIndexed { k, ls -> if (k == t) ls.mapIndexed { j, l -> if (j == i) volume to muted else l } else ls }
        Looper.layer(t, i, volume, muted)
    }
    /** After a clear or an undo a loop's layers start again at full volume, unmuted. */
    fun resetLoopLayers(t: Int, from: Int = 0) {
        loopLayers = loopLayers.mapIndexed { k, ls -> if (k == t) ls.mapIndexed { j, l -> if (j >= from) 1f to false else l } else ls }
        for (j in from until Looper.MAX_LAYERS) Looper.layer(t, j, 1f, false)
    }

    // ---- presets ----------------------------------------------------------------------------

    val allPresets get() = BUILT_IN_PRESETS + userPresets
    /** The preset on view, if it is one of yours: what you change is kept in it. */
    val activePreset get() = userPresets.firstOrNull { it.name == presetName }
    private var loading = false

    /** What is playing now, as a preset of yours. */
    private fun current(name: String) = Preset(name, perf, controller = controller.id, settings = controller.save().toString())

    /** Something changed: keep it in your preset on view, or note that a built-in one was changed. */
    private fun touched() {
        if (loading || presetName.isEmpty()) return
        val p = activePreset
        if (p == null) { presetEdited = true; return }
        val now = current(p.name)
        if (now == p) return
        userPresets = userPresets.map { if (it.name == p.name) now else it }
        saver.removeCallbacks(savePresetsNow)
        saver.postDelayed(savePresetsNow, 400)
    }

    fun load(p: Preset) {
        loading = true
        val c = p.controller?.let { Controllers.byId(it) } ?: controller
        if (c !== controller) { controller.stop(); controller = c; side = 0; prefs.edit().putString("controller", c.id).apply() }
        val s = p.settings?.let { runCatching { JSONObject(it) }.getOrNull() }
        if (s != null) c.loadPreset(s) else c.clearForPreset()
        val old = perf
        perf = p.perf
        applyEngine(old)
        save()
        presetName = p.name
        presetEdited = false
        prefs.edit().putString("preset", presetName).apply()
        controllerChanged(c, engine = false)        // saved as the controller's; loading, so not into a preset
        loading = false
    }

    /** A name not yet taken: "Cello", then "Cello 2", "Cello 3"... */
    fun freeName(base: String, except: String? = null): String {
        val b = base.trim().ifEmpty { "Preset" }
        val taken = allPresets.map { it.name }.filter { it != except }.toSet()
        if (b !in taken) return b
        val stem = b.replace(Regex(" \\d+$"), "")
        var n = 2
        while ("$stem $n" in taken) n++
        return "$stem $n"
    }

    /** What is playing now, as a name: the instrument, or both of a split or layer. */
    fun soundName(p: Perf = perf, count: Int = partCount): String {
        fun n(i: Int) = instruments.firstOrNull { it.id == p.part(i).instrument }?.name ?: "Preset"
        return (0 until count).joinToString(" + ") { n(it) }
    }

    /** Keep what is playing now as a new preset of yours (after the one on view), and switch to it. */
    fun newPreset(name: String = soundName()): String {
        val clean = freeName(name)
        val fresh = current(clean)
        val at = userPresets.indexOfFirst { it.name == presetName }
        userPresets = if (at < 0) userPresets + fresh else userPresets.take(at + 1) + fresh + userPresets.drop(at + 1)
        presetName = clean
        presetEdited = false
        savePresets()
        return clean
    }

    fun saveAs(name: String) {
        val clean = name.trim().ifEmpty { return }
        val fresh = current(clean)
        userPresets = if (userPresets.any { it.name == clean }) userPresets.map { if (it.name == clean) fresh else it } else userPresets + fresh
        presetName = clean
        presetEdited = false
        savePresets()
    }

    fun rename(p: Preset, name: String) {
        if (p.builtIn) return
        val clean = freeName(name, except = p.name)
        userPresets = userPresets.map { if (it.name == p.name) it.copy(name = clean) else it }
        if (presetName == p.name) presetName = clean
        savePresets()
    }

    /** A copy of yours: a built-in one gets the rest of its controller's settings from how they are now. */
    fun duplicate(p: Preset) {
        val c = p.controller?.let { Controllers.byId(it) } ?: controller
        val full = c.save()
        // A package keeps its settings "byId": a preset's own (the Kotlin touch pad's format, say) go in there.
        val byId = full.optJSONObject("byId")
        p.settings?.let { s ->
            val o = JSONObject(s)
            val src = if (byId != null && LegacyTouchPad.isLegacy(o)) LegacyTouchPad.flatten(o, preset = true) else o
            val into = if (byId != null && !src.has("byId")) byId else full
            for (k in src.keys()) into.put(k, src.get(k))
        }
        if (p.settings == null && c.id == LegacyTouchPad.PACKAGE) (byId ?: full).put("split", 0)
        val copy = p.copy(name = freeName(p.name), builtIn = false, controller = c.id, settings = full.toString())
        val at = userPresets.indexOfFirst { it.name == p.name }
        userPresets = if (at < 0) userPresets + copy else userPresets.take(at + 1) + copy + userPresets.drop(at + 1)
        savePresets()
    }

    /** Move one of your presets one place earlier (-1) or later (+1). */
    fun move(p: Preset, by: Int) {
        val i = userPresets.indexOfFirst { it.name == p.name }
        val j = i + by
        if (i < 0 || j !in userPresets.indices) return
        userPresets = userPresets.toMutableList().apply { add(j, removeAt(i)) }
        savePresets()
    }

    fun delete(p: Preset) {
        if (p.builtIn) return
        userPresets = userPresets.filter { it.name != p.name }
        if (presetName == p.name) presetName = ""
        savePresets()
    }

    // ---- preset files (docs/formats/PRESET_FORMAT.md), the same as odo for computers' -----------------------

    /**
     * What Share sends: the preset on view as it plays now; a built-in one for any controller as it
     * is (its sound alone); with none on view, what plays now named for its sound.
     */
    fun presetToShare(): Preset {
        val on = allPresets.firstOrNull { it.name == presetName }
        return if (on != null && on.builtIn && on.controller == null && !presetEdited) on
               else current(presetName.ifEmpty { soundName() })
    }

    /** [p] as a preset file's text (mpe::preset writes it). */
    fun presetFileText(p: Preset): String {
        // A Float as the number it reads as ("0.6", not 0.6000000238418579, its exact double).
        fun d(f: Float) = f.toString().toDouble()
        val pf = p.perf
        val s = p.settings?.let { runCatching { JSONObject(it) }.getOrNull() }
        val settings = s?.optJSONObject("byId") ?: s ?: JSONObject()
        val parts = JSONArray()
        for (part in pf.parts) parts.put(JSONObject().put("sound", part.instrument)
            .put("version", find(part.instrument, part.version)?.version ?: "").put("transpose", part.transpose)
            .put("volume", d(part.volume)).put("brightness", d(part.brightness)).put("drive", d(part.drive))
            .put("pressureCurve", d(part.pressureCurve)).put("reverb", d(part.reverb))
            .put("delay", d(part.delay)).put("release", d(part.release))
            .put("arp", JSONObject().put("on", part.arp.on).put("order", part.arp.order).put("rate", d(part.arp.rate))
                .put("gate", d(part.arp.gate)).put("octaves", part.arp.octaves).put("latch", part.arp.latch)))
        val o = JSONObject().put("format", "odo-preset/1").put("name", p.name)
            .put("madeBy", "odo ${BuildConfig.VERSION_NAME} (Android)").put("split", 0).put("layered", pf.layered)
            .put("effects", JSONObject().put("bpm", d(pf.bpm)).put("reverbSize", d(pf.reverbSize))
                .put("reverbDamp", d(pf.reverbDamp)).put("delayBeats", d(pf.delayBeats))
                .put("delayFeedback", d(pf.delayFeedback)).put("delayPingPong", pf.delayPingPong))
            .put("sides", JSONArray().put(JSONObject().put("controller", p.controller ?: "").put("settings", settings).put("parts", parts)))
        return String(Native.nativePresetWrite(o.toString().toByteArray(Charsets.UTF_8)), Charsets.UTF_8)
    }

    fun presetFileName(p: Preset) = String(Native.nativePresetFileName(p.name.toByteArray(Charsets.UTF_8)), Charsets.UTF_8)

    /**
     * A preset file's [text] as a new preset of yours, after the rest, and on view. Its first side
     * only: a computer's split is its sides, which odo on Android has not. A controller or sound
     * this phone has not: what plays now plays instead. What happened, in a sentence or two.
     */
    fun openPresetFile(text: String): String {
        val out = String(Native.nativePresetRead(text.toByteArray(Charsets.UTF_8)), Charsets.UTF_8)
        if (out.startsWith("!")) return out.drop(1)
        val f = runCatching { JSONObject(out) }.getOrNull() ?: return "This file is not an odo preset."
        val sides = f.optJSONArray("sides") ?: return "This preset has nothing in it to play."
        val side = sides.optJSONObject(0) ?: return "This preset has nothing in it to play."
        val notes = mutableListOf<String>()

        var ctl: String? = side.optString("controller").ifEmpty { null }
        var settings = side.optJSONObject("settings")
        if (ctl != null && Controllers.all.none { it.id == ctl }) {
            notes += "Its controller isn't on this phone: the one in use plays it"
            ctl = controller.id; settings = null
        }
        val c = ctl?.let { Controllers.byId(it) }
        val s = settings?.let { if (c is com.patrickreynolds.mpeinstruments.controllers.PackageController) JSONObject().put("byId", it) else it }

        var missing = 0
        var olderMissing = 0
        val a = side.optJSONArray("parts") ?: JSONArray()
        val parts = (0 until minOf(a.length(), PARTS)).mapNotNull { k -> a.optJSONObject(k) }.mapIndexed { k, q ->
            val id = q.optString("sound")
            val known = instruments.any { it.id == id }
            if (!known && k < 2) missing++
            val sound = if (known) id else perf.part(k).instrument
            // The version the file names, if this phone has it; the newest plays as "".
            val want = if (known) q.optString("version") else ""
            val has = want.isNotEmpty() && allInstruments.any { it.id == sound && it.version == want }
            if (want.isNotEmpty() && !has && k < 2) olderMissing++
            val version = if (has && instruments.none { it.id == sound && it.version == want }) want else ""
            // The file's arp says "on"; the app's own JSON (Arp.json) "arp".
            val arp = q.optJSONObject("arp")?.let { JSONObject(it.toString()).put("arp", it.optBoolean("on")) } ?: Arp().json()
            if (q.has("volume")) Part.from(q.put("instrument", sound).put("version", version).put("arpeggio", arp))
            else com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(sound).applyTo(perf.part(k), sound).copy(version = version)
        }.ifEmpty { perf.parts }
        if (missing > 0) notes += "$missing ${if (missing == 1) "sound isn't" else "sounds aren't"} here: what was playing plays instead"
        if (olderMissing > 0) notes += "$olderMissing ${if (olderMissing == 1) "sound's version isn't" else "sounds' versions aren't"} here: the newest plays instead"

        val fx = f.optJSONObject("effects")
        val room = com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(parts[0].instrument)
        val made = Perf(layered = f.optBoolean("layered"), parts = parts,
            bpm = fx?.optDouble("bpm")?.toFloat() ?: perf.bpm,
            reverbSize = fx?.optDouble("reverbSize")?.toFloat() ?: room.reverbSize,
            reverbDamp = fx?.optDouble("reverbDamp")?.toFloat() ?: room.reverbDamp,
            delayBeats = fx?.optDouble("delayBeats")?.toFloat() ?: perf.delayBeats,
            delayFeedback = fx?.optDouble("delayFeedback")?.toFloat() ?: perf.delayFeedback,
            delayPingPong = fx?.optBoolean("delayPingPong") ?: perf.delayPingPong)
        if (sides.length() > 1) notes += "It splits the screen into ${sides.length()} sides, which odo on a phone doesn't: here is the first"

        val p = Preset(freeName(f.optString("name")), made, controller = ctl, settings = s?.toString())
        userPresets = userPresets + p
        savePresets()
        load(p)
        return (listOf("Opened “${p.name}”") + notes).joinToString(". ")
    }

    /**
     * Your first presets: a spread of sounds to loop together, on the touch pad (and the touch
     * pad's sound slots of 0.13, which become presets of their own).
     */
    private fun startPresets() {
        val fromSlots = runCatching {
            val a = JSONArray(prefs.getString("slots", null) ?: return@runCatching null)
            (0 until a.length()).mapNotNull { k -> a.optJSONObject(k)?.let { Part.from(it) } }
        }.getOrNull()
        val parts = fromSlots ?: STARTING_SOUNDS
        val made = mutableListOf<Preset>()
        for (part in parts) {
            val p = Perf(parts = listOf(part, Part(instrument = "voices/soprano")))
            val base = soundName(p, count = 1)
            val taken = (BUILT_IN_PRESETS + userPresets + made).map { it.name }.toSet()
            var name = base; var n = 2
            while (name in taken) name = "$base ${n++}"
            val drums = instruments.firstOrNull { it.id == part.instrument }?.group == "drums"
            val pads = List(2) { if (drums) PadSettings(vertical = 2) else PadSettings() }
            made += Preset(name, p, settings = LegacyTouchPad.preset(pads = pads).toString())
        }
        userPresets = userPresets + made
        prefs.edit().putBoolean("presetsStarted", true).remove("slots").apply()
        savePresets()
    }

    private val savePresetsNow = Runnable { savePresets() }
    private fun savePresets() {
        val a = JSONArray()
        for (p in userPresets) a.put(p.json())
        prefs.edit().putString("presets", a.toString()).putString("preset", presetName).apply()
    }

    // A slider drag changes the performance many times a second: save once it settles.
    private val saver = android.os.Handler(android.os.Looper.getMainLooper())
    private val saveNow = Runnable { prefs.edit().putString("perf", perf.json().toString()).putString("preset", presetName).apply() }
    private fun save() { saver.removeCallbacks(saveNow); saver.postDelayed(saveNow, 400) }
}

/** A built-in preset for any controller: the sound only. */
private fun preset(name: String, perf: Perf) = Preset(name, perf, builtIn = true, controller = null)
/** A built-in touch pad preset: the sound (with [arps], if given), and the pad's split and (if given) pads. */
private fun padPreset(name: String, perf: Perf, split: Boolean = false, arps: List<Arp>? = null,
                      pads: List<PadSettings>? = null) =
    Preset(name, arps?.let { perf.withArps(it) } ?: perf, builtIn = true, settings = LegacyTouchPad.preset(split, pads).toString())
private fun two(a: Part, b: Part = Part(instrument = "voices/soprano")) = listOf(a, b)
private fun arp(order: Int, rate: Float, gate: Float, octaves: Int = 1, latch: Boolean = false) =
    Arp(on = true, order = order, rate = rate, gate = gate, octaves = octaves, latch = latch)

/** A starting point for each kind of playing. */
val BUILT_IN_PRESETS = listOf(
    // Snapped keys, no finger vibrato (a piano cannot bend); press firmly as you lift to hold a note.
    padPreset("Grand piano", Perf(reverbSize = 0.65f, parts = two(Part("keys/piano", reverb = 0.22f))),
              pads = listOf(PadSettings(size = 12, glide = 0f, vibrato = 0), PadSettings())),
    preset("Solo cello", Perf(parts = two(Part("strings/cello", reverb = 0.25f)), reverbSize = 0.6f)),
    preset("Solo violin", Perf(parts = two(Part("strings/violin", reverb = 0.25f)))),
    preset("Breathy flute", Perf(parts = two(Part("winds/flute", reverb = 0.35f, pressureCurve = 0.7f)), reverbSize = 0.75f)),
    preset("Choir layer", Perf(layered = true, parts = listOf(
        Part("strings/cello", reverb = 0.3f, volume = 0.8f),
        Part("voices/soprano", transpose = 12, reverb = 0.45f, volume = 0.6f)), reverbSize = 0.8f)),
    padPreset("Bass + lead split", Perf(parts = listOf(
        Part("voices/bass", transpose = -12, reverb = 0.1f),
        Part("synths/lead", reverb = 0.2f, delay = 0.25f))), split = true),
    padPreset("Arp left, cello right", Perf(bpm = 100f, parts = listOf(
        Part("struck/marimba", reverb = 0.2f, delay = 0.2f),
        Part("strings/cello", reverb = 0.3f))), split = true,
        arps = listOf(arp(order = 2, rate = 4f, gate = 0.5f, octaves = 2, latch = true), Arp())),
    padPreset("Pluck arp", Perf(bpm = 118f, delayBeats = 0.75f, delayFeedback = 0.4f, parts = two(
        Part("plucked/steel-guitar", reverb = 0.2f, delay = 0.35f))),
        arps = listOf(arp(order = 0, rate = 4f, gate = 0.4f, octaves = 2), Arp())),
    preset("Dreamy pad", Perf(reverbSize = 0.95f, parts = two(
        Part("synths/pad", brightness = 0.7f, reverb = 0.6f, delay = 0.15f, pressureCurve = 0.8f)))),
    preset("Gritty lead", Perf(bpm = 124f, parts = two(Part("synths/lead", drive = 0.5f, brightness = 0.85f, reverb = 0.15f, delay = 0.3f)))),
    padPreset("Vibraphone, latched", Perf(bpm = 90f, parts = two(Part("struck/vibraphone", reverb = 0.4f))),
              arps = listOf(arp(order = 3, rate = 3f, gate = 0.8f, latch = true), Arp())),
)

/** Your first presets: a spread of sounds to loop together. */
val STARTING_SOUNDS = listOf(
    Part(instrument = "drums/kit", reverb = 0.08f),
    Part(instrument = "plucked/nylon-guitar", reverb = 0.15f),
    Part(instrument = "strings/cello", reverb = 0.2f),
    Part(instrument = "synths/pad", reverb = 0.3f),
    Part(instrument = "synths/lead", reverb = 0.12f, delay = 0.15f),
)
