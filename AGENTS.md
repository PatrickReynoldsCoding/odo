# Working on odo

## One design for every app

odo ships on Android (`app/`) and on computers (`desktop/`). Keep the two looking and working
as alike as you can. A user who knows one app should know the other.

- **Change both apps together.** A UI change in one app (rail layout, pickers, panels,
  presets, words on screen) goes into the other in the same change. If it can't, write down
  what is still missing. A short TODO list in the commit message will do.
- **Share before you copy.** Put shared things in shared code, so each app does not make
  them again:
  - Colours, type sizes, corner radii: `ui/tokens.json`. Run `tools/tokens.py` to write
    `ui/Tokens.kt` and `desktop/tokens.h`. Do not put colours or sizes in either app.
  - Fonts: `ui/fonts` (Roboto, and Noto Sans Symbols for the music symbols).
  - Which settings a controller shows, in which group, with which control, and how their
    values read: `mpe::rail` (`engine/include/mpe/rail.h`). Android gets it through JNI.
  - Controllers: packages in `controllers/` (C plus `controller.json`), which run in both apps.
- **Same words, same order.** Use the same labels, group names, headings and order of items
  in both apps. Only change something because the platform needs it (touch vs. mouse, screen
  size, system back).
- **Same file formats.** A preset or a setting saved by one app must load in the other.

## Docs

Docs go in `docs/`, by kind: `formats/` (files both apps read and write), `vision/`, `plans/`,
`android/`. List a new one in `docs/README.md`. Keep `README.md` and this file at the top.

