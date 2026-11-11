# Controller format, version 1 (draft)

How a MESH controller is packaged, so that the same controller plays the same way on every
touch-screen system: Android, iOS, and Windows, macOS and Linux computers with touch
screens. The companion to [INSTRUMENT_FORMAT.md](INSTRUMENT_FORMAT.md).

Status: version 1, in use since 2026-11-06. odo on Android and odo for computers
(`desktop/`) both run `.odoc` files through the engine's `mpe::ControllerHost` (wasm3). The
Tonnetz (`controllers/tonnetz/`) is the first; `controllers/build.sh` builds them, and
`controllers/include/mesh.h` is the interface for C.

## In short

A controller is a small WebAssembly program and a manifest. A MESH gives it touches and
time, and draws what it asks for. It sends back MPE notes. One implementation runs on
every system, the way one Faust file does for instruments.

## Where controllers run

- **On touch screens only.** A controller needs fingers. A system without a touch screen
  plays instruments from a hardware MPE controller, or from a phone through MIDI out.
- **A controller says what it needs** (several fingers, touch size or force). A MESH that
  can't give it shows the controller as unavailable, and why.
- **Sensors belong to the MESH, not the controller** ([VISION_PLAN.md](../vision/VISION_PLAN.md)
  decision 5). A phone MESH has tilt and breath; a computer MESH can have the keyboard and
  mouse instead. A controller never sees them: they modify what it plays.

## The package

```
tonnetz.odoc                a zip (the extension is a placeholder)
├── controller.json         the manifest
├── controller.wasm         its logic and drawing
└── icon.png                optional, for the library
```

- **WebAssembly**, built from any language that compiles to it (C, C++, Rust, Zig,
  AssemblyScript). It runs in a sandbox: no files, no network, nothing but the interface
  below.
- Versions sit side by side, as for instruments, and presets remember theirs.

## The interface

The MESH calls the controller; the controller answers through a few functions the MESH
provides. All positions are in the controller's own units: its surface is `width` ×
`height` dp.

**The MESH calls:**

| call | when |
| --- | --- |
| `init(width, height, flags, time)` | the controller is opened, or the surface changes size; flags 1 = e-ink, 2 = no one sees it (a take played through it to hear its notes): it need not draw, but must play just as it would on screen |
| `instrument(part, centerNote)` | the middle of the range of the instrument a part plays, as a MIDI note |
| `touch(id, phase, x, y, size, force, time)` | a finger goes down (0), moves (1), lifts (2) or is cancelled (3) |
| `tick(time)` | every frame, for controllers that do things over time (an arp of their own, animation) |
| `setting(index, value)` | one of its settings, by its place in the manifest; all are sent after loading |
| `stop()` | let go of every note (switching controller) |

Times are milliseconds on the MESH's clock. All but `touch` are optional.

**The controller calls:**

| call | does |
| --- | --- |
| `note_on(voice, part, pitch, pressure, slide, velocity, time)` | starts a note on part (sound) `part`; `pitch` in semitones, with fractions for bends; the rest 0..1 |
| `note_change(voice, pitch, pressure, slide, time)` | moves a held note |
| `note_off(voice, time)` | ends it |
| `note_volume(voice, volume, time)` | a note's own volume, 0..1: a fade apart from pressure. Called before `note_on` for the same voice, it is the volume the note starts at (else 1). Optional. |
| `draw(list)` | replaces what is on screen: a list of shapes |
| `set_setting(index, value)` | changes one of its own settings (an editor, a button on its surface); the MESH keeps and shows it as if set on the rail |
| `save(state)` | state to keep between sessions (where the notes were dragged to, say). Not yet built. |

The imports are in module `mesh`. The MESH turns notes into MPE: each voice its own MIDI
channel (up to 15), pitch bend over 48 semitones, pressure as channel pressure, slide as CC 74.

- **Notes go out at once**, not on the next screen frame. Drawing waits for the frame;
  playing never does.
- **Time stamps** let a controller with its own arp play in time, even though it doesn't run
  on the audio thread.
- **Shapes**: a list of 32-bit words, each shape an operation number and its values, colours
  as `0xRRGGBBAA` (alpha 0: none). Each MESH draws them with its own toolkit. A MESH in e-ink
  mode says so in `init`; the controller draws black on white without motion.

| op | shape | values |
| --- | --- | --- |
| 1 | fill the surface | colour |
| 2 | rectangle | x y w h corner-radius fill stroke stroke-width |
| 3 | circle | cx cy r fill stroke stroke-width |
| 4 | line | x1 y1 x2 y2 colour width |
| 5 | polygon (convex) | n fill stroke stroke-width, then n points |
| 6 | text | x y(baseline) size colour flags(0 left, 1 centre, 2 right, +4 bold) length, then UTF-8 padded to 4 bytes |
| 7 | vertical gradient | x y w h top-colour bottom-colour |
| 8 | glow | cx cy r centre-colour edge-colour |

Text is Roboto (odo's MESHes carry it, in ui/fonts; sharps, flats and arrows from Noto Sans
Symbols), `size` points to the em, so capitals stand about 0.71 x `size` above the baseline.

A MESH that meets an operation it doesn't know stops drawing that list, rather than misread
the rest.

## The manifest

```json
{
  "format": 1,
  "id": "patrickreynolds/tonnetz",
  "version": "1.0.0",
  "name": "Tonnetz",
  "description": "A net of notes: a node plays a note, a triangle a chord; slide for smooth chord changes",
  "author": "Patrick Reynolds",
  "licence": "MIT",
  "needs": { "fingers": 5, "touchSize": false },
  "orientation": "landscape",
  "parts": ["Main"],
  "arp": "app",
  "settings": [
    { "id": "key", "name": "Key", "type": "choice", "options": ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"], "default": 0 },
    { "id": "size", "name": "Size", "type": "number", "min": 4, "max": 12, "step": 1, "default": 7 },
    { "id": "bass", "name": "Bass", "type": "switch", "default": false }
  ]
}
```

| field | meaning |
| --- | --- |
| `needs` | the most fingers it uses, and whether it uses touch size or force |
| `orientation` | `landscape`, `portrait`, `upright` (stays put as the device turns) or `any` |
| `parts` | the sounds it plays, by name: one, or the sections of a split |
| `partsFrom` | optional: the id of a setting that says how many of `parts` it plays now, its value + 1 (a switch: the first part off, the first two on); or a list of ids, one more part for each that is on, up to the first that is off (the Grid's and Hex's `["split", "third"]`: one, two or three keyboards). The MESH gives it that many sounds, and calls `instrument` for each. Without it, every part always |
| `bend` | optional: `"two fingers out"` if it bends a note only with two fingers in its key slid out of it, then by how far along its row the finger is (the Hex). A take written by hand (`pitch`, `bend`, `vib`) is then played so; without it, by one finger moving toward the notes. |
| `sideThemes` | optional: for each side of a split (1st, 2nd, 3rd, 4th), the value of its `theme` setting a new side starts with, so the sides of a split stand apart (the Grid: Neon, Ember, Ice, Violet). |
| `themeColours` | optional: each `theme` option's colour (`RRGGBB`): on a split screen the app's rail takes the colour of the side it changes. |
| `rowTurn` | optional, with `bend`: the id of a number setting saying how many degrees (clockwise on the screen) the keys' rows are turned from where they lie unturned (the Hex: `"viewTurn"`, its keyboard moved and turned). A take finds a key's row that way. |
| `drumsFrom` | optional: for each of `parts`, the id of a `switch` the MESH sets on while that part's sound is a drum kit (an instrument in the `drums` group) and off otherwise, when the sound changes and when the controller opens. The touch pad lays out drums on it. Usually `hidden` |
| `arp` | `app` (the MESH's arp applies), `off`, or `own` (it has its own) |
| `settings` | its options: `choice`, `number`, `switch`, `scala` ([Scala tunings](#scala-tunings)), shown on the MESH's rail as [Settings on the rail](#settings-on-the-rail) says. `"hidden": true` keeps one off the rail: state the controller sets itself (with `set_setting`) and the MESH remembers. `"group"` (optional): a short heading it is shown under |

## Settings on the rail

Every MESH shows a controller's settings the same way: the engine's `mpe::rail`
(`engine/include/mpe/rail.h`) turns the manifest and the current values into the rail's rows,
and steps them, so a change is made in one place.

Each setting has an `id`, a `name` (the `id` if none), a `type`, a `default` and these optional fields:

| field | meaning |
| --- | --- |
| `options` | a `choice`'s options, by name; its value is the option's index |
| `min`, `max`, `step` | a `number`'s range (0 and 1 if missing) and step. A missing or 0 step: continuous |
| `hidden` | `true`: not on the rail |
| `group` | a short name it is shown under: `Layout`, `Notes`, `Split`, `Touch`, `Look`, `Playing`, `Controls`... |
| `showIf` | shown only when other settings are set so: an object of setting ids, each with the value it must have (a number, a list of numbers any of which will do, or `true`/`false` for a switch), all of which must hold; or a list of such objects, any one of which will do. `{"layout": 1}`, `{"split": true}`, `[{"chordMode": 1}, {"split": true, "rightNotes": 2}]`. For settings that mean nothing in the mode the controller is in |
| `panel` | `true` on a `switch` that opens the controller's own panel (drawn on its surface; it turns the switch off when closed). A MESH whose rail opens a group as a panel opens this one instead, for its group |

- **Order.** Hidden settings are left out. The rest are grouped by `group` (no group: a group
  named `""`). Groups appear in the order of their first setting (hidden or not); within a group, settings keep
  the manifest's order. A setting is still set by its place in the manifest, whatever its group.
- **switch**: an on/off switch, shown `On` or `Off`. A step flips it.
- **choice**: chips, one per option, the chosen one marked. A step on or back moves to the next
  or previous option, wrapping round.
- **number**: if it has a step and `(max - min) / step + 1` is from 2 to 16, chips of its values
  (`min`, `min + step`, ... up to `max`), the nearest marked; otherwise a slider. A step moves by
  `step` (a continuous number: `(max - min) / 20`), stops at `min` and `max` (no wrap), and lands
  on the step grid from `min`. A slider snaps to the step too.
- **Numbers shown**: whole numbers when the step (with `min`) or the value is whole; otherwise up
  to 2 decimals, trailing zeros dropped (`0.5`, `0.25`). A name ending in `(%)` shows the value
  with `%` (`50%`), one ending in `(dB)` with ` dB` (`24 dB`); the name is then shown without it.
- **Values**: a missing value is the setting's `default`.
- **Modes**: a setting whose `showIf` is not met by the values now is left out, as a hidden one
  is, until it is. Its group still shows, with how many it leaves out (`hiddenNow`), even with
  none left in it.

## Scala tunings

A controller that plays in other tunings can take one from a Scala file (`.scl`,
https://www.huygens-fokker.org/scala/scl_format.html), which it can't read itself: the MESH reads
it (the engine's `mpe::scala`, `engine/include/mpe/scala.h`, so both apps read a file the same way)
and gives it as settings.

```json
{ "id": "scala", "name": "Scala file", "type": "scala", "max": 128, "default": 0, "group": "Notes", "showIf": {"tuning": 9} },
{ "id": "scala1", "type": "number", "min": -1000000, "max": 1000000, "default": 0, "hidden": true },
...
{ "id": "scala128", "type": "number", "min": -1000000, "max": 1000000, "default": 0, "hidden": true }
```

- **`scala`**: how many notes the tuning has before it repeats (0: none yet); `max` the most it
  takes (up to 1024). On the rail, a row with what it holds (`None`, `12 notes, to 2/1 (1200 ¢)`,
  `13 notes, to 1901.955 ¢`) and **Open a Scala file…**. A step doesn't change it.
- **`<id>1` to `<id><max>`**, hidden numbers, in any place in the manifest: each note's cents
  above the first, as the file lists them; the last note's, where it repeats. Places past the
  last note are 0.
- The MESH sets the places, then the count, so a controller can build its tuning when the count
  arrives. A preset or take keeps them as any settings, and they go from one app to the other.
- A file that isn't a Scala file, or has more notes than `max`, is refused with a sentence that
  says why, and nothing changes.

The Hex keyboard (`controllers/hex/`) is the first: Tuning → Scala file.

## Takes

A take ([TAKE_FORMAT.md](TAKE_FORMAT.md)) records what a controller is given and what it plays,
and plays it back, with nothing to do in the controller. To play a piece written by hand, the
MESH touches a copy of the controller all over, on a clock of its own, to find where each note
starts; so a controller works with takes if:

- a finger down starts its note then, or at the next `tick`;
- it takes its time from the MESH (`touch`, `tick`), never from anything of its own;
- a tap on its surface's buttons that changes a setting does it with `set_setting` (the copy's
  settings are put back after each probe).
## Layouts without code

Decision 5's simplest controllers, layouts and mappings, are data: zones, keys and strips,
and what each touch sends. They are read by one standard **layout controller**, itself a
`.odoc`. A creator who doesn't code makes a layout; one who does writes WebAssembly. Both
run the same everywhere.

## Steps

1. ~~A WebAssembly runtime in the engine~~: done (wasm3).
2. ~~The Tonnetz as the first `.odoc`~~: done, 17 KB of WebAssembly.
3. ~~odo plays it beside its Kotlin controllers~~: done, as "Tonnetz (package)".
4. The touch pad and Grid follow; `save()`; controllers downloaded rather than built in.
5. ~~The Hex keyboard~~ (`controllers/hex/`): the Grid's code with hexagonal keys, isomorphic
   layouts and other tunings ([HEX_CONTROLLER.md](../plans/HEX_CONTROLLER.md)). It uses a touch's
   `size` or `force` as pressure.
