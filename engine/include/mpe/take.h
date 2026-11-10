// Takes: a performance kept as the touches that made it, not as sound. Both apps record, open
// and play takes through here, so a take made by one plays in the other. The file format is
// docs/formats/TAKE_FORMAT.md; how to write a piece by hand, docs/formats/TAKE_AUTHORING.md.
//
// A take holds, for each side (a controller on part of the screen):
//   - the set-up: the controller, its settings, the sounds, the surface's size; and the rest of
//     a preset (each part's effects and level, the reverb and delay, the arp, the split) and the
//     master volume, so it plays as it did;
//   - the touches, at full rate, in coordinates of the surface (0..1 across and down);
//   - the notes the controller played from them (pitch with its bends, pressure, slide);
//   - sections, and words to show (for teaching).
// Or, written by hand, a score: notes in bars and beats, with gestures (bend, vibrato, swell,
// slide). A score is expanded on the controller in use (Layout, expand) into touches, and those
// played through the controller (render) into notes: the same as a recorded take.
//
// Playing (Player) sends the take's notes into a ControllerHost, as its own notes go, so it
// sounds as it was played; and draws a ghost finger for each touch, over the controller.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "mpe/controller.h"
#include "mpe/package.h"
#include "mpe/preset.h"

namespace mpe::take {

constexpr const char* kFormat = "odo-take/1.1";  // "odo-take/<major>[.<minor>]": a newer major is refused
                                                  // 1.1: the "preset" and "volume" lines
constexpr const char* kExtension = ".odotake";
constexpr const char* kMimeType = "text/vnd.odo.take";
constexpr size_t kMaxBytes = 32u << 20;           // about 15 minutes of ten fingers

enum Phase { Down = 0, Move = 1, Up = 2, Cancel = 3 };
enum NoteKind { On = 0, Change = 1, Off = 2, Volume = 3 };

// A finger on a side's surface. t: ms from the take's start. x, y: 0..1 across and down the
// surface; force 0..1; size in dp (0: not known).
struct Touch { double t = 0; int side = 0, finger = 0, phase = Down; float x = 0, y = 0, force = 0, size = 0; };
// A note the controller played: as ControllerHost's taps give it. pitch in semitones (MIDI note
// numbers, fractions for bends); pressure, slide, velocity 0..1; Volume carries its volume in pressure.
struct Note { double t = 0; int side = 0, voice = 0, kind = On, part = 0; float pitch = 0, pressure = 0, slide = 0, velocity = 0; };
// A setting changed while it played (on the rail, or by the controller itself).
struct Setting { double t = 0; int side = 0; std::string id; double value = 0; };
struct Section { double t = 0; std::string name; };
struct Say { double t = 0, dur = 0; std::string text; };

// What a side played on.
struct Side {
    std::string controller, controllerVersion;               // "patrickreynolds/grid", "2.8.0"
    std::vector<std::pair<std::string, double>> settings;    // setting id -> value, at the start
    std::vector<std::string> sounds;                         // each part's sound (instrument package id)
    float width = 0, height = 0;                             // its surface, dp (0: not known)
    float centers[2] = {60, 60};                             // each part's instrument middle (MIDI note)
    bool hasCenters = false;
};

struct Take {
    std::string name, by, about, madeBy;
    double tempo = 0;                    // bpm (quarter notes), 0: not known
    int meterBeats = 4, meterUnit = 4;
    std::string key;                     // "D dorian"; for the eye
    double speed = 1;                    // the speed it was played at (a take recorded while playing along slowly)
    std::vector<Side> sides;
    std::vector<Touch> touches;          // in time order
    std::vector<Note> notes;             // in time order
    std::vector<Setting> settings;
    std::vector<Section> sections;
    std::vector<Say> says;
    // The rest of the set-up, as a preset (PRESET_FORMAT.md): each side's parts (the sound, its
    // version, its effects, level and arp), the shared effects, the split, layering. Each side's
    // controller and settings are the Side's (the "controller" and "settings" lines), not here:
    // presetOf() puts the two together. Not given (a take of before 1.1, or a score that names
    // only what it needs): hasPreset false.
    bool hasPreset = false;
    preset::Preset preset;
    double volume = -1;                  // the master volume (1: the usual level, to 2); < 0: not given
    std::string score;                   // the score's text (a take written by hand), or ""
    int scoreLine = 0;                   // the file line its first line is on, for messages

    bool performed() const { return !touches.empty() || !notes.empty(); }
    double length() const;               // ms: to its last touch, note or word
};

// Reads a take. False, with *error a sentence for the user, if it is not one or is too new.
// Lines it does not know are left out, with a warning each (line numbers from 1).
bool parse(const std::string& text, Take& out, std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);
// The file's text. Numbers are written so they read back exactly.
std::string write(const Take& t);
// A file name for it: the name, without characters a file system minds, plus kExtension.
std::string fileName(const Take& t);
// The take's whole set-up as a preset, named as the take: its preset (if it has one), with each
// side's controller, settings and sounds from the take's own lines over it. What an app applies
// before playing it (as loading a preset does), and what "Keep its settings as a new preset" keeps.
preset::Preset presetOf(const Take& t);

// ---- recording -----------------------------------------------------------------------------------

// A change or a volume that says nothing new (the same numbers as the voice's last) is left out of
// a take: it would send no MIDI, and a controller may send one every frame. Whatever records notes
// (the Recorder, render) keeps one of these per side.
class Unchanged {
public:
    bool keep(int voice, int kind, float pitch, float pressure, float slide);
private:
    std::map<int, std::array<float, 3>> shape_;
    std::map<int, float> volume_;
};

// Keeps what is played on one or more controllers. attach() each side's host after start(); the
// take begins at the first finger down (as a loop does: no silence before it). Fingers already
// down when it starts, and their notes, are left out. UI thread only, as the hosts are.
class Recorder {
public:
    Recorder();
    ~Recorder();
    // setup: what plays now, as a preset (each side's controller and settings are taken from the
    // hosts; the rest is kept as the take's preset); volume: the master volume (< 0: not kept).
    void start(const preset::Preset* setup = nullptr, double volume = -1);
    // A side to record, with what is playing on it now: its sounds (the instrument package ids).
    void attach(ControllerHost& host, int side, const std::vector<std::string>& sounds = {});
    bool recording() const;
    double elapsedMs() const;            // since the first finger, 0 before
    Take stop();                         // what was recorded; the hosts are let go of
private:
    struct State;
    std::shared_ptr<State> s_;
};

// ---- a score: a take written by hand (TAKE_AUTHORING.md) ------------------------------------------

struct Gesture {
    enum Kind { Vibrato, Bend, Swell, Press, Tone, Slide, Staccato, Say, At, Part, Pitch } kind;
    float a = 0, b = 0, c = 0;           // Vibrato: depth semitones, rate Hz, from (0..1). Bend: to, from, until.
                                         // Swell: start, end, middle (<0: none). Tone: start, end. Slide: fraction. At: x, y.
    bool absolute = false;               // Bend: `a` is a note number, not semitones from the note
    // Press, Tone, Pitch: values spread evenly through the note, from its start to its end (Pitch:
    // semitones from the note). One value: all through it.
    std::vector<float> pts;
    std::string text;                    // Say
};

struct ScoreNote {
    double beat = 0, beats = 1;          // from the start, in quarter notes
    std::vector<float> pitches;          // MIDI notes, a chord has several; none: a rest (not kept)
    std::string voice;
    std::vector<Gesture> gestures;
    int line = 0;                        // in the file
};

struct Score {
    double tempo = 100;                  // at beat 0
    std::vector<std::pair<double, double>> tempos;   // beat -> bpm, in order (the first at 0)
    int meterBeats = 4, meterUnit = 4;
    std::vector<ScoreNote> notes;        // in time order
    std::vector<std::pair<double, std::string>> sections;   // beat, name
    std::vector<std::pair<double, std::string>> says;       // beat, words (until the next, or the section's end)
    double end = 0;                      // beats: the end of the last voice
    double ms(double beat) const;        // beat -> ms, by the tempos
    double barBeats() const { return meterBeats * 4.0 / meterUnit; }
};

// A score's text (Take::score) to notes. Errors (the line, and what is wrong) stop it; warnings
// (a bar that does not add up) do not. `firstLine`: the file line of its first line.
bool parseScore(const std::string& text, Score& out, std::vector<std::string>* errors, std::vector<std::string>* warnings,
                int firstLine = 1, double defaultTempo = 0, int meterBeats = 0, int meterUnit = 0, const std::string& key = "");
// A note's name to its number: "C4" 60, "F#3" 54, "Bb5" 82; -1 if it is not one.
int noteNumber(const std::string& name);
std::string noteName(float pitch);       // 60 -> "C4", 60.5 -> "C4+0.50"

// ---- where the notes are on a controller ---------------------------------------------------------

// A controller's notes found by touching it: a copy of it (not the one on screen) is touched all
// over, on its own clock, and where each note starts is kept. Works for any controller.
class Layout {
public:
    struct Cell {
        int note = 0, part = 0;
        float x = 0, y = 0;              // its middle, dp
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the box around it
        int probes = 0;                  // how many probes landed on it (its size)
    };
    // How a finger's place in a cell, and its force, change what it plays (fitted from probes).
    struct Response {
        float p0 = 0, s0 = 0;            // pressure and slide at the middle
        float pdx = 0, pdy = 0, sdx = 0, sdy = 0;   // per dp across and down
        bool force = false;              // the pressure follows the finger's force
        bool known = false;
    };

    Layout();
    ~Layout();
    Layout(Layout&&) noexcept;
    Layout& operator=(Layout&&) noexcept;
    // values: its settings by position (as ControllerHost::values()). False and *error if it can't run.
    bool scan(const Package& controller, const std::vector<double>& values, float width, float height,
              const float centers[2], std::string* error = nullptr);
    const std::vector<Cell>& cells() const { return cells_; }
    float width() const { return w_; }
    float height() const { return h_; }
    // The cell playing `note` nearest (x, y) (dp; < 0: the middle of the surface); null if none.
    // part < 0: any part.
    const Cell* find(int note, float x, float y, int part = -1) const;
    Response response(const Cell& c);    // probed the first time it is asked for
    // What a touch at (x, y) starts: false if no note.
    bool probe(float x, float y, float force, Note* out);

private:
    struct Sim;
    std::unique_ptr<Sim> sim_;
    std::vector<Cell> cells_;
    std::vector<Response> responses_;
    float w_ = 0, h_ = 0;
};

struct Report {
    std::vector<std::string> errors, warnings;
    // Each score note: what the controller played for it (the sounding pitch nearest the one
    // wanted, just after it starts; the lowest and highest while held), to check against it.
    struct Played {
        int line = 0; float wanted = 0; double t = 0, end = 0; float pitch = 0, low = 0, high = 0; bool found = false;
        bool curved = false; // it has a pitch curve
        float slack = 0;     // ... and the nearest that curve comes to the note (semitones)
        // Heard as written: it came within `tolerance` of the note. A curved note within a quarter of a
        // semitone of its curve's nearest (a controller follows a moving finger a moment late).
        bool asWritten(float tolerance = 0.05f) const {
            return found && std::fabs(pitch - wanted) <= (curved ? slack + std::max(tolerance, 0.25f) : tolerance);
        }
    };
    std::vector<Played> played;
};

// A score, on this layout, to touches (in `out`: touches, sections, says; times in ms; side 0).
bool expand(const Score& score, Layout& layout, Take& out, Report* report);
// Plays a take's touches through a copy of the controller, on its own clock, and keeps the notes
// it plays in `t.notes` (replacing any). values: its settings by position.
bool render(Take& t, const Package& controller, const std::vector<double>& values, float width, float height,
            const float centers[2], std::string* error = nullptr);
// All of it for a take written by hand: its score, expanded on this controller as set (with the
// take's own settings over `values`) at this size, and rendered. A take that was performed is
// left as it is. Afterwards t.performed().
bool prepare(Take& t, const Package& controller, std::vector<double> values, float width, float height,
             const float centers[2], Report* report = nullptr);
// The take's settings for side `side` over `values` (by position, from the controller's manifest).
std::vector<double> settingsFor(const Take& t, int side, const Json& manifest, std::vector<double> values);

// Whether side 0's controller is set (values, by position) and sized (w x h dp) as the take was
// laid out on: its touches (the ghost fingers) land on the keys that play its notes.
bool laidOutFor(const Take& t, const Json& manifest, const std::vector<double>& values, float width, float height);
// Its ghost fingers laid out again on the controller as it is now (values as they are, not the
// take's): a take written by hand from its score, one that was played from its notes (each held
// note a finger on the key for it, moving with its bends, pressure and tone). What it plays (its
// notes) is unchanged. Side 0's touches are replaced; false if the controller cannot be scanned.
bool relayout(Take& t, const Package& controller, const std::vector<double>& values, float width, float height,
              const float centers[2], Report* report = nullptr);

// ---- playing -------------------------------------------------------------------------------------

// The look of the ghost fingers: from ui/tokens.json's "ghost" (each app gives its own tokens).
struct GhostStyle {
    uint32_t finger = 0xFFFFFFFFu, trail = 0xFFFFFF80u, next = 0xFFFFFF60u, text = 0xFFFFFFFFu, back = 0x000000A0u;
    float radius = 22, grow = 16, trailMs = 450, previewMs = 700, textSize = 15;
    // How the fingers look (the app's Fingers choice): rings closing in on the next note, a ring under
    // each finger and its trail; small dots; notes falling from the top to their key, each held one
    // with a bar of the time it has left; or none (only the line along the top and the words).
    enum Look { Rings = 0, Dots = 1, Falling = 2, Off = 3 };
    int look = Rings;
};
// The Fingers choice's names, in order (Look): the same words in both apps.
inline const char* const kGhostLooks[4] = {"Rings", "Dots", "Falling", "Off"};

class Player {
public:
    enum Mode { Listen = 0, PlayAlong = 1 };
    // Where its notes go: side, and the note (its voice its own; play it with injectNote).
    using Sink = std::function<void(const Note&)>;

    void load(Take t);                   // a performed take (prepare() one written by hand first)
    const Take& take() const { return take_; }
    bool loaded() const { return loaded_; }

    void play(Mode m, double nowMs, const Sink& sink);   // from the loop's start, or the start
    void stop(const Sink& sink);         // its notes let go of
    bool playing() const { return playing_; }
    Mode mode() const { return mode_; }
    void setSpeed(double s);             // 0.25..2: its timing, not its pitch
    double speed() const { return speed_; }
    // Play section k over and over (-1: all of it, once).
    void loopSection(int k, double nowMs, const Sink& sink);
    int loopedSection() const { return loop_; }
    // Time moves on: the notes due go to the sink. At the end it stops by itself.
    void update(double nowMs, const Sink& sink);

    double position() const { return pos_; }   // ms into the take
    double length() const { return length_; }
    int sectionAt(double pos) const;     // -1 before the first
    const Say* sayAt(double pos) const;

    // The ghost fingers over side `side`'s surface (w x h dp) now, as a draw list
    // (CONTROLLER_FORMAT.md's): each finger down, with its trail; the next ones coming; the words.
    std::vector<uint8_t> drawGhosts(int side, float w, float h, const GhostStyle& style) const;
    // New touches for the take playing (relayout()'s): it plays on where it was, its notes as they were.
    void retouch(std::vector<Touch> touches);

private:
    // One finger from down to up: its touches (indices into take_.touches) and the note it started.
    struct Stroke { int side = 0, finger = 0; std::vector<size_t> events; double down = 0, up = 0; std::string label; };
    void seek(double pos);
    void buildStrokes();
    void allOff(const Sink& sink);
    bool at(const Stroke& s, double pos, float* x, float* y, float* force) const;

    Take take_;
    std::vector<Stroke> strokes_;
    bool loaded_ = false, playing_ = false;
    Mode mode_ = Listen;
    double speed_ = 1, pos_ = 0, last_ = 0, length_ = 0;
    size_t next_ = 0;                    // the next note to play
    int loop_ = -1;
    double loopFrom_ = 0, loopTo_ = 0;
    std::vector<std::pair<int, int>> sounding_;   // side, voice
};

}  // namespace mpe::take
