# Review of the vision plan

A second opinion on [VISION_PLAN.md](VISION_PLAN.md) and [MESH.md](MESH.md): does the idea
have a unique selling point (USP), and which parts look premature or low-value next to the
tools that already exist?

Written 2026-11-06. The notes on other apps come from memory, not fresh research. Check
which of them are on Android today before relying on them (see "To check" at the bottom).

## In short

There is a real USP, but it is smaller and more specific than the documents suggest. Most
of the plan is about the ecosystem (specs, stores, conformance, other MESH apps). The
ecosystem is not what will make people want odo.

## Where the USP is

### 1. Expressive playing on a phone, on Android

On iOS this space is crowded: GeoShred, ThumbJam, Animoog, and the AUv3 world with hosts
such as AUM. On Android there is very little, mostly because Android audio latency was bad
for years.

About 25 ms from touch to sound ([LATENCY.md](../android/LATENCY.md)), with MPE, tilt and breath, on a
phone people already own, fills a real gap. This is the strongest card, and the app already
has it.

### 2. Controllers and instruments as separate, swappable things

This is the new idea. Today's tools each cover half of it:

- **TouchOSC, Lemur**: you can design and share touch controllers, with scripting, but
  they make no sound. They only send MIDI or OSC.
- **Decent Sampler, Kontakt, Pianobook libraries**: you can share instruments, but each one
  comes with its own fixed way to play it.
- **GeoShred, ThumbJam**: a great playing surface paired with great sounds, but you can't
  share or swap either part.

As far as we know, nobody offers "any playing surface × any sound, with sensors on top" in
one app you can play straight away. That is a genuine USP.

### 3. Sensors as modifiers

Tilt and breath work on any controller. That's a good feature, but not a USP by itself:
ThumbJam and GeoShred already use tilt. It makes point 2 stronger.

## What looks premature or low-value

### MESH as a category, with conformance tests, many stores and other MESH apps

Standards come from a product that has succeeded, not the other way round. Nobody will
build a second MESH until odo has users. HTML was just what Mosaic and Netscape did before
it became a standard.

**Suggestion:** keep the package formats clean and versioned. That's cheap and keeps
options open. Put off the conformance suite, the store protocol, signing and the licence
field until someone actually asks for them.

### The store and the creator marketplace

- You would need two groups of creators at once: instrument makers and controller makers.
  Very few people write Faust.
- Paid mobile instruments are a small market.

**Suggestion:** instead of waiting for creators, import formats that already have large
free libraries. SFZ and SF2 are already in decision 15. Add **Decent Sampler**
(`.dspreset`): it has a big free community library, much of Pianobook among it. That gives
odo thousands of instruments on day one, with no creators needed.

### The DAW plugin

Inside a DAW, odo's controllers and sensors disappear. What's left is one more synth plugin
among thousands.

**Suggestion:** turn it around. Make odo on the phone an **MPE controller for the
computer**, sending MPE with tilt and breath to a DAW over USB or Bluetooth MIDI. That
carries the USP into the studio instead of leaving it behind, and it is probably cheaper to
build than a CLAP plugin.

### Five platforms

iOS is where the competition is strongest. On desktop, people play through a DAW. Android
first, with the desktop maybe as a companion, matches where the gap is.

### Controller layer 3 (controllers that draw themselves: physics, games)

Fun, but far away. Fine to leave room for in the spec and nothing more.

## The tension to watch: two audiences

- **The player on the train** wants good sound straight away: great presets, scale lock,
  pitch assist, no setup. They will never care about specs, packages or stores.
- **Creators and developers** care about open formats, sandboxes and stores.

The appeal comes from the first group, but most of the vision plan serves the second.

**Suggestion:** turn that around. Make odo great for the player. Build the
controller/instrument split as good internal design, and open it up once people ask to
make their own.

## A gap in the plan: nothing tests demand

Phases 2 and 3 test whether the technology works. No phase tests whether people want it.

**Suggestion: a demand test now.** Post the current APK and a short video of someone
playing it on a phone to:

- r/synthesizers and r/WeAreTheMusicMakers;
- KVR's mobile music forum;
- the MPE and ROLI communities.

Watch whether people come back to it and what they ask for. That says more about the USP
than any spec will.

## Suggested changes to the phases

| Phase in the plan | Suggestion |
| --- | --- |
| 0. Separate the controller | Keep. Good design, whatever happens next. |
| 1. Write the specs | Shrink to versioned instrument and controller formats. Drop signing, licence field and store protocol for now. |
| 2. Interpreter experiment, core library | Keep. |
| 3. First other platform (CLAP or desktop) | Consider "odo as an MPE controller for a computer" first. |
| 4. Library site | Put off. Import SFZ, SF2 and Decent Sampler first. |
| 5–8. Creators, sandbox, payments, platforms | Only once the demand test shows people turning up. |
| *(new)* Demand test | Add now, before phase 1. |

## To check

These claims came from memory. They decide how big the Android gap really is:

- Which of GeoShred, ThumbJam, Animoog, ROLI's apps and Decent Sampler run on Android
  today, and how well.
- Whether TouchOSC on Android still supports scripted controllers.
- The licence of Decent Sampler's format and of the Pianobook libraries, before importing
  them.
