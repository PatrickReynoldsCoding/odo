# Vision plan: MESH

A draft we are working out together. Open questions are at the bottom, most important
first. Decisions go in the Decisions log as we make them.

What a MESH is and is not: [MESH.md](MESH.md). Our own MESH app is called **odo**
(this repo, `PatrickReynoldsCoding/odo`).

## The vision

- **A library of instruments and controllers you own**, like Steam. Some free, some sold by
  their creators, with a cut going to the platform.
- **The instruments run in any software**: as plugins in a DAW (VST3, AU, CLAP, LV2) and in
  our own app.
- **A new kind of app, a MESH** (Multimodal Expression Sensor Host): not a DAW. You pick it up and play
  at once, to express yourself rather than build a song step by step. You get your phone out
  on the train home and play. It takes many kinds of input (touch, tilt, breath, other
  sensors) and can also connect to MIDI and MPE controllers.
- **On every platform**: Android, iOS, Windows, macOS, Linux, all running the same
  instruments and controllers.
- **The Android app buys and downloads from the library.**

## Where we are now

- The Faust instruments live in their own repo (`third_party/mpe-instruments`), in six
  packs with manifests the app reads.
- The engine is plain C++ (Faust voices → effects → Oboe), tested on the desktop.
- The touch pad, tilt and breath already work as inputs on Android.
- `DECOUPLE_CONTROLLER_PLAN.md` is the first step: the touch pad becomes one controller
  among many.

## What we know about the technology

### Faust

One Faust file compiles to VST3, AU, CLAP, LV2, desktop apps, iOS, Android and WebAssembly,
so the instruments can be portable. Two catches:

- **iOS won't let an app download native code.** Apple bans it. We can't compile an
  instrument on a server and send it to an iPhone. Instead, ship each instrument as Faust
  source and have every app run it through Faust's interpreter or a WebAssembly runtime.
  That is slower than native, so it has to be measured on a cheap phone. This choice
  affects every platform.
- **One loader plugin, not one plugin per instrument.** 62 instruments × 4 formats × 3
  operating systems, all signed and notarised, would be a build-farm job. A single loader
  plugin (like Kontakt) runs in any DAW, signs into your library, and loads any instrument
  you own. One thing to build and sign, and ownership is checked in one place.

### Controllers

A controller is interactive code (touch layouts, gyro, breath), and Faust doesn't help
there. Every controller outputs MPE, and every instrument accepts it: **MPE is the
contract.** The controller spec is open, from plain layouts up to controllers with their
own logic and drawing, run in a sandbox (see decision 5).

## Concerns

1. **Controllers are harder to make portable than instruments.** See above.
2. **The app stores take a cut and set rules.** Selling digital goods inside a Play Store
   or App Store app generally has to use their billing, at a 15–30% cut. The rules vary by
   region and have changed since the Epic lawsuits. The current sideloaded APK from GitHub
   avoids this but reaches far fewer people. iOS has no realistic way around the App Store.
3. **Running a marketplace is a business, not a feature.** It means taxes (VAT on every
   sale), refunds, paying creators, moderating content, piracy, and fraud. A "merchant of
   record" service (Paddle, Lemon Squeezy) handles the tax and payments side for a fee.
4. **Licensing.** If the core stays open source, decide how paid instruments fit around it.
   Check the licences of the Faust libraries the instruments build on before anyone sells
   something derived from them.
5. **Five platforms is a lot of user interface.** Settled by decision 13: each platform
   can have its own app; the shared core library and specs keep packages universal.

## The name

**MESH**: Multimodal Expression Sensor Host. Said "mesh". It is a category, like "DAW":
any app that plays these controllers and instruments is a MESH. Our own app is called
**odo**; that is the name to trademark.

## Draft phases

Each phase ships something people can use (decision 2). Phases 2 and 3 test whether the
vision holds before any selling is built.

0. **Separate the controller** (`DECOUPLE_CONTROLLER_PLAN.md`): the touch pad becomes one
   controller behind an interface (done in 0.16); the arp and the sensors are app-wide
   modifiers, split stays the controller's (decisions 5, 7). Done, not yet released.
1. **Write the specs, each with a version number** (Apache 2.0):
   - instrument package: Faust source, sound files, manifest, presets (decision 6);
   - controller package: layout and mappings now, room for logic and drawing (decision 5);
   - signing and an empty licence field (decision 11);
   - the store protocol: list, download, owned items (decision 12).
   The current pack manifests are halfway to the instrument package.
2. **The interpreter experiment** (decision 3), then **the shared core library** (decision
   13): today's engine, loading packages from files instead of building them in.
3. **The first other platform**: a desktop app or the CLAP plugin, on Linux. Proves the
   same packages play the same way on two platforms. Start the conformance tests here.
4. **A library site with free instruments** (decision 8): accounts, "my instruments",
   pick your operating system, plain-words "what can I play it in". The Android app
   downloads from it.
5. **Creators upload** their own instruments and controllers, still free, with review.
6. **Controller logic** (layer 2) in the WebAssembly sandbox, then drawing (layer 3).
7. **Payments**, only if people turn up (decision 1): paid items, the platform's cut,
   creator payouts, through a merchant-of-record service. No DRM at first (decision 11).
8. **More platforms**: iOS, Windows, macOS; the Play Store when ready (decision 4).

## Open questions

All nine first questions are answered; see the Decisions log. What's left is under
"Under discussion" at the bottom.

## Decisions log

1. **Passion project first** (2026-11-06). Build the app and a free library for the joy of
   it. Selling stays at the end of the plan (phase 5) and is only built if people turn up.
   Until then, keep accounts and package formats simple, but don't rule out paid items
   later (for example, give every package an owner and a price field that is always free).
2. **Solo, time not known yet** (2026-11-06). Keep every phase small enough to finish and
   ship on its own. Pick one or two platforms at a time rather than all five; Android first,
   because it already works.
3. **Measure before choosing how instruments run** (2026-11-06). Apple and Google Play both
   ban downloading native code, so store builds would need Faust source or WebAssembly run
   by an interpreter. Before deciding, run the experiment below.

   **Experiment: interpreter vs native.**
   - Take two current instruments: one light (a plucked string) and one heavy (the most
     expensive physical model).
   - Run each three ways on the phone: today's native build, Faust's interpreter backend,
     and WebAssembly (in an embedded runtime such as wasm3 or WAMR).
   - Measure: CPU per voice, how many voices before audio drops out, and note-to-sound
     latency (as in `LATENCY.md`).
   - If the interpreter or WASM keeps the latency and allows about 8–10 voices of the
     heavy instrument, use it everywhere. If not, use native where allowed.
4. **GitHub sideloading for now** (2026-11-06). *Replaced by decision 19.* Keep the APK and in-app updater on GitHub
   while the formats and library are young. Move to the Play Store once there is something
   to show people; the experiment in decision 3 tells us whether the library can follow
   Play's rules on downloaded code.
5. **The controller spec is open; sensors belong to the app** (2026-11-06).
   - The controller spec must not limit what a designer can build. A controller may carry
     its own logic, including its own arpeggiator, not only a layout and mappings.
   - So a controller package has layers, each optional above the first:
     1. **Layout and mappings** (data): zones, keys, strips, and how touches map to MPE.
        Enough for pads, keyboards and Seaboard-style surfaces. No code.
     2. **Logic** (code in a sandbox): reacts to touches and time, and sends MPE. This is
        where a custom arp, a strummer or a generative controller lives.
     3. **Drawing** (code in a sandbox): the controller draws its own surface through a
        drawing API the app provides, for controllers that aren't zones at all (physics,
        games).
   - The code runs in a sandbox inside the app, not as native code: the stores ban
     downloaded native code, and a downloaded controller must not reach the user's files or
     network. **Proposal: WebAssembly**, the same runtime the interpreter experiment tests
     for instruments. Designers can write it in C++, Rust, AssemblyScript and others.
   - We build layer 1 first, but write the spec for all three from the start, so later
     layers don't break earlier controllers.
   - Logic must stamp the notes it sends with a time, so that a scripted arp plays in time
     even though it doesn't run on the audio thread.
   - Tilt, breath and other sensors are **not** part of a controller. They are features of
     the app and work as **modifiers** on whatever controller is playing, as they do on the
     touch pad and the Seaboard today.
   - The signal path is:

     ```
     controller (touch layout + optional logic, or a hardware MPE/MIDI controller)
       -> MPE notes, each for a part (the controller's split)
       -> app modifiers (tilt, breath, pitch assist, arp)
       -> the part's instrument (the app's parts, layered or not)
     ```

6. **Instruments come from you and other Faust creators; samples are supported** *(samples dropped by decision 20)*
   (2026-11-06).
   - Instruments should be as capable as VST or any other format and not rule anything out.
   - **Sample instruments are in.** The instrument format carries audio files alongside
     the DSP, and the library stores them.
   - A no-code builder is wanted but not a priority. Before it is planned, check whether it
     restricts or changes any other part of this plan.

7. **Modifiers are a replaceable chain; a controller can bring its own** (2026-11-06).
   - A modifier is a stage that takes MPE in and gives MPE out: arp, pitch assist, tilt,
     breath. Each has the same interface, so a better one can replace ours.
   - A controller declares, for each app modifier, on, off, or "I do this myself", plus its
     defaults. A controller with its own arp switches the app arp off.
   - **Split is not a modifier; it belongs to the controller.** Corrected while building
     step 0: the touch pad splits by screen area and the Grid by sections, so which sound
     a note goes to depends on where on the layout you touch. The controller says which
     part each note is for; the app owns the parts (sounds) and layering.
   - Later, modifiers can be packages of their own, in the same WebAssembly sandbox.
   - **Done for the arp in odo** (step 0, 2026-11-06): each part (sound) has its arp, saved
     with the sound in presets; controllers say `ArpUse.App`, `Off` or `Own`. External
     MIDI (the Seaboard) has the arp again; it had none from 0.16 to 0.20.

8. **The site is for non-technical people** (2026-11-06).
   - It is where you buy and download instruments (and controllers). Selling comes later
     (decision 1), but the site is designed with it in mind.
   - Downloading, you pick your operating system: Android, iOS, Windows, macOS, Linux.
   - Each instrument's page says, in plain words, what you can play it in. For example:
     "On your phone: the app. On Windows or Mac: the app, or any music software that loads
     plugins, such as Ableton Live, FL Studio, Logic or Reaper."
   - The first download on a computer must install whatever is needed (the app and the
     loader plugin) in one step. After that, new instruments appear in "my instruments"
     on every device. Nobody should need to know what VST3 or CLAP means.
9. **Open source; money only from a cut of creator sales** (2026-11-06).
   - The app, the engine and the package specs are open source. The aim is to make the
     world a better place.
   - If the project makes money, it is only by taking a cut of what creators sell, like
     Steam. No paid app, no subscription.
   - Licences: see decision 10.

10. **Everything open except our store** (2026-11-06).
    - **Open**: the app, the engine, the controller and instrument formats, and your own
      instruments and controllers.
      - App and engine: **MPL 2.0** (`LICENSE`). Changes to our files stay open; works on
        the App Store; mixes with closed creator content.
      - Package specs, once written: **Apache 2.0**, so any app or store can adopt them.
      - Your current instruments (`third_party/mpe-instruments/LICENSE`): **MIT**, and
        free. Paid instruments you make later live in a separate, private repo, so the
        MIT licence doesn't cover them. Check the licences of the Faust libraries each
        paid instrument uses before it is sold.
    - **Closed**: our website and store. Its code stays private and unlicensed (all rights
      reserved).
    - **Other stores are welcome**, the way PC gaming has Steam, GOG and Epic. The open
      formats make that possible: any store can sell packages that every app can play.
    - Creators choose their own licence. "Open" means the formats; a creator's paid
      instrument can be closed.
    - Contributors sign off their commits (DCO) so it's clear they had the right to
      contribute.

11. **Start like GOG: no DRM, room to add it later** (2026-11-06).
    - Paid items download as plain files you own and keep, like GOG's games. No online
      checks.
    - The package format leaves room for protection later, so adding it doesn't break old
      packages:
      - Every package is **signed** by the store that sells it. That's useful now anyway:
        the app can tell a package hasn't been tampered with, and which store it came from.
      - The manifest has an optional **licence** field (who bought it, from which store).
        Empty for now.
      - The format allows a protected payload (for example, compiled WebAssembly instead
        of Faust source, or encrypted) that an app may refuse to load without a valid
        licence. Not built yet.
    - Each store decides its own policy; ours starts with none.
12. **The app supports many stores** (2026-11-06).
    - The app reads a list of library sources. Ours is the default; users can add others,
      as F-Droid lets you add repositories. Users can also import packages from local files.
    - "My instruments" shows everything owned, from every store, in one place.
    - Stores need a common protocol (list, download, owned items). It belongs with the
      package specs, under Apache 2.0.

13. **Apps can differ per platform; packages are universal** (2026-11-06).
    - Each platform can have its own app, in whatever code suits it. The Android app stays
      Jetpack Compose. The others are chosen when we get to them.
    - What must be the same everywhere is how **controllers and instruments** behave. Like
      web browsers and HTML, every app implements the same specs. Three things make that
      hold:
      1. **The specs** for instrument, controller and store packages.
      2. **A shared core library in C++**, which every app and the plugin embed. It loads
         packages, runs instruments (Faust, samples, WebAssembly), runs controller logic
         in the sandbox, and runs the modifier chain. Apps only draw the UI and connect
         inputs and audio. Today's engine is the start of it.
      3. **A conformance test suite**: test controllers and instruments, with the output
         each must produce, that every app runs to show it plays packages correctly.
    - A controller's layout (layer 1) and drawing (layer 3) must look the same on every
      app, so the spec defines them as simple primitives (shapes, text, colours) that
      each app draws with its own toolkit.
    - The DAW plugin is one **CLAP** plugin, wrapped into VST3 and AU with clap-wrapper,
      with a small UI. It embeds the same core library.

14. **The category is called MESH** (2026-11-06): Multimodal Expression Sensor Host.

15. **Getting close to VST, in order** (2026-11-06). *Replaced by decision 20: Faust only.* Matching VST fully would mean
    downloadable native code, which the stores ban. So, in order:
    1. **Faust with sound files** first: one engine for synths, physical models and samplers.
    2. **Import SFZ and SoundFont (SF2)**, so there are many instruments from day one.
    3. **Any DSP compiled to WebAssembly**, planned for when the sandbox exists: the
       general route, as capable as VST if the speed allows.
    4. **Hosting real VST3/CLAP plugins**, desktop only, optional.
    Sample packages can be tens to hundreds of MB, and a recording can't change its tone
    the way a physical model can.
    WebAssembly is also the sandbox for controller logic and drawing (decision 5).

16. **Our app is called odo** (2026-11-06). The GitHub repo is now
    `PatrickReynoldsCoding/odo` (GitHub redirects the old address, so older installs still
    find updates). The Android package id stays `com.patrickreynolds.mpeinstruments`:
    changing it would make Android treat odo as a different app, losing updates, settings
    and presets.

17. **The player on the train comes first** (2026-11-06), after
    [VISION_PLAN_REVIEW.md](VISION_PLAN_REVIEW.md). odo is for someone who gets their phone
    out and plays: great sounds straight away, no setup. Nothing built for creators,
    developers or stores may add friction for them. Where the two pull apart, the player
    wins.
18. **A MESH sends MIDI out too** (2026-11-06). Besides playing its own sound, odo can be
    the controller for a DAW or another synth. How it's designed is under discussion below.

19. **No sideloading: the Play Store** (2026-11-06). Replaces decision 4. Asking a player
    to allow "install unknown apps" is friction (decision 17). odo moves to the Play Store;
    the GitHub APK and its in-app updater carry existing users across until then (Play
    doesn't allow an app to update itself from elsewhere, so the Play build has no
    updater). New personal Play accounts run a closed test first (about 12 testers for 14
    days), so start that early.
20. **Only Faust instruments** (2026-11-06). Replaces decision 15 and the samples in
    decision 6. No SFZ, SoundFont or Decent Sampler imports, no other DSP in WebAssembly, no
    hosting of VST plugins. One language keeps every instrument portable and the same
    everywhere.
21. **No odo sound in the DAW from the phone** (2026-11-06). MIDI out (decision 18) sends
    what you play; the DAW makes the sound. No audio from the phone to the computer.
22. **The same instruments on every system** (2026-11-06). You get attached to an
    instrument like an old guitar you inherited; it should be there wherever you make
    music. Every instrument you have plays on Android, iOS, Windows, macOS and Linux, and
    sounds the same. Faust (decision 20) is what makes that possible. On a computer that
    means a MESH app there, and, for music made in a DAW, the odo plugin playing the same
    instruments (decision 13).

23. **The instrument format** (2026-11-06): see [INSTRUMENT_FORMAT.md](../formats/INSTRUMENT_FORMAT.md).
    One package per instrument (manifest + self-contained Faust). Versions sit side by side
    and presets remember theirs, so an instrument never changes under you. Instruments may
    have a few knobs of their own, and be mono or stereo.

24. **The controller format** (2026-11-06): see [CONTROLLER_FORMAT.md](../formats/CONTROLLER_FORMAT.md).
    A controller is a WebAssembly program and a manifest, the same on every system. Touch
    screens only: a controller says what it needs. Sensors stay the MESH's, so a computer
    MESH can offer keyboard and mouse where a phone offers tilt and breath.
25. **Next: a basic computer MESH** (2026-11-06). A small MESH on Linux and/or Windows that
    plays the same instrument and controller files as odo, to test the formats for real.

26. **The formats work on two systems** (2026-11-06). The same `.odoi` and `.odoc` files play
    in odo on Android and in odo for computers (`desktop/`, SDL3, Linux now, Windows next).
    The engine compiles instrument packages in on Android and with libfaust while running on
    a computer; both run controllers in wasm3. Run it with `desktop/run.sh`.

27. **Interpreter experiment, first results** (2026-11-06). All 62 instruments compiled
    to WebAssembly (`faust -lang wasm`) and run in wasm3, the interpreter the engine already
    has, against the same instruments compiled to native code. 8 fingers each, on the x86_64
    emulator and on the Linux laptop (`engine/tools/bench_wasm`, results in
    `engine/tools/results/`).
    - **Same sound:** WebAssembly and native differ by at most 2 parts in 10,000.
    - **About 7x the processing** (7.2x on the emulator, 6.0x on the laptop; 3x to 18x by
      instrument).
    - **Most instruments add no latency:** 52 of 62 still play 8 fingers in callbacks of 2 ms
      or less on the emulator, as native does.
    - **The physical models don't fit:** cello, violin, the bass voice, marimba and piano
      can't keep up with 8 fingers at any buffer size; soprano and tenor need 8 to 16 ms
      buffers. Native plays all 62 at 0.7 to 2 ms.
    - **Caveat:** the emulator runs on this laptop's CPU. A phone is slower, so on a phone
      the heavy instruments fail by more and some middle ones may join them. Run
      `bench_wasm` on a real phone (arm64) before deciding.
    - Directions: a faster WebAssembly engine (a JIT, which Android allows for sandboxed
      code but iOS does not), fewer fingers for heavy downloaded instruments, or keep
      built-in instruments native and only downloaded ones as WebAssembly.

## Under discussion

### MIDI out: how should it work?

odo as a controller (MIDI only, decision 21): odo sends what you play as MPE; the DAW's
synth makes the sound and odo stays quiet. What goes out is what you play *after* odo's
   modifiers: tilt vibrato becomes pitch bend, breath becomes pressure or tone, the arp's
   notes go out as notes. Settings: MPE or plain MIDI (for synths without MPE), the pitch
   bend range, and odo's own sound on or off.

How MIDI gets to the computer, in order:
- **USB**: Android can appear to a computer as a USB MIDI device ("MIDI" in the phone's
  USB settings). Lowest latency, no pairing, and works with every DAW. First.
- **Wi-Fi** (RTP-MIDI): built into macOS; Windows needs a free driver. Wireless.
- **Bluetooth LE MIDI**: wireless and common for controllers, but Android has no built-in
  support for being a Bluetooth MIDI device, so odo would have to implement it.

Engineering note: the arp, tilt and breath run inside the engine, so the engine has to
emit the MIDI it plays, not the app. The engine needs a queue of outgoing MIDI events.
