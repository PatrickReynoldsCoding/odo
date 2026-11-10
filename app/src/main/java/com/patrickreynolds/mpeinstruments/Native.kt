package com.patrickreynolds.mpeinstruments

import android.media.midi.MidiDevice

/** The C++ side (Synth.cpp). One synth for the whole process. */
object Native {
    init { System.loadLibrary("mpeandroid") }

    external fun nativeStart(): Boolean
    external fun nativeStop()

    external fun nativeAttachMidi(device: MidiDevice, port: Int): Boolean
    external fun nativeDetachMidi()
    /** One MIDI message from the on-screen pad (UI thread only). */
    external fun nativeTouch(status: Int, d1: Int, d2: Int)

    // A controller package (.odoc) running in the engine (wasm3): one at a time, UI thread only.
    external fun nativeCtlLoad(bytes: ByteArray): Boolean
    external fun nativeCtlError(): String
    external fun nativeCtlUnload()
    /** Its surface in dp; flags 1 = e-ink. */
    external fun nativeCtlInit(width: Float, height: Float, flags: Int)
    external fun nativeCtlInstrument(part: Int, centerNote: Float)
    /** phase: 0 down, 1 move, 2 up, 3 cancel; x, y in dp. */
    external fun nativeCtlTouch(id: Int, phase: Int, x: Float, y: Float, size: Float, force: Float)
    external fun nativeCtlSetting(index: Int, value: Double)
    external fun nativeCtlStop()
    /** A frame: its draw list if it changed (docs/formats/CONTROLLER_FORMAT.md), else null. */
    external fun nativeCtlTick(): ByteArray?
    /** Settings it changed itself (its editor, a button on its surface): index, value pairs, or null. */
    external fun nativeCtlTakeSettings(): DoubleArray?

    // The rail's settings model (engine mpe/rail.h), from a controller's manifest: the same rows and
    // steps as odo for computers. It needs no controller loaded. Any thread.
    /** A handle for a manifest (controller.json as UTF-8), parsed once; the same text gives the same handle. */
    external fun nativeRailOpen(manifest: ByteArray): Int
    /** The rail's groups and rows for these values, as UTF-8 JSON (rail::toJson): see RailGroup.parse. */
    external fun nativeRail(manifest: Int, values: DoubleArray): ByteArray
    /** Setting `index` one step on (dir 1) or back (-1). */
    external fun nativeRailStepped(manifest: Int, index: Int, value: Double, dir: Int): Double
    /** A slider's position 0..1 to the setting's value (snapped to its step), and back. */
    external fun nativeRailFromFraction(manifest: Int, index: Int, t: Double): Double
    external fun nativeRailToFraction(manifest: Int, index: Int, value: Double): Double
    /** The value of chip k. */
    external fun nativeRailChipValue(manifest: Int, index: Int, k: Int): Double
    /** A Scala file's bytes for the "scala" setting `index` (mpe/scala.h), as UTF-8 JSON: {"error": …}, or
     *  {"description": …, "set": [index, value, …]}, the settings that give the controller its tuning. */
    external fun nativeRailScala(manifest: Int, index: Int, file: ByteArray): ByteArray

    external fun nativeInstrumentCount(): Int
    /** field: 0 id, 1 group, 2 name, 3 description, 4 range ("low high" in Hz, or ""), 5 its own effects, 6 "1" if it sustains (mpe::sustains) */
    external fun nativeInstrumentField(index: Int, field: Int): String
    /** mpe::compareVersions: "1.10.0" > "1.9.2"; < 0, 0 or > 0. */
    external fun compareVersions(a: String, b: String): Int

    external fun nativeSetInstrument(index: Int)
    external fun nativeSetPartInstrument(part: Int, index: Int)
    /** Perf.toArray() */
    external fun nativeSetPerformance(perf: FloatArray)
    /** Sounding voices, 8 floats each (part, finger, note, gate, pitch, pressure, slide, level); returns the count. */
    external fun nativeVoices(out: FloatArray): Int
    /** Held fingers, 5 floats each (channel, note, pitch, pressure, slide); returns the count. */
    external fun nativeFingers(out: FloatArray): Int
    external fun nativeSetVolume(v: Float)
    external fun nativeSetTranspose(semitones: Int)
    external fun nativeSetBendRange(semitones: Float)
    external fun nativeSetWatchdogMs(ms: Int)
    external fun nativePanic()
    external fun nativeResetLatencyTuner()
    external fun nativeStats(): DoubleArray

    // The phone as a controller (PhoneMods.kt).
    /** Debug: the phone's twist in degrees, clockwise positive (the other sources left alone). */
    external fun nativeSetTilt(degrees: Float)
    /** From the sensor thread: twist, tip, turn in degrees (unwrapped), shake 0..1. See Motion. */
    external fun nativeSetMotion(twist: Float, tip: Float, turn: Float, shake: Float)
    /** Per source (twist, tip, turn, shake) a target (GyroSettings.TARGETS index) and amount in its unit. */
    external fun nativeSetGyro(targets: IntArray, amounts: FloatArray, bendSnap: Float, bendReset: Boolean)
    /** mode: 0 off, 1 swell, 2 tone. Opens or closes the microphone, so not on the UI thread. Returns 0 off, 1 listening, -1 failed. */
    external fun nativeSetBreath(mode: Int, sensitivity: Float): Int
    /** Debug: a fixed breath level in place of the microphone's; < 0 = the microphone again. */
    external fun nativeDebugBreath(level: Float)
    /** Loop 0..3. op: 0 press, 1 undo, 2 clear, 3 pause (flag), 4 a layer's volume and mute, 5 snap to beats (flag), 6 mute (flag). */
    external fun nativeLoop(track: Int, op: Int, layer: Int, vol: Float, flag: Boolean)
    /** One loop's state, 15 floats: see Looper.kt. */
    external fun nativeLoopView(track: Int, out: FloatArray)
    /** What the gyro and breath are doing, 21 floats: see ModReading. */
    external fun nativeModifiers(out: FloatArray): Int

    // Debug: record the audio output (used by the emulator tests).
    external fun nativeCaptureStart(seconds: Float): Boolean
    external fun nativeCaptureProgress(): Double
    external fun nativeCaptureSave(path: String): Boolean

    // Preset files (docs/formats/PRESET_FORMAT.md): mpe::preset reads and writes them, with this app's ids
    // ("pkg:" controllers, sounds by their first alias) changed to and from the files'. UTF-8 bytes.
    /** The app's preset in the file's shape to the file's text; empty if it isn't one. */
    external fun nativePresetWrite(json: ByteArray): ByteArray
    /** A file's text to the file's shape with the app's ids; or "!" and why it can't be read. */
    external fun nativePresetRead(text: ByteArray): ByteArray
    external fun nativePresetFileName(name: ByteArray): ByteArray

    // Takes (docs/formats/TAKE_FORMAT.md): recorded from, and played into, the controller on screen
    // (mpe::take). UI thread only, as the controller. Text as UTF-8 bytes.
    /** Start recording what it plays; sounds: its parts' sounds, one a line; setup: what plays now as a
     *  preset file's text (the take keeps it); volume: the master volume (1 the usual level). False if no controller is loaded. */
    external fun nativeTakeRecord(sounds: ByteArray, setup: ByteArray, volume: Double): Boolean
    /** ms since the first finger while recording, 0 before it; -1 when not recording. */
    external fun nativeTakeRecording(): Double
    /** Stop: the take's file text with this name, or empty if nothing was played. */
    external fun nativeTakeStopRecording(name: ByteArray, madeBy: ByteArray): ByteArray
    external fun nativeTakeFileName(name: ByteArray): ByteArray
    /** A take file, as JSON: name, controller ("pkg:..."), written, length (ms), sections, settings, sounds, volume; or {"error"}. */
    external fun nativeTakeInfo(text: ByteArray): ByteArray
    /** A take's whole set-up as a preset file's text (for AppState.presetFromFile); "!" and why, if it can't be read. */
    external fun nativeTakePreset(text: ByteArray): ByteArray
    /** Play it: mode 0 listen, 1 play along. "" if every note plays; else why not ("!" first: it does not play). */
    external fun nativeTakePlay(text: ByteArray, mode: Int, values: DoubleArray, width: Float, height: Float,
                                center0: Float, center1: Float, speed: Double, loop: Int): ByteArray
    external fun nativeTakeStop()
    external fun nativeTakeRelayout(values: DoubleArray, width: Float, height: Float, center0: Float, center1: Float)
    external fun nativeTakeSpeed(speed: Double)
    /** Play section k round and round; -1: all of it. */
    external fun nativeTakeLoop(section: Int)
    /** playing (0/1), mode, position ms, length ms. */
    external fun nativeTakeState(): DoubleArray
    /** A frame: the take moves on; its ghost fingers over a w x h dp surface (a draw list), or null if none plays.
     *  colours, sizes: Tokens.Ghost's. */
    external fun nativeTakeTick(width: Float, height: Float, colours: IntArray, sizes: FloatArray): ByteArray?

    data class Instrument(val index: Int, val id: String, val group: String, val name: String, val description: String,
                          val lowHz: Float, val highHz: Float, val sound: String = "",
                          /** A held note keeps sounding (bowed, blown, sung, organs, most synths), or it fades on its own: mpe::sustains. */
                          val sustains: Boolean = true,
                          /** Its version; versions sit side by side, older ones kept to go back to (docs/formats/INSTRUMENT_FORMAT.md). */
                          val version: String = "", val newest: Boolean = true) {
        /** The middle of the instrument's range as a MIDI note, for the octave check. */
        val centerNote: Float get() = if (lowHz > 0 && highHz > 0)
            69f + 12f * (kotlin.math.ln(kotlin.math.sqrt(lowHz * highHz) / 440f) / kotlin.math.ln(2f)) else 60f
    }

    fun instruments(): List<Instrument> = (0 until nativeInstrumentCount()).map {
        val range = nativeInstrumentField(it, 4).trim().split(Regex("\\s+")).mapNotNull { v -> v.toFloatOrNull() }
        Instrument(it, nativeInstrumentField(it, 0), nativeInstrumentField(it, 1),
                   nativeInstrumentField(it, 2), nativeInstrumentField(it, 3),
                   range.getOrElse(0) { 0f }, range.getOrElse(1) { 0f }, nativeInstrumentField(it, 5),
                   nativeInstrumentField(it, 6) == "1", nativeInstrumentField(it, 7), nativeInstrumentField(it, 8) == "1")
    }
}
