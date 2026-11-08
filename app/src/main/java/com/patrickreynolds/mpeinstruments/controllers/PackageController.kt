package com.patrickreynolds.mpeinstruments.controllers

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.patrickreynolds.mpeinstruments.AppState
import com.patrickreynolds.mpeinstruments.EngineShape
import com.patrickreynolds.mpeinstruments.Native
import com.patrickreynolds.mpeinstruments.ui.PackageSurface
import org.json.JSONArray
import org.json.JSONObject
import java.util.zip.ZipInputStream

/**
 * A controller package (.odoc): its WebAssembly runs in the engine (Native.nativeCtl*), the same
 * file a computer MESH plays (docs/formats/CONTROLLER_FORMAT.md). Its settings come from its manifest and are
 * shown on the rail like any controller's. One runs at a time: the one in use.
 */
class PackageController(private val bytes: ByteArray, val manifest: JSONObject,
                        manifestText: ByteArray = manifest.toString().encodeToByteArray()) : Controller {
    override val id: String = "pkg:" + manifest.optString("id")
    override val name: String get() = Controllers.nameFor(manifest.optString("name", id))
    override val description: String = manifest.optString("description") + " · from a controller package (.odoc)"
    private val orientation = manifest.optString("orientation", "landscape")
    // "landscape": on its side; "upright": stays upright, the screen still; "portrait" or "any":
    // as the device is held (a controller that turns itself, as the Grid does, says "any").
    override val landscape = orientation == "landscape"
    override val upright = orientation == "upright"
    private val parts: List<String> = manifest.optJSONArray("parts")?.let { a -> List(a.length()) { a.optString(it) } } ?: listOf("Main")
    override val shape get() = EngineShape(split = inUse > 1, parts = inUse.coerceIn(2, 4), transpose = 0, bendRange = 48f)
    override val splitNames get() = if (inUse > 1) parts.take(inUse) else null
    override val arpUse = when (manifest.optString("arp", "app")) { "off" -> ArpUse.Off; "own" -> ArpUse.Own; else -> ArpUse.App }

    /** Its settings, in the manifest's order. */
    val settings: List<JSONObject> = manifest.optJSONArray("settings")?.let { a -> List(a.length()) { a.getJSONObject(it) } } ?: emptyList()
    var values by mutableStateOf(settings.map { default(it) })
        private set

    /** Its manifest in the engine's rail model (Native.nativeRail*), which shows and steps its settings as
     *  odo for computers does; it needs no controller loaded, so it serves whichever controller the rail shows. */
    private val rail = Native.nativeRailOpen(manifestText)

    /** The rail's groups and rows for its values now. */
    fun railGroups(): List<RailGroup> =
        if (settings.isEmpty()) emptyList() else RailGroup.parse(Native.nativeRail(rail, values.toDoubleArray()).decodeToString())
    fun stepped(index: Int, value: Double, dir: Int) = Native.nativeRailStepped(rail, index, value, dir)
    fun fromFraction(index: Int, t: Double) = Native.nativeRailFromFraction(rail, index, t)
    fun toFraction(index: Int, value: Double) = Native.nativeRailToFraction(rail, index, value)
    fun chipValue(index: Int, k: Int) = Native.nativeRailChipValue(rail, index, k)

    /** "partsFrom": the setting that says how many of its parts it plays now (its value + 1; a switch:
     *  one part off, two on), as the Grid's split does. Without one, all of them. */
    private val partsFrom = manifest.optString("partsFrom").let { id -> settings.indexOfFirst { it.optString("id") == id } }
    val inUse: Int get() = if (partsFrom < 0) parts.size else (1 + (values.getOrNull(partsFrom) ?: 0.0).toInt()).coerceIn(1, parts.size)

    /** The engine follows when the parts in use change; the rail goes back to the first if its part went. */
    private fun changed(index: Int) {
        val engine = index == partsFrom
        if (engine && AppState.side >= inUse) AppState.side = 0
        AppState.controllerChanged(this, engine)
    }

    private fun default(s: JSONObject) = when (s.optString("type")) {
        "switch" -> if (s.optBoolean("default")) 1.0 else 0.0
        else -> s.optDouble("default", 0.0)
    }

    /** True while its WebAssembly is the one loaded in the engine. */
    private var loaded = false

    /** Load it into the engine (when its surface opens): false if it can't run. */
    fun start(): Boolean {
        if (!loaded) {
            loaded = Native.nativeCtlLoad(bytes)
            if (loaded) values.forEachIndexed { k, v -> Native.nativeCtlSetting(k, v) }
        }
        return loaded
    }

    /** A setting it changed itself: kept and saved, not sent back to it. */
    fun changedByIt(index: Int, value: Double) {
        if (index !in values.indices || values[index] == value) return
        values = values.mapIndexed { k, v -> if (k == index) value else v }
        changed(index)
    }

    fun change(index: Int, value: Double) {
        if (values.getOrNull(index) == value) return
        values = values.mapIndexed { k, v -> if (k == index) value else v }
        if (loaded) Native.nativeCtlSetting(index, value)
        changed(index)
    }

    /** Its settings by name, so a new version that adds or moves settings keeps the rest. */
    override fun save(): JSONObject = JSONObject().put("byId", JSONObject().apply {
        settings.forEachIndexed { k, st -> put(st.optString("id", "$k"), values[k]) }
    })

    override fun load(o: JSONObject?) = load(o, settings.map { default(it) }, preset = false)

    /** A preset's: what it does not say stays as it is (a built-in touch pad preset may give only the split). */
    override fun loadPreset(o: JSONObject) = load(o, values, preset = true)

    /** A preset made for another controller, or none: what is per preset (a split) goes back to off. */
    override fun clearForPreset() { if (partsFrom >= 0) change(partsFrom, 0.0) }

    private fun load(o: JSONObject?, base: List<Double>, preset: Boolean) {
        // The Kotlin touch pad's settings (until 0.26), or a preset made with it: as this package's.
        val src = if (o != null && id == LegacyTouchPad.PACKAGE && LegacyTouchPad.isLegacy(o)) LegacyTouchPad.flatten(o, preset) else o
        val byId = src?.optJSONObject("byId")
        val a = src?.optJSONArray("values")          // before 0.22.2: by position
        // Neither: a Kotlin controller's own settings (the Grid's, the Tonnetz's), or a preset made with
        // one. Their keys that are this package's ids mean the same, so they carry over.
        val flat = if (byId == null && a == null) src else null
        values = settings.mapIndexed { k, s ->
            val id = s.optString("id", "$k")
            when {
                byId != null && byId.has(id) -> byId.optDouble(id, default(s))
                byId == null && a != null && k < a.length() -> a.optDouble(k, default(s))
                flat != null && flat.has(id) -> when (val v = flat.opt(id)) {
                    is Boolean -> if (v) 1.0 else 0.0
                    is Number -> v.toDouble()
                    else -> base.getOrElse(k) { default(s) }
                }
                else -> base.getOrElse(k) { default(s) }
            }
        }
        if (loaded) values.forEachIndexed { k, v -> Native.nativeCtlSetting(k, v) }
    }

    /** Until 0.21 the Kotlin touch pad's pads each had an arp. */
    override fun legacyArps(o: JSONObject?) = if (id == LegacyTouchPad.PACKAGE && o != null && LegacyTouchPad.isLegacy(o)) LegacyTouchPad.arps(o) else null

    /** Setting [id]'s value now, if it has one. */
    fun valueOf(id: String): Double? = settings.indexOfFirst { it.optString("id") == id }.takeIf { it >= 0 }?.let { values[it] }

    /** "drumsFrom": for each part, the switch that says its sound is a drum kit (the touch pad's), set by the app. */
    private val drumsFrom: List<Int> = manifest.optJSONArray("drumsFrom")?.let { a ->
        List(a.length()) { i -> a.optString(i).let { id -> settings.indexOfFirst { it.optString("id") == id } } }
    } ?: emptyList()

    /** Each part's sound, whether it is a drum kit: the "drumsFrom" switches follow. */
    fun syncDrums(drums: List<Boolean>) {
        for ((k, i) in drumsFrom.withIndex()) if (i >= 0 && k < drums.size) {
            val v = if (drums[k]) 1.0 else 0.0
            if (values[i] != v) change(i, v)
        }
    }

    override fun stop() {
        if (loaded) Native.nativeCtlUnload()
        loaded = false
    }

    /** Its settings are on the rail by group, from [railGroups] (PlayHost), not as options. */
    @Composable
    override fun options(part: Int) = emptyList<Option>()

    @Composable
    override fun Surface(part: Int) = PackageSurface(this)

    companion object {
        /** The controller packages that come with the app: assets/controllers/<name>.odoc. */
        fun builtIn(context: Context): List<PackageController> =
            (runCatching { context.assets.list("controllers") }.getOrNull() ?: emptyArray())
                .filter { it.endsWith(".odoc") }.sorted()
                .mapNotNull { f -> runCatching { fromBytes(context.assets.open("controllers/$f").use { it.readBytes() }) }.getOrNull() }

        /** A package's bytes, if it is a controller package (it has controller.json). */
        fun fromBytes(bytes: ByteArray): PackageController? {
            ZipInputStream(bytes.inputStream()).use { z ->
                while (true) {
                    val e = z.nextEntry ?: return null
                    if (e.name == "controller.json") {
                        val text = z.readBytes()
                        return PackageController(bytes, JSONObject(text.decodeToString()), text)
                    }
                }
            }
        }
    }
}
