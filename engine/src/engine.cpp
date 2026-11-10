#include "mpe/engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#if defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"

dsp* mpe_create_fx_reverb();            // generated from fx/reverb.dsp

namespace mpe {

static const int kChannels = 16;      // MPE: channel 0 is the master, 1..15 carry notes
static const int kMaxBlock = 64;      // control values are refreshed at least this often
// The output: the mix times the volume and kMasterGain, which leaves room for a chord before the
// limiter has to turn it down; and the limiter's ceiling (-1 dBFS).
static const float kMasterGain = 0.5f, kCeiling = 0.89f;

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- instrument copies, one per MPE channel (one per finger) --------------------------------

struct Slot {
    dsp* d = nullptr;
    MapUI ui;
    // A control is null when the instrument does not use it (Faust drops unused ones).
    FAUSTFLOAT *freq = nullptr, *gate = nullptr, *pressure = nullptr, *slide = nullptr, *velocity = nullptr,
               *legato = nullptr;
};

struct InstrumentSet {
    int index;
    int outputs = 1;
    Slot slots[kChannels];
    dsp* post = nullptr;            // the stage all voices share (an instrument body), if any
    int postOutputs = 1;
    int postQuiet = 0;              // samples of silence in and out of post (skipped when long)
    InstrumentSet(int idx, const InstrumentInfo& info, int sr) : index(idx) {
        if (info.createPost && (post = info.createPost())) {
            post->init(sr);
            postOutputs = std::min(2, post->getNumOutputs());
        }
        for (int ch = 1; ch < kChannels; ch++) {
            Slot& s = slots[ch];
            s.d = info.create();
            s.d->init(sr);
            s.d->buildUserInterface(&s.ui);
            s.freq = s.ui.getParamZone("freq");
            s.gate = s.ui.getParamZone("gate");
            s.pressure = s.ui.getParamZone("pressure");
            s.slide = s.ui.getParamZone("slide");
            s.velocity = s.ui.getParamZone("velocity");
            s.legato = s.ui.getParamZone("legato");
            outputs = std::min(2, s.d->getNumOutputs());
        }
    }
    ~InstrumentSet() { for (Slot& s : slots) delete s.d; delete post; }
};

// ---- fingers, parts and voices -----------------------------------------------------------
//
// A finger is one MPE channel: the note, bend, pressure and slide the controller sends on it.
// A part is an instrument and 15 voices (one copy of the instrument each). In play mode voice i
// follows finger i, as an MPE synth does. In arp mode the part's held fingers are played one at
// a time on a clock, each note still following its finger's bend, pressure and slide.
// Split sends each new finger to the left or right part by its note; Layer sends it to both.

struct Engine::Channel {
    int note = -1;                  // -1 = no note held
    int noteOnCount = 0;
    float bend = 0, pressure = 0, slide = 0.5f, velocity = 0;
    float volume = 1;               // CC 7 on the finger's channel: a gain on its note (the screen pad's fade)
    int force = 0;                  // CC 9 on the finger's channel: in a split, 1 = left part, 2 = right, 0 = by note
    int parts = 1;                  // bit p: this finger plays part p (fixed at note on)
    uint32_t order = 0;             // press order, for ArpOrder::AsPlayed
};

struct Engine::Voice {
    int finger = -1, fingerNoteOn = 0;        // the finger it follows (arp: while that note is held)
    int note = -1;                  // base note, octave offset included
    bool gate = false, sounding = false, start = false;
    bool legato = false;            // this note started while another of the part was sounding (a slur)
    int releasedSamples = 1 << 30;  // samples since this voice's gate closed
    int seen = 0;                   // play mode: the finger's noteOnCount already handled
    float bend = 0, pressure = 0, slide = 0.5f, velocity = 0;   // last expression from the finger
    float volume = 1, volumeS = 1;
    bool wasGated = false;
    bool ringing = false;           // the finger lifted, the part has a release: still gated, fading
    float relTime = 0;              // s, the release in effect when the finger lifted
    float relGain = 1;              // the release fade: 1 while held, down to 0 over the release
    float freq = 440, pitch = 0, pressureS = 0, slideS = 0.5f, level = 0;
    float pull = 0, prevPitch = 0, speed = 0;   // pitch assist
    int quietSamples = 0;
    int64_t started = 0;
};

struct Engine::Part {
    InstrumentSet* set = nullptr;
    Voice v[kChannels];
    bool arp = false;               // the mode in effect
    bool running = false;
    double nextStep = 0;            // frame of the next arp step
    int64_t gateOff = INT64_MAX;
    uint32_t step = 0;
    struct Entry { int finger, noteOn, note; float bend, pressure, slide, velocity, volume; uint32_t order; };
    Entry latch[kChannels];         // the last chord held, for latch
    int latchCount = 0;
};

static float smoothTo(float cur, float target, float coef) { return cur + (target - cur) * coef; }
static float noteToHz(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f); }
static float clampf(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }
static float deadZone(float x, float d) { return x > d ? x - d : x < -d ? x + d : 0.f; }

// Master effects (a Faust reverb and a delay) and each part's tone and drive.
struct Engine::Fx {
    dsp* reverb = nullptr;
    MapUI ui;
    FAUSTFLOAT *size = nullptr, *damp = nullptr;
    int reverbQuiet = 0;                       // samples since the reverb went silent
    std::vector<float> dl, dr;                 // delay lines
    int pos = 0;
    float time = -1, lpL = 0, lpR = 0;         // delay time in frames (smoothed), feedback low-pass
    float tone[kParts][4] = {};                     // per part: two one-pole stages, left and right
    float air[kParts][2] = {};                      // per part: the air shelf's low-pass, left and right
    std::vector<float> wide[kParts];                // per part: the width's delay line
    int widePos[kParts] = {};
    float wideHp[kParts][2] = {};                   // its high-pass (the low end stays in the middle)
    float wideNow[kParts] = {};                     // the width, smoothed
    explicit Fx(int sr) : dl(size_t(sr) * 5 / 2), dr(size_t(sr) * 5 / 2) {
        for (auto& w : wide) w.assign(size_t(sr) / 40 + 2, 0.f);   // 25 ms
        reverb = mpe_create_fx_reverb();
        reverb->init(sr);
        reverb->buildUserInterface(&ui);
        size = ui.getParamZone("size");
        damp = ui.getParamZone("damp");
    }
    ~Fx() { delete reverb; }
};

Engine::Engine(int sampleRate)
    : sr_(sampleRate), ch_(new Channel[kChannels]), parts_(new Part[kParts]), fx_(new Fx(sampleRate)),
      pubVoices_(new PubVoice[kParts * kChannels]), pubFingers_(new PubFinger[kChannels]) {
    for (auto& lp : loops_) lp.reset(new Looper(sampleRate));
    // A 150 Hz low-pass (2nd order), the same band the breath detector listens to.
    const float w0 = 2.f * float(M_PI) * 150.f / sr_, cw = std::cos(w0), al = std::sin(w0) / (2.f * 0.7071f), a0 = 1.f + al;
    lowB_[0] = (1.f - cw) / 2.f / a0; lowB_[1] = (1.f - cw) / a0; lowB_[2] = lowB_[0];
    lowB_[3] = -2.f * cw / a0; lowB_[4] = (1.f - al) / a0;
    lowRel_ = 1.f - std::exp(-1.f / (0.15f * sr_));
    limHoldN_ = int(0.03f * sr_);                    // longer than the slowest wave's cycle (33 Hz), so the gain does not ripple in it
    limRise_ = 1.f - std::exp(-1.f / (0.25f * sr_));
}

Engine::~Engine() {
    for (int p = 0; p < kParts; p++) { delete parts_[p].set; delete pending_[p].exchange(nullptr); }
    delete pendingPerf_.exchange(nullptr);
    collectRetired();
}

bool Engine::pushMidi(const Msg& m) {
    if (queue_.push(m)) return true;
    overflow_ = true;
    dropped_++;
    return false;
}

bool Engine::setInstrument(int part, int index) {
    if (part < 0 || part >= kParts || index < 0 || index >= instrumentCount()) return false;
    InstrumentSet* fresh = new InstrumentSet(index, instrumentAt(index), sr_);
    delete pending_[part].exchange(fresh);  // a still-unclaimed older request is replaced
    current_[part] = index;
    collectRetired();
    return true;
}

void Engine::setPerformance(const Performance& p) {
    delete pendingPerf_.exchange(new Performance(p));
    collectRetired();
}

void Engine::collectRetired() {
    InstrumentSet* s;
    while (retired_.pop(s)) delete s;
    Performance* q;
    while (retiredPerf_.pop(q)) delete q;
}

void Engine::apply(const Msg& m) {
    const int ch = m.channel();
    Channel& c = ch_[ch];
    switch (m.type()) {
    case kNoteOn:
        if (ch == 0) return;
        if (m.d2 > 0) {
            c.bend = 0;
            c.pressure = 0;
            c.velocity = m.d2 / 127.f;
            noteStarted_ = true;
            c.note = m.d1;
            c.noteOnCount++;
            c.order = ++pressCounter_;
            c.parts = perf_.layout == Layout::Layer ? 3
                    : perf_.layout != Layout::Split ? 1
                    : c.force >= 1 && c.force <= kParts ? 1 << (c.force - 1)   // a split screen: its section's part
                    : (m.d1 < perf_.splitNote ? 1 : 2);
            recentNotes_[recentPos_] = m.d1;
            recentPos_ = (recentPos_ + 1) % 16;
            recentCount_ = std::min(recentCount_ + 1, 16);
            int sum = 0;
            for (int i = 0; i < recentCount_; i++) sum += recentNotes_[i];
            noteCenter_ = double(sum) / recentCount_;
        } else {
            c.note = -1;
            c.volume = 1;
            c.force = 0;
        }
        return;
    case kNoteOff:
        // The volume is sent before the note on (so a faded note never starts loud) and lasts
        // until the note ends; the voice keeps the last value for its release.
        if (ch != 0) { c.note = -1; c.volume = 1; c.force = 0; }
        return;
    case kPitchBend: c.bend = bend14(m) / 8192.f * bendRange_.load(); return;
    case kChanPress: c.pressure = m.d1 / 127.f; return;
    case kController:
        if (m.d1 == 74 && ch != 0) c.slide = m.d2 / 127.f;
        else if (m.d1 == 7 && ch != 0) c.volume = m.d2 / 127.f;
        else if (m.d1 == 9 && ch != 0) c.force = m.d2;
        else if (m.d1 == 120 || m.d1 == 123) panicFlag_ = true;
        return;
    default: return;
    }
}

void Engine::releasePart(int p) {
    Part& P = parts_[p];
    for (int i = 1; i < kChannels; i++) {
        Voice& v = P.v[i];
        v.gate = false;
        v.wasGated = false;                      // a panic or a mode change ends notes now, without the release
        v.ringing = false;
        v.start = false;
        v.finger = -1;
        v.seen = ch_[i].noteOnCount;            // play mode: do not restart fingers already down
    }
    P.running = false;
    P.gateOff = INT64_MAX;
}

// Instruments and settings from the control thread, picked up between blocks.
void Engine::takeUpdates() {
    for (int p = 0; p < kParts; p++) {
        if (InstrumentSet* fresh = pending_[p].exchange(nullptr)) {
            Part& P = parts_[p];
            if (P.set && !retired_.push(P.set)) delete P.set;    // (only if the control side never collects)
            P.set = fresh;
            for (int i = 1; i < kChannels; i++) {
                Voice& v = P.v[i];
                v.sounding = false;
                if (P.arp) { if (v.gate) v.start = true; }                 // the arp note goes on, on the new sound
                else if (ch_[i].note >= 0 && (ch_[i].parts >> p & 1)) v.seen = ch_[i].noteOnCount - 1;   // restart held notes
            }
        }
    }
    if (Performance* np = pendingPerf_.exchange(nullptr)) {
        for (int p = 0; p < kParts; p++)
            if (np->part[p].arp != parts_[p].arp) { releasePart(p); parts_[p].arp = np->part[p].arp; }
        perf_ = *np;
        if (!retiredPerf_.push(np)) delete np;
    }
}

int Engine::freeVoice(int p, int64_t frame) {
    Part& P = parts_[p];
    int best = -1;
    for (int i = 1; i < kChannels; i++) if (!P.v[i].sounding && !P.v[i].start) return i;
    for (int i = 1; i < kChannels; i++)                       // the oldest one already released
        if (!P.v[i].gate && (best < 0 || P.v[i].started < P.v[best].started)) best = i;
    if (best > 0) return best;
    best = 1;
    for (int i = 2; i < kChannels; i++) if (P.v[i].started < P.v[best].started) best = i;
    (void)frame;
    return best;
}

void Engine::stepArp(int p, int64_t frame) {
    Part& P = parts_[p];
    const PartConfig& cfg = perf_.part[p];
    Part::Entry set[kChannels];
    int n = 0;
    for (int ch = 1; ch < kChannels; ch++) {
        const Channel& f = ch_[ch];
        if (f.note >= 0 && (f.parts >> p & 1))
            set[n++] = {ch, f.noteOnCount, f.note, f.bend, f.pressure, f.slide, f.velocity, f.volume, f.order};
    }
    if (n) { std::copy(set, set + n, P.latch); P.latchCount = n; }
    else if (cfg.latch && P.latchCount) { n = P.latchCount; std::copy(P.latch, P.latch + n, set); }
    for (int i = 1; i < kChannels; i++) P.v[i].gate = false;  // end the last step's note (it rings on)
    if (!n) { P.running = false; P.gateOff = INT64_MAX; return; }

    if (cfg.order == ArpOrder::AsPlayed) std::sort(set, set + n, [](auto& a, auto& b) { return a.order < b.order; });
    else std::sort(set, set + n, [](auto& a, auto& b) { return a.note < b.note; });
    const int oct = std::min(std::max(cfg.octaves, 1), 4);
    const int len = n * oct;

    auto start = [&](const Part::Entry& e, int octave) {
        Voice& v = P.v[freeVoice(p, frame)];
        v.finger = e.finger;
        v.fingerNoteOn = e.noteOn;
        v.note = e.note + 12 * octave;
        v.bend = e.bend; v.pressure = e.pressure; v.slide = e.slide; v.velocity = e.velocity; v.volume = e.volume;
        v.gate = true;
        v.start = true;
        v.started = frame;
    };
    if (cfg.order == ArpOrder::Chord) {
        for (int i = 0; i < n; i++) start(set[i], int(P.step % oct));
    } else {
        int idx = int(P.step % len);
        if (cfg.order == ArpOrder::Down) idx = len - 1 - idx;
        else if (cfg.order == ArpOrder::UpDown) {
            const int per = len > 1 ? 2 * len - 2 : 1;
            const int k = int(P.step % per);
            idx = k < len ? k : per - k;
        } else if (cfg.order == ArpOrder::Random) {
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            idx = int(rng_ % uint32_t(len));
        }
        start(set[idx % n], idx / n);
    }
    P.step++;
    arpSteps_++;

    const double stepLen = sr_ * 60.0 / (std::max(20.f, perf_.bpm) * std::max(0.25f, cfg.rate));
    if (!P.running) { P.running = true; P.nextStep = double(frame); }
    P.nextStep += stepLen;
    const float gate = std::min(std::max(cfg.gate, 0.05f), 1.f);
    P.gateOff = gate >= 0.999f ? INT64_MAX : frame + std::max<int64_t>(1, int64_t(gate * stepLen));
}

// Pitch assist: while a finger rests (pitch moving slower than 4 semitones/s), pull it into tune,
// by `assist`, over about 80 ms. Near a semitone (within 30 cents) that is the semitone, in the
// scale or not: a note played on purpose stays in tune. Between notes (where a glide stopped) it is
// the nearest note of the scale. A glide itself is left alone.
// Bend arrives in steps a few ms apart, so the speed is a peak that decays over ~30 ms; a block
// between two steps does not count as resting.
float Engine::assisted(float pitch, float& pull, float& prev, float& speed, float dt) const {
    speed = std::max(std::fabs(pitch - prev) / dt, speed * std::exp(-dt / 0.03f));
    prev = pitch;
    if (!perf_.scaleMask || perf_.assist <= 0) { pull = 0; return pitch; }
    pull = smoothTo(pull, speed < 4.f ? perf_.assist : 0.f, 1.f - std::exp(-dt / 0.08f));
    const float rel = pitch - perf_.scaleRoot;
    const float semi = std::round(rel);
    if (std::fabs(rel - semi) < 0.3f) return pitch + (semi + perf_.scaleRoot - pitch) * pull;
    const int base = int(std::floor(rel));
    float nearest = rel, bestDist = 1e9f;
    for (int k = -6; k <= 7; k++) {
        const int cand = base + k;
        if (!(perf_.scaleMask >> (((cand % 12) + 12) % 12) & 1)) continue;
        const float d = std::fabs(cand - rel);
        if (d < bestDist) { bestDist = d; nearest = float(cand); }
    }
    return pitch + (nearest + perf_.scaleRoot - pitch) * pull;
}

// Tilt and breath, once per callback (a few ms): what every voice gets added this time.
void Engine::updateModifiers(float dt) {
    auto coef = [&](float tau) { return 1.f - std::exp(-dt / tau); };
    bool held = false;
    for (int ch = 1; ch < kChannels; ch++) held = held || ch_[ch].note >= 0;

    // Bend reset: a new note, or a held finger gliding onto another semitone. (With a little
    // give, so a finger resting between two notes does not reset it over and over.)
    bool newNote = false;
    for (int ch = 1; ch < kChannels; ch++) {
        const Channel& c = ch_[ch];
        if (c.note < 0) { lastOn_[ch] = -1; continue; }
        const float semi = c.note + c.bend;
        if (c.noteOnCount != lastOn_[ch] || std::fabs(semi - lastSemi_[ch]) > 0.6f) {
            newNote = true;
            lastOn_[ch] = c.noteOnCount;
            lastSemi_[ch] = std::round(semi);
        }
    }
    const bool reset = newNote && bendReset_.load(std::memory_order_relaxed);

    float bendTurns = 0, vib = 0, slide = 0, gain = 1, bright = 1, rel = 0, rev = 0, del = 0, shown[kGyroAxes] = {};
    const float shake = clampf(motion_[3].load(std::memory_order_relaxed), 0.f, 1.f);
    shakePhase_ = std::fmod(shakePhase_ + dt * 5.5f, 1.f);
    for (int a = 0; a < kGyroAxes; a++) {
        const GyroTarget t = GyroTarget(gyroTarget_[a].load(std::memory_order_relaxed));
        const float amount = gyroAmount_[a].load(std::memory_order_relaxed);
        float s = 0, wobble = 0;
        if (a < 3) {
            const float ang = motion_[a].load(std::memory_order_relaxed);
            if (!gyroStarted_[a] || int(t) != gyroNow_[a]) {
                gyroZero_[a] = gyroPrev_[a] = ang; gyroHp1_[a] = gyroHp2_[a] = 0; gyroStarted_[a] = true;
            }
            gyroNow_[a] = int(t);
            // The wobble: the angle through two high-pass stages at 2 Hz. A 5 Hz vibrato comes
            // through (86%); a held slant gives 0, and so does a slow turn once it stops.
            const float k = 1.f / (1.f + dt * 2.f * float(M_PI) * 2.f);
            const float hp1 = gyroHp1_[a];
            gyroHp1_[a] = k * (gyroHp1_[a] + ang - gyroPrev_[a]);
            gyroHp2_[a] = k * (gyroHp2_[a] + gyroHp1_[a] - hp1);
            gyroPrev_[a] = ang;
            wobble = gyroHp2_[a];
            // The zero: wherever the phone is while no finger is down, so the first note is in
            // tune; and, for a bend, again at every new note when the reset is on.
            if (!held) gyroZero_[a] = smoothTo(gyroZero_[a], ang, coef(0.1f));
            if (reset && t == GyroTarget::Bend) gyroZero_[a] = ang;
            const float relDeg = ang - gyroZero_[a];
            const float dz = t == GyroTarget::Bend ? 2.f : 1.5f;      // the first degrees do nothing
            s = clampf(deadZone(relDeg, dz) / (30.f - dz), -1.f, 1.f);
            if (t != GyroTarget::Off) shown[a] = t == GyroTarget::Vibrato ? wobble : relDeg;
        } else {
            s = shake;
            if (t != GyroTarget::Off) shown[a] = shake;
        }
        const bool turn = a < 3;
        switch (t) {
        case GyroTarget::Bend: bendTurns += s * amount; break;
        case GyroTarget::Vibrato:                                    // a turn: cents at a 3.5 degree wobble, a tremor under 0.2 ignored
            vib += turn ? clampf(deadZone(wobble, 0.2f) / 3.3f, -1.5f, 1.5f) * amount / 100.f
                        : s * amount / 100.f * std::sin(2.f * float(M_PI) * shakePhase_);
            break;
        case GyroTarget::Volume: gain *= turn ? 1.f + s * amount : 1.f - amount + amount * s; break;
        case GyroTarget::Tone: slide += s * amount; break;
        case GyroTarget::Brightness: bright *= turn ? clampf(1.f + s * amount, 0.05f, 1.f) : 1.f - amount + amount * s; break;
        case GyroTarget::Release: rel += (turn ? std::max(s, 0.f) : s) * amount; break;
        case GyroTarget::Reverb: rev += s * amount; break;
        case GyroTarget::Delay: del += s * amount; break;
        default: break;
        }
    }
    // Snap the bend onto semitones: the middle of each semitone holds the note, and the move to
    // the next is squeezed towards the edges (all of it, at full snap).
    const float g = 1.f - clampf(bendSnap_.load(std::memory_order_relaxed), 0.f, 1.f);
    if (g < 0.995f && bendTurns != 0.f) {
        const float base = std::floor(bendTurns), t = bendTurns - base;
        bendTurns = base + (g < 0.005f ? (t < 0.5f ? 0.f : 1.f) : clampf((t - (1.f - g) / 2.f) / g, 0.f, 1.f));
    }
    if (reset) modBend_ = vib;                                       // the reset is heard at once, with the new note
    modBend_ = smoothTo(modBend_, bendTurns + vib, coef(0.005f));
    modSlide_ = smoothTo(modSlide_, slide, coef(0.02f));
    gyroGain_ = smoothTo(gyroGain_, clampf(gain, 0.f, 2.f), coef(0.01f));
    gyroBright_ = smoothTo(gyroBright_, clampf(bright, 0.05f, 1.f), coef(0.02f));
    gyroRelease_ = std::max(0.f, rel);
    gyroReverb_ = smoothTo(gyroReverb_, rev, coef(0.02f));
    gyroDelay_ = smoothTo(gyroDelay_, del, coef(0.02f));
    for (int a = 0; a < kGyroAxes; a++) pubAxis_[a].store(shown[a], std::memory_order_relaxed);
    pubGain_.store(gyroGain_, std::memory_order_relaxed);
    pubBright_.store(gyroBright_, std::memory_order_relaxed);
    pubRelease_.store(gyroRelease_, std::memory_order_relaxed);
    pubReverb_.store(gyroReverb_, std::memory_order_relaxed);
    pubDelay_.store(gyroDelay_, std::memory_order_relaxed);
    const float shownTilt = shown[0];

    breathNow_ = BreathMode(breathMode_.load(std::memory_order_relaxed));
    const float b = breathNow_ == BreathMode::Off ? 0.f : clampf(breathIn_.load(std::memory_order_relaxed), 0.f, 1.f);
    breath_ = smoothTo(breath_, b, coef(b > breath_ ? 0.003f : 0.012f));
    // Breath tone: the parts' low-pass, from dark (about 550 Hz) to the part's own brightness.
    toneOpen_ = breathNow_ == BreathMode::Tone ? 0.12f + 0.88f * breath_ : smoothTo(toneOpen_, 1.f, coef(0.02f));

    pubTilt_.store(shownTilt, std::memory_order_relaxed);
    pubBend_.store(modBend_, std::memory_order_relaxed);
    pubSlide_.store(modSlide_, std::memory_order_relaxed);
    pubBreath_.store(breathNow_ == BreathMode::Off ? 0.f : breath_, std::memory_order_relaxed);
    pubHeld_.store(held, std::memory_order_relaxed);
}

ModView Engine::modifiers() const {
    ModView m;
    m.tilt = pubTilt_.load(std::memory_order_relaxed);
    m.bend = pubBend_.load(std::memory_order_relaxed);
    m.slide = pubSlide_.load(std::memory_order_relaxed);
    m.breath = pubBreath_.load(std::memory_order_relaxed);
    m.held = pubHeld_.load(std::memory_order_relaxed);
    for (int a = 0; a < kGyroAxes; a++) m.axis[a] = pubAxis_[a].load(std::memory_order_relaxed);
    m.gain = pubGain_.load(std::memory_order_relaxed);
    m.bright = pubBright_.load(std::memory_order_relaxed);
    m.release = pubRelease_.load(std::memory_order_relaxed);
    m.reverb = pubReverb_.load(std::memory_order_relaxed);
    m.delay = pubDelay_.load(std::memory_order_relaxed);
    return m;
}

void Engine::renderVoices(int p, float* mixL, float* mixR, int n, float dt) {
    Part& P = parts_[p];
    if (!P.set) return;
    const bool swell = breathNow_ == BreathMode::Swell;
    const int transpose = transpose_.load() + perf_.part[p].transpose;
    const float velScale = velocityScale_.load(), gain = perf_.part[p].volume;
    auto coef = [&](float tau) { return 1.f - std::exp(-dt / tau); };
    const float cFreq = coef(0.004f), cPress = coef(0.012f), cSlide = coef(0.02f);
    const int quietLimit = int(0.4f * sr_);
    const int legatoSamples = int(0.08f * sr_);                 // a note ended less than 80 ms ago still counts
    float inBuf[kMaxBlock] = {0}, bufL[kMaxBlock], bufR[kMaxBlock];
    float* ins[1] = { inBuf };

    for (int i = 1; i < kChannels; i++) {
        Slot& sl = P.set->slots[i];
        Voice& v = P.v[i];
        if (!P.arp) {                                            // play mode: voice i is finger i
            const Channel& f = ch_[i];
            const bool mine = f.parts >> p & 1;
            if (f.noteOnCount != v.seen) {
                v.seen = f.noteOnCount;
                if (mine) {
                    if (f.note < 0) v.sounding = false;          // on and off within one block
                    else { v.start = true; v.note = f.note; v.velocity = f.velocity; v.finger = i; v.fingerNoteOn = f.noteOnCount; }
                }
            }
            v.gate = mine && f.note >= 0;
        }
        // Release: when the gate closes the note plays on as it was (the instrument stays gated) and
        // fades out over the part's release, then ends. It is cut off if the voice starts again.
        const float release = perf_.part[p].release + gyroRelease_;
        if (!v.gate && v.wasGated && release > 0.01f && v.sounding) { v.ringing = true; v.relTime = release; }
        if (v.gate) v.ringing = false;
        v.wasGated = v.gate;
        v.releasedSamples = v.gate ? 0 : std::min(v.releasedSamples + n, 1 << 30);
        // Expression: from the finger while it still holds this note, else the last values.
        if (v.finger > 0) {
            const Channel& f = ch_[v.finger];
            if (f.noteOnCount == v.fingerNoteOn && f.note >= 0) { v.bend = f.bend; v.pressure = f.pressure; v.slide = f.slide; v.volume = f.volume; }
        }
        const float target = v.note + transpose + v.bend;
        // The phone's tilt and breath. Tilt bends after pitch assist, so a deliberate bend or
        // vibrato is not pulled back onto the scale.
        const float pressT = swell ? breath_ : v.pressure;
        const float slideT = modSlide_ == 0.f ? v.slide : clampf(v.slide + modSlide_, 0.f, 1.f);

        if (v.start) {                                           // a new note on this voice
            v.start = false;
            v.legato = false;                                    // slurred: another note held or just ended
            for (int k = 1; k < kChannels; k++)
                if (k != i && (P.v[k].gate || P.v[k].releasedSamples < legatoSamples)) v.legato = true;
            v.sounding = true;
            v.quietSamples = 0;
            v.prevPitch = target;
            v.speed = 0;
            v.pull = perf_.scaleMask ? perf_.assist : 0.f;
            v.pitch = assisted(target, v.pull, v.prevPitch, v.speed, dt) + modBend_;
            v.freq = noteToHz(v.pitch);
            v.pressureS = pressT;
            v.slideS = slideT;
            v.volumeS = v.volume;
            v.ringing = false;
            v.relGain = 1;
            sl.d->instanceClear();                               // so `gate > gate'` sees a clean 0 -> 1
        }
        if (!v.sounding) continue;

        if (v.gate) {                                            // after release, freeze pitch so ringing notes don't drift
            v.pitch = assisted(target, v.pull, v.prevPitch, v.speed, dt) + modBend_;
            v.freq = smoothTo(v.freq, noteToHz(v.pitch), cFreq);
            v.pressureS = smoothTo(v.pressureS, pressT, cPress);
            v.slideS = smoothTo(v.slideS, slideT, cSlide);
        }
        v.freq = std::min(std::max(v.freq, 20.f), 5000.f);

        if (sl.freq) *sl.freq = v.freq;
        if (sl.gate) *sl.gate = v.gate || v.ringing ? 1.f : 0.f;
        const float curve = perf_.part[p].pressureCurve;
        if (sl.pressure) *sl.pressure = curve == 1.f ? v.pressureS : std::pow(v.pressureS, std::min(std::max(curve, 0.2f), 4.f));
        if (sl.slide) *sl.slide = v.slideS;
        if (sl.velocity) *sl.velocity = std::min(1.f, v.velocity * velScale);
        if (sl.legato) *sl.legato = v.legato ? 1.f : 0.f;

        float* outs[2] = { bufL, bufR };
        sl.d->compute(n, ins, outs);
        const float* r = P.set->outputs > 1 ? bufR : bufL;
        float e = 0;
        for (int k = 0; k < n; k++) e += bufL[k] * bufL[k] + r[k] * r[k];
        if (!std::isfinite(e) || e > 1e6f) {
            // The instrument blew up (NaN/Inf or runaway feedback, e.g. a note far outside its
            // range). Drop this block, reset the voice, and let the rest of the mix play on.
            sl.d->instanceClear();
            v.sounding = false;
            v.level = 0;
            voiceResets_++;
            continue;
        }
        // The finger's volume, ramped across the block so a fade has no steps.
        const float g0 = v.volumeS;
        if (v.gate) v.volumeS = smoothTo(v.volumeS, v.volume, cPress);
        // The release fade, as a gain squared (falls slowly at first, like a decaying sound).
        const float r0 = v.relGain;
        if (v.ringing) {
            v.relGain = std::max(0.f, v.relGain - n / (std::max(v.relTime, 0.01f) * sr_));
            if (v.relGain <= 0.f) v.ringing = false;            // faded out: the voice ends below
        }
        const float a0 = g0 * r0 * r0, a1 = v.volumeS * v.relGain * v.relGain;
        const float dg = (a1 - a0) / n;
        for (int k = 0; k < n; k++) {
            const float g = gain * (a0 + dg * (k + 1));
            mixL[k] += bufL[k] * g; mixR[k] += r[k] * g;
        }
        v.level = smoothTo(v.level, std::sqrt(e / (2 * n)), 0.2f);
        if (!v.gate) v.quietSamples = (v.level < 3e-5f) ? v.quietSamples + n : 0;
        if (!v.gate && v.quietSamples > quietLimit) v.sounding = false;     // fully decayed
        if (!v.gate && !v.ringing && v.relGain <= 0.f) v.sounding = false;  // the release has faded it out
    }
    // One body for all the part's notes, as on a real instrument (lib/mpe.lib `post`).
    if (InstrumentSet* S = P.set; S->post) {
        float in = 0;
        for (int k = 0; k < n; k++) in += mixL[k] * mixL[k];
        if (in > 1e-14f || S->postQuiet < sr_) {
            float* pin[1] = { mixL };
            float* pout[2] = { bufL, bufR };
            S->post->compute(n, pin, pout);
            const float* r = S->postOutputs > 1 ? bufR : bufL;
            float out = 0;
            for (int k = 0; k < n; k++) out += bufL[k] * bufL[k];
            if (!std::isfinite(out) || out > 1e6f) { S->post->instanceClear(); out = 0; std::fill(bufL, bufL + n, 0.f); r = bufL; voiceResets_++; }
            S->postQuiet = (in > 1e-14f || out > 1e-12f) ? 0 : S->postQuiet + n;
            for (int k = 0; k < n; k++) { mixL[k] = bufL[k]; mixR[k] = r[k]; }
        } else {
            std::fill(mixL, mixL + n, 0.f);
            std::fill(mixR, mixR + n, 0.f);
        }
    }
}

// A part's tone (12 dB/octave low-pass, 300 Hz .. open) and drive.
void Engine::partFx(int p, float* l, float* r, int n) {
    const PartConfig& c = perf_.part[p];
    const float bright = std::min(c.brightness, 1.f) * toneOpen_ * gyroBright_;   // breath tone and the gyro close it further
    float* st = fx_->tone[p];
    if (bright < 0.999f) {
        const float fc = 300.f * std::pow(2.f, std::max(0.f, bright) * 6.f);   // 300 Hz .. 19 kHz
        const float a = 1.f - std::exp(-2.f * float(M_PI) * fc / sr_);
        for (int i = 0; i < n; i++) {
            st[0] += (l[i] - st[0]) * a; st[1] += (st[0] - st[1]) * a; l[i] = st[1];
            st[2] += (r[i] - st[2]) * a; st[3] += (st[2] - st[3]) * a; r[i] = st[3];
        }
    } else if (n > 0) {
        st[0] = st[1] = l[n - 1]; st[2] = st[3] = r[n - 1];                  // so switching it on later does not click
    }
    if (c.drive > 0.001f) {
        const float k = 1.f + c.drive * 8.f, makeup = 1.f / std::sqrt(k);
        for (int i = 0; i < n; i++) { l[i] = std::tanh(l[i] * k) * makeup; r[i] = std::tanh(r[i] * k) * makeup; }
    }
    // Air: brightness past 1 lifts the top, a shelf from about 6 kHz (up to +8 dB at 2).
    float* sa = fx_->air[p];
    if (c.brightness > 1.001f) {
        const float g = std::pow(10.f, std::min(c.brightness - 1.f, 1.f) * 8.f / 20.f) - 1.f;
        const float a = 1.f - std::exp(-2.f * float(M_PI) * 6000.f / sr_);
        for (int i = 0; i < n; i++) {
            sa[0] += (l[i] - sa[0]) * a; l[i] += (l[i] - sa[0]) * g;
            sa[1] += (r[i] - sa[1]) * a; r[i] += (r[i] - sa[1]) * g;
        }
    } else if (n > 0) {
        sa[0] = l[n - 1]; sa[1] = r[n - 1];
    }
    // Width: the part's middle, and beside it a copy a few ms late (above about 250 Hz), added to
    // one side and taken from the other: a mono sound spread across the stereo field.
    float& wn = fx_->wideNow[p];
    const float wantW = clampf(c.width, 0.f, 1.f);
    if (wantW > 0.001f || wn > 0.001f) {
        std::vector<float>& d = fx_->wide[p];
        int& pos = fx_->widePos[p];
        float* hp = fx_->wideHp[p];
        const int len = int(d.size());
        const int lag = std::min(len - 1, int(0.011f * float(sr_)));      // 11 ms
        const float ha = 1.f - std::exp(-2.f * float(M_PI) * 250.f / sr_);
        const float k = 1.f - std::exp(-1.f / (0.05f * float(sr_)));     // 50 ms smoothing
        for (int i = 0; i < n; i++) {
            wn += (wantW - wn) * k;
            const float mid = (l[i] + r[i]) * 0.5f, side = (l[i] - r[i]) * 0.5f;
            d[size_t(pos)] = mid;
            float late = d[size_t((pos - lag + len) % len)];
            pos = (pos + 1) % len;
            hp[0] += (late - hp[0]) * ha;                                 // its low end out
            const float s2 = side + (late - hp[0]) * wn * 0.7f;
            l[i] = mid + s2;
            r[i] = mid - s2;
        }
    }
}

// The delay (tempo-synced, optionally ping-pong) and the reverb, added to the mix.
void Engine::masterFx(float* mixL, float* mixR, const float* revL, const float* revR,
                      const float* delL, const float* delR, int n) {
    Fx& fx = *fx_;
    const int len = int(fx.dl.size());
    const float target = std::min(std::max(perf_.delayBeats * 60.f / std::max(20.f, perf_.bpm) * sr_, 1.f), float(len - 2));
    if (fx.time < 0) fx.time = target;
    const float fb = std::min(std::max(perf_.delayFeedback, 0.f), 0.95f);
    const float lpA = 1.f - std::exp(-2.f * float(M_PI) * 5000.f / sr_);     // darker repeats
    for (int i = 0; i < n; i++) {
        fx.time += (target - fx.time) * 0.0005f;                              // ~40 ms glide when the tempo changes
        float rp = fx.pos - fx.time;
        if (rp < 0) rp += len;
        const int i0 = int(rp), i1 = (i0 + 1) % len;
        const float fr = rp - i0;
        const float yl = fx.dl[i0] + (fx.dl[i1] - fx.dl[i0]) * fr;
        const float yr = fx.dr[i0] + (fx.dr[i1] - fx.dr[i0]) * fr;
        fx.lpL += (yl - fx.lpL) * lpA;
        fx.lpR += (yr - fx.lpR) * lpA;
        if (perf_.delayPingPong) {                                            // in on the left, bouncing across
            fx.dl[fx.pos] = (delL[i] + delR[i]) * 0.5f + fx.lpR * fb;
            fx.dr[fx.pos] = fx.lpL * fb;
        } else {
            fx.dl[fx.pos] = delL[i] + fx.lpL * fb;
            fx.dr[fx.pos] = delR[i] + fx.lpR * fb;
        }
        fx.pos = (fx.pos + 1) % len;
        mixL[i] += yl;
        mixR[i] += yr;
    }

    bool send = false;
    for (int p = 0; p < kParts; p++) send = send || perf_.part[p].reverb + gyroReverb_ > 0;
    if (!send && fx.reverbQuiet > 8 * sr_) return;                           // off, and its tail has died away
    if (fx.size) *fx.size = perf_.reverbSize;
    if (fx.damp) *fx.damp = perf_.reverbDamp;
    float wl[kMaxBlock], wr[kMaxBlock];
    float* ins[2] = { const_cast<float*>(revL), const_cast<float*>(revR) };
    float* outs[2] = { wl, wr };
    fx.reverb->compute(n, ins, outs);
    float e = 0;
    for (int i = 0; i < n; i++) { mixL[i] += wl[i]; mixR[i] += wr[i]; e += wl[i] * wl[i] + wr[i] * wr[i]; }
    fx.reverbQuiet = e < 1e-9f * n ? fx.reverbQuiet + n : 0;
}

void Engine::publish() {
    for (int p = 0; p < kParts; p++)
        for (int i = 1; i < kChannels; i++) {
            const Voice& v = parts_[p].v[i];
            PubVoice& o = pubVoices_[p * kChannels + i];
            o.on.store(v.sounding, std::memory_order_relaxed);
            o.finger.store(v.finger > 0 && ch_[v.finger].noteOnCount == v.fingerNoteOn && ch_[v.finger].note >= 0 ? v.finger : -1,
                           std::memory_order_relaxed);
            o.note.store(v.note, std::memory_order_relaxed);
            o.gate.store(v.gate, std::memory_order_relaxed);
            o.pitch.store(v.pitch, std::memory_order_relaxed);
            o.pressure.store(v.pressureS, std::memory_order_relaxed);
            o.slide.store(v.slideS, std::memory_order_relaxed);
            o.level.store(v.level, std::memory_order_relaxed);
        }
    const int transpose = transpose_.load();
    for (int ch = 1; ch < kChannels; ch++) {
        const Channel& c = ch_[ch];
        PubFinger& o = pubFingers_[ch];
        o.note.store(c.note, std::memory_order_relaxed);
        o.pitch.store(c.note + c.bend + transpose, std::memory_order_relaxed);
        o.pressure.store(c.pressure, std::memory_order_relaxed);
        o.slide.store(c.slide, std::memory_order_relaxed);
    }
}

int Engine::voices(VoiceView* out, int max) const {
    int n = 0;
    for (int p = 0; p < kParts; p++)
        for (int i = 1; i < kChannels && n < max; i++) {
            const PubVoice& o = pubVoices_[p * kChannels + i];
            if (!o.on.load(std::memory_order_relaxed)) continue;
            VoiceView& v = out[n++];
            v.part = p;
            v.finger = o.finger.load(std::memory_order_relaxed);
            v.note = o.note.load(std::memory_order_relaxed);
            v.gate = o.gate.load(std::memory_order_relaxed);
            v.pitch = o.pitch.load(std::memory_order_relaxed);
            v.pressure = o.pressure.load(std::memory_order_relaxed);
            v.slide = o.slide.load(std::memory_order_relaxed);
            v.level = o.level.load(std::memory_order_relaxed);
        }
    return n;
}

int Engine::fingers(FingerView* out, int max) const {
    int n = 0;
    for (int ch = 1; ch < kChannels && n < max; ch++) {
        const PubFinger& o = pubFingers_[ch];
        const int note = o.note.load(std::memory_order_relaxed);
        if (note < 0) continue;
        out[n++] = {ch, note, o.pitch.load(std::memory_order_relaxed),
                    o.pressure.load(std::memory_order_relaxed), o.slide.load(std::memory_order_relaxed)};
    }
    return n;
}

// While audio is made: numbers too small to hear (denormals, in the tails of decaying filters) are
// flushed to zero, not worked out slowly; as it was afterwards.
namespace {
struct FlushDenormals {
#if defined(__x86_64__) || defined(__SSE__)
    unsigned was;
    FlushDenormals() : was(_mm_getcsr()) { _mm_setcsr(was | 0x8040); }   // FTZ and DAZ
    ~FlushDenormals() { _mm_setcsr(was); }
#elif defined(__aarch64__)
    uint64_t was;
    FlushDenormals() { asm volatile("mrs %0, fpcr" : "=r"(was)); uint64_t v = was | (uint64_t(1) << 24); asm volatile("msr fpcr, %0" : : "r"(v)); }
    ~FlushDenormals() { asm volatile("msr fpcr, %0" : : "r"(was)); }
#endif
};
}  // namespace

void Engine::render(float* out, int frames, int64_t nowNs) {
    FlushDenormals ftz;
    const int64_t t0 = steadyNowNs();
    takeUpdates();

    // Apply every queued MIDI message before this block, so the block already plays it.
    Msg m;
    int got = 0;
    while (queue_.pop(m)) {
        apply(m);
        got++;
        if (m.timeNs > 0) {
            double age = (nowNs - m.timeNs) / 1e6;
            if (age < 0) age = 0;
            ageSum_.store(ageSum_.load(std::memory_order_relaxed) + age, std::memory_order_relaxed);
            ageCount_++;
            ageLast_ = age;
            if (age > ageMax_.load(std::memory_order_relaxed)) ageMax_ = age;
        }
    }
    if (got) { events_ += got; lastEventFrame_ = frameCounter_; }

    // Safety: a lost event, or a silent link while a note is held, must not leave a note stuck.
    bool doPanic = panicFlag_.exchange(false) || overflow_.exchange(false);
    const int wd = watchdogMs_.load();
    if (!doPanic && wd > 0 && !got) {
        bool any = false;
        for (int ch = 1; ch < kChannels; ch++) any = any || ch_[ch].note >= 0;
        if (any && (frameCounter_ - lastEventFrame_) * 1000 / sr_ > wd) doPanic = true;
    }
    if (doPanic) {
        for (int ch = 0; ch < kChannels; ch++) { ch_[ch].note = -1; ch_[ch].pressure = 0; ch_[ch].bend = 0; ch_[ch].volume = 1; ch_[ch].force = 0; }
        for (int p = 0; p < kParts; p++) { releasePart(p); parts_[p].latchCount = 0; }   // a latched arp stops too
        panics_++;
    }

    updateModifiers(float(frames) / sr_);

    const float vol = volume_.load();
    float mixL[kMaxBlock], mixR[kMaxBlock], partL[kMaxBlock], partR[kMaxBlock];
    float revL[kMaxBlock], revR[kMaxBlock], delL[kMaxBlock], delR[kMaxBlock];
    float deepest = 1;                               // the limiter's lowest gain in this callback
    int done = 0;
    while (done < frames) {
        const int64_t now = frameCounter_ + done;
        // Arp clock: steps and note ends land on the exact frame; blocks are cut there.
        int64_t next = now + std::min(kMaxBlock, frames - done);
        for (int p = 0; p < kParts; p++) {
            Part& P = parts_[p];
            if (!P.arp || !P.set) continue;
            bool held = false;
            for (int ch = 1; ch < kChannels; ch++) held = held || (ch_[ch].note >= 0 && (ch_[ch].parts >> p & 1));
            if (!P.running && held) stepArp(p, now);                         // the first note plays at once
            else if (P.running && !held && !perf_.part[p].latch) stepArp(p, now);   // hand lifted: stop now
            if (P.running) {
                if (P.gateOff <= now) {
                    for (int i = 1; i < kChannels; i++) P.v[i].gate = false;
                    P.gateOff = INT64_MAX;
                }
                if (int64_t(P.nextStep) <= now) stepArp(p, now);
                if (P.running) next = std::min(next, std::max(now + 1, int64_t(P.nextStep)));
                if (P.gateOff != INT64_MAX) next = std::min(next, std::max(now + 1, P.gateOff));
            }
        }
        const int n = int(next - now);
        const float dt = float(n) / sr_;

        std::fill(mixL, mixL + n, 0.f);
        std::fill(mixR, mixR + n, 0.f);
        std::fill(revL, revL + n, 0.f);
        std::fill(revR, revR + n, 0.f);
        std::fill(delL, delL + n, 0.f);
        std::fill(delR, delR + n, 0.f);
        for (int p = 0; p < kParts; p++) {
            // A part not in use (only the first plays alone, the first two layered): let its notes ring out.
            if ((p >= 1 && perf_.layout == Layout::Single) || (p >= 2 && perf_.layout == Layout::Layer)) {
                bool any = false;
                for (int i = 1; i < kChannels; i++) any = any || parts_[p].v[i].sounding;
                if (!any) continue;
            }
            std::fill(partL, partL + n, 0.f);
            std::fill(partR, partR + n, 0.f);
            renderVoices(p, partL, partR, n, dt);
            partFx(p, partL, partR, n);
            // Breath swell: the part's loudness follows the breath (before the effects, so the
            // reverb and delay ring on when the breath stops). Smoothed per sample: no zipper.
            // The gyro's volume comes in here too.
            const float gt = (breathNow_ == BreathMode::Swell ? breath_ : 1.f) * gyroGain_;
            if (std::fabs(gt - 1.f) > 1e-4f || std::fabs(gain_[p] - 1.f) > 1e-4f) {
                const float k = 1.f - std::exp(-1.f / (0.003f * sr_));
                float g = gain_[p];
                for (int i = 0; i < n; i++) { g += (gt - g) * k; partL[i] *= g; partR[i] *= g; }
                gain_[p] = std::fabs(gt - 1.f) < 1e-4f && std::fabs(g - 1.f) < 5e-4f ? 1.f : g;
            }
            const float rs = clampf(perf_.part[p].reverb + gyroReverb_, 0.f, 1.f), ds = clampf(perf_.part[p].delay + gyroDelay_, 0.f, 1.f);
            for (int i = 0; i < n; i++) {
                mixL[i] += partL[i]; mixR[i] += partR[i];
                revL[i] += partL[i] * rs; revR[i] += partR[i] * rs;
                delL[i] += partL[i] * ds; delR[i] += partR[i] * ds;
            }
        }
        masterFx(mixL, mixR, revL, revR, delL, delR, n);
        // The loops record this (everything played, with its effects) and add themselves to it.
        float liveL[kMaxBlock], liveR[kMaxBlock];
        std::copy(mixL, mixL + n, liveL);
        std::copy(mixR, mixR + n, liveR);
        for (auto& lp : loops_) lp->process(liveL, liveR, mixL, mixR, n, noteStarted_, perf_.bpm);
        noteStarted_ = false;
        for (int i = 0; i < n; i++) {
            // The mix, with room for many notes (each plays at its full level), then the limiter.
            float l = mixL[i] * vol * kMasterGain, r = mixR[i] * vol * kMasterGain;
            const float pk = std::max(std::fabs(l), std::fabs(r));
            if (pk >= limHeld_) { limHeld_ = pk; limHold_ = limHoldN_; }
            else if (limHold_ > 0) limHold_--;
            else limHeld_ = pk + (limHeld_ - pk) * (1.f - limRise_);
            const float want = limHeld_ > kCeiling ? kCeiling / limHeld_ : 1.f;
            limGain_ = want < limGain_ ? want : limGain_ + (want - limGain_) * limRise_;
            deepest = std::min(deepest, limGain_);
            l = clampf(l * limGain_, -1.f, 1.f); r = clampf(r * limGain_, -1.f, 1.f);
            out[2 * (done + i)] = l;
            out[2 * (done + i) + 1] = r;
            // The low end of what goes to the speaker, two passes of the low-pass (24 dB/octave).
            float x = 0.5f * (l + r);
            for (int k = 0; k < 2; k++) {
                float* z = lowZ_ + 2 * k;
                const float y = lowB_[0] * x + z[0];
                z[0] = lowB_[1] * x - lowB_[3] * y + z[1];
                z[1] = lowB_[2] * x - lowB_[4] * y;
                x = y;
            }
            const float pw = x * x;
            lowEnv_ = pw > lowEnv_ ? pw : lowEnv_ + (pw - lowEnv_) * lowRel_;
        }
        done += n;
    }
    int voices = 0;
    for (int p = 0; p < kParts; p++) for (int i = 1; i < kChannels; i++) voices += parts_[p].v[i].sounding;
    activeVoices_ = voices;
    frameCounter_ += frames;
    if (!std::isfinite(limHeld_) || !std::isfinite(limGain_)) { limHeld_ = 0; limGain_ = 1; }
    limitDb_.store(20.f * std::log10(std::max(deepest, 1e-6f)), std::memory_order_relaxed);
    if (!std::isfinite(lowEnv_)) { lowEnv_ = 0; for (float& z : lowZ_) z = 0; }
    lowDb_.store(10.f * std::log10(lowEnv_ + 1e-12f), std::memory_order_relaxed);
    publish();

    const double spent = (steadyNowNs() - t0) / 1e9, budget = double(frames) / sr_;
    const double load = spent / budget;
    loadSum_.store(loadSum_.load(std::memory_order_relaxed) + load, std::memory_order_relaxed);
    if (load > loadPeak_.load(std::memory_order_relaxed)) loadPeak_ = load;
    blocks_++;
}

Stats Engine::stats() const {
    Stats s;
    uint64_t n = ageCount_.load();
    s.eventAgeAvgMs = n ? ageSum_.load() / n : 0;
    s.eventAgeMaxMs = ageMax_.load();
    s.eventAgeLastMs = ageLast_.load();
    s.blocks = blocks_.load();
    s.loadAvg = s.blocks ? loadSum_.load() / s.blocks : 0;
    s.loadPeak = loadPeak_.load();
    s.events = events_.load();
    s.dropped = dropped_.load();
    s.panics = panics_.load();
    s.voiceResets = voiceResets_.load();
    s.noteCenter = noteCenter_.load();
    s.arpSteps = arpSteps_.load();
    return s;
}

void MidiInput::feed(const uint8_t* bytes, int n, int64_t timeNs) {
    parser_.feed(bytes, n, timeNs, [this](const Msg& m) { clean_.process(m); });
}

}  // namespace mpe
