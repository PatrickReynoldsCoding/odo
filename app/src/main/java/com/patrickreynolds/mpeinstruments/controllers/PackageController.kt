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
import com.patrickreynolds.mpeinstruments.ui.packageOptions
import org.json.JSONArray
import org.json.JSONObject
import java.util.zip.ZipInputStream

/**
 * A controller package (.odoc): its WebAssembly runs in the engine (Native.nativeCtl*), the same
 * file a computer MESH plays (CONTROLLER_FORMAT.md). Its settings come from its manifest and are
 * shown on the rail like any controller's. One runs at a time: the one in use.
 */
class PackageController(private val bytes: ByteArray, val manifest: JSONObject) : Controller {
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

    override fun load(o: JSONObject?) {
        val byId = o?.optJSONObject("byId")
        val a = o?.optJSONArray("values")            // before 0.22.2: by position
        // Neither: a Kotlin controller's own settings (the Grid's, the Tonnetz's), or a preset made with
        // one. Their keys that are this package's ids mean the same, so they carry over.
        val flat = if (byId == null && a == null) o else null
        values = settings.mapIndexed { k, s ->
            val id = s.optString("id", "$k")
            when {
                byId != null && byId.has(id) -> byId.optDouble(id, default(s))
                byId == null && a != null && k < a.length() -> a.optDouble(k, default(s))
                flat != null && flat.has(id) -> when (val v = flat.opt(id)) {
                    is Boolean -> if (v) 1.0 else 0.0
                    is Number -> v.toDouble()
                    else -> default(s)
                }
                else -> default(s)
            }
        }
        if (loaded) values.forEachIndexed { k, v -> Native.nativeCtlSetting(k, v) }
    }

    override fun stop() {
        if (loaded) Native.nativeCtlUnload()
        loaded = false
    }

    @Composable
    override fun options(part: Int) = packageOptions(this)

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
                    if (e.name == "controller.json") return PackageController(bytes, JSONObject(z.readBytes().decodeToString()))
                }
            }
        }
    }
}
