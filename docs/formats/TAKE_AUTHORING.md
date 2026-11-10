# Writing a take by hand (`score`)

A brief for writing a piece for odo as a take file, by hand or by an LLM: from a song you know
(its tune, its chords, its phrasing) to a `.odotake` file that plays on whatever controller is in
use, with ghost fingers that show a newcomer how to play it. The file around the score is
[TAKE_FORMAT.md](TAKE_FORMAT.md). Everything here is checked by `tools/take check`.

## What to make

One UTF-8 text file, `<name>.odotake`:

```
odo-take/1.1
name: <the tune's name>
by: <who wrote the tune (and when)>; written for odo by <you>
about: <one or two lines: what it teaches>
tempo: <quarter notes a minute>
meter: <3/4>
key: <A minor>
controller: patrickreynolds/grid
settings: {<the controller settings it needs>}
sound: <an instrument package id, if it matters>

score
<the score: below>
end
```

A take may also carry its sound's effects, the room and the volume: see [Effects](#effects-the-preset-line).

Then run `tools/take check FILE` and fix what it says until it says
`all N notes play as written.` Check it at a phone's size and a computer's too:
`--size 700x380`, `--size 1000x560`.

Rules:

- **Only music that is free to use**: traditional tunes, and music whose composer died long ago
  (Beethoven, Bach, folk songs). Not a recording's arrangement, not a song still in copyright.
  Write the tune alone, from what it is, not from someone's sheet music. The one exception: a
  short excerpt (about ten seconds) of a piece still in copyright, to learn from, with its
  composer and where it comes from in `about` (`resurrections-v1.odotake`).
- **Keep it in the middle**: the Grid shows about an octave and a half around the middle of the
  instrument playing (middle C for most). Move a tune down or up an octave (or two) so that it sits
  from about F3 to G4, and say so in `about`. Choose a `sound` whose middle is near C4 (the
  string machine, the organ, the Rhodes, the pad) if the tune needs it.
- **The settings it needs**: a tune with notes outside C major needs the Grid's
  `{"chromatic": 1}`; sliding from note to note needs `{"slide": 1}`. Without them those notes
  are not there, and `check` says so.

## Effects: the `preset` line

A take is a take and a preset: Listen puts its whole set-up on before it plays, as loading a
preset does. A score names only what it needs: the `controller`, the `settings` it needs and a
`sound`, and each sound then plays with its own effects (as the pack means it). To set the effects
too, add a `preset` line (`odo-take/1.1`): a [preset file](PRESET_FORMAT.md)'s JSON, on one line.
Leave out `format`, `name`, and each side's `controller` and `settings` (the take's own lines give
those); give what you want to set, and the rest takes a preset's defaults:

```
volume: 1
preset: {"effects": {"reverbSize": 0.7, "reverbDamp": 0.35}, "sides": [{"parts": [{"sound": "patrickreynolds/piano", "volume": 1, "brightness": 0.9, "reverb": 0.35, "release": 0.6}]}]}
```

| Key | The app's words | Meaning |
|---|---|---|
| `effects.reverbSize`, `effects.reverbDamp` | Reverb: Size, Damping | The room all the sounds share, 0 to 1. A small room 0.25, a hall 0.7 to 0.85; damping higher for a darker room. |
| `effects.delayBeats`, `effects.delayFeedback`, `effects.delayPingPong` | Delay | The echo: its time in beats (at `effects.bpm`), how much comes back (0 to 0.9), and whether it goes left and right. |
| `effects.bpm` | | The tempo the delay and arps follow. Set it to the score's `tempo`. |
| `parts[].volume` | Level | 0 to 1.5; 1 as the pack means it. |
| `parts[].brightness` | Brightness | 0 to 1: 1 open, lower is darker. |
| `parts[].drive` | Drive | 0 to 1: 0 clean. |
| `parts[].pressureCurve` | Touch | Below 1 sensitive (a light touch is loud), above 1 firm. |
| `parts[].release` | Release | Seconds the note plays on after the finger lifts; 0 the instrument's own. A pedal held down, for a piano. |
| `parts[].reverb`, `parts[].delay` | the sends | How much of this part goes into the reverb and the delay, 0 to 1. |
| `parts[].transpose` | | Semitones. Leave it at 0: `check` hears the notes before it. |
| `parts[].arp` | Arp | As a preset's. Off for a score. |
| `volume:` (its own line) | Volume | The master volume, 1 the usual level, 0 to 2. |

A part with only `sound` plays with that sound's own effects; one with any other key is set in
full (what it does not name takes the defaults above, not the pack's), so name all you care about.
Each part's `sound` should be the take's `sound` line for it. Without a `preset` line the sounds
play as the pack means them. `tools/take check` reads the line and warns if it is not one.

## The score

One thing to a line. `#` at the start of a line, or ` # ` in it, starts a comment.

### Words

| Line | What it does |
|---|---|
| `tempo 92` | Quarter notes a minute, from here on (at the start: the whole piece). |
| `meter 3/4` | Beats in a bar. Its length in quarter notes: 4/4 is 4, 3/4 is 3, 6/8 is 3. |
| `key A3 minor` | The key, for degrees (`^1`...). The octave given is degree 1's. Modes: major, minor, dorian, phrygian, lydian, mixolydian, locrian, harmonic-minor, melodic-minor, pentatonic, minor-pentatonic, blues, chromatic. |
| `voice melody` | The notes after it are this voice's: one finger, or a hand. Each voice keeps its own place in time. Starts as `main`. |
| `bar 5` | This voice is at the start of bar 5 (bars count from 1). `bar 5 3`: at its third beat. `bar 0 3`: a pick-up, before bar 1. If the voice is not where the bar says, `check` warns: the bar before it is too long or too short. |
| `section Verse` | A section starts here: what Loop offers, and shown at the top. |
| `say Lift between the notes` | Words to show from here to the next words or section. Short (under 60 letters), and about the hands. |
| `press 0.6` | This voice's pressure from here on, 0 to 1 (how hard; on the Grid, how high in the key). |
| `tone 0.5` | This voice's tone from here on, 0 to 1 (slide, CC74; on the Grid, where across the key). |
| `part 1` | This voice plays the controller's second part (the Grid's second grid). |
| anything else | Notes. |

### Notes

A line of notes is notes and rests, apart by spaces: `PITCH:BEATS`, and gestures in brackets.

```
C4:1 D4:1/2 E4:1/2 F4:2(vib 0.3 5)  r:1  C4+E4+G4:2
```

- **PITCH**: a name with its octave, `C4` (middle C, MIDI 60), `F#3`, `Bb5`; or a degree of the
  key: `^1` (the tonic), `^3`, `^5#`, `^8` (the tonic an octave up), `^0` (the step below the
  tonic), `^-1`. `r` is a rest. `A3+C4+E4` is a chord: a finger each.
- **BEATS**: its length in quarter notes: `1` a quarter, `2` a half, `4` a whole, `1/2` an
  eighth, `1/4` a sixteenth, `3/2` a dotted quarter, `3/4` a dotted eighth, `1/3` a triplet
  eighth, `2+1/2` a half tied to an eighth. A voice's notes and rests follow each other: the next
  starts where this one ends.

### Gestures

In brackets after the length, apart by commas: `A3:3(vib 0.25 5 0.35, swell 0.5 0.8 0.5)`.
Times in a note (FROM, UNTIL) are parts of it, 0 its start, 1 its end.

| Gesture | Defaults | What the finger does |
|---|---|---|
| `vib DEPTH RATE FROM` | 0.3, 5.5, 0.2 | Rocks to and fro along the way to the next note up, DEPTH semitones each way, RATE times a second, from FROM on (easing in over a quarter of a second). |
| `bend TO FROM UNTIL` | -, 0.15, FROM+0.35 | Slides toward TO between FROM and UNTIL, and stays. TO: `+2`, `-1`, `+0.5` (semitones from the note), `0` (back to it), or a note (`E4`). Bends follow each other: `bend +1 0.1 0.4, bend 0 0.6 0.9` goes up and comes back. |
| `swell A B [C]` | - | Pressure from A to B through the note (A, B at its middle, then C). |
| `press P [P...]` | 0.7 | Pressure P all through; several: a curve, the values spread evenly from the note's start to its end. |
| `tone A [B...]` | - | Tone (slide) from A to B; more than two: a curve, as `press`. |
| `pitch D [D...]` | - | The pitch as a curve: semitones from the note (`0`, `+0.3`, `-1.5`), spread evenly from its start to its end, the finger going between them. For a performance's own scoops, falls and vibrato, measured from a recording. Up to 256 values. Needs a controller whose pitch follows the finger: the touch pad with `"glide": 1` (free; at 0 it snaps to the notes). The Grid with `"slide": 1` starts a new note at each square. A curved note is checked to within a quarter of a semitone of its curve. |
| `slide [PART]` | 0.25 | Does not lift: in the last PART of the note it slides to this voice's next note, which starts without a new finger. A portamento, or a leading note sliding into the tonic. |
| `stacc` | | Lifts half way through: short and detached. |
| `say WORDS` | | Words to show while this note plays. Last in the brackets. |
| `at X Y` | | Plays this note's first finger at this place (0 to 1 across and down), not where odo finds it. For a controller with no notes to find. |
| `part N` | | This note on part N. |

Without gestures a finger goes down in the middle of its key, stays still, and lifts a moment
(25 ms at most) before the note's end, so the next note is heard as a new one.

### How it becomes touches

odo touches a copy of the controller in use, as it is set, all over, and keeps where each note
starts: its keys. Each note goes to the key for it nearest where that voice's finger was, so a
hand stays in one place; a chord's notes go near its first. Pressure and tone become a place in
the key (on the Grid: higher for louder) and the finger's force. Vibrato rocks toward the key a
semitone up; a bend slides toward the key it bends to. Then those touches are played through the
controller, and what it plays is the take's notes: what you hear is what the controller does with
those movements. `check` says, note by note, if it played another note than the one written.

## Making it good to learn from

- **Sections** for each phrase or line of the tune (four or eight bars). Loop plays one round and
  round, so a newcomer can learn a line at a time. Name them as a musician would: Verse, Refrain,
  First line, Middle.
- **Words** where something new starts: a new movement, a hard bar. One at a time, about the hands,
  in a few plain words: "Lift between the notes", "Slide up into A without lifting", "Hold it and
  wiggle slowly". Not on every bar.
- **Pressure** about 0.55 to 0.7 for the tune; softer (0.4) for a quiet bar; `swell` on the long
  last note of a phrase (`swell 0.5 0.8`), falling at the end of the piece (`swell 0.6 0.9 0.4`).
- **Vibrato** on notes of two beats or more, late (FROM 0.3 to 0.4), not deep (DEPTH 0.15 to 0.35),
  RATE 4.5 to 6. Not on short notes.
- **Slides** for leading notes rising into the tonic, and for a sung, joined line; a slide's PART
  0.25 to 0.5. On the Grid it needs `"slide": 1`.
- **Curves** (`pitch`, `press`, several values) when copying a recorded performance: a value every 50 to 100 ms is plenty.
- **Bends** for blues and folk ornaments: up a semitone or two and back (`bend +1 0.1 0.35, bend 0 0.6 0.85`).
- **Staccato** for dance tunes and marches.
- Simple first: a take for a newcomer has one voice. A second voice (`voice chords`, chords in
  long notes, `part 1` on a split Grid) is for later lessons.

## Writing one from a song

1. Make sure the tune is free to use (above). Write down its key, meter and a tempo that is easy
   to follow (slower than it is usually played: speed can be raised).
2. Choose the octave: move the tune so that its lowest and highest notes are about F3 and G4.
3. Write it bar by bar: a `bar N` line, then that bar's notes. The beats in each bar must add up
   to its meter; `check` warns where they don't. A tune that starts before the first beat starts
   with `bar 0 BEAT`.
4. Put in sections at its phrases, and a few words where something new comes.
5. Put in the gestures (above): few, where they make the music.
6. `tools/take check` it, at a phone's size and a computer's. Fix every error and warning.
   `odo-take render FILE -o out.wav` (the tool built with `MPE_LIBFAUST`, as odo for computers is) plays it
   through its sounds and effects into a WAV, to listen to or to compare with a recording.
7. `tools/take notes` it, and read the notes through: is that the tune?

## A worked example

"Twinkle, Twinkle, Little Star" (the French tune *Ah ! vous dirai-je, maman*, 18th century) in
C major, all of it in the Grid's C major as it starts:

```
odo-take/1.1
name: Twinkle, Twinkle
by: Traditional French (18th century); written for odo by Claude
about: Six notes, two of each, a line at a time.
tempo: 96
meter: 4/4
key: C major
controller: patrickreynolds/grid

score
key C4 major
press 0.6
section Up and down
say Two taps on each square, then the next
bar 1
^1:1 ^1:1 ^5:1 ^5:1
bar 2
^6:1 ^6:1 ^5:2(swell 0.5 0.75)
bar 3
^4:1 ^4:1 ^3:1 ^3:1
bar 4
^2:1 ^2:1 ^1:2
section Middle
say The middle line, twice
bar 5
G4:1 G4:1 F4:1 F4:1
bar 6
E4:1 E4:1 D4:2
bar 7
G4:1 G4:1 F4:1 F4:1
bar 8
E4:1 E4:1 D4:2
section Up and down again
bar 9
C4:1 C4:1 G4:1 G4:1
bar 10
A4:1 A4:1 G4:2
bar 11
F4:1 F4:1 E4:1 E4:1
bar 12
D4:1 D4:1 C4:2(vib 0.25 5 0.4, swell 0.6 0.85 0.4)
bar 13
end
```

`tools/take check twinkle.odotake` says:

```
twinkle.odotake: Twinkle, Twinkle, 4/4 at 96, 0:29.975 long
  on patrickreynolds/grid 2.8.0 at 900x500 dp: 328 touches, 882 note events, 3 sections, 2 words
  all 42 notes play as written.
```

Longer examples, in `takes/`: `ode-to-joy.odotake` (C major, a held swell and a last note with
vibrato) and `greensleeves.odotake` (A minor in 3/4 with a pick-up, chromatic, slides into the
tonic, vibrato on the long notes, a sound chosen for its middle). `resurrections-v1.odotake` and
`-v2.odotake` are ten seconds of a piano piece on the Hex: several voices (a held bass, a figure
of four notes held as the pedal holds them, the tune), a slowing at the end with `tempo` lines in a
voice of rests, and, in v2, the piano's effects in a `preset` line.

## What `check` says, and what to do

| It says | Do |
|---|---|
| `"H4" is not a note` | A note's name: A to G, `#` or `b`, then its octave. |
| `its length in beats ... is wrong` | `1`, `1/2`, `3/2`, `1+1/2`: no letters. |
| `bar 9: voice main is at beat 31 ... beats short of it` | The bar before has too little in it: a note too short, or a missing rest. |
| `... beats past it` | The bar before has too much. |
| `G4 is not on the controller as it is set` | Move the tune an octave, or set the controller so it has the note (`"chromatic": 1`), or choose a sound with a higher or lower middle. |
| `wanted E4, heard C4-0.50` | A slide or bend that did not get there: the controller does not slide between notes as set (the Grid: `"slide": 1`). |
| `a degree needs a key first` | A `key` line before the first `^`. |
