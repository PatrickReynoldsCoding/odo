# odo's docs

How to work on odo (both apps) is in [AGENTS.md](../AGENTS.md); what odo is, in the
[README](../README.md).

## formats/: the files odo reads and writes

Both apps (Android and computers) follow these, so a file made by one works in the other.

- [INSTRUMENT_FORMAT.md](formats/INSTRUMENT_FORMAT.md): instrument packages (`.odoi`), the voice
  contract, and **versions** (keeping an old version, going back to it).
- [CONTROLLER_FORMAT.md](formats/CONTROLLER_FORMAT.md): controller packages (`.odoc`) and how
  their settings show on the rail.
- [PRESET_FORMAT.md](formats/PRESET_FORMAT.md): preset files (`.odopreset`).

## vision/: where odo is going

- [MESH.md](vision/MESH.md): what a MESH is.
- [VISION_PLAN.md](vision/VISION_PLAN.md): the plan and its decisions log;
  [VISION_PLAN_REVIEW.md](vision/VISION_PLAN_REVIEW.md), a second opinion on it.
- [NATIVE_SPEED.md](vision/NATIVE_SPEED.md): native speed on the phone stores (to discuss).
- [PROMOTING_MESH.md](vision/PROMOTING_MESH.md): not written yet.

## plans/: plans for one piece of work, and notes

- [DECOUPLE_CONTROLLER_PLAN.md](plans/DECOUPLE_CONTROLLER_PLAN.md): taking the touch pad out of
  the app as a controller.
- [STUDY.md](plans/STUDY.md): other music libraries to look at.
- [HEX_CONTROLLER.md](plans/HEX_CONTROLLER.md): the Hex keyboard, an isomorphic controller: what
  the Lumatone and Hexatone do (layouts, tunings, colours, touch), and what we built.

## android/: the phone

- [LATENCY.md](android/LATENCY.md): what odo does about latency, and what was measured.
- [PHONE_TEST.md](android/PHONE_TEST.md): the first run on a real phone.
