package com.patrickreynolds.mpeinstruments

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.compose.BackHandler
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.patrickreynolds.mpeinstruments.ui.Edge
import com.patrickreynolds.mpeinstruments.ui.Eink
import com.patrickreynolds.mpeinstruments.ui.Ink
import com.patrickreynolds.mpeinstruments.ui.Line
import com.patrickreynolds.mpeinstruments.ui.MpeIcons
import com.patrickreynolds.mpeinstruments.ui.MpeTheme
import com.patrickreynolds.mpeinstruments.ui.Panel
import com.patrickreynolds.mpeinstruments.ui.LibraryScreen
import com.patrickreynolds.mpeinstruments.ui.LocalDevices
import com.patrickreynolds.mpeinstruments.ui.PlayHost
import com.patrickreynolds.mpeinstruments.ui.PartColors
import com.patrickreynolds.mpeinstruments.ui.PlayScreen
import com.patrickreynolds.mpeinstruments.ui.PresetSheet
import com.patrickreynolds.mpeinstruments.ui.SetupActions
import com.patrickreynolds.mpeinstruments.ui.SetupScreen
import com.patrickreynolds.mpeinstruments.ui.TextDim
import com.patrickreynolds.mpeinstruments.ui.Warn
import kotlinx.coroutines.delay

/**
 * Three tabs: Play (the controller in use and the way into it, full screen; what is loaded),
 * Library (presets and sounds) and Settings (the app and the phone). Everything you play with is on
 * the play screen's rail.
 */
class MainActivity : ComponentActivity() {
    private var ble: BleScanner? = null
    private val bleFound = mutableStateListOf<BluetoothDevice>()
    private var scanning by mutableStateOf(false)
    private var midiTick by mutableIntStateOf(0)            // bumped when devices come, go or connect
    private var padOpen by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        requestNeededPermissions()
        startForegroundService(Intent(this, SynthService::class.java))
        MidiConnector.init(applicationContext)
        AppState.init(applicationContext)
        Updater.checkIfDue(applicationContext)
        MidiConnector.onChange = { runOnUiThread { midiTick++ } }
        SynthService.onStopped = { runOnUiThread { finishAndRemoveTask() } }
        if (savedInstanceState == null) openPresetIn(intent)
        setContent {
            // Status and navigation bar icons dark on the e-ink theme's white, light on the dark one.
            LaunchedEffect(AppState.eink) {
                val style = if (AppState.eink) SystemBarStyle.light(android.graphics.Color.WHITE, android.graphics.Color.BLACK)
                            else SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
                enableEdgeToEdge(style, style)
            }
            MpeTheme { App() }
        }
    }

    // A preset file another app opened with odo or shared to it (PresetFiles): one of yours, on view.
    // A take file (Takes): one of your takes.
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); openPresetIn(intent) }
    private fun openPresetIn(intent: Intent?) {
        val uri = PresetFiles.uriIn(intent) ?: return
        val take = intent?.type == Takes.MIME || uri.lastPathSegment?.endsWith(".odotake") == true
        val said = if (take) Takes.open(this, uri) else PresetFiles.open(this, uri)
        android.widget.Toast.makeText(this, said, android.widget.Toast.LENGTH_LONG).show()
        intent?.action = null                     // not again when the activity is made again
    }

    override fun onPause() { ble?.stop(); scanning = false; Motion.setResumed(this, false); super.onPause() }
    override fun onResume() { super.onResume(); Motion.setResumed(this, true) }

    private fun requestNeededPermissions() {
        val want = mutableListOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        if (Build.VERSION.SDK_INT >= 33) want += Manifest.permission.POST_NOTIFICATIONS
        val missing = want.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), 1)
    }

    private fun scanBle() {
        if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) { requestNeededPermissions(); return }
        val scanner = ble ?: BleScanner(this) { d -> runOnUiThread { if (bleFound.none { it.address == d.address }) bleFound += d } }.also { ble = it }
        if (!scanner.available) return
        scanner.start()
        scanning = true
    }

    /** The play screen takes the whole screen: no system bars, screen kept on, and on its side for the touch pad. */
    private fun showPad(open: Boolean) {
        padOpen = open
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (open) {
            requestedOrientation = when {
                AppState.controller.landscape -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                AppState.controller.upright -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                else -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            }
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun stopApp() {
        stopService(Intent(this, SynthService::class.java))
        finishAndRemoveTask()
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun App() {
        var tab by remember { mutableIntStateOf(0) }
        var stats by remember { mutableStateOf<Stats?>(null) }
        var presets by remember { mutableStateOf(false) }
        LaunchedEffect(Unit) { while (true) { stats = Stats.from(Native.nativeStats()); delay(250) } }
        val devices = SetupActions(bleFound, scanning, ::scanBle, ::stopApp)
        if (padOpen) {
            BackHandler { showPad(false) }
            // Gyro on: keep the pad the way up it is, or a firm turn would flip the screen mid-note.
            val landscape = androidx.compose.ui.platform.LocalConfiguration.current.orientation ==
                android.content.res.Configuration.ORIENTATION_LANDSCAPE
            val wantsSide = AppState.controller.landscape
            val upright = AppState.controller.upright
            LaunchedEffect(AppState.mods.gyroOn, landscape, wantsSide, upright) {
                requestedOrientation = when {
                    // The Grid's squares never turn: only the rail's labels follow the phone (PlayHost).
                    upright -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                    AppState.mods.gyroOn && (landscape || !wantsSide) -> ActivityInfo.SCREEN_ORIENTATION_LOCKED
                    wantsSide -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                    else -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                }
            }
            CompositionLocalProvider(LocalDevices provides devices) { PlayHost { showPad(false) } }
            return
        }
        val tick = midiTick
        Scaffold(
            containerColor = Ink,
            topBar = {
                TopAppBar(
                    colors = TopAppBarDefaults.topAppBarColors(containerColor = Ink),
                    title = {
                        Column {
                            Text(AppState.presetName.ifEmpty { "odo" }, fontWeight = FontWeight.SemiBold, fontSize = 20.sp)
                            Status(stats, tick)
                        }
                    },
                    actions = { TextButton(onClick = { presets = true }) { Text("Presets", color = PartColors[0]) } },
                )
            },
            bottomBar = {
                NavigationBar(containerColor = Panel, modifier = if (Eink) Modifier.border(BorderStroke(Edge, Line)) else Modifier) {
                    val items = listOf(
                        "Play" to Icons.Filled.PlayArrow, "Library" to MpeIcons.Sliders, "Settings" to Icons.Filled.Settings)
                    items.forEachIndexed { i, (label, icon) ->
                        NavigationBarItem(selected = tab == i, onClick = { tab = i }, icon = { Icon(icon, label) }, label = { Text(label) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = if (Eink) Color.White else PartColors[0], selectedTextColor = PartColors[0],
                                indicatorColor = if (Eink) Color.Black else PartColors[0].copy(alpha = 0.16f)))
                    }
                }
            },
        ) { pad ->
            Box(Modifier.padding(pad).fillMaxSize()) {
                CompositionLocalProvider(LocalDevices provides devices) {
                    when (tab) {
                        0 -> PlayScreen(stats, openPlay = { showPad(true) }, openSetup = { tab = 2 })
                        1 -> LibraryScreen()
                        else -> SetupScreen(stats, devices, tick)
                    }
                }
            }
        }
        if (presets) PresetSheet { presets = false }
    }

    @Composable
    private fun Status(stats: Stats?, @Suppress("UNUSED_PARAMETER") tick: Int) {
        val name = MidiConnector.connectedName
        val waiting = MidiConnector.reconnecting
        val (color, text) = when {
            name.isNotEmpty() -> PartColors[1] to name
            waiting.isNotEmpty() -> Warn to "Waiting for $waiting"
            else -> TextDim to AppState.controller.name
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(8.dp).clip(CircleShape).background(color))
            Text("  $text", color = TextDim, fontSize = 12.sp, maxLines = 1)
            val lat = stats?.let { if (it.events > 0) it.totalMs else it.outputLatencyMs }
            if (lat != null && lat > 0) Text("  ·  ${lat.toInt()} ms", color = TextDim, fontSize = 12.sp)
        }
    }
}
