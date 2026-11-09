# The Hex keyboard: an isomorphic controller

What the Lumatone and Hexatone do, and what odo's Hex keyboard (`controllers/hex/`) takes from
them. Written 2026-11-09, with the first version of the controller.

## In short

- **Keys**: hexagons in rows, each row half a key over from the one below. A layout is two steps:
  to the next key in a row, and up to the right. Every interval then has one shape everywhere.
- **Layouts**: Wicki-Hayden, Harmonic table, Bosanquet (the Lumatone's standard), or your own two
  steps.
- **Tunings**: 12, 19, 22, 31, 41 and 53 equal; 5-limit just intonation; Pythagorean;
  quarter-comma meantone. A layout is written as intervals (ratios of 2, 3 and 5), so it works in
  every tuning.
- **Colours**: the Grid's five ways, and four more: White keys, Sharps and flats (the Lumatone's
  way), Rainbow (Hexatone's spectrum), Distance from the key.
- **Touch**: everything the Grid does (it is the Grid's code), plus a touch's size or force as
  pressure, the nearest a screen has to a Lumatone key's aftertouch.

## What we looked at

### The Lumatone

- 280 hexagonal keys in one isomorphic matrix, each with its own colour light, 24.15 mm apart.
  It began as Siemen Terpstra's generalized keyboard (from Erv Wilson's designs); the instrument
  was renamed Lumatone in 2019 and shown at NAMM in January 2020.
  ([features](https://www.lumatone.io/features), [our story](https://www.lumatone.io/ourstory),
  [Wikipedia](https://en.wikipedia.org/wiki/Lumatone))
- **Any tuning**: each key sends any MIDI note on any channel, so a mapping can be any tuning,
  equal or not. The editor's isomorphic tool sets the steps to the right and up to the right, and
  a colour per scale degree. Ten mappings are kept on its preset buttons.
  ([features](https://www.lumatone.io/features), [mapping basics](https://help.lumatone.io/en/creating-mappings))
- **The standard mapping** is the Bosanquet-Wilson layout: whole tones along a row, the diatonic
  semitone down to the right and the chromatic semitone (a sharp) up to the right. It works
  unchanged for any tuning made of octaves and fifths (12, 19, 31, 53 equal, meantones,
  Pythagorean). ([xen wiki: standard Lumatone mapping](https://en.xen.wiki/w/Standard_Lumatone_mapping_for_Pythagorean))
- **Colours** in its mappings mark the notes' names: naturals white, sharps and flats colours of
  their own, further accidentals others again (for example [Cam's DR94 mappings](https://patchstorage.com/cams-dr94-bosanquet-and-multi-ring-mappings-for-the-lumatone/):
  white naturals, seven colours light and dark, cooler low and warmer high).
- **How a key senses**: a non-contact Hall-effect sensor follows the key all through its travel
  (torsion springs, long travel). From it: velocity (with curves), polyphonic aftertouch past the
  note-on point (up to 15 keys at once), a key as a continuous controller, and "Lumatouch", the
  whole travel as one continuous control. ([features](https://www.lumatone.io/features),
  [key modes](https://help.lumatone.io/en/what-is-lumatone/what-are-the-four-key-modes-of-lumatone),
  [aftertouch](https://help.lumatone.io/en/what-is-lumatone/so-lumatone-really-has-polyphonic-aftertouch-what-else-is-special-about-its-keys))

### Hexatone

Hexatone is Siemen Terpstra's hexagonal keyboard as a web app (Marc Sabat's MIDI version,
Plainsound, GPL-3.0: [source](https://github.com/000masa000/hexatone)). We found no separate iPad
app of that name; the web app runs on an iPad.

- **Layout**: `rSteps` (to the right) and `urSteps` (labelled "right-downward-facing steps"), a
  hex size and a rotation. Its presets per tuning, right and down-right: 12 equal 2, 1; 19 equal
  3, 2; 22 equal 4, 1; 31 equal 5, 3; 41 equal 7, 3; 53 equal 9, 4. These are Bosanquet's steps,
  and our Bosanquet gives the same in each (a test checks it).
- **Tunings**: Scala files (`.scl`): equal divisions (12 to 72), many just intonations (5-limit,
  7-limit, Partch's 43, Farabi's 22, Indian srutis), meantones, harmonic series. Each preset also
  has its note names and a reference pitch.
- **Colours**: either a colour per scale degree (`note_colors`: for 12 equal, light for naturals
  and grey for accidentals), or `spectrum_colors`: the hue turns with the pitch's cents within the
  octave, from a fundamental colour.

### Layouts

From [Wikipedia](https://en.wikipedia.org/wiki/Isomorphic_keyboard) and [Musix Pro's
notes](https://shiverware.com/musixpro/customization.html), with rows across:

| layout | along a row | up to the right | up to the left | in 12 (right, up-right) |
| --- | --- | --- | --- | --- |
| Wicki-Hayden | whole tone 9/8 | fifth 3/2 | fourth 4/3 | 2, 7 |
| Harmonic table | semitone 25/24 | major third 5/4 | minor third 6/5 | 1, 4 |
| Bosanquet | whole tone 9/8 | sharp 2187/2048 | flat (the diatonic semitone down) | 2, 1 |
| Janko | whole tone | semitone | semitone down | 2, 1: Bosanquet's in 12 |

Janko's is Bosanquet's in 12 equal, so it is not a layout of its own here.

## What we built

### Notes

`controllers/hex/tuning.h`, plain C shared by the controller and the tests:

- **A tuning** is n steps to the octave, and either equal steps, a chain of fifths of some size
  (Pythagorean 701.955 cents, quarter-comma meantone 696.578), or a table of cents (5-limit just:
  Hexatone's 12-JI-5L). Adding one is a line in `HT_TUNINGS` and a word on the rail.
- **A layout** is two intervals as exponents of 2, 3 and 5. A tuning turns them into its steps by
  its val, the nearest whole steps to 2, 3 and 5 (twelve-note tunings use 12's). So Wicki-Hayden is
  5, 18 in 31 equal and 9, 31 in 53; Harmonic table 3, 17 in 53. Harmonic table in 31 equal only
  reaches every other step (its thirds are 10 and 8 steps); Your own steps choose any two.
- **The key stays put**: the key's root sounds at its 12-tone pitch, and the tuning is built from
  it (just intonation in A is just from A). A note is a number of the tuning's steps from C-1; its
  name comes from its place on the chain of fifths (31 equal from C: C, D♭♭, C♯, D♭, C♯♯ ... as letters with
  up to two sharps or flats, else its step number).
- **Scales** mark notes in other tunings by their place on the chain of fifths (C major in 31
  equal: steps 0 5 10 13 18 23 28); notes outside the scale are dimmed, as the Grid's are.

### Colours (Look → Colours)

The Grid's (Plain, Every degree, Chord tones, Root 4th 5th, By note name; in other tunings by each
note's nearest 12-tone degree on the chain of fifths), and:

- **White keys**: the key's seven naturals light, the rest dark, like a piano.
- **Sharps and flats** (the default, the Lumatone's way): naturals white, sharps blue, flats
  green, double sharps violet, double flats amber, anything further slate.
- **Rainbow**: Hexatone's spectrum, the hue going round once an octave from the root.
- **Distance from the key**: warm near the root on the chain of fifths, cooler further away;
  flats paler than sharps.

### Touch

The Hex keyboard is the Grid with hexagons: `hex.c` defines `KEYS_HEX` and includes
`controllers/grid/grid.c`, and gives only the keys' shape (where they are, which note each plays,
how it sounds, how it looks). Every way a finger plays a key is the Grid's own code: dynamics and
tone inside a key, sliding out past it to bend (toward the next key's note, now in any tuning) or
change the tone, vibrato, Slide between notes and its glide (between the six keys round one), the
Slide, Inside and Slide out toggles (double tap for good, held for now), Latch, Hold, the Bend,
Tone and Dynamics controls, moving, sizing and turning them with two fingers, the split, the
flips and Flip 90°. Its rail is the Grid's, the same groups and words ("key" for "square"), plus
Note layout, Tuning, the two steps and Press harder. Its manifest is made from the Grid's
(`controllers/hex/manifest.py`) and a test checks they keep the same order.

A Lumatone key knows how far down it is, all the time. A touch screen knows where a finger is and,
on most, how big the touch is (Android's pressure is usually worked out from it) or, on some, how
hard. Pressing a finger harder spreads it. So **Press harder** (Touch):

- **Pressure** (default): pressing on past how the touch came down adds pressure, as aftertouch:
  half as big (or as hard) again is all of it. It is measured from the touch's own start, so it
  works whatever a screen reports.
- **Pressure and velocity**: also how big or hard the touch was as it landed sets how hard the note
  is struck.
- The MESH's touch carries `size` and `force`; the controller takes force, else size. odo on
  Android sends Compose's pointer pressure (from the contact on most phones) as force and no size;
  odo for computers sends SDL's finger pressure, and a mouse a steady 0.8 (so no aftertouch).

Inside a key, its height is still the dynamics and its place across the tone, as on the Grid;
Press harder adds to the pressure from there.

## Not done yet

- **Scala files**: a controller can't read files. A `.scl` (or a Lumatone `.ltn` mapping) would
  need the rail to load one into hidden settings, as Your own notes keeps notes.
- **The tilt**: the Lumatone's rows rise, and Hexatone turns its keyboard by any angle; here the
  rows are level, so Bosanquet's rows (a sharp up to the right, a flat up to the left) do not climb
  and it does not scroll: its range is the keys across a row (Keys top to bottom, up to 16).
- **Touch size on Android**: Compose does not give the touch's major axis; odo could send
  `MotionEvent.getTouchMajor` as `size` from a pointer interop filter.
- **Presets**: neither app has a built-in preset with the Hex keyboard yet.
- **Colours per degree**: Hexatone and the Lumatone editor let you pick each degree's colour.
- Checked without a touch screen only: engine tests (every key of nine layout and tuning pairs
  played through the MESH), and desktop screenshots. Not by hand, not on Android.
