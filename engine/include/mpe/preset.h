// A preset as a file: what one odo sends another (a phone to a computer, say). Both apps read and
// write presets through here, so a file made by one loads in the other. PRESET_FORMAT.md has the
// format; this is the one reader and the one writer of it.
#pragma once
#include <string>
#include <vector>
#include "mpe/package.h"

namespace mpe::preset {

constexpr const char* kFormat = "odo-preset/1";   // "odo-preset/<major>": a newer major is refused
constexpr const char* kExtension = ".odopreset";
constexpr const char* kMimeType = "application/vnd.odo.preset+json";
constexpr size_t kMaxBytes = 1 << 20;             // a preset is a few kB: anything near this is not one
constexpr int kMaxSides = 4;
constexpr int kMaxParts = 4;

// One part's arpeggiator (mpe::ArpConfig's values).
struct Arp {
    bool on = false;
    int order = 0;             // ArpOrder: 0 up, 1 down, 2 up-down, 3 as played, 4 random, 5 chord
    double rate = 4, gate = 0.6;
    int octaves = 1;
    bool latch = false;
};

// One part: a sound, and (if `full`) how it is played. Not full: only the sound is given, and it
// plays with that sound's own effects (what odo for computers does, which has no part settings).
struct Part {
    std::string sound;         // the instrument package's id: "patrickreynolds/cello"
    bool full = false;
    int transpose = 0;
    double volume = 1, brightness = 1, drive = 0, pressureCurve = 1, reverb = 0.15, delay = 0;
    double release = 0;        // s the note plays on after the finger lifts; 0: the instrument's own
    Arp arp;
};

// What a side of the screen plays: a controller, its settings, and its parts' sounds (a split
// controller's, such as the Grid's second grid, are its second part and on).
struct Side {
    std::string controller;    // the controller package's id ("patrickreynolds/grid"), "midi" for
                               // External MIDI, or "": any controller, the one in use
    Json settings;             // an object: setting id -> its value (a number or true/false)
    std::vector<Part> parts;   // 0 to kMaxParts
};

// The room and the clock all parts share.
struct Effects {
    double bpm = 110, reverbSize = 0.5, reverbDamp = 0.4, delayBeats = 0.75, delayFeedback = 0.35;
    bool delayPingPong = true;
};

struct Preset {
    std::string name;
    int split = 0;             // how the screen is split into sides: odo for computers' kSplits index; 0 one side
    std::vector<Side> sides;   // 1 to kMaxSides
    bool layered = false;      // one side's every note plays its first two parts
    bool hasEffects = false;   // false: each app keeps its own
    Effects effects;
    std::string madeBy;        // "odo 0.28.0 (Android)": shown, never relied on
};

// Reads a preset file. False, with *error saying why in a sentence for the user, if it is not one
// or is too new. What it does not know it leaves out; what it leaves out takes the defaults above.
bool parse(const std::string& text, Preset& out, std::string* error = nullptr);
// The file, as text (UTF-8 JSON, two-space indented).
std::string write(const Preset& p);

// A file name for it: the name without characters a file system minds, plus kExtension.
std::string fileName(const Preset& p);

// One part, or the effects, as the file has them: for an app that keeps a preset's parts in its own
// settings (odo for computers keeps those it was given, to give them back).
Json partJson(const Part& part);
Part partFrom(const Json& v);
Json effectsJson(const Effects& e);
Effects effectsFrom(const Json& v);

// Compact JSON of any value (each number as the shortest text that reads back the same).
std::string toJson(const Json& v);

}  // namespace mpe::preset
