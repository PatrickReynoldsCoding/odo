# Instrument format, version 1 (draft)

How a MESH instrument is packaged, so that the same instrument plays, and sounds the same,
on every system: Android, iOS, Windows, macOS, Linux, and in the odo plugin in a DAW
([VISION_PLAN.md](../vision/VISION_PLAN.md) decisions 20 and 22).

Status: version 1, in use since 2026-11-06. `tools/pack.py` in the instruments repo makes the
packages; odo on Android compiles them in, and odo for computers (`desktop/`) compiles them
while running with libfaust.

## In short

An instrument is one package: a manifest, its Faust source, and every library it uses. Every system
builds the Faust its own way. The instrument is what you own, the way you own a guitar. A
pack is just a list of instruments, like an album.

## The package

```
flute.odoi                  a zip (the extension is a placeholder)
├── instrument.json         the manifest
├── instrument.dsp          the Faust source, as written
├── lib/*.lib               every library it imports: ours (mpe.lib, bowed.lib, ...) and Faust's
└── icon.png                optional, for the library
```

- **Self-contained.** The package carries a copy of every library the source imports, ours
  and Faust's standard ones, and is compiled with `-I lib`. An instrument then never changes
  or breaks because a library changed somewhere else. (Inlining everything with `faust -e`
  was tried first: it unrolls the fitted tables, so the cello became a 5 MB file, and it
  drops `post`.) The largest package is about 150 KB.
- **Source, not compiled code.** Each system compiles it its own way: natively on a
  computer and in the plugin; on a phone, the interpreter or a build bundled with the app,
  depending on the interpreter experiment (decision 3).
- **Only Faust** (decision 20).

## The contract: `mpe-voice/1`

The MESH runs one copy of the instrument per finger (per MPE channel) and sets six controls
on it. What each control does is up to the instrument. This is today's `lib/mpe.lib`.

| control    | meaning |
|------------|---------|
| `freq`     | Hz, pitch bend already applied |
| `gate`     | 1 while the finger is down, 0 after it lifts |
| `pressure` | 0..1, how hard this finger presses |
| `slide`    | 0..1, the finger's position up and down the key (CC74) |
| `velocity` | 0..1, how hard the note was struck; fixed for the note |
| `legato`   | 1 if the note started while another was sounding; optional |

- A note starts with `gate` going 0 → 1 on a cleared voice.
- **`post`** (optional): one copy runs on the sum of all voices, for what every note shares
  (a body, a resonance).
- **Mono or stereo:** a voice has one output. `post` has one input and one or two outputs.
  Without `post`, the instrument is mono.
- The contract has a version. If it ever gains a control, an instrument still says which
  version it was written for, and a MESH plays it that way.

## Its own knobs

Besides the six touch controls, an instrument may have a few knobs of its own: a violin's
bow position, a synth's filter. They are Faust sliders (`hslider`, `vslider`, `nentry`,
`checkbox`) with any other name. A MESH shows them as sliders and switches; the plugin
shows them as parameters a DAW can automate.

- Every knob has the same value on every voice.
- Keep them few. Each one has to work, and sound the same, on every system.
- Knob values are saved in presets with the sound.

## The manifest

The author writes only the `.dsp`, with `declare` lines. A packing tool makes
`instrument.json` from it (and from Faust's own description of the controls), so there is one
source of truth. Example:

```json
{
  "format": 1,
  "contract": "mpe-voice/1",
  "id": "patrickreynolds/flute",
  "version": "1.2.0",
  "aliases": ["winds/flute"],
  "name": "Flute",
  "description": "Breathy flute. Press to blow harder, slide to brighten or soften the tone.",
  "author": "Patrick Reynolds",
  "licence": "MIT",
  "range": [294, 1047],
  "channels": 1,
  "tags": ["winds"],
  "knobs": [],
  "sound": { "reverb": 0.35, "reverbSize": 0.75, "pressureCurve": 0.7 },
  "faust": "2.75.7"
}
```

| field | meaning | `declare` in the `.dsp` |
| --- | --- | --- |
| `format` | this format's version: 1 | |
| `contract` | the voice contract it follows | `contract` (default `mpe-voice/1`) |
| `id` | `author/name`, unique everywhere | `id` |
| `version` | its version, `major.minor.patch` | `version` |
| `aliases` | older ids, so old presets find it (today's `winds/flute`) | `aliases` |
| `name`, `description` | shown to players | `name`, `description` |
| `author`, `licence` | who made it, and on what terms | `author`, `licence` |
| `range` | playable range in Hz | `range` (already there) |
| `channels` | 1 or 2 | from `post` |
| `post` | whether it has a `post` stage | the packing tool |
| `tags` | for finding it: winds, keys, drums | `tags` |
| `knobs` | its own knobs: id, name, range, default | from its sliders |
| `sound` | the effects it is meant to be heard with: reverb and delay sends, room, brightness, drive, touch response, release | `sound` (today in the pack manifests) |
| `faust` | the Faust version that expanded it | the packing tool |

Test hints stay as they are (`ringout`, `window`, `pitched`) and go into the manifest too.

## Versions sit side by side

An instrument you own should not change under you, like an old guitar.

- Old versions stay installed beside the new one. Each is one package, about 100 KB.
- A preset file remembers the version it used and plays that one (if it is there).
- Ids never change. An instrument that moves (today's `winds/flute`) keeps its old id as an
  alias.

Today (since 2026-11-08) the newest version plays unless you pick another, and you can always go
back. A new version is not yet held back for you to opt in to.

### How it works

- **The version** is the instrument's `declare version "1.1.0";` (none: `1.0.0`). It is
  `major.minor.patch`, compared by number (`1.10.0` is newer than `1.9.2`).
- **Keeping a version.** Before changing how an instrument sounds, keep the version it is now,
  then raise its `declare version`:

      tools/pack.py --keep instruments/keys/piano.dsp    # versions/piano@1.0.0.odoi
      # edit piano.dsp, then: declare version "1.1.0";

  `--keep` packs the instrument as it is into `versions/<name>@<version>.odoi` in the instruments
  repo and never overwrites one that is there. The package carries its own copy of every library,
  so the old version still sounds the same after a library changes. Commit `versions/`.
- **Building.** `tools/pack.py` copies the kept packages beside the new ones (`build/packages`).
  The engine compiles all of them in (Android); odo for computers finds them in its library.
- **Finding one.** `mpe::findInstrument("patrickreynolds/piano")` (or the old id `keys/piano`)
  gives the newest; `"patrickreynolds/piano@1.0.0"` gives that version. `instrumentVersions`
  lists an instrument's versions, newest first; `isNewestVersion`; `compareVersions`.
- **In the apps.** The sound pickers list only the newest version of each sound. For a sound with
  older versions, the picker shows **Version** above *Find a sound*: `1.1.0 · newest`, `1.0.0`,
  and "older versions are kept as they were, to go back to." The sound tile shows an older
  version's number (`Piano 1.0.0`). Picking a sound from the list plays its newest.
- **What is saved.** An app's own settings and presets name only an older version
  (`"version": "1.0.0"` on Android's part; `patrickreynolds/piano@1.0.0` in odo for computers'
  config), so a sound left on the newest moves on when a new version comes. A preset *file*
  always names the version (`parts[].version`, [PRESET_FORMAT.md](PRESET_FORMAT.md)); if that
  version isn't there, the newest plays and the app says so.
- **Rolling back for good** (everyone gets the old sound): restore the old `.dsp` from git and
  raise its version again, so the version that sounded wrong is still kept to compare.

### Versions so far

| instrument | version | what changed |
|---|---|---|
| cello, violin, pad, lead, piano, max-richter | 1.1.0 | level only, to about -20 dBFS a note in odo (1.0.0 kept) |

## Packs

A pack is a list of instruments with a name, like an album: the six packs in odo today
(Classical, Keys & Organs, ...). It names its groups and lists instrument ids. The `sound`
settings move from the pack into each instrument, so they travel with it.

## Checks

The instruments repo's `make test` already plays every instrument at three pitches and at
the extremes of pressure and slide. It checks for blow-ups, real sound while held, silence
after release, and pitch within 30 cents. It becomes the check every package must pass, on
every system that compiles it.

## Steps

1. ~~The packing tool~~: done (`tools/pack.py`). Until the instruments have their own
   `declare`s for `id`, `sound` and `tags`, it takes the ids from the file names, the group
   from the folder and the sound from odo's pack manifests.
2. ~~odo builds from the packages~~: done. The engine (`engine/CMakeLists.txt`) compiles every
   package in; a computer compiles them while running. Both pass the same checks, and the two
   ways sound the same (the flute differs by 2 parts in a million).
3. ~~Add the new `declare`s to the instruments themselves~~: `sound` is done (every instrument says
   what it is meant to be heard with; the pack manifests no longer carry effects, and odo reads
   them from the instrument, falling back to a pack's only for an instrument that declares none).
   `id`, `version` and `tags` are still taken from file names and folders.
4. ~~Versions side by side~~: done (2026-11-08). In the instruments repo, `tools/pack.py --keep
   <dsp>` keeps an instrument's version as it is now in `versions/<name>@<version>.odoi`, before
   its `declare version` is raised; the kept packages are copied beside the new ones and both apps
   compile them all. `mpe::findInstrument` finds the newest by id, or a version by `id@version`.
   The sound pickers list only the newest; a sound with older versions gets a Version choice.
   A preset file names the version in the part (`"version"`, PRESET_FORMAT.md); an app's own
   settings say nothing for the newest, so they move on to new versions.
