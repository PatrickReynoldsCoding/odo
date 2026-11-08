package com.patrickreynolds.mpeinstruments.controllers

import org.json.JSONArray
import org.json.JSONObject

/**
 * A controller's settings as the rail shows them, from the engine's rail model (mpe/rail.h, and
 * CONTROLLER_FORMAT.md "Settings on the rail"): odo for computers shows the same.
 */
enum class RailWidget { Switch, Chips, Slider }

/** One setting on the rail. [index] is its place in the manifest's settings (what it is set by). */
data class RailRow(
    val index: Int, val id: String, val label: String, val group: String, val widget: RailWidget,
    /** Chips: one label each, in order; [chosen] the one marked (-1 none). */
    val choices: List<String>, val chosen: Int,
    val min: Double, val max: Double, val step: Double,
    val value: Double,
    /** The value as shown: "On"/"Off", the choice's name, or the number with its unit. */
    val display: String,
)

/** Settings shown together under [name] ("" for those with no group). */
data class RailGroup(val name: String, val rows: List<RailRow>) {
    companion object {
        /** rail::toJson's text; anything it can't read is left out. */
        fun parse(json: String): List<RailGroup> {
            val a = runCatching { JSONArray(json) }.getOrNull() ?: return emptyList()
            return List(a.length()) { a.optJSONObject(it) }.filterNotNull().map { g ->
                val rows = g.optJSONArray("rows") ?: JSONArray()
                RailGroup(g.optString("name"), List(rows.length()) { rows.optJSONObject(it) }.filterNotNull().map(::row))
            }.filter { it.rows.isNotEmpty() }     // a group its mode leaves empty (showIf) is not shown
        }

        private fun row(r: JSONObject): RailRow {
            val choices = r.optJSONArray("choices") ?: JSONArray()
            return RailRow(
                index = r.optInt("index"), id = r.optString("id"), label = r.optString("label"), group = r.optString("group"),
                widget = when (r.optString("widget")) { "switch" -> RailWidget.Switch; "chips" -> RailWidget.Chips; else -> RailWidget.Slider },
                choices = List(choices.length()) { choices.optString(it) }, chosen = r.optInt("chosen", -1),
                min = r.optDouble("min", 0.0), max = r.optDouble("max", 1.0), step = r.optDouble("step", 0.0),
                value = r.optDouble("value", 0.0), display = r.optString("display"),
            )
        }
    }
}
