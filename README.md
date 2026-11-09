# odo

odo is a [MESH](docs/vision/MESH.md) (Multimodal Expression Sensor Host) for Android: an app that plays the Faust instruments from `mpe-instruments` from a
Seaboard BLOCK (or any MPE controller) over USB or Bluetooth LE, with low latency: about
25 ms from note to sound on a POCO F2 Pro, measured ([LATENCY.md](docs/android/LATENCY.md)).

```
Seaboard -> android.media.midi -> core (clean) -> parts (play / arp) -> Faust voices -> effects -> Oboe
```

What it does:
- **62 instruments in six packs** (Classical; Keys & Organs; Plucked & Struck; Winds, Brass & Voices;
  Synthesizers; Percussion), each finger its own voice (MPE): pitch glides, pressure swells, slide
  shapes the tone. **Each instrument owns how it is meant to be heard**: its reverb and delay
  sends, tone, drive, touch curve and release come from the instrument (`declare sound`), and its
  character effects (rotating speaker, ensemble chorus, cabinet, small room) run inside it.
  odo keeps what belongs to the player: the hall and the tempo delay, the arpeggiator, split and
  layer, pitch assist, the controllers and the phone's tilt and breath.
- **Split and layer**: two instruments, left and right of a split point (or "learn" it from a
  key), or both at once with their own octave and level.
- **An MPE arpeggiator** per part: up, down, up-down, as played, random, chord; rate, gate,
  octaves, latch. Each arpeggiated note keeps following the finger that holds it, so pressing
  swells the arpeggio and sliding changes its tone. Arpeggiate the left hand, play the right.
- **Pitch assist**: a finger that comes to rest is drawn onto the nearest note of a scale;
  glides and vibrato are left alone.
- **Sound**: per part brightness, drive, touch response, reverb and delay sends; a Zita reverb
  and a tempo-synced ping-pong delay. Tap tempo.
- **Presets**: ten built in, save your own, and share them as files between your phone and computer ([PRESET_FORMAT.md](docs/formats/PRESET_FORMAT.md)).
- **A fullscreen touch pad**: play with your fingers, no controller needed. Columns of notes in
  a key and scale, slide to glide, higher up for louder and brighter; each finger its own MPE
  voice, so the arp, split and effects apply. A Seaboard played at the same time shows there too.
  Drag the note strip along the bottom sideways to move the notes (Lock on the rail stops that);
  six colour themes and soft, clear or bold lines.
- **The Hex keyboard**: hexagonal keys in an isomorphic layout, as on a Lumatone (Wicki-Hayden,
  Harmonic table, Bosanquet or your own steps), in 12, 19, 22, 31, 41 or 53 equal steps, just
  intonation, Pythagorean or meantone; coloured by note name, sharps and flats, or pitch. Each key
  plays as the Grid's squares do, and pressing harder adds pressure
  ([HEX_CONTROLLER.md](docs/plans/HEX_CONTROLLER.md)).
- **An e-ink look** (Setup): black on white, thick borders, no colour or motion, and the pad only
  redraws when something changes.
- **The phone as a controller** (experimental: Setup → Experimental), on every instrument, from
  the pad or a Seaboard:
  - **Tilt** (the gyro): turn the phone like a steering wheel for vibrato (only the wobble
    counts, so a slant stays in tune), a bend (each note starts in tune wherever the phone is),
    or a change of tone.
  - **Breath** (the microphone): blow across its bottom edge. Swell makes breath the loudness, so
    fingers only pick the notes. Tone opens the sound from dark to bright, like a wah.
  - Both show what they are doing: a gauge with a little phone that turns as yours does, and a
    breath meter (in Setup, and in the pad's Tilt and Breath panels, which join the pad's rail
    once one is on), plus a live readout along the top of the pad, orbs that move with the bend,
    and a glow that rises as you blow.
  - Only the microphone's loudness below 150 Hz is measured; nothing is recorded. Through the
    speaker the app ignores its own bass, so a note cannot keep itself playing.
- **An octave warning** when the controller's notes are far from the instrument's range.
- **Updates from inside the app** (Setup → Updates): it asks GitHub for the latest release when
  it opens (at most once a day; can be turned off), downloads the APK, checks that it is this app,
  newer, and signed with the same key, and hands it to Android, which asks you to confirm. The
  first time, Android also asks you to allow odo to install apps. GitHub is the only
  place the app ever connects to.
- Bluetooth reconnects by itself (can be turned off); Stop turns everything off.

Status: the engine is tested on the desktop (sanitizers included), the app on an emulator with
an emulated Seaboard over USB and Bluetooth LE, and on a real phone with the real Seaboard over
USB. Bluetooth with the real Seaboard is not tested yet.

## Install

Download `odo-<version>.apk` (`MPE-Instruments-<version>.apk` up to 0.20.0) from the [Releases](../../releases) page on the phone and
open it (allow installing from your browser or file manager when Android asks). Android 12 or
newer.

From 0.5.0 on, later versions can be installed from inside the app (Setup → Updates).

## Releasing

For the in-app updater to offer and accept a release:

- tag it `vX.Y.Z` and publish it (drafts and pre-releases are skipped);
- attach one `.apk`, the release build (`gradle assembleRelease`), signed with the same key as
  before: the app refuses an update signed with another key, and so does Android;
- raise `versionCode` and `versionName` in `app/build.gradle.kts`.

To try the updater, build an older-numbered release copy, install it, and update it from the app:
`gradle assembleRelease -Pmpe.versionCode=3 -Pmpe.versionName=0.3.9`.

## Build and test

Engine, on the desktop (needs `cmake`, a C++17 compiler, and `faust`):

    git clone --recurse-submodules https://github.com/PatrickReynoldsCoding/odo
    cd odo/engine
    cmake -S . -B build && cmake --build build -j && (cd build && ctest --output-on-failure)
    ./build/tools/bench 8 96 3        # cost of 8 fingers, 96-frame callbacks

Android app (needs Android Studio or the command-line SDK with NDK, JDK 17, and `faust` on the
machine running Gradle, because the build turns each `.dsp` into C++):

    source tools/env.sh                      # paths of the local toolchain; edit for your machine
    gradle assembleDebug                     # the app, and the fake Seaboard (fakeseaboard/)
    gradle assembleRelease                   # signed if a key is set up (MPE_SIGNING, see app/build.gradle.kts)

End to end on an emulator (boot one first, for example
`emulator -avd <name> -no-window -no-audio`):

    python3 -I tools/emulator_test.py

Bluetooth LE MIDI, with a second emulator as the Seaboard (emulators on one machine share a
virtual Bluetooth radio):

    emulator -avd <name2> -port 5556 -no-window -no-audio &
    python3 -I tools/emulator_test.py --serial emulator-5554 --ble-peer emulator-5556

`fakeseaboard/` is a small debug app that registers a virtual MIDI device called "Seaboard BLOCK
(emulated)". The app connects to it exactly as it would to the real controller (MidiManager,
then AMidi in the audio callback). The test drives it over adb: known notes on several
instruments (the app records its own output and the pitch is measured), 30 s of the recorded
session, a Bluetooth-style drop with the watchdog on and off, unplug and replug mid-note,
instrument switching, screen off and Doze, the audio server dying, a stress run, and an app
restart. In `--ble-peer` mode the same app is a Bluetooth LE MIDI device (the BLE-MIDI GATT
service) on the other emulator: scan and connect, notes and the session over Bluetooth, the link
lost mid-note, and automatic reconnection. The debug build of the app has a `DebugReceiver` for
this; release builds do not include it.

Minimum Android 12 (API 31). Instruments are compiled in from the `third_party/mpe-instruments`
submodule: add a `.dsp` there and rebuild.

## Layout

```
engine/    C++: clean-up core + voice engine + generated Faust instruments. No Android code.
app/       Kotlin: MIDI devices, service, UI. Calls engine through JNI.
tests/     host tests for engine; emulator tests for app
captures/  recorded Seaboard MIDI to replay in tests
```

## Docs

The file formats, the vision and plans, and the phone notes are in [docs/](docs/README.md).

## Test plan: what runs where

The aim is that the phone is only needed for the last two rows.

| Layer | How it is tested | Needs |
|---|---|---|
| Clean-up logic (held notes, panic, silence watchdog) | Unit tests on the desktop; engine is plain C++ | nothing |
| Voice engine and instruments | Desktop build renders audio to a file; test checks pitch, level, no clicks, no hanging notes (as `tests/test_instruments.py` does in `mpe-instruments`) | nothing |
| Real Seaboard behaviour | Replay a recorded capture (from `aseqdump`) through the engine; same file every run | one capture |
| App on Android (MIDI connection, JNI, service, audio, safety) | `tools/emulator_test.py` with the fake Seaboard; the app records its own output and the test measures it | emulator |
| USB permission, real Bluetooth, real latency | Real phone with the Seaboard | phone |

Emulator limits, so nothing surprises us: it cannot see the Seaboard over USB or
Bluetooth, and its audio latency says nothing about a phone's.
