# Native speed on the phone stores (to discuss later)

Written up 2026-11-06 from a conversation; nothing here is decided. Background: the interpreter
experiment ([VISION_PLAN.md](VISION_PLAN.md) decision 27) found that instruments run as
WebAssembly in wasm3 sound the same as native code but cost about 7x the processing. 52 of 62
add no latency; the heavy physical models (cello, violin, the bass voice, marimba, piano) can't
play 8 fingers at all, and soprano and tenor need 8 to 16 ms buffers instead of 0.7 ms.

## What each store allows

| | Google Play | Apple App Store | Computers |
| --- | --- | --- | --- |
| Native code compiled into the app | yes | yes | yes |
| Downloading native code (.so) | no, not even imported by the user | no | yes |
| Compiling to native code on the device, or a JIT | grey area (browsers and emulators such as Dolphin and PPSSPP do it) | no (only Apple's web engine, and approved browsers in the EU) | yes (odo desktop does, with libfaust) |
| Downloaded code run by an interpreter | yes (the policy's exception for sandboxed interpreters) | very likely, if it doesn't change what the app is | yes |
| Native code delivered by the store on demand | yes: Play Feature Delivery modules | only as separate apps (AUv3 Audio Units) | n/a |

Why Play bans downloaded native code: it was never reviewed, it could change the app after
review, and it runs with all the app's permissions (for odo, the microphone). A Faust instrument
can only make sound, unless it uses `ffunction` (calls outside C code), which odo would refuse
in anything it didn't build itself.

## Ways to get native speed that both stores allow

1. **Every catalogue instrument compiled into the app, unlocked by owning it.** New instruments
   arrive with app updates (a day or two of review). About 50–200 KB of machine code each. On
   Android, Play Feature Delivery can split packs into modules downloaded on demand.
2. **Instrument packs as separate store apps**, loaded as plug-ins: AUv3 on iOS, an Android
   equivalent (such as AAP, or odo's own). Native and allowed, but each pack is its own app.
3. **Native building blocks, data-only instruments.** odo ships fast native blocks (bowed
   string, reed, vocal tract, piano, resonators, filters); a downloaded instrument only wires
   and sets them. Near native and allowed everywhere. Limits creators to the blocks there are:
   they couldn't invent new sound sources, build their own physical-model loops (a loop that
   runs every sample must live inside one block), write custom per-sample maths, or use
   existing Faust code directly.
   - **Middle way:** instruments stay Faust, with the heavy loops from a library of native
     blocks offered as Faust functions (`odo.bowedString(...)`); only the instrument's own glue
     is interpreted. Close to native speed; today's 62 instruments already call shared engines
     (bowed.lib, vocal.lib, piano.lib, pm.*), which would become the blocks.
4. **A faster interpreter:** perhaps 2–3x native at best. Closer, not the same.

## Automating blocks for an online repo of creators' instruments

Native code always arrives in a reviewed app release, but a robot builds the releases:

1. Someone uploads a Faust instrument.
2. CI checks it: it compiles, passes the instrument tests, uses no `ffunction`; its cost is
   measured.
3. It plays at once in odo, interpreted (heavy ones with a voice limit).
4. On a schedule (weekly, say), CI builds a release with the repo's instruments compiled to
   native code and submits it to both stores (fastlane).
5. After the update, odo finds the native build of each instrument by a fingerprint of its
   exact source (each version its own), and plays that instead of the interpreted one.

What a block is, from simplest to cleverest:

| Level | Block | Automation | Catch |
| --- | --- | --- | --- |
| A | Whole instruments | Easy: the engine already builds from packages | Size grows: ~100 MB per 1,000 instruments (Feature Delivery helps) |
| B | Shared library functions (pm.fluteModel, the bowed string) | Medium: compile the most used, most costly | Custom code stays interpreted |
| C | The per-sample loops themselves (Faust marks each `~`) | Hard: find each loop in the signal graph, compile it, interpret the rest | Telling identical loops apart from ones that differ only in a constant |

Suggestion: A first, B when size matters, C if the repo grows big enough to justify it.

## Open questions

- Measure the interpreter on a real phone (arm64) before deciding anything.
- Which mix: catalogue built in (1) plus interpreted imports, or the blocks route (3)?
- Does Play accept on-device compiling of Faust (or a WebAssembly JIT) for a music app?
