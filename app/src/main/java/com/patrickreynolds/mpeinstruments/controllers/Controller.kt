package com.patrickreynolds.mpeinstruments.controllers

import androidx.compose.runtime.Composable
import com.patrickreynolds.mpeinstruments.Arp
import com.patrickreynolds.mpeinstruments.EngineShape
import org.json.JSONObject

/**
 * A controller: a way of playing the app's instruments. It sends MPE to the engine (through
 * Native.nativeTouch, the same parser as a Seaboard's), and brings its own settings, its own
 * options on the rail and its own play surface. What every controller shares (the sounds,
 * effects, release, the arp, loops, gyro and breath, presets) is the app's, around it.
 *
 * The app's modifiers (the arp; gyro and breath) work on whatever a controller plays. A
 * controller can turn the arp off, or bring its own instead ([arpUse]).
 *
 * The touch pad and an external MIDI controller are built in. This is the seam a downloaded
 * controller, a plug-in, will implement.
 */
interface Controller {
    /** Stable, saved in presets: "touchpad", "midi". */
    val id: String
    val name: String
    val description: String

    /** The play surface wants the screen on its side (the touch pad), or either way up. */
    val landscape: Boolean

    /** The play surface is upright and stays so: the screen does not turn with the phone (the Grid). */
    val upright: Boolean get() = false

    /** What it sets in the engine beside the sound: a split, pitch assist, transpose; whether the app's arps play. */
    val shape: EngineShape

    /** How it uses the app's arpeggiator: the app's (the default), none, or one of its own. */
    val arpUse: ArpUse get() = ArpUse.App

    /** A word on the app's arp with this controller, under its settings (the grid: how to keep it going). */
    val arpNote: String? get() = null

    /** Its arps from before 0.21, when they were its own, from settings saved by then; null if it had none. */
    fun legacyArps(o: JSONObject?): List<Arp>? = null

    /** The parts (sounds) it plays, by name, when it splits the notes between them: "Left", "Right". */
    val splitNames: List<String>? get() = null

    /** Its own colour for each part, so the rail matches what is on screen; null for the app's. */
    val partColors: List<androidx.compose.ui.graphics.Color>? get() = null

    /** Its settings, kept between sessions and in each preset. */
    fun save(): JSONObject

    /** Settings saved by [save], or null for its defaults. */
    fun load(o: JSONObject?)

    /** A preset's settings: like [load], but what belongs to the screen rather than the preset stays (the pad's look). */
    fun loadPreset(o: JSONObject) = load(o)

    /** A preset that says nothing of this controller: what is per preset goes back to off (the pad's split and arps). */
    fun clearForPreset() {}

    /** Its own options on the rail, in order, for the part the rail is changing. */
    @Composable
    fun options(part: Int): List<Option>

    /** What you play on; it fills the screen beside the rail. */
    @Composable
    fun Surface(part: Int)

    /** Let go of every note it holds (switching to another controller). */
    fun stop() {}
}

/** How a controller uses the app's arpeggiator. */
enum class ArpUse {
    /** The app's arp, on the rail, for each part the controller plays. */
    App,
    /** No arp: it makes no sense with this controller (one that plays its own rhythm, say). */
    Off,
    /** The controller arpeggiates itself, so the app's is off and not on the rail. */
    Own,
}

/**
 * One option on the rail: a label, a word or two for its value, and either a panel that opens
 * beside the rail or, for a switch, a tap.
 */
data class Option(
    val label: String,
    val value: String,
    /** Changes the part the rail is on (named in the panel's title when the notes are split). */
    val perPart: Boolean = true,
    /** A switch: on (highlighted) or off; [tap] flips it. */
    val on: Boolean = false,
    val tap: (() -> Unit)? = null,
    /** A group heading the rail shows above it (a controller package's settings group: "Layout", "Notes"...). */
    val heading: String? = null,
    val panel: (@Composable () -> Unit)? = null,
)

/**
 * The controllers there are: built in, then the controller packages that come with the app. The
 * Grid is the package one from 0.22 (the same file odo for computers plays); KeyGrid, the Kotlin
 * Grid it replaced, is no longer offered, and presets that used it open the package.
 */
object Controllers {
    var packaged: List<PackageController> = emptyList()
    private val builtIn: List<Controller> get() = listOf(TouchPad, ExternalMidi)
    val all: List<Controller> get() = builtIn + packaged
    /** Older ids a package now answers to: the Kotlin Grid's, and from 0.23.7 the Kotlin Tonnetz's (the
     *  package has the same settings, by the same names, so they carry over: PackageController.load). */
    private val moved = mapOf(KeyGrid.id to "pkg:patrickreynolds/grid", Tonnetz.id to "pkg:patrickreynolds/tonnetz")
    /** The ids a controller had before: where its settings were kept, if it has none of its own yet. */
    fun formerIds(id: String) = moved.filterValues { it == id }.keys
    fun byId(id: String?): Controller = all.firstOrNull { it.id == (moved[id] ?: id) } ?: TouchPad
    /** A package's name, told apart from a built-in controller of the same name. */
    fun nameFor(name: String) = if (builtIn.any { it.name == name }) "$name (package)" else name
}
