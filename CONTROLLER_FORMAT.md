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
- **Sensors belong to the MESH, not the controller** ([VISION_PLAN.md](VISION_PLAN.md)
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
| `init(width, height, flags, time)` | the controller is opened, or the surface changes size; flags 1 = e-ink |
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
| `partsFrom` | optional: the id of a setting that says how many of `parts` it plays now, its value + 1 (a switch: the first part off, the first two on). The MESH gives it that many sounds, and calls `instrument` for each. Without it, every part always |
| `arp` | `app` (the MESH's arp applies), `off`, or `own` (it has its own) |
| `settings` | its options: `choice`, `number`, `switch`, shown on the MESH's rail as [Settings on the rail](#settings-on-the-rail) says. `"hidden": true` keeps one off the rail: state the controller sets itself (with `set_setting`) and the MESH remembers. `"group"` (optional): a short heading it is shown under |

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

- **Order.** Hidden settings are left out. The rest are grouped by `group` (no group: a group
  named `""`). Groups appear in the order of their first setting; within a group, settings keep
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
