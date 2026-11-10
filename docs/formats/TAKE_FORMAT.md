# Take files (`.odotake`)

A take is a performance kept as the **touches that made it**, not as sound: every finger down,
move and lift, at full rate, and the notes the controller played from them (the pitch with its
bends and vibrato, the pressure, the slide). Played back, a take sounds exactly as it was played,
and **ghost fingers** on the controller show how it was played. A take can also be **written by
hand**, as notes in bars and beats with gestures (bend, vibrato, swell, slide); odo works out
where the fingers go on the controller in use. How to write one is
[TAKE_AUTHORING.md](TAKE_AUTHORING.md).

Both apps record, read and play takes through the same code, `mpe::take`
(`engine/include/mpe/take.h`), so a take made by one plays in the other.

## In the apps

The rail's **App** group has **Takes**, after **App**. Its panel, the same in both apps:

| Heading | Rows |
|---|---|
| RECORD | **Record**; while recording, **Stop recording · 0:12**. Recording starts at the first finger down. Stopped, it is kept as "Take 1", "Take 2"... |
| YOURS | Each take: its name, and "0:38 · Grid · recorded" (or "written by hand"). Tap one to choose it. **Open a take file…** |
| THIS TAKE | **Listen** (it plays, with ghost fingers) or **Play along** (only the ghost fingers: you play). Tapped again: **Stop**. **Speed**: 50%, 75%, 100%. **Fingers**: Rings, Dots, Falling, Off (how the ghost fingers look; kept). **Loop**: "All of it", or one section round and round. **Use its settings** (play with how it was set up, without playing it). **Keep its settings as a new preset** (one of yours, named as the take, on view). **Share "…" as a file…**, **Delete "…"** (tap twice). |

A take is a take and a preset: it keeps everything a preset keeps (each side's controller and all
its settings, each part's sound and its version, the part's level, tone, drive, touch, release,
reverb and delay sends and arp, the shared reverb and delay, the tempo, the split, layering) and
the master volume. Not the phone's tilt and breath, which belong to the device, as for a preset.
Listen and Play along first put all of it on, as loading a preset does (not kept as a preset of
yours: no preset is on view after it), and so does **Use its settings**. A sound or controller
that is not here is said, as when a preset file is opened. The rail's Takes row says what is happening ("Recording 0:12",
"Listening: Greensleeves").

Takes are kept in `~/.local/share/odo/takes` on a computer and in the app's own folder on
Android. **Share** sends a take as a file (Android: the share sheet; a computer: a file dialog).
**Open a take file…** copies one in; on a computer a take file can also be dropped on the window,
or given on the command line (`odo FILE.odotake`, or `--take FILE`).

The ghost fingers: a glowing circle where each finger is, bigger with more force, with the note's
name in it and a fading trail behind it; a ring closing in where the next finger goes down; a thin
line across the top for how far it has played; the section's name and the take's words at the
top. Their colours and sizes are `ghost` in `ui/tokens.json` (dark and e-ink).
That is **Rings**, the first of the **Fingers** looks. **Dots**: a small dot for each finger (bigger
with more force) and for the next ones coming, a short trail, no rings. **Falling**: each next note
falls from the top of the surface to its key; a held one lights its key, with a bar above it of
the time it has left. **Off**: no fingers, only the line along the top and the words.

## The file

UTF-8 text, one thing to a line, at most 32 MB. Extension `.odotake`, MIME type
`text/vnd.odo.take`. A line starting with `#` is a comment; blank lines are ignored.

```
odo-take/1.1
name: Take 3
madeBy: odo 0.0.32 (computer)
volume: 1
preset: {"format":"odo-preset/1.1","split":0,"layered":false,"effects":{"bpm":110,"reverbSize":0.5,"reverbDamp":0.4,"delayBeats":0.75,"delayFeedback":0.35,"delayPingPong":true},"sides":[{"parts":[{"sound":"patrickreynolds/cello","version":"1.0.0","transpose":0,"volume":1,"brightness":1,"drive":0,"pressureCurve":1,"reverb":0.3,"delay":0,"release":0,"arp":{"on":false,"order":0,"rate":4,"gate":0.6,"octaves":1,"latch":false}}]}]}
controller: patrickreynolds/grid 2.8.0
settings: {"cols": 3, "layout": 0, "chromatic": 1, "slide": 1}
sound: patrickreynolds/cello
surface: 980 720
centers: 58.4 72

section 0 Verse
say 0 2000 Wiggle slowly

s 3200.5 slide 0
t 0 1 down 0.31415927 0.7 0.62
n 0 1 vol 0.82
n 0 1 on 0 60 0.62 0.48 0.62
t 8.333 1 move 0.3150 0.7012 0.63
n 8.333 1 ch 60.04 0.63 0.48
t 412.5 1 up 0.3161 0.7020 0
n 412.5 1 off
```

### The first line

`odo-take/<major>[.<minor>]`. A reader refuses a newer major ("This take was made by a newer odo")
and reads a newer minor, leaving out lines it does not know (with a warning each).

- `odo-take/1`: the set-up is the controller, its settings and the sounds. Played, each sound
  plays with its own effects.
- `odo-take/1.1`: adds `preset` and `volume`. An odo that reads only 1 plays a 1.1 take as a 1 take
  (it warns of the two lines and leaves them out).

### The header: `key: value`

| Key | Meaning |
|---|---|
| `name` | Shown in the list. |
| `by` | Who played or wrote it. |
| `about` | What it is, to read. May repeat: each is a line. |
| `madeBy` | Which app made it. Only shown. |
| `tempo`, `meter`, `key` | For a score (beats a minute, `3/4`, `D dorian`); shown for a recorded take. |
| `speed` | The speed it was played at, if not 1. |
| `side` | `0` to `15`: the lines after it are about that side (a computer's split window). Left out: side 0. |
| `controller` | The controller package's id, and its version: `patrickreynolds/grid 2.8.0`. |
| `settings` | A JSON object on one line: setting id to value, as a preset's `settings`. A recorded take has them all; a score names those it needs. |
| `sound` | A part's sound (instrument package id). One line per part, in order. |
| `surface` | The surface's width and height in dp when it was played. |
| `centers` | Each part's instrument middle (a MIDI note) when it was played. |
| `preset` | (1.1) The rest of the set-up, as a preset: a [preset file](PRESET_FORMAT.md)'s JSON on one line, without its `name` and `madeBy`, and without each side's `controller` and `settings` (those are the `controller` and `settings` lines). Its `sides` are the take's sides in order; each side's `parts` give each part's sound, version, level, effects and arp. `split`, `layered` and `effects` as in a preset. Once, before or after the `side` lines. |
| `volume` | (1.1) The master volume when it was played: 1 the usual level, 0 to 2. |

The take's whole set-up (`mpe::take::presetOf`) is its `preset`, with each side's `controller`,
`settings` and `sound` lines over it: the controller and settings named there, and a `sound` line
taking the place of that part's sound (and its version, if it differs). A take without a `preset`
line has that alone: its controller, the settings it names, and its sounds, each with its own
effects. In a `preset` line `format` and `sides` may be left out (a score that gives only the
effects); what it leaves out takes a preset's defaults.

### What happened: one line each

Times are milliseconds from the take's start (the first finger down), to the microsecond. Every
number is written so that it reads back exactly (a float to the bit).

| Line | Meaning |
|---|---|
| `t TIME FINGER PHASE X Y FORCE [SIZE]` | A touch. PHASE `down`, `move`, `up` or `cancel`. X and Y are **0 to 1 across and down the controller's surface** (so they mean the same on any screen); FORCE 0 to 1; SIZE in dp, if known. |
| `n TIME VOICE on PART PITCH PRESSURE SLIDE VELOCITY` | A note starts. PITCH in semitones (MIDI note numbers, fractions for bends); the rest 0 to 1. PART: which of the controller's parts (sounds). |
| `n TIME VOICE ch PITCH PRESSURE SLIDE` | A sounding note changes (a bend, vibrato, pressure, slide). |
| `n TIME VOICE off` | It is let go of. |
| `n TIME VOICE vol VOLUME` | Its own volume (`note_volume` of the controller interface); may come just before its `on`. |
| `s TIME ID VALUE` | A setting changed while it played (on the rail, or by the controller itself). |
| `section TIME NAME` | A section starts: what Loop offers. |
| `say TIME DURATION TEXT` | Words to show over the controller while it plays ("Lift your finger here"). |

FINGER and VOICE are the controller's own numbers. On a side other than 0 they are written
`SIDE:N` (`1:4`). The notes are what the controller sent (the `note_on`, `note_change`,
`note_off` and `note_volume` of [CONTROLLER_FORMAT.md](CONTROLLER_FORMAT.md)), before they were
turned into MIDI.

### A score

```
score
...
end
```

The lines between are a piece written by hand, in bars and beats: [TAKE_AUTHORING.md](TAKE_AUTHORING.md).
A take with a score and no touches or notes is expanded when it is played: odo touches a copy of
the controller in use all over to find where each note is, puts fingers there with the gestures,
and plays those touches through the controller to get the notes. Expanded (`tools/take expand`),
it is written with its touches and notes and keeps its score; a take with touches or notes plays
those, and its score is only kept.

## Playing it back

- **The notes are what sounds.** They go into the controller host on the side they were played
  on, as its own notes go (`ControllerHost::injectNote`), so the engine gets the very MIDI it got
  when the take was played: the same bytes, at the same times (tested:
  `engine/tests/test_take.cpp`). The sound is whatever that side plays now; Listen first puts the
  take's sound there if it is in the library.
- **The touches are what is shown.** Each finger from down to up is a ghost, at X and Y times the
  surface's size now.
- **Speed** stretches the time (not the pitch). **Loop** plays a section to the next section's start,
  and lets go of every note at each turn.
- **Play along** moves the ghosts and plays nothing; you play the controller as usual.

## Recording

- It starts at the first finger down, as a loop does: no silence before it. Fingers already down
  when Record is tapped, and their notes, are left out.
- Everything the controller is given and plays is kept: every touch at the rate the screen gives
  them, every note change. A change or a volume that says nothing new (the same numbers as before)
  is left out: it would send no MIDI, and a controller may send one every frame.
- The settings at the start go in the header; a change later is an `s` line.
- What plays when Record is tapped, as a preset (the one on view, or what plays now, as Share
  would send it), goes in the `preset` line, and the master volume in `volume`.
- Fingers still down at Stop are lifted, and notes still sounding let go of, at the end.
- On a computer every side of a split window is recorded, each its own `side`. Odo on Android has
  one side.

## Any controller

Nothing in a take is about one controller's layout: touches are fractions of the surface, notes
are pitches, and the controller is named by its id. A score's notes are found on whatever
controller plays it, by touching it (`take::Layout`), so a take written by hand plays on the Grid,
the touch pad, the Tonnetz or a controller made later, as long as it has the notes.

## Tools

`tools/take` checks a take and expands a score, on the controller packages in `controllers/build`:

```
tools/take check takes/*.odotake        every note as written? (exit 1 if not)
tools/take expand FILE -o OUT           a score expanded into touches and notes
tools/take notes FILE                   the notes it plays: time, bar and beat, name, bends, pressure
tools/take touches FILE                 its touches
  --controller FILE.odoc  --size WxH  --center N[,M]  --setting ID=VALUE
```

## Not yet

- A take recorded on a surface of another size or shape shows its ghosts at the same fractions of
  the surface. On a controller whose keys are a fixed size in dp (the Grid), they can then be over
  other keys than the ones that were played; the sound is still exact. Re-placing them by their
  notes is to come.
- Play along does not wait for you, nor say how close you came.
