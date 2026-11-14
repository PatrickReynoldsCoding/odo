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
 *
 * Those are its first side's. Split ([split], a Splits index; 0 the whole screen), [more] has each
 * further side's controller, settings and sounds, and [splitAt] where the dividers are (empty: evenly).
 */
data class Preset(val name: String, val perf: Perf, val builtIn: Boolean = false,
                  val controller: String? = LegacyTouchPad.ID, val settings: String? = null,
                  val split: Int = 0, val splitAt: List<Double> = emptyList(), val more: List<PresetSide> = emptyList()) {
    fun json(): JSONObject = JSONObject().put("name", name).put("perf", perf.json()).apply {
        controller?.let { put("controller", it) }
        settings?.let { put("settings", JSONObject(it)) }
        if (split != 0) {
            put("split", split)
            put("splitAt", JSONArray().apply { for (d in splitAt) put(d) })
            put("sides", JSONArray().apply { for (s in more) put(s.json()) })
        }
    }
    companion object {
        fun from(o: JSONObject): Preset {
            val perf = o.getJSONObject("perf")
            if (o.has("controller")) {
                val settings = o.optJSONObject("settings")
                var p = Perf.from(perf)
                // Saved by 0.16 to 0.20: the arps were the controller's.
                if (Perf.arpsInController(perf)) Controllers.byId(o.getString("controller")).legacyArps(settings)?.let { p = p.withArps(it) }
                val at = o.optJSONArray("splitAt")
                val sides = o.optJSONArray("sides")
                return Preset(o.getString("name"), p, controller = o.getString("controller"), settings = settings?.toString(),
                              split = o.optInt("split", 0),
                              splitAt = if (at == null) emptyList() else List(at.length()) { at.optDouble(it, 0.5) },
                              more = if (sides == null) emptyList() else List(sides.length()) { PresetSide.from(sides.getJSONObject(it)) })
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

    // ---- the sides of the screen (a split: Splits) ----
    // Each side has its own controller (its own instance, with its own settings) and its own parts.
    // The rail changes one, [activeSide]: [controller] is its controller, and [perf] holds its parts
    // (with the tempo and effects all the sides share); the other sides' parts are in [sides].

    /** How the screen is split (a Splits index; 0 the whole screen). */
    var split by mutableStateOf(0)
        private set
    /** Where its dividers are, 0..1: across (left to right), then down (top to bottom). */
    var splitAt by mutableStateOf<List<Double>>(emptyList())
        private set
    /** The side the rail changes. */
    var activeSide by mutableStateOf(0)
        private set
    /** Every side, four (those past the split's are kept, not shown). [partsOf] has a side's parts now. */
    var sides by mutableStateOf(List(Controllers.SIDES) { Side(ExternalMidi, Perf.DEFAULT_PARTS) })
        private set
    /** How many sides the screen has now. */
    val count get() = Splits.of(split).sides
    /** Side [k]'s name ("Left", "Top right"; "Whole"). */
    fun sideName(k: Int) = Splits.of(split).side.getOrElse(k) { "" }
    fun sideController(k: Int) = sides[k].controller
    /** Side [k]'s parts now (the side the rail changes: [perf]'s). */
    fun partsOf(k: Int): List<Part> = if (k == activeSide) perf.parts else sides[k].parts
    /** Side [k]'s part [j]'s instrument. */
    fun instrumentOf(k: Int, j: Int) = partsOf(k).getOrElse(j) { Perf.DEFAULT_PARTS[j.coerceIn(0, PARTS - 1)] }.let { find(it.instrument, it.version) }
    /** How many parts side [k] plays: its controller's split's, or one; the whole screen, as [partCount]. */
    fun sidePartCount(k: Int): Int {
        if (count == 1) return perf.partCount(shape)
        val s = sides[k].controller.shape
        return if (s.split) s.parts.coerceIn(2, PARTS) else 1
    }
    /** The engine part of side [k]'s first sound: the sides' parts one after another (at most PARTS in all). */
    fun partBase(k: Int): Int = (0 until k).sumOf { sidePartCount(it) }.coerceAtMost(PARTS - 1)

    /** The controller the rail changes: its options are on the rail and its surface is what you play. */
    val controller: Controller get() = sides[activeSide].controller
    /** What the controller sets in the engine beside the sound; the app's arps play if it lets them. */
    val shape get() = controller.shape.let { it.copy(appArp = it.appArp && controller.arpUse == ArpUse.App) }
    /** How many parts sound: 1, a split's sections (2 to 4), or 2 layered; split, the active side's. */
    val partCount get() = if (count == 1) perf.partCount(shape) else sidePartCount(activeSide)
    /** Every note plays two sounds (the whole screen only). */
    val layeredNow get() = count == 1 && perf.layout(shape) == 2
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
        for (c in Controllers.every) runCatching {
            val saved = prefs.getString(key(c), null)
                ?: (if (slotOf(c) == 0) Controllers.formerIds(c.id).firstNotNullOfOrNull { prefs.getString("ctl.$it", null) } else null)
            c.load(saved?.let { JSONObject(it) })
        }
        // The sides: the first's controller and parts as before splits; the others' kept beside them.
        val first = Controllers.byId(prefs.getString("controller", LegacyTouchPad.ID))
        val kept = runCatching { JSONArray(prefs.getString("sides", "[]")) }.getOrDefault(JSONArray())
        sides = List(Controllers.SIDES) { k ->
            if (k == 0) Side(first, perf.parts)
            else kept.optJSONObject(k - 1)?.let { o -> Side(Controllers.byId(o.optString("controller"), k), partsFrom(o.optJSONArray("parts"))) }
                ?: Side(Controllers.byId(if (first is ExternalMidi) LegacyTouchPad.PACKAGE else first.id, k), newSideParts(k))
        }
        split = prefs.getInt("split", 0).coerceIn(Splits.all.indices)
        if (count > 1 && first === ExternalMidi) sides = sides.mapIndexed { k, s -> if (k == 0) s.copy(controller = Controllers.byId(LegacyTouchPad.PACKAGE)) else s }
        splitAt = Splits.dividers(split, runCatching { JSONArray(prefs.getString("splitAt", "[]")).let { a -> List(a.length()) { a.optDouble(it, 0.5) } } }.getOrDefault(emptyList()))
        // From 0.16 to 0.20 the arps were each controller's: the one in use brings its own into the sound.
        if (Perf.arpsInController(perfJson)) {
            val saved = runCatching { prefs.getString("ctl.${sides[0].controller.id}", null)?.let { JSONObject(it) } }.getOrNull()
            sides[0].controller.legacyArps(saved)?.let { perf = perf.withArps(it) }
            prefs.edit().putString("perf", perf.json().toString()).apply()
        }
        showSide(prefs.getInt("activeSide", 0).coerceIn(0, count - 1))
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
        // Your preset on view: changed since saved, if what plays (kept from last time) is not it.
        presetEdited = activePreset?.let { current(it.name) != it } ?: false
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
        built.fill(-1)
        applyEngine()
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

    /** The instrument each engine part has (-1: none yet). Parts after the first are only built when a
     *  split or layer needs them: 15 more copies of an instrument each. */
    private val built = IntArray(PARTS) { -1 }

    /**
     * What the engine plays: the whole screen, the sound and the controller's shape as they are; split,
     * each side's parts one after another (at most PARTS), each with its arp if its controller lets the
     * app's play, and nothing else of a controller's shape (only External MIDI has one, and it plays
     * the whole screen).
     */
    private fun engine(): Pair<Perf, EngineShape> {
        if (count == 1) return perf to shape
        val parts = mutableListOf<Part>()
        for (k in 0 until count) {
            val c = sides[k].controller
            for (j in 0 until sidePartCount(k)) if (parts.size < PARTS) {
                val p = partsOf(k).getOrElse(j) { Perf.DEFAULT_PARTS[j] }
                val arp = when { c.arpUse != ArpUse.App -> Arp(); !c.shape.arpLatch -> p.arp.copy(latch = false); else -> p.arp }
                parts += p.copy(arp = arp)
            }
        }
        return perf.copy(layered = false, parts = parts) to EngineShape(split = parts.size > 1, parts = parts.size.coerceIn(2, PARTS))
    }

    /** The sound and the controllers' shape to the engine (instruments only where they changed), and
     *  each side's notes to its own parts and MIDI channels, as odo for computers' route(). */
    private fun applyEngine() {
        val (p, s) = engine()
        Native.nativeSetTranspose(s.transpose)
        Native.nativeSetBendRange(s.bendRange)
        for (k in 0 until p.partCount(s)) {
            val i = index(p.part(k))
            if (built[k] != i) { Native.nativeSetPartInstrument(k, i); built[k] = i }
        }
        Native.nativeSetPerformance(p.toArray(s))
        val per = 15 / count
        for (k in 0 until count) Native.nativeCtlRoute(k, if (count == 1) 0 else partBase(k), 1 + k * per, per)
    }

    fun update(change: (Perf) -> Perf) {
        val old = perf
        perf = change(perf)
        if (perf == old) return
        applyEngine()
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
            // The room is shared: the first side's first sound's.
            if ((twoParts && k >= 1) || activeSide > 0) q else q.copy(reverbSize = d.reverbSize, reverbDamp = d.reverbDamp)
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
        val shown = (0 until count).any { sides[it].controller === c }
        if (shown && engine && !loading) applyEngine()
        pendingSave += c
        saver.removeCallbacks(saveControllers)
        saver.postDelayed(saveControllers, 400)
        if (shown) touched()
    }
    private val pendingSave = mutableSetOf<Controller>()
    private val saveControllers = Runnable {
        val e = prefs.edit()
        for (c in pendingSave) e.putString(key(c), c.save().toString())
        pendingSave.clear()
        e.apply()
    }
    /** The side a controller is for (its own of a package; External MIDI is the first side's). */
    private fun slotOf(c: Controller) = (c as? com.patrickreynolds.mpeinstruments.controllers.PackageController)?.slot ?: 0
    /** Where a controller's settings are kept: the first side's as before splits, the others' beside them. */
    private fun key(c: Controller) = slotOf(c).let { if (it == 0) "ctl.${c.id}" else "ctl@$it.${c.id}" }

    /** Play with another controller on the side the rail changes: the sound stays; its own settings
     *  come back as you left them. */
    fun switchController(c: Controller) {
        if (c === controller) return
        if (count > 1 && c === ExternalMidi) return            // a split's sides are played on the screen
        controller.stop()
        sides = sides.mapIndexed { k, s -> if (k == activeSide) s.copy(controller = c) else s }
        side = 0
        if (activeSide == 0) prefs.edit().putString("controller", c.id).apply()
        save()
        applyEngine()
        touched()
    }

    // ---- splits -----------------------------------------------------------------------------

    /** The sounds a new side starts with: the default parts, from its own (side 2: the soprano, ...). */
    private fun newSideParts(k: Int): List<Part> = List(PARTS) { j ->
        val id = Perf.DEFAULT_PARTS[(k + j) % PARTS].instrument
        com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(id).applyTo(Part(), id)
    }

    /** Side [k] is the one the rail changes: [perf] takes its parts, the side it was on keeps its own. */
    private fun showSide(k: Int) {
        if (k == activeSide) return
        sides = sides.mapIndexed { i, s -> if (i == activeSide) s.copy(parts = perf.parts) else s }
        activeSide = k
        perf = perf.copy(parts = fullParts(sides[k].parts))
    }

    /** Change side [k] (tapped under CHANGING): the rail's tiles and groups are now its. */
    fun setActive(k: Int) {
        val n = k.coerceIn(0, count - 1)
        if (n == activeSide) return
        showSide(n)
        side = 0
        save()
    }

    /**
     * Split the screen as split [n] says. [fresh]: chosen now (not a preset's): each new side takes its
     * controller's own look for that side (its manifest's "sideThemes"), so the sides stand apart.
     * A side no longer shown lets go of its notes; what it had is kept, for when it comes back.
     */
    fun setSplit(n: Int, fresh: Boolean = true) {
        val m = n.coerceIn(Splits.all.indices)
        if (m == split) return
        val before = count
        val now = Splits.of(m).sides
        if (activeSide >= now) { showSide(0); side = 0 }
        for (k in now until before) sides[k].controller.stop()
        split = m
        splitAt = Splits.dividers(m, emptyList())
        // External MIDI plays the whole screen: split, the first side is the touch pad.
        if (now > 1 && sides[0].controller === ExternalMidi) {
            val pad = Controllers.byId(LegacyTouchPad.PACKAGE)
            sides = sides.mapIndexed { k, s -> if (k == 0) s.copy(controller = pad) else s }
            prefs.edit().putString("controller", pad.id).apply()
            if (activeSide == 0) side = 0
        }
        if (fresh) for (k in before until now) (sides[k].controller as? com.patrickreynolds.mpeinstruments.controllers.PackageController)?.takeSideTheme(k)
        save()
        applyEngine()
        touched()
    }

    /** Divider [i] dragged to (fx, fy), fractions of the play area: more of one side shows, less of
     *  another; no controller is laid out again. [done]: let go, so it is kept. */
    fun dragDivider(i: Int, fx: Float, fy: Float, done: Boolean = false) {
        if (count < 2) return
        if (!done) splitAt = Splits.dragged(split, splitAt, i, fx, fy)
        else { save(); touched() }
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
    /** The preset on view, if it is one of yours. A preset is a save point: what you change after is
     *  not kept in it until you save it (saveChanges). */
    val activePreset get() = userPresets.firstOrNull { it.name == presetName }
    private var loading = false

    /** The tempo and effects, with side [k]'s parts. */
    private fun perfOf(k: Int) = if (k == activeSide) perf else perf.copy(parts = fullParts(sides[k].parts))

    /** What is playing now, as a preset of yours: every side, the split and where its dividers are. */
    private fun current(name: String) = Preset(name, perfOf(0), controller = sides[0].controller.id,
        settings = sides[0].controller.save().toString(), split = split, splitAt = if (count > 1) splitAt else emptyList(),
        more = (1 until count).map { k -> PresetSide(sides[k].controller.id, sides[k].controller.save().toString(), fullParts(partsOf(k))) })

    /** Something changed: the preset on view is now changed since it was saved (yours) or loaded (a
     *  built-in one). It is not kept in it: Save changes does that. */
    private fun touched() {
        if (loading || presetName.isEmpty()) return
        val p = activePreset
        presetEdited = p == null || current(p.name) != p
    }

    /** Save changes: your preset on view becomes what plays now. */
    fun saveChanges() {
        val p = activePreset ?: return
        userPresets = userPresets.map { if (it.name == p.name) current(p.name) else it }
        presetEdited = false
        savePresets()
    }

    /** [p] on, as loading a preset does. [onView] false: what plays is changed, but no preset is on
     *  view (a take's settings, which are not one of yours). */
    fun load(p: Preset, onView: Boolean = true) {
        loading = true
        // The rail goes back to the first side; a side the preset has not lets go of its notes.
        showSide(0)
        side = 0
        val m = p.split.coerceIn(Splits.all.indices)
        val now = Splits.of(m).sides
        for (k in now until count) sides[k].controller.stop()
        val loaded = mutableListOf<Controller>()
        var next = sides
        for (k in 0 until now) {
            val ps = if (k == 0) PresetSide(p.controller ?: sides[0].controller.id, p.settings, p.perf.parts) else p.more.getOrNull(k - 1)
            // A side the preset has not (a file with fewer than its split): it keeps what it has.
            if (ps == null) continue
            var c = if (k == 0 && p.controller == null) sides[0].controller else Controllers.byId(ps.controller, k)
            if (now > 1 && c === ExternalMidi) c = Controllers.byId(LegacyTouchPad.PACKAGE)   // a split is played on the screen
            if (c !== next[k].controller) next[k].controller.stop()
            val s = ps.settings?.let { runCatching { JSONObject(it) }.getOrNull() }
            if (s != null) c.loadPreset(s) else c.clearForPreset()
            next = next.mapIndexed { i, sd -> if (i == k) Side(c, fullParts(ps.parts)) else sd }
            loaded += c
        }
        sides = next
        split = m
        splitAt = Splits.dividers(m, p.splitAt)
        perf = p.perf.copy(parts = fullParts(sides[0].parts))
        prefs.edit().putString("controller", sides[0].controller.id).apply()
        applyEngine()
        save()
        presetName = if (onView) p.name else ""
        presetEdited = false
        prefs.edit().putString("preset", presetName).apply()
        for (c in loaded) controllerChanged(c, engine = false)   // saved as the controllers'; loading, so not into a preset
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
        val c = p.controller?.let { Controllers.byId(it) } ?: sides[0].controller
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
        // Each side: its controller, its settings by id, its parts.
        fun side(controller: String, settingsText: String?, ps: List<Part>): JSONObject {
            val s = settingsText?.let { runCatching { JSONObject(it) }.getOrNull() }
            val settings = s?.optJSONObject("byId") ?: s ?: JSONObject()
            val parts = JSONArray()
            for (part in ps) parts.put(JSONObject().put("sound", part.instrument)
                .put("version", find(part.instrument, part.version)?.version ?: "").put("transpose", part.transpose)
                .put("volume", d(part.volume)).put("brightness", d(part.brightness)).put("drive", d(part.drive))
                .put("pressureCurve", d(part.pressureCurve)).put("reverb", d(part.reverb))
                .put("delay", d(part.delay)).put("release", d(part.release)).put("width", d(part.width))
                .put("arp", JSONObject().put("on", part.arp.on).put("order", part.arp.order).put("rate", d(part.arp.rate))
                    .put("gate", d(part.arp.gate)).put("octaves", part.arp.octaves).put("latch", part.arp.latch)))
            return JSONObject().put("controller", controller).put("settings", settings).put("parts", parts)
        }
        val sides = JSONArray().put(side(p.controller ?: "", p.settings, pf.parts))
        if (p.split != 0) for (s in p.more) sides.put(side(s.controller, s.settings, s.parts))
        val o = JSONObject().put("format", "odo-preset/1").put("name", p.name)
            .put("madeBy", "odo ${BuildConfig.VERSION_NAME} (Android)").put("split", p.split).put("layered", pf.layered)
            .put("effects", JSONObject().put("bpm", d(pf.bpm)).put("reverbSize", d(pf.reverbSize))
                .put("reverbDamp", d(pf.reverbDamp)).put("delayBeats", d(pf.delayBeats))
                .put("delayFeedback", d(pf.delayFeedback)).put("delayPingPong", pf.delayPingPong))
            .put("sides", sides)
        if (p.split != 0 && p.splitAt.isNotEmpty()) o.put("splitAt", JSONArray().apply { for (v in p.splitAt) put(v) })
        return String(Native.nativePresetWrite(o.toString().toByteArray(Charsets.UTF_8)), Charsets.UTF_8)
    }

    fun presetFileName(p: Preset) = String(Native.nativePresetFileName(p.name.toByteArray(Charsets.UTF_8)), Charsets.UTF_8)

    /**
     * A preset file's [text] as a new preset of yours, after the rest, and on view: every side of
     * its split. A controller or sound this phone has not: what that side plays now plays instead.
     * What happened, in a sentence or two.
     */
    fun openPresetFile(text: String): String = presetFromFile(text, keep = true, said = "Opened")

    /** What plays now, as a preset file's text: what a take keeps of how it was played. */
    fun currentPresetFileText(): String = presetFileText(current(presetName.ifEmpty { soundName() }))

    /**
     * A preset file's [text] on: [keep], as a new preset of yours (on view), as opening a file
     * does; or not, only playing with it (a take's settings). "[said] “its name”", and what was
     * left out, in a sentence or two.
     */
    fun presetFromFile(text: String, keep: Boolean, said: String): String {
        val out = String(Native.nativePresetRead(text.toByteArray(Charsets.UTF_8)), Charsets.UTF_8)
        if (out.startsWith("!")) return out.drop(1)
        val f = runCatching { JSONObject(out) }.getOrNull() ?: return "This file is not an odo preset."
        val fileSides = f.optJSONArray("sides") ?: return "This preset has nothing in it to play."
        if (fileSides.optJSONObject(0) == null) return "This preset has nothing in it to play."
        val notes = mutableListOf<String>()
        val split = f.optInt("split", 0).coerceIn(Splits.all.indices)
        val sidesNow = Splits.of(split).sides             // a split's sides; a file may give more

        var missingCtl = 0
        var missing = 0
        var olderMissing = 0
        // Side k of the file: its controller (or what that side plays now), settings and parts.
        fun side(k: Int): PresetSide? {
            val side = fileSides.optJSONObject(k) ?: return null
            val here = if (k < count) sides[k] else sides[0]
            var ctl = side.optString("controller")
            var settings = side.optJSONObject("settings")
            if (ctl.isEmpty() || Controllers.forSide(k).none { it.id == ctl }) {
                if (ctl.isNotEmpty()) missingCtl++
                ctl = here.controller.id; settings = null
            }
            val c = Controllers.byId(ctl, k)
            val s = settings?.let { if (c is com.patrickreynolds.mpeinstruments.controllers.PackageController) JSONObject().put("byId", it) else it }
            val was = partsOf(if (k < count) k else 0)
            val a = side.optJSONArray("parts") ?: JSONArray()
            val parts = (0 until minOf(a.length(), PARTS)).mapNotNull { j -> a.optJSONObject(j) }.mapIndexed { j, q ->
                val id = q.optString("sound")
                val known = instruments.any { it.id == id }
                if (!known && j < 2) missing++
                val sound = if (known) id else was.getOrElse(j) { Perf.DEFAULT_PARTS[j] }.instrument
                // The version the file names, if this phone has it; the newest plays as "".
                val want = if (known) q.optString("version") else ""
                val has = want.isNotEmpty() && allInstruments.any { it.id == sound && it.version == want }
                if (want.isNotEmpty() && !has && j < 2) olderMissing++
                val version = if (has && instruments.none { it.id == sound && it.version == want }) want else ""
                // The file's arp says "on"; the app's own JSON (Arp.json) "arp".
                val arp = q.optJSONObject("arp")?.let { JSONObject(it.toString()).put("arp", it.optBoolean("on")) } ?: Arp().json()
                if (q.has("volume")) Part.from(q.put("instrument", sound).put("version", version).put("arpeggio", arp))
                else com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(sound).applyTo(was.getOrElse(j) { Perf.DEFAULT_PARTS[j] }, sound).copy(version = version)
            }.ifEmpty { was }
            return PresetSide(c.id, s?.toString(), fullParts(parts))
        }
        val first = side(0)!!
        // The sides after the first; one the file has not keeps what it plays now.
        val more = (1 until sidesNow).map { k ->
            side(k) ?: PresetSide(sides[k].controller.id, sides[k].controller.save().toString(), fullParts(partsOf(k)))
        }
        if (missingCtl > 0) notes += if (missingCtl == 1) "Its controller isn't on this phone: the one in use plays it"
                                     else "Its controllers aren't on this phone: the ones in use play it"
        if (missing > 0) notes += "$missing ${if (missing == 1) "sound isn't" else "sounds aren't"} here: what was playing plays instead"
        if (olderMissing > 0) notes += "$olderMissing ${if (olderMissing == 1) "sound's version isn't" else "sounds' versions aren't"} here: the newest plays instead"
        if (fileSides.length() > sidesNow)
            notes += "Its sides after the " + listOf("first", "second", "third", "fourth")[sidesNow - 1] + " are left out"

        val fx = f.optJSONObject("effects")
        val room = com.patrickreynolds.mpeinstruments.packs.Packs.defaultsFor(first.parts[0].instrument)
        val made = Perf(layered = f.optBoolean("layered"), parts = first.parts,
            bpm = fx?.optDouble("bpm")?.toFloat() ?: perf.bpm,
            reverbSize = fx?.optDouble("reverbSize")?.toFloat() ?: room.reverbSize,
            reverbDamp = fx?.optDouble("reverbDamp")?.toFloat() ?: room.reverbDamp,
            delayBeats = fx?.optDouble("delayBeats")?.toFloat() ?: perf.delayBeats,
            delayFeedback = fx?.optDouble("delayFeedback")?.toFloat() ?: perf.delayFeedback,
            delayPingPong = fx?.optBoolean("delayPingPong") ?: perf.delayPingPong)
        val at = f.optJSONArray("splitAt")
        val p = Preset(if (keep) freeName(f.optString("name")) else f.optString("name"), made, controller = first.controller,
                       settings = first.settings, split = split,
                       splitAt = if (split == 0) emptyList() else Splits.dividers(split, if (at == null) emptyList() else List(at.length()) { at.optDouble(it, 0.5) }),
                       more = more)
        if (keep) {
            userPresets = userPresets + p
            savePresets()
        }
        load(p, onView = keep)
        return (listOf("$said “${p.name}”") + notes).joinToString(". ")
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
    // The first side's parts in "perf" and its controller in "controller", as before splits; the other
    // sides beside them in "sides" (each its controller and parts; their settings are each controller's own).
    private val saveNow = Runnable {
        val more = JSONArray()
        for (k in 1 until Controllers.SIDES) more.put(JSONObject().put("controller", sides[k].controller.id).put("parts", partsJson(partsOf(k))))
        prefs.edit().putString("perf", perfOf(0).json().toString()).putString("preset", presetName)
            .putString("sides", more.toString()).putInt("split", split)
            .putString("splitAt", JSONArray().apply { for (d in splitAt) put(d) }.toString()).putInt("activeSide", activeSide).apply()
    }
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
