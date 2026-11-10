// Split, layer, arpeggiator and pitch assist: how fingers become notes.
#include <algorithm>
#include "check.h"

using namespace mpe;

static std::vector<VoiceView> voicesOf(Rig& r) {
    VoiceView v[kParts * 16];
    int n = r.engine.voices(v, kParts * 16);
    return std::vector<VoiceView>(v, v + n);
}

static int gated(Rig& r, int part, int note) {
    int n = 0;
    for (auto& v : voicesOf(r)) n += v.part == part && v.gate && (note < 0 || v.note == note);
    return n;
}

// Renders in small blocks and returns the note of each new arp step, in order.
static std::vector<int> arpNotes(Rig& r, double seconds, int part = 0) {
    std::vector<int> seq;
    uint64_t last = r.engine.stats().arpSteps;
    const int blocks = int(seconds * kSr / 32);
    for (int b = 0; b < blocks; b++) {
        r.run(32.0 / kSr, 32);
        uint64_t now = r.engine.stats().arpSteps;
        if (now != last) {
            last = now;
            int best = -1;
            for (auto& v : voicesOf(r)) if (v.part == part && v.gate) best = v.note;
            seq.push_back(best);
        }
    }
    return seq;
}

static std::string str(const std::vector<int>& v) {
    std::string s;
    for (int x : v) s += std::to_string(x) + " ";
    return s;
}

static bool startsWith(const std::vector<int>& seq, const std::vector<int>& want) {
    return seq.size() >= want.size() && std::equal(want.begin(), want.end(), seq.begin());
}

int main() {
    // ---- the default still plays like an MPE synth: voice i follows finger i ----
    {
        Rig r("synths/fm-keys");
        r.noteOn(1, 60, 80); r.noteOn(2, 64, 80);
        r.run(0.05);
        auto v = voicesOf(r);
        bool ok = v.size() == 2;
        for (auto& x : v) ok = ok && x.part == 0 && x.gate && x.finger == (x.note == 60 ? 1 : 2);
        CHECK(ok, "single: two fingers, two voices on part 0, each following its own finger (%zu voices)", v.size());
    }

    // ---- split: left of the split point plays part 0, the rest part 1 ----
    {
        Rig r("strings/cello");
        r.engine.setInstrument(1, findInstrument("winds/flute"));
        Performance p;
        p.layout = Layout::Split;
        p.splitNote = 60;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(1, 48, 80); r.noteOn(2, 72, 80);
        r.press(1, 70); r.press(2, 70);       // pressed as a finger is: a bowed note with no pressure is near silent
        auto x = r.run(0.6);
        CHECK(gated(r, 0, 48) == 1 && gated(r, 1, 48) == 0, "split: note 48 plays the left part only");
        CHECK(gated(r, 1, 72) == 1 && gated(r, 0, 72) == 0, "split: note 72 plays the right part only");
        CHECK(finite(x) && rms(x, kSr / 5, x.size()) > 0.005, "split: both parts audible (rms %.3f)", rms(x, kSr / 5, x.size()));
        // A finger keeps its part when it glides over the split point.
        r.bend(1, 8192 + 8192 * 24 / 48);                  // +24 semitones: 48 -> 72
        r.run(0.1);
        CHECK(gated(r, 0, 48) == 1, "split: a finger that glides across the split stays on its part");
    }

    // ---- split, chosen by the finger (CC 9, the screen pad): any note can play either part ----
    {
        Rig r("strings/cello");
        r.engine.setInstrument(1, findInstrument("winds/flute"));
        Performance p;
        p.layout = Layout::Split;
        p.splitNote = 60;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.send(0xB1, 9, 2); r.noteOn(1, 48, 80);          // low note, right part
        r.send(0xB2, 9, 1); r.noteOn(2, 72, 80);          // high note, left part
        r.run(0.2);
        CHECK(gated(r, 1, 48) == 1 && gated(r, 0, 48) == 0, "split CC 9: note 48 plays the right part when the finger asks");
        CHECK(gated(r, 0, 72) == 1 && gated(r, 1, 72) == 0, "split CC 9: note 72 plays the left part when the finger asks");
        r.noteOff(1, 48); r.noteOff(2, 72); r.run(0.05);
        r.noteOn(1, 48, 80);                                // the choice ended with the note
        r.run(0.2);
        CHECK(gated(r, 0, 48) == 1 && gated(r, 1, 48) == 0, "split CC 9: the next note on that channel goes by note again");
    }

    // ---- four sections (the grid): CC 9 picks any of the four parts, each its own sound ----
    {
        Rig r("strings/cello");
        r.engine.setInstrument(1, findInstrument("winds/flute"));
        r.engine.setInstrument(2, findInstrument("plucked/nylon-guitar"));
        r.engine.setInstrument(3, findInstrument("synths/fm-keys"));
        Performance p;
        p.layout = Layout::Split;
        p.part[3].reverb = 0.5f;
        r.engine.setPerformance(p);
        r.run(0.01);
        for (int k = 0; k < kParts; k++) { r.send(0xB0 | (k + 1), 9, k + 1); r.noteOn(k + 1, 50 + k * 5, 80); }
        auto x = r.run(0.3);
        bool each = true;
        for (int k = 0; k < kParts; k++)
            for (int q = 0; q < kParts; q++) each = each && gated(r, q, 50 + k * 5) == (q == k ? 1 : 0);
        CHECK(each, "four parts: each finger plays only the part its CC 9 names");
        CHECK(finite(x) && rms(x, kSr / 10, x.size()) > 0.005, "four parts: audible (rms %.3f)", rms(x, kSr / 10, x.size()));
        Performance single;
        r.engine.setPerformance(single);
        for (int k = 0; k < kParts; k++) r.noteOff(k + 1, 50 + k * 5);
        r.run(0.05);
        r.noteOn(5, 62, 80); r.run(0.1);
        CHECK(gated(r, 0, 62) == 1 && gated(r, 2, 62) == 0 && gated(r, 3, 62) == 0, "four parts: alone again, only part 0 plays");
    }

    // ---- layer: every finger plays both parts ----
    {
        Rig r("strings/cello");
        r.engine.setInstrument(1, findInstrument("voices/soprano"));
        Performance p;
        p.layout = Layout::Layer;
        p.part[1].transpose = 12;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(3, 55, 80);
        r.run(0.1);
        auto v = voicesOf(r);
        float p0 = 0, p1 = 0;
        for (auto& x : v) (x.part == 0 ? p0 : p1) = x.pitch;
        CHECK(v.size() == 2 && gated(r, 0, 55) == 1 && gated(r, 1, 55) == 1, "layer: one finger, a voice on each part (%zu)", v.size());
        CHECK(std::fabs(p1 - p0 - 12) < 0.01f, "layer: part 1's own transpose (+12) applies to it only (%.2f vs %.2f)", p0, p1);
        r.noteOff(3, 55);
        r.run(0.1);
        CHECK(gated(r, 0, -1) == 0 && gated(r, 1, -1) == 0, "layer: lifting the finger releases both");
    }

    // ---- arpeggiator ----
    auto arpRig = [](ArpOrder order, int octaves = 1, bool latch = false) {
        Rig* r = new Rig("synths/fm-keys");
        Performance p;
        p.bpm = 120;
        p.part[0].arp = true;
        p.part[0].order = order;
        p.part[0].rate = 4;                                // 8 steps a second
        p.part[0].octaves = octaves;
        p.part[0].latch = latch;
        r->engine.setPerformance(p);
        r->run(0.01);
        return r;
    };
    auto chord = [](Rig& r) { r.noteOn(1, 64, 80); r.noteOn(2, 60, 80); r.noteOn(3, 67, 80); };   // pressed out of order

    {
        Rig& r = *arpRig(ArpOrder::Up);
        chord(r);
        auto seq = arpNotes(r, 1.0);
        CHECK(startsWith(seq, {60, 64, 67, 60, 64, 67}), "arp up: %s", str(seq).c_str());
        CHECK(seq.size() >= 8 && seq.size() <= 9, "arp: 8 steps a second at 120 bpm sixteenths (%zu)", seq.size());
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::Down);
        chord(r);
        auto seq = arpNotes(r, 0.8);
        CHECK(startsWith(seq, {67, 64, 60, 67}), "arp down: %s", str(seq).c_str());
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::UpDown);
        chord(r);
        auto seq = arpNotes(r, 1.0);
        CHECK(startsWith(seq, {60, 64, 67, 64, 60, 64, 67}), "arp up-down, ends not repeated: %s", str(seq).c_str());
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::AsPlayed);
        chord(r);
        auto seq = arpNotes(r, 0.5);
        CHECK(startsWith(seq, {64, 60, 67}), "arp as played: %s", str(seq).c_str());
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::Up, 2);
        chord(r);
        auto seq = arpNotes(r, 1.0);
        CHECK(startsWith(seq, {60, 64, 67, 72, 76, 79, 60}), "arp over 2 octaves: %s", str(seq).c_str());
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::Chord);
        chord(r);
        r.run(0.02);
        CHECK(gated(r, 0, -1) == 3, "arp chord: all three notes on each step (%d)", gated(r, 0, -1));
        delete &r;
    }
    {
        Rig& r = *arpRig(ArpOrder::Random);
        chord(r);
        auto seq = arpNotes(r, 2.0);
        bool inSet = !seq.empty();
        for (int n : seq) inSet = inSet && (n == 60 || n == 64 || n == 67);
        CHECK(inSet && std::count(seq.begin(), seq.end(), 60) > 0 && std::count(seq.begin(), seq.end(), 67) > 0,
              "arp random: only held notes, all of them come up (%zu steps)", seq.size());
        delete &r;
    }

    // timing: steps land on exact frames, whatever the callback size
    for (int block : {37, 192, 480}) {
        Rig& r = *arpRig(ArpOrder::Up);
        r.noteOn(1, 60, 80);
        r.run(double(block) / kSr, block);
        const bool immediate = r.engine.stats().arpSteps == 1;
        std::vector<double> times;
        uint64_t last = 1;
        double t = double(block) / kSr;
        while (t < 2.0) {
            r.run(double(block) / kSr, block);
            t += double(block) / kSr;
            if (r.engine.stats().arpSteps != last) { last = r.engine.stats().arpSteps; times.push_back(t); }
        }
        // Seen at callback ends, so each interval is 125 ms give or take one callback.
        bool even = times.size() >= 14;
        for (size_t i = 1; i < times.size(); i++) even = even && std::fabs(times[i] - times[i - 1] - 0.125) <= double(block) / kSr + 1e-9;
        CHECK(immediate && even, "arp timing, %d-frame callbacks: first step at once, then every 125 ms (%zu steps)", block, times.size() + 1);
        delete &r;
    }

    {   // gate: the note is held for that part of the step
        Rig& r = *arpRig(ArpOrder::Up);
        Performance p;
        p.part[0].arp = true;
        p.part[0].gate = 0.5f;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(1, 60, 80);
        r.run(0.125 * 0.25, 16);
        const bool on = gated(r, 0, 60) == 1;
        r.run(0.125 * 0.5, 16);
        const bool off = gated(r, 0, 60) == 0;
        CHECK(on && off, "arp gate 50%%: held at a quarter of the step, released by three quarters");
        delete &r;
    }

    {   // MPE: an arpeggiated note follows its finger's pressure and bend
        Rig& r = *arpRig(ArpOrder::Up);
        r.noteOn(1, 60, 80);
        r.press(1, 110);
        r.bend(1, 8192 + 8192 * 2 / 48);                    // +2 semitones
        r.run(0.3);
        float pressure = 0, pitch = 0;
        for (auto& v : voicesOf(r)) if (v.gate) { pressure = v.pressure; pitch = v.pitch; }
        CHECK(pressure > 0.8f, "arp MPE: the arp note follows the finger's pressure (%.2f)", pressure);
        CHECK(std::fabs(pitch - 62) < 0.05f, "arp MPE: and its bend (pitch %.2f, want 62)", pitch);
        delete &r;
    }

    {   // no latch: the arp stops when the hand lifts
        Rig& r = *arpRig(ArpOrder::Up);
        chord(r);
        r.run(0.3);
        r.noteOff(1, 64); r.noteOff(2, 60); r.noteOff(3, 67);
        r.run(0.02);
        const uint64_t steps = r.engine.stats().arpSteps;
        r.run(0.5);
        CHECK(gated(r, 0, -1) == 0 && r.engine.stats().arpSteps == steps, "arp, no latch: stops at once when the hand lifts");
        delete &r;
    }
    {   // latch: it goes on, and a new chord replaces the old one
        Rig& r = *arpRig(ArpOrder::Up, 1, true);
        chord(r);
        r.run(0.3);
        r.noteOff(1, 64); r.noteOff(2, 60); r.noteOff(3, 67);
        auto seq = arpNotes(r, 0.5);
        CHECK(seq.size() >= 3 && std::count(seq.begin(), seq.end(), 67) > 0, "arp latch: keeps playing the chord after the hand lifts: %s",
              str(seq).c_str());
        r.noteOn(4, 50, 80);
        r.run(0.15);
        r.noteOff(4, 50);
        seq = arpNotes(r, 0.5);
        bool only50 = !seq.empty();
        for (int n : seq) only50 = only50 && n == 50;
        CHECK(only50, "arp latch: a new chord after lifting replaces the old one: %s", str(seq).c_str());
        r.engine.panic();
        r.run(0.05);
        const uint64_t steps = r.engine.stats().arpSteps;
        r.run(0.5);
        CHECK(r.engine.stats().arpSteps == steps && gated(r, 0, -1) == 0, "arp latch: panic stops it");
        delete &r;
    }

    {   // the arp on one side of a split, plain playing on the other
        Rig r("synths/fm-keys");
        r.engine.setInstrument(1, findInstrument("strings/cello"));
        Performance p;
        p.layout = Layout::Split;
        p.splitNote = 60;
        p.part[0].arp = true;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(1, 48, 80); r.noteOn(2, 52, 80);          // left hand: arpeggio
        r.noteOn(3, 72, 80);                                // right hand: a held note
        auto seq = arpNotes(r, 0.6, 0);
        CHECK(startsWith(seq, {48, 52, 48}), "split + arp: the left hand arpeggiates: %s", str(seq).c_str());
        CHECK(gated(r, 1, 72) == 1 && gated(r, 0, 72) == 0, "split + arp: the right hand plays normally");
        auto x = r.run(0.5);
        CHECK(finite(x) && rms(x, 0, x.size()) > 0.005, "split + arp: audible, finite");
    }

    {   // switching the arp off with notes held: no stuck note, the fingers play again
        Rig& r = *arpRig(ArpOrder::Up, 1, true);
        chord(r);
        r.run(0.3);
        r.engine.setPerformance(Performance());
        r.run(0.05);
        r.noteOff(1, 64); r.noteOff(2, 60); r.noteOff(3, 67);
        r.run(3.0);
        CHECK(gated(r, 0, -1) == 0 && r.engine.activeVoices() == 0, "arp switched off mid-chord: nothing left sounding (%d)",
              r.engine.activeVoices());
        delete &r;
    }

    // ---- pitch assist ----
    {
        Rig r("synths/fm-keys");
        Performance p;
        p.scaleMask = 0b101010110101;                       // C major
        p.assist = 1;
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(1, 60, 80);
        r.bend(1, 8192 + int(8192 * 0.4 / 48));            // resting 40 cents sharp
        r.run(0.4);
        float pitch = voicesOf(r)[0].pitch;
        CHECK(std::fabs(pitch - 60) < 0.02f, "assist: a resting finger 40 cents off is pulled onto the note (%.3f)", pitch);
        // a glide is left alone: bend fast and look mid-way
        float raw = 0, got = 0;
        for (int i = 1; i <= 20; i++) {                      // 0 -> +5 semitones in 0.2 s (25 st/s)
            raw = 0.4f + 5.f * i / 20;
            r.bend(1, 8192 + int(8192 * raw / 48));
            r.run(0.01);
        }
        got = voicesOf(r)[0].pitch;
        CHECK(std::fabs(got - (60 + raw)) < 0.3f, "assist: a fast glide is not snapped (%.2f, finger at %.2f)", got, 60 + raw);
        r.run(0.4);                                          // rests at 65.4: F is in C major
        pitch = voicesOf(r)[0].pitch;
        CHECK(std::fabs(pitch - 65) < 0.02f, "assist: where the glide stops, it settles on the scale note (%.3f)", pitch);
    }
    {
        // A note outside the scale, played on purpose, stays in tune (F# and Bb in C major).
        for (int note : {66, 70}) {
            Rig r("synths/fm-keys");
            Performance p;
            p.scaleMask = 0b101010110101;                   // C major
            p.assist = 1;
            r.engine.setPerformance(p);
            r.run(0.01);
            r.noteOn(1, note, 80);
            r.bend(1, 8192 + int(8192 * 0.15 / 48));        // and resting 15 cents sharp of it
            r.run(0.4);
            float pitch = voicesOf(r)[0].pitch;
            CHECK(std::fabs(pitch - note) < 0.02f, "assist: note %d, outside the scale, is pulled onto itself, not the scale (%.3f)", note, pitch);
        }
    }
    {
        Rig r("synths/fm-keys");
        r.noteOn(1, 60, 80);
        r.bend(1, 8192 + int(8192 * 0.4 / 48));
        r.run(0.4);
        float pitch = voicesOf(r)[0].pitch;
        CHECK(std::fabs(pitch - 60.4f) < 0.02f, "assist off: the pitch is exactly what the finger plays (%.3f)", pitch);
    }

    // ---- octave check: where the player's notes sit ----
    {
        Rig r("strings/cello");
        for (int i = 0; i < 6; i++) { r.noteOn(1, 96 + i, 80); r.run(0.02); r.noteOff(1, 96 + i); r.run(0.02); }
        const double c = r.engine.stats().noteCenter;
        CHECK(std::fabs(c - 98.5) < 0.01, "note center: the average of the notes sent (%.1f)", c);
    }

    // ---- sound: effects and expression ----
    auto pluck = [](Performance p, double seconds, int note = 60) {
        Rig r("struck/marimba");
        r.engine.setPerformance(p);
        r.run(0.01);
        r.noteOn(1, note, 100);
        r.run(0.05);
        r.noteOff(1, note);
        return r.run(seconds);
    };
    {
        Performance dry, wet;
        wet.part[0].reverb = 0.6f;
        wet.reverbSize = 0.8f;
        auto a = pluck(dry, 3.0), b = pluck(wet, 3.0);
        const double ta = rms(a, 2 * kSr, 3 * kSr), tb = rms(b, 2 * kSr, 3 * kSr);
        CHECK(finite(b) && tb > 10 * ta + 1e-4, "reverb: a tail 2-3 s after a marimba note (%.5f, dry %.5f)", tb, ta);
    }
    {
        Performance wet;
        wet.part[0].delay = 1.f;
        wet.delayBeats = 0.5f;                              // 250 ms at 120 bpm
        wet.delayFeedback = 0.f;
        wet.delayPingPong = false;
        auto x = pluck(wet, 1.0, 84);
        // energy envelope in 10 ms windows, from the note on (0.05 s before the recording)
        auto win = [&](double t) { size_t a = size_t((t - 0.05) * kSr); return rms(x, a, a + kSr / 100); };
        const double echo = win(0.255), before = win(0.20);
        CHECK(finite(x) && echo > 2 * before, "delay: an echo 250 ms after the note (%.4f vs %.4f just before)", echo, before);
        wet.bpm = 60;                                       // the same half beat is now 500 ms
        auto y = pluck(wet, 1.0, 84);
        auto win2 = [&](double t) { size_t a = size_t((t - 0.05) * kSr); return rms(y, a, a + kSr / 100); };
        CHECK(win2(0.505) > 2 * win2(0.45), "delay: follows the tempo (500 ms at 60 bpm)");
    }
    {
        Performance bright, dark;
        dark.part[0].brightness = 0.f;
        auto hf = [](const std::vector<float>& x) {           // energy of the first difference ~ high frequencies
            double d = 0, e = 0;
            for (size_t i = 1; i < x.size(); i++) { d += double(x[i] - x[i - 1]) * (x[i] - x[i - 1]); e += double(x[i]) * x[i]; }
            return d / (e + 1e-12);
        };
        const double hb = hf(pluck(bright, 0.3, 79)), hd = hf(pluck(dark, 0.3, 79));
        CHECK(hd < hb * 0.5, "brightness 0: much less treble than open (%.4f vs %.4f)", hd, hb);
    }
    {
        auto bowed = [](float curve) {
            Rig r("synths/pad");                            // its level follows pressure (a bowed string's tone does)
            Performance p;
            p.part[0].pressureCurve = curve;
            r.engine.setPerformance(p);
            r.run(0.01);
            r.noteOn(1, 48, 80);
            r.press(1, 40);                                  // a light touch
            auto x = r.run(1.0);
            return rms(x, kSr / 2, kSr);
        };
        const double soft = bowed(0.5f), firm = bowed(2.f);
        CHECK(soft > firm * 1.3, "pressure curve: 0.5 plays a light touch louder than 2 (%.4f vs %.4f)", soft, firm);
    }

    // ---- what the touch view sees ----
    {
        Rig r("synths/fm-keys");
        r.noteOn(5, 62, 80);
        r.press(5, 64);
        r.slide(5, 100);
        r.run(0.1);
        FingerView f[16];
        int n = r.engine.fingers(f, 16);
        CHECK(n == 1 && f[0].channel == 5 && f[0].note == 62 && std::fabs(f[0].pressure - 64 / 127.f) < 1e-3 &&
              std::fabs(f[0].slide - 100 / 127.f) < 1e-3, "fingers(): channel, note, pressure, slide of the held finger");
        r.noteOff(5, 62);
        r.run(0.02);
        CHECK(r.engine.fingers(f, 16) == 0, "fingers(): empty once lifted");
    }

    return finish();
}
