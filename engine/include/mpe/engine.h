// The sound engine: MPE messages in, stereo audio out. No Android and no ALSA here.
//
// Threads:
//   MIDI thread   -> MidiInput::feed()          (parse, clean, queue; wait-free)
//   audio thread  -> Engine::render()           (never allocates, never locks)
//   control/UI    -> setInstrument(), setVolume(), ... collectRetired()
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "mpe/clean.h"
#include "mpe/midi.h"
#include "mpe/spsc.h"
#include "mpe/looper.h"

class dsp;   // Faust

namespace mpe {

// One instrument: compiled in (generated from instruments/<group>/<name>.dsp, or from instrument
// packages), or added while running (an instrument package compiled by libfaust: mpe/faust_loader.h).
struct InstrumentInfo {
    std::string id;            // "patrickreynolds/cello" (a package's), or "strings/cello"
    std::string group;         // "strings"
    std::string name;          // "Cello"
    std::string description;
    std::function<dsp*()> create;
    std::function<const char*(const char*)> meta;   // a `declare <key> "<value>"` from the .dsp, or ""
    std::function<dsp*()> createPost;                // the stage all the voices share (an instrument body), or null
    std::vector<std::string> aliases;                // older ids it is also found by ("strings/cello")
    std::string version;                             // a package's version, or ""
};
// The instruments there are. Only the control thread adds to or reads the table; an entry never
// moves once added.
int instrumentCount();
const InstrumentInfo& instrumentAt(int index);
int addInstrument(InstrumentInfo info);             // its index
// By id or alias: the newest version there is, or with "@<version>" ("patrickreynolds/cello@1.0.0")
// that one. Index, or -1.
int findInstrument(const char* id);
// Versions sit side by side (INSTRUMENT_FORMAT.md): the instrument's versions, newest first
// (index's own id; just index if it has no others).
std::vector<int> instrumentVersions(int index);
bool isNewestVersion(int index);
// "1.10.0" > "1.9.2": < 0, 0 or > 0. "" is older than any version.
int compareVersions(const std::string& a, const std::string& b);

// How the fingers are turned into notes. Handed to the audio thread whole (setPerformance).
enum class Layout : int { Single = 0, Split = 1, Layer = 2 };
enum class ArpOrder : int { Up = 0, Down = 1, UpDown = 2, AsPlayed = 3, Random = 4, Chord = 5 };

struct PartConfig {
    bool arp = false;          // false: every finger plays its own note, as an MPE controller does
    ArpOrder order = ArpOrder::Up;
    float rate = 4;            // arp steps per beat (4 = sixteenths)
    float gate = 0.6f;         // part of a step the note is held, 0.05..1
    int octaves = 1;           // the held notes, then again 1, 2 or 3 octaves up
    bool latch = false;        // keep arpeggiating the last chord after the hand lifts
    int transpose = 0;         // semitones, on top of the engine's transpose
    float volume = 1;
    // sound
    float brightness = 1;      // 0 = dark (300 Hz low-pass) .. 1 = open (no filter)
    float drive = 0;           // 0 = clean .. 1 = saturated
    float pressureCurve = 1;   // pressure ^ curve: < 1 swells sooner, > 1 needs a firmer press
    float reverb = 0;          // send to the master reverb, 0..1
    float delay = 0;           // send to the master delay, 0..1
    float release = 0;         // s: after the finger lifts the note plays on as it was and fades out
                               // over this long (0 = the instrument's own ending)
};

// The sounds that can play at once: a split can give each section its own (the grid, up to four).
constexpr int kParts = 4;

struct Performance {
    Layout layout = Layout::Single;
    int splitNote = 60;        // Split: notes below go to part 0 (left), the rest to part 1, unless the
                               // finger says its part (CC 9 = part + 1, before its note on)
    float bpm = 120;
    PartConfig part[kParts];
    // Pitch assist: a finger that rests is pulled to the nearest note of the scale.
    uint16_t scaleMask = 0;    // bit n: (root + n) % 12 is in the scale; 0 = off
    int scaleRoot = 0;         // 0 = C
    float assist = 0;          // 0 = off .. 1 = locked to the scale
    // master effects
    float reverbSize = 0.5f, reverbDamp = 0.4f;
    float delayBeats = 0.75f;  // delay time in beats (0.75 = dotted eighth), follows bpm
    float delayFeedback = 0.35f;
    bool delayPingPong = true;
};

// What is sounding, for the touch view and the tests. Copied out of the audio thread's state
// field by field, so a snapshot can mix two blocks; fine for drawing.
struct VoiceView {
    int part = 0, finger = -1, note = -1;   // finger = MIDI channel it follows, -1 = latched
    bool gate = false;
    float pitch = 0;           // semitones, as played (note + bend + transpose + assist)
    float pressure = 0, slide = 0, level = 0;
};
struct FingerView {
    int channel = 0, note = -1;             // note -1 = not held
    float pitch = 0, pressure = 0, slide = 0;
};

// Modifiers from the phone itself: tilt (its gyro) and breath (its microphone). They act on every
// voice of both parts, whoever plays the notes (a controller or the touch pad).
//   Vibrato: only the quick wobble of the phone bends the pitch; holding it at a slant does not.
//   Bend:    tilting bends the pitch, from the angle the phone was at when the first finger went down.
//   Tone:    tilting moves slide (each instrument's own tone control) up or down.
enum class TiltMode : int { Off = 0, Vibrato = 1, Bend = 2, Tone = 3 };   // the twist only (see setTiltMode)

// The gyro: four sources from the phone's motion, each driving one target.
//   Twist: turned like a steering wheel (about the axis out of the screen), clockwise positive.
//   Tip:   the top edge tipped away from you, positive (about the screen's left-right axis).
//   Turn:  the right edge turned away from you, positive (about the screen's up-down axis).
//   Shake: how hard the phone is being shaken, 0..1.
// The turns are in degrees from where the phone was when the notes started: the zero follows
// the phone while no finger is down. A turn's full effect is at 30 degrees, a shake's at 1.
enum class GyroAxis : int { Twist = 0, Tip = 1, Turn = 2, Shake = 3 };
constexpr int kGyroAxes = 4;
// What a source does, and its amount's unit (at the full turn or shake):
//   Bend semitones · Vibrato cents (a turn: at a firm 3.5 degree wobble; a shake: a 5.5 Hz
//   vibrato as deep as the shake) · Volume, Tone, Brightness, Reverb, Delay 0..1 · Release seconds.
//   Volume and Brightness from a turn go both ways from the zero (tip back: quieter, darker);
//   from a shake they are lowered by the amount at rest and come back as you shake.
enum class GyroTarget : int { Off = 0, Bend, Vibrato, Volume, Tone, Brightness, Release, Reverb, Delay };
//   Swell:   breath is the loudness: no breath, no sound. It also replaces the fingers' pressure,
//            so instruments that respond to pressure (bow, voice, reed, filter) respond to it.
//   Tone:    notes play as usual; breath opens a filter, dark when not blowing (a wah).
enum class BreathMode : int { Off = 0, Swell = 1, Tone = 2 };

// What the modifiers are doing now, for the screen.
struct ModView {
    float tilt = 0;        // degrees the twist's effect sees: the wobble (Vibrato) or the angle from the zero
    float axis[kGyroAxes] = {};   // what each source's effect sees: degrees (or the wobble) and the shake 0..1
    float gain = 1, bright = 1;   // the gyro's volume and brightness, as multipliers
    float release = 0, reverb = 0, delay = 0;   // added by the gyro: seconds, sends
    float bend = 0;        // semitones added to every held note
    float slide = 0;       // added to every note's slide (-1..1)
    float breath = 0;      // 0..1, as applied
    bool held = false;     // a finger is down (the tilt zero is fixed while one is)
};

struct Stats {
    // Time from "bytes arrived" to "the audio block that plays them started", in ms.
    double eventAgeAvgMs = 0, eventAgeMaxMs = 0, eventAgeLastMs = 0;
    // Time spent in render() as a fraction of the audio it produced. Over 1.0 = dropouts.
    double loadAvg = 0, loadPeak = 0;
    uint64_t events = 0, dropped = 0, panics = 0, blocks = 0;
    uint64_t voiceResets = 0;     // voices that produced NaN/Inf and were silenced
    double noteCenter = -1;       // average of the last 16 notes played (as sent, no transpose); -1 = none yet
    uint64_t arpSteps = 0;
};

struct InstrumentSet;

class Engine {
public:
    explicit Engine(int sampleRate);
    ~Engine();

    // ---- audio thread ----
    // Writes `frames` interleaved stereo float frames. nowNs is the time the first frame
    // will start playing if known, else the callback time (steady clock).
    void render(float* out, int frames, int64_t nowNs);

    // ---- MIDI thread ----
    bool pushMidi(const Msg& m);                    // false if the queue was full (note-off may be lost)

    // ---- control thread ----
    bool setInstrument(int index) { return setInstrument(0, index); }
    bool setInstrument(int part, int index);        // builds off the audio thread, then hands over
    void setPerformance(const Performance& p);      // takes effect at the next block
    void collectRetired();                          // frees what the audio thread has let go of
    void setVolume(float v) { volume_ = v; }
    void setTranspose(int semitones) { transpose_ = semitones; }
    void setBendRange(float semitones) { bendRange_ = semitones; }
    void setVelocityScale(float s) { velocityScale_ = s; }
    // If a note is held and no MIDI arrives for this long, release all (0 = off).
    // Meant for Bluetooth, where a lost link is silent.
    void setWatchdogMs(int ms) { watchdogMs_ = ms; }
    void panic() { panicFlag_ = true; }
    // Tilt. amount is in the mode's own unit: Vibrato cents at a firm (3.5 degree) wobble, Bend
    // semitones at a 30 degree tilt, Tone the change in slide (0..1) at 30 degrees.
    // (The twist only, as before the gyro had more sources: the other sources are left alone.)
    void setTiltMode(TiltMode m, float amount) {
        setGyro(GyroAxis::Twist, m == TiltMode::Vibrato ? GyroTarget::Vibrato : m == TiltMode::Bend ? GyroTarget::Bend
                                 : m == TiltMode::Tone ? GyroTarget::Tone : GyroTarget::Off, amount);
    }
    // Any thread (the sensor's): the phone's twist about the screen's axis, in degrees, positive
    // = clockwise as you look at the screen. Unwrapped: it may run past 360.
    void setTilt(float degrees) { motion_[0] = degrees; }
    // The gyro: what each source does, and how much (see GyroTarget for the units).
    void setGyro(GyroAxis a, GyroTarget t, float amount) { gyroAmount_[int(a)] = amount; gyroTarget_[int(a)] = int(t); }
    // Bends from the gyro pulled onto semitones: 0 not at all .. 1 straight onto the nearest.
    void setBendSnap(float k) { bendSnap_ = k; }
    // A new note, or a finger gliding onto another semitone, zeroes the gyro's bend.
    void setBendReset(bool on) { bendReset_ = on; }
    // Any thread (the sensor's): twist, tip and turn in degrees (unwrapped), and the shake 0..1.
    void setMotion(float twist, float tip, float turn, float shake) {
        motion_[0] = twist; motion_[1] = tip; motion_[2] = turn; motion_[3] = shake;
    }
    void setBreathMode(BreathMode m) { breathMode_ = int(m); }
    // Any thread (the microphone's): 0..1, see mpe::Breath.
    void setBreath(float level) { breathIn_ = level; }
    ModView modifiers() const;
    // Any thread: the output below 150 Hz in dBFS, peak-held for 150 ms (for mpe::Breath::setEcho).
    float outputLowDb() const { return lowDb_.load(std::memory_order_relaxed); }
    // How far the output limiter turned the sound down in the last callback, in dB (0: not at all).
    float limiterDb() const { return limitDb_.load(std::memory_order_relaxed); }

    // The loops, each with its own length and layers (any thread may read a view; the control
    // thread presses the buttons).
    static constexpr int kLoops = 4;
    Looper& looper(int i = 0) { return *loops_[std::min(std::max(i, 0), kLoops - 1)]; }

    int sampleRate() const { return sr_; }
    int currentInstrument(int part = 0) const { return current_[part]; }
    Stats stats() const;
    int activeVoices() const { return activeVoices_; }
    int voices(VoiceView* out, int max) const;      // the sounding voices
    int fingers(FingerView* out, int max) const;    // the held fingers

private:
    struct Channel;
    struct Voice;
    struct Part;
    void apply(const Msg& m);
    void takeUpdates();
    void stepArp(int p, int64_t frame);
    void releasePart(int p);
    int freeVoice(int p, int64_t frame);
    float assisted(float pitch, float& pull, float& prev, float& speed, float dt) const;
    void renderVoices(int p, float* mixL, float* mixR, int n, float dt);
    void partFx(int p, float* l, float* r, int n);
    void masterFx(float* mixL, float* mixR, const float* revL, const float* revR, const float* delL, const float* delR, int n);
    void updateModifiers(float dt);
    struct Fx;
    void publish();

    const int sr_;
    Spsc<Msg> queue_{1024};
    std::unique_ptr<Channel[]> ch_;
    std::unique_ptr<Part[]> parts_;                 // 2; audio thread owns
    std::unique_ptr<Fx> fx_;
    std::unique_ptr<Looper> loops_[kLoops];
    bool noteStarted_ = false;                      // a note started since the looper last looked
    std::atomic<InstrumentSet*> pending_[kParts] = {};
    Spsc<InstrumentSet*> retired_{8};
    std::atomic<int> current_[kParts] = {{-1}, {-1}, {-1}, {-1}};
    Performance perf_;                              // audio thread's copy
    std::atomic<Performance*> pendingPerf_{nullptr};
    Spsc<Performance*> retiredPerf_{8};
    uint32_t pressCounter_ = 0, rng_ = 0x9e3779b9u;
    int recentNotes_[16] = {}, recentCount_ = 0, recentPos_ = 0;

    std::atomic<float> volume_{0.8f};
    std::atomic<int> transpose_{0};
    std::atomic<float> bendRange_{48.f};            // MPE default for member channels
    std::atomic<float> velocityScale_{5.f};         // the Block sends small velocities
    std::atomic<int> watchdogMs_{0};
    std::atomic<bool> panicFlag_{false};
    std::atomic<bool> overflow_{false};
    std::atomic<int> activeVoices_{0};
    int64_t lastEventFrame_ = 0, frameCounter_ = 0;

    // phone modifiers: settings and inputs from other threads ...
    std::atomic<int> breathMode_{0};
    std::atomic<int> gyroTarget_[kGyroAxes] = {};
    std::atomic<float> gyroAmount_[kGyroAxes] = {}, motion_[kGyroAxes] = {}, bendSnap_{0}, breathIn_{0};
    std::atomic<bool> bendReset_{true};
    // ... and the audio thread's state (see updateModifiers)
    bool gyroStarted_[3] = {};
    int gyroNow_[3] = {};
    float gyroZero_[3] = {}, gyroHp1_[3] = {}, gyroHp2_[3] = {}, gyroPrev_[3] = {};   // degrees
    float shakePhase_ = 0;
    int lastOn_[16] = {}; float lastSemi_[16] = {};  // per finger: the note and semitone the bend reset last saw
    float modBend_ = 0, modSlide_ = 0, breath_ = 0, gain_[kParts] = {1, 1, 1, 1}, toneOpen_ = 1;
    float gyroGain_ = 1, gyroBright_ = 1, gyroRelease_ = 0, gyroReverb_ = 0, gyroDelay_ = 0;
    BreathMode breathNow_ = BreathMode::Off;
    std::atomic<float> pubTilt_{0}, pubBend_{0}, pubSlide_{0}, pubBreath_{0};
    std::atomic<float> pubAxis_[kGyroAxes] = {}, pubGain_{1}, pubBright_{1}, pubRelease_{0}, pubReverb_{0}, pubDelay_{0};
    std::atomic<bool> pubHeld_{false};
    float lowZ_[4] = {}, lowB_[5] = {}, lowEnv_ = 0, lowRel_ = 0;   // the output's low end, for the breath detector
    std::atomic<float> lowDb_{-120.f};
    // The output limiter: a gain that drops at once to keep a peak under the ceiling, holds, then
    // rises slowly. It turns the whole sound down, so a chord is not bent out of shape (a tanh on
    // the mix made the notes of a chord beat against each other).
    float limHeld_ = 0, limGain_ = 1, limRise_ = 0;
    int limHold_ = 0, limHoldN_ = 0;
    std::atomic<float> limitDb_{0.f};

    // stats, written by the audio thread
    std::atomic<double> ageSum_{0}, ageMax_{0}, ageLast_{0}, loadSum_{0}, loadPeak_{0};
    std::atomic<uint64_t> ageCount_{0}, events_{0}, dropped_{0}, panics_{0}, blocks_{0}, voiceResets_{0}, arpSteps_{0};
    std::atomic<double> noteCenter_{-1};

    // published by the audio thread after each block, for voices() / fingers()
    struct PubVoice { std::atomic<int> finger{-1}, note{-1}; std::atomic<bool> gate{false}, on{false};
                      std::atomic<float> pitch{0}, pressure{0}, slide{0}, level{0}; };
    struct PubFinger { std::atomic<int> note{-1}; std::atomic<float> pitch{0}, pressure{0}, slide{0}; };
    std::unique_ptr<PubVoice[]> pubVoices_;         // kParts * 16
    std::unique_ptr<PubFinger[]> pubFingers_;       // 16
};

// MIDI thread front end: bytes in, cleaned messages queued for the engine.
class MidiInput {
public:
    explicit MidiInput(Engine& e) : engine_(e), clean_(&MidiInput::sinkThunk, this) {}

    // `timeNs` is when the bytes arrived (steady clock). Call from ONE thread.
    void feed(const uint8_t* bytes, int n, int64_t timeNs);
    // The device went away: end every note and centre bend/pressure.
    void deviceLost() { clean_.panic(); }
    const Clean& clean() const { return clean_; }

private:
    static void sinkThunk(void* self, const Msg& m) {
        MidiInput* s = static_cast<MidiInput*>(self);
        if (!s->engine_.pushMidi(m)) s->engine_.panic();     // lost an event: do not risk a stuck note
    }
    Engine& engine_;
    Clean clean_;
    Parser parser_;
};

int64_t steadyNowNs();

}  // namespace mpe
