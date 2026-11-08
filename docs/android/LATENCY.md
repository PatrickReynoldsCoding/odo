# Latency

Goal: show that an Android phone can play an MPE controller with latency close to
iOS and macOS. This file says what the app does about it, what is measured, and what
has **not** been measured yet. Nothing here claims a number that was not measured.

## Where the time goes

```
finger -> Seaboard scan ----- USB or BLE ----- Android MIDI service -> [app callback] -> audio buffer -> DAC -> air
 (device)        (transport)                         (OS)                 (us)         (OS + hardware)
```

| Stage | Who controls it | What the app does |
|---|---|---|
| Controller scan time | the device | nothing to do |
| USB transfer | the USB spec (1 ms polling at full speed, 125 us at high speed) | nothing to do |
| BLE transfer | the BLE connection interval, set by the phone and the device | see "Bluetooth" below |
| Android MIDI service -> app | Android | read the port from the audio thread (below) |
| Wait for the next audio callback | burst size | smallest burst the device gives |
| Our rendering | us | Faust code; measured, about 1-10% of a callback on the desktop (see `engine/tools/bench`) |
| Audio buffer and hardware | Android audio HAL | Oboe low-latency + exclusive (MMAP), native rate, 2 bursts, auto-tuned |

## What the app does

1. **Audio output: Oboe, `PerformanceMode::LowLatency`, `SharingMode::Exclusive`, `Usage::Game`.**
   Per Google's Oboe guide, the default mode can cost over 200 ms and shared mode adds to
   the buffer. If the device refuses exclusive mode the stream still opens, in shared mode;
   the status screen says which one was granted and whether MMAP is in use.
2. **Native sample rate.** No `setSampleRate`. Forcing 44.1 kHz on a 48 kHz device sends audio
   through a slower path. The engine takes whatever rate the stream gives it.
3. **Small buffer, tuned automatically.** The buffer starts at 1 burst; each underrun adds one.
   "Reset latency tuner" goes back to 1 burst. This is our own small tuner (`Synth::tune`), not
   Oboe's `LatencyTuner`: that one has a floor of 2 bursts and counts the underrun many phones
   report while a stream starts, so it never stays at 1. Ours ignores the first half second.
4. **16-bit output.** Many vendor HALs offer their fastest output ("raw", `FAST|RAW`) only for
   PCM_16, so the stream asks for I16 and the engine's float output is converted.
5. **The audio thread pins itself to the fast cores.** Left alone, the scheduler puts a light
   real-time thread on a little core, where a burst of notes takes far more of the callback.
6. **MIDI is read inside the audio callback**, with the NDK AMidi API (`AMidiOutputPort_receive`
   is non-blocking, and Google says it is safe in an audio callback). There is no separate MIDI
   thread, no queue between threads on the hot path, and no Java or JNI call per message. A
   message waits at most until the next callback, which is one burst.
7. **Callback rules.** The engine never allocates, locks, or logs in `render`. Instrument
   switching is built off the audio thread and swapped in with an atomic pointer.
8. **The stream is watched.** If the audio stream reports an impossible latency (it happens
   after the audio server restarts: the stream keeps running but its timestamps are seconds
   off) a health check reopens it. The status screen counts these restarts.
9. **No Java `MidiReceiver`.** That path goes through Binder, a Java thread and the garbage
   collector, and is the usual source of "Android MIDI is laggy" reports.

## What the status screen measures

| Line | Meaning |
|---|---|
| MIDI -> synth | time from the timestamp Android put on the message to the start of the callback that played it. Includes the wait for the callback. |
| synth -> speaker | Oboe's `calculateLatencyMillis()`: buffer plus hardware, from the stream's own timestamps |
| total | the two added. This is the latency that this app and Android add. |
| underruns | glitches. If this grows, the tuner raises the buffer. |
| cpu load | time in `render` divided by the audio produced. Over 100% means dropouts. |

The engine tests check the first line's logic: an event that arrives during a callback is played
by the next one, so the engine adds at most one callback (`test_engine`, "engine adds at most one
callback").

## What is NOT measured yet, and how to measure it

- **The whole chain, finger to sound.** The total above starts when Android receives the note.
  It leaves out the controller's scan time and the USB or BLE transfer. A true figure needs an
  outside measurement: record the key strike and the sound with one microphone or a second
  phone's slow-motion video, and count the frames. Do this for USB and for BLE, on at least
  two phones, and write the numbers in the table below.
- **Whether the AMidi timestamp is the arrival time we assume.** The app uses it only if it is
  within one second of "now", otherwise it counts the message as arriving at the callback.
  Check on a real phone that "MIDI -> synth" is a believable few milliseconds and not zero.
- **Whether exclusive/MMAP is granted** on the target phone. The status screen shows it.
- **Bluetooth connection interval.** The app cannot set it, but Android's own Bluetooth MIDI
  service already asks for the shortest one: its log shows
  `requestConnectionPriority(CONNECTION_PRIORITY_HIGH):true` on every connection (seen on the
  emulator). The interval the phone and the Seaboard actually agree on still needs measuring.

## What the emulator says (and does not)

The emulator runs the whole chain (`tools/emulator_test.py`), but its audio is a software
device: burst 960 frames (20 ms), shared mode, no MMAP, about 110-120 ms output latency. Those
numbers say nothing about a phone. What the emulator does show: every MIDI message reaches the
engine within the callback after it arrives, none are dropped, and the engine's CPU use is
small. The latency work can only be judged on real phones.

## Results

| Phone | Android | Link | Audio path | burst/buffer | MIDI -> synth | synth -> speaker | finger -> sound (outside) |
|---|---|---|---|---|---|---|---|
| Xiaomi POCO F2 Pro (Snapdragon 865) | 12 (API 31) | USB, real Seaboard BLOCK | AAudio legacy, shared, fast track (no MMAP: `aaudio.mmap_policy=1`) | 192 / 192 | avg 2.4 ms, max 6.8 ms | 24-27 ms (speaker and wired headphones alike) | not measured yet |

What the POCO F2 Pro taught us (2026-10-27):

- Start of the day: 31 ms out (float, 2 bursts). Now 24-27 ms: 1 burst, kept by our own tuner.
- The phone has a `FAST|RAW` output with a 144-frame HAL buffer, but its audio policy never
  routes an app there: not with 16-bit, any usage, content type or sharing mode
  (`tools/aaudio_probe.c` sweeps these over adb with no reinstall). Wired headphones gave the
  same latency as the speaker, so the delay is in the driver/DSP, not speaker protection.
- The audio thread ran on a little core (CPU 3). Pinned to the fast cores, the CPU peak with
  the recorded session went from 78% to 36% of a callback.
- Notes far above an instrument's range (the Seaboard was octave-shifted up ~5 octaves) all
  sound at one pitch (the `freq` control tops out at 5000 Hz) and pushed the CPU peak to 123%.

## Ideas, not done

- Android Dynamic Performance Framework (`PerformanceHintManager`) hints for the audio thread, API 33+.
- Splitting the callback at the event's sample position so a note starts mid-burst, not at the start of it.
- A full-duplex Oboe loopback test (OboeTester has one) to give the audio-only round trip for each phone.
- ~~Compare `Usage::Game` and `Usage::Media`~~: no difference on the POCO F2 Pro.
