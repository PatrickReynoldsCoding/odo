# What is a MESH?

A **MESH** (Multimodal Expression Sensor Host, said "mesh") is software for playing music
expressively, straight away, with whatever you have in your hands: a touch screen, the
sensors in a phone, or an MPE or MIDI controller.

MESH is a category, like "DAW". Any app that meets the requirements below is a MESH, on any
platform, written in any code.

**In one sentence:** a MESH lets you pick up any controller and play any instrument at once,
with every finger its own voice.

Status: draft. The decisions behind it are in [VISION_PLAN.md](VISION_PLAN.md).

## A MESH is

- **For playing, not producing.** You open it and play within seconds. You get your phone
  out on the train home and express yourself. It is about the moment, not about building a
  song.
- **A host for packages.** It plays two kinds of package, and later a third:
  - **controllers**: how you play (a touch layout, with optional logic and drawing);
  - **instruments**: what you hear, written in Faust;
  - **modifiers** (later): stages that change what you play, such as an arpeggiator.
- **Universal.** A package plays the same on every MESH, on every platform. Like HTML in web
  browsers, the packages follow open specs and every MESH implements them.
- **Yours everywhere.** You get attached to an instrument the way you do to an old guitar
  you inherited. An instrument you own is there wherever you make music: on your phone, on
  your computer, whatever system it runs.
- **Built on MPE.** Controllers send MPE, and instruments receive it. Every finger is its own
  voice, with its own pitch, pressure and slide.
- **Multimodal.** Touch, tilt, breath and other sensors, and hardware MPE and MIDI
  controllers, can all play. Sensors belong to the MESH, not to a controller, so they
  modify whatever controller is playing.
- **Open.** The package formats are open. A MESH can get packages from any store, and its
  user can add more stores.

The signal path in every MESH:

```
controller (a touch layout + optional logic, or a hardware MPE/MIDI controller)
  -> MPE notes, each for a part (sound): the controller decides, for a split layout
  -> modifiers (tilt, breath, pitch assist, arpeggiator, ...)
  -> the part's instrument
  -> sound
```

A controller can switch any modifier off, or say "I do this myself" and bring its own (for
example, its own arpeggiator).

## A MESH is not

- **Not a DAW.** There is no timeline, no tracks, no arranging, no mixing and no editing
  of songs. A MESH may record or loop what you play, but it is not for building a song
  step by step.
- **Not a sound design tool.** Instruments are made elsewhere, in Faust, and played in a
  MESH.
- **Not a plugin format.** VST3, AU and CLAP put instruments inside a DAW. A plugin that
  loads MESH instruments into a DAW is useful, but it is a bridge, not a MESH: it has no
  controllers of its own.
- **Not a store.** Stores sell and give away packages. A MESH uses stores; it is not one,
  and it is not tied to one.
- **Not one app.** Our app, odo, is one MESH. Others can make their own, open or closed.
- **Not tied to one device or controller.** A phone alone is enough, and so is a Seaboard
  plugged into a laptop.

## What makes software a MESH

**Must:**

1. Load and play **instrument packages** as the instrument spec describes.
2. Load and play **controller packages**, at least their layouts and mappings.
3. Be playable with at least one input: touch, or a hardware MPE controller.
4. Pass the **conformance tests** for the spec version it claims, so packages behave the
   same as on every other MESH.

**Should:**

- Accept hardware MPE and MIDI controllers.
- Send what you play out as MPE or MIDI, so a MESH can also be the controller for a DAW
  or another synth, with its sensors and modifiers applied.
- Offer sensors (tilt, breath, ...) as modifiers on any controller.
- Let the user add stores, and show everything they own, from every store, in one place.
- Feel instant: a target of under about 30 ms from touch to sound. (The Android app does
  about 25 ms today; see [LATENCY.md](../android/LATENCY.md).)
- Be ready to play within seconds of opening.

**May:**

- Look and work however it likes beyond that: its own UI, menus and themes.
- Add its own modifiers, effects, presets, recording and looping.
- Run controller logic and drawing (controller layers 2 and 3), once the specs for them
  exist.

## Words

- **Controller**: a package that turns what you do (touch, and anything its logic reacts
  to) into MPE notes. Also a hardware controller, such as a Seaboard.
- **Instrument**: a package that turns MPE notes into sound.
- **Modifier**: a stage between controller and instrument that takes MPE in and gives MPE
  out.
- **Package**: a controller, instrument or (later) modifier, in an open, versioned format,
  signed by the store it came from.
- **Store**: a place that lists packages and lets you download the ones you own. There can
  be many.
- **MPE**: MIDI Polyphonic Expression. Each note has its own channel, so each finger can
  bend, press and slide on its own.
