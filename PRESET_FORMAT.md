# Preset files (`.odopreset`)

A preset file is how one odo gives a preset to another: a phone to a computer, or one person to
another. Both apps read and write it through the same code, `mpe::preset`
(`engine/include/mpe/preset.h`), so a file one app makes loads in the other.

In the preset picker, **Share** sends the preset on view as a file, and **Open a preset file**
reads one in as one of yours. On Android, Share uses the share sheet (send it by email, a chat
or Drive), and opening an `.odopreset` file from another app also adds it. On a computer, both
buttons use a file dialog, and you can also drop a file on the window.

## The file

UTF-8 JSON, at most 1 MB. MIME type `application/vnd.odo.preset+json`.

```json
{
  "format": "odo-preset/1",
  "name": "Bass below, lead above",
  "madeBy": "odo 0.29.0 (Android)",
  "split": 0,
  "layered": false,
  "effects": {"bpm": 110, "reverbSize": 0.5, "reverbDamp": 0.4,
              "delayBeats": 0.75, "delayFeedback": 0.35, "delayPingPong": true},
  "sides": [
    {
      "controller": "patrickreynolds/grid",
      "settings": {"split": 1, "slide": 0},
      "parts": [
        {"sound": "patrickreynolds/bass-guitar", "transpose": 0, "volume": 1, "brightness": 1,
         "drive": 0, "pressureCurve": 1, "reverb": 0.15, "delay": 0, "release": 0,
         "arp": {"on": false, "order": 0, "rate": 4, "gate": 0.6, "octaves": 1, "latch": false}},
        {"sound": "patrickreynolds/lead"}
      ]
    }
  ]
}
```

| Key | Meaning |
|---|---|
| `format` | `odo-preset/<major>[.<minor>]`. A reader refuses a newer major and reads a newer minor, leaving out the keys it doesn't know. |
| `name` | Shown in the picker. A name already taken gets " 2", " 3"... when opened. |
| `madeBy` | Which app made it. Only shown; never relied on. |
| `split` | How the screen is split into sides (odo for computers' splits; 0: one side). |
| `layered` | One side: every note plays its first two parts. |
| `effects` | The room and clock the parts share. Left out: the app keeps its own. |
| `sides[]` | 1 to 4. Each: the `controller`, its `settings` and its `parts`. |
| `sides[].controller` | The controller package's id, `midi` for External MIDI, or `""` for any controller (the one in use). |
| `sides[].settings` | Setting id to value: numbers, or `true`/`false` for a switch. Settings a controller doesn't have are left out; those the file doesn't name stay as they are. |
| `sides[].parts[]` | 0 to 4. A split controller's second part (the Grid's second grid) is its second. |
| `parts[].sound` | The instrument package's id (`patrickreynolds/cello`, not the older `strings/cello`). |
| `parts[].*` | How the part plays. A part with only `sound` plays with that sound's own effects. |

Values out of range are clamped. A value of the wrong type takes the default.

## What each app does with it

- **odo for Android** plays one side. When it opens a file with more sides, it takes the first one
  and says the rest were left out. It writes every part with all its settings, and the effects.
- **odo for computers** plays every side. It has no part settings or effects of its own: it keeps
  those it was given (so a phone's preset sent there and back stays the same, as long as the
  sounds are not changed) and writes the rest as the sound alone.
- **Not in a preset:** the volume, the look, gestures, loops, the gyro and breath, and the MIDI
  device. These belong to the device.
