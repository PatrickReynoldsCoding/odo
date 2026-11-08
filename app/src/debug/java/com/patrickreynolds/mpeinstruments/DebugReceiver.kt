package com.patrickreynolds.mpeinstruments

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.patrickreynolds.mpeinstruments.controllers.Controllers
import com.patrickreynolds.mpeinstruments.controllers.ExternalMidi
import com.patrickreynolds.mpeinstruments.controllers.LegacyTouchPad
import java.io.File

/**
 * Test hooks, debug builds only. Results go to logcat with the tag "mpe-test".
 *   adb shell am broadcast -n com.patrickreynolds.mpeinstruments/.DebugReceiver -a RECORD --ef seconds 5 --es name note
 *   ... -a STATS
 *   ... -a SET --es instrument strings/cello --ei watchdog 1500 --ei transpose 0 --ef volume 0.8 --ez eink false
 *       --es controller midi|touchpad picks the controller; transpose and bend are External MIDI's;
 *       --es pad '{"split":true,"arp0":{"arp":true,"rate":4}}' sets the touch pad's split and arps (as a preset).
 *   ... -a PANIC
 *   ... -a BLE_CONNECT --ef seconds 15     scan for a BLE MIDI device, connect to the first found
 *   ... -a PHONE --ei tilt 2 --ef amount 2 --ef angle 16 --ei breath 1 --ef level 0.5 --ef sensitivity 0.5
 *       tilt and breath: modes as in PhoneMods; `angle` replaces the gyro (--ez gyro true gives it back);
 *       `level` replaces the microphone (-1 gives it back). Logs what they are doing (MODS alone just logs).
 * Recordings are saved to /sdcard/Android/data/com.patrickreynolds.mpeinstruments/files/<name>.wav
 */
class DebugReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            "RECORD" -> {
                val seconds = intent.getFloatExtra("seconds", 5f)
                val name = intent.getStringExtra("name") ?: "capture"
                val dir = context.getExternalFilesDir(null) ?: context.filesDir
                if (!Native.nativeCaptureStart(seconds)) { Log.w(TAG, "RECORD refused: already recording"); return }
                Log.i(TAG, "RECORD started $name ${seconds}s")
                Thread {
                    val deadline = System.currentTimeMillis() + (seconds * 1000).toLong() + 10_000
                    while (Native.nativeCaptureProgress() < 1.0 && System.currentTimeMillis() < deadline) Thread.sleep(50)
                    val file = File(dir, "$name.wav")
                    val ok = Native.nativeCaptureSave(file.absolutePath)
                    Log.i(TAG, if (ok) "RECORD saved ${file.absolutePath}" else "RECORD failed $name (progress ${Native.nativeCaptureProgress()})")
                }.start()
            }
            "STATS" -> {
                val s = Stats.from(Native.nativeStats())
                Log.i(TAG, "STATS connected=\"${MidiConnector.connectedName}\" midiState=${s.midiState} midiBytes=${s.midiBytes} " +
                    "events=${s.events} dropped=${s.dropped} panics=${s.panics} voices=${s.voices} " +
                    "ageAvgMs=${s.eventAgeAvgMs} ageMaxMs=${s.eventAgeMaxMs} outLatencyMs=${s.outputLatencyMs} " +
                    "rate=${s.sampleRate} burst=${s.burstFrames} buffer=${s.bufferFrames} xruns=${s.xruns} " +
                    "exclusive=${s.exclusive} path=${s.pathName.replace(' ', '_')} loadAvg=${s.loadAvg} loadPeak=${s.loadPeak} restarts=${s.streamRestarts} " +
                    "arpSteps=${s.arpSteps} noteCenter=${s.noteCenter} parts=" +
                    FloatArray(64 * 8).let { v -> (0 until Native.nativeVoices(v)).joinToString(",") { "${v[8 * it].toInt()}:${v[8 * it + 2].toInt()}" } })
            }
            "SET" -> {
                // Through AppState, so the screen and the saved settings agree with what is playing.
                AppState.init(context)
                intent.getStringExtra("instrument")?.let { id ->
                    if (AppState.instruments.any { it.id == id }) AppState.updatePart(0) { it.copy(instrument = id) }
                    else Log.w(TAG, "unknown instrument $id")
                }
                if (intent.hasExtra("watchdog")) Native.nativeSetWatchdogMs(intent.getIntExtra("watchdog", 0))
                intent.getStringExtra("controller")?.let { AppState.switchController(Controllers.byId(it)) }
                if (intent.hasExtra("transpose")) ExternalMidi.update { it.copy(transpose = intent.getIntExtra("transpose", 0)) }
                if (intent.hasExtra("volume")) AppState.changeVolume(intent.getFloatExtra("volume", 0.8f))
                if (intent.hasExtra("bend")) ExternalMidi.update { it.copy(bendRange = intent.getFloatExtra("bend", 48f)) }
                // --es grid '<KeyGrid settings json>' sets the grid's settings.
                intent.getStringExtra("grid")?.let { j ->
                    com.patrickreynolds.mpeinstruments.controllers.KeyGrid.load(org.json.JSONObject(j))
                    AppState.controllerChanged(com.patrickreynolds.mpeinstruments.controllers.KeyGrid, true) }
                // --es pad '<touch pad settings json>' (the package's, or the Kotlin pad's of before 0.26).
                intent.getStringExtra("pad")?.let { j ->
                    val pad = Controllers.byId(LegacyTouchPad.PACKAGE)
                    pad.loadPreset(org.json.JSONObject(j)); AppState.controllerChanged(pad) }
                if (intent.hasExtra("eink")) AppState.changeEink(intent.getBooleanExtra("eink", false))
                // --es perf '<Perf json>' sets the whole sound (parts, layer, effects); --es preset <name> loads one.
                intent.getStringExtra("perf")?.let { j -> AppState.update { Perf.from(org.json.JSONObject(j)) } }
                intent.getStringExtra("preset")?.let { n -> AppState.allPresets.firstOrNull { it.name == n }?.let { AppState.load(it) } }
                Log.i(TAG, "SET done")
            }
            "PANIC" -> { Native.nativePanic(); Log.i(TAG, "PANIC done") }
            "PHONE" -> {
                AppState.init(context)
                PhoneControl.forceBreath = true                    // breath without the microphone permission
                if (intent.hasExtra("angle")) {
                    Motion.debugAngle = intent.getFloatExtra("angle", 0f)
                    Native.nativeSetTilt(if (AppState.mods.source(0).invert) -Motion.debugAngle!! else Motion.debugAngle!!)
                }
                if (intent.getBooleanExtra("gyro", false)) Motion.debugAngle = null
                if (intent.hasExtra("level")) Native.nativeDebugBreath(intent.getFloatExtra("level", -1f))
                AppState.updateMods { m ->
                    var n = m.copy(breath = intent.getIntExtra("breath", m.breath).coerceIn(0, 2),
                                   sensitivity = intent.getFloatExtra("sensitivity", m.sensitivity))
                    // --ei tilt N: the twist, as before the gyro (0 off, 1 vibrato, 2 bend, 3 tone).
                    // --ei source S --ei target T: any source (0 twist, 1 tip, 2 turn, 3 shake) and target.
                    val src = if (intent.hasExtra("tilt")) 0 else intent.getIntExtra("source", -1)
                    if (src in 0..3) {
                        val t = if (intent.hasExtra("tilt")) when (intent.getIntExtra("tilt", 0)) {
                            1 -> GyroSettings.VIBRATO; 2 -> GyroSettings.BEND; 3 -> GyroSettings.TONE; else -> 0 }
                            else intent.getIntExtra("target", 0).coerceIn(GyroSettings.TARGETS.indices)
                        val a = intent.getFloatExtra("amount", GyroSettings.defaultAmount(t))
                        n = n.withSource(src) { GyroSource(t, GyroSettings.clampAmount(t, a)) }
                    }
                    if (intent.hasExtra("snap")) n = n.copy(bendSnap = intent.getFloatExtra("snap", 0f))
                    n
                }
                // The gyro only runs while the app is on screen; from adb it may not be.
                if (intent.hasExtra("tilt") || intent.hasExtra("source")) Motion.forceOn(context, AppState.mods.gyroOn)
                logMods()
            }
            "MODS" -> logMods()
            "BLE_CONNECT" -> {
                val seconds = intent.getFloatExtra("seconds", 15f)
                var done = false
                lateinit var scanner: BleScanner
                scanner = BleScanner(context.applicationContext) { device ->
                    if (done) return@BleScanner
                    done = true
                    scanner.stop()
                    Log.i(TAG, "BLE_CONNECT found ${device.address}, connecting")
                    MidiConnector.connectBluetooth(device)
                }
                if (!scanner.available) { Log.w(TAG, "BLE_CONNECT: Bluetooth is off"); return }
                scanner.start()
                Log.i(TAG, "BLE_CONNECT scanning")
                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                    if (!done) { scanner.stop(); Log.w(TAG, "BLE_CONNECT: nothing found in ${seconds}s") }
                }, (seconds * 1000).toLong())
            }
            else -> Log.w(TAG, "unknown command ${intent.action}")
        }
    }

    private fun logMods() {
        Thread {
            Thread.sleep(150)                                      // let a callback or two pick the change up
            val b = FloatArray(12)
            Native.nativeModifiers(b)
            Log.i(TAG, "MODS tilt=${b[0]} bend=${b[1]} slide=${b[2]} breath=${b[3]} held=${b[4]} micDb=${b[5]} floorDb=${b[6]} " +
                "onDb=${b[7]} fullDb=${b[8]} micLevel=${b[9]} echoDb=${b[10]} micState=${b[11]} mods=${AppState.mods.json()}")
        }.start()
    }

    companion object { private const val TAG = "mpe-test" }
}
