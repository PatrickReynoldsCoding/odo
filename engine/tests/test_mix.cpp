// The mix of many notes: a chord must sound like its notes played together, not bent out of shape.
// The output used to go through tanh(1.2x), which made the notes of a chord beat against each
// other (a 4-note piano chord came out with distortion at -7 dB); now a limiter turns the whole
// sound down instead. And instrument versions side by side (INSTRUMENT_FORMAT.md).
#include <string>
#include "check.h"

namespace {

// `notes` started together on fingers 1.., held for `seconds`. Dry (no reverb or delay).
std::vector<float> play(const char* id, const std::vector<int>& notes, double seconds, float* limitDb = nullptr, int vel = 100) {
    Rig r(id);
    r.run(0.1);
    int ch = 1;
    for (int n : notes) { r.noteOn(ch, n, vel); r.press(ch, 90); ch++; }
    std::vector<float> out;
    float lim = 0;
    for (int b = 0; b < int(seconds * kSr / 128); b++) {
        std::vector<float> x = r.run(128.0 / kSr);
        out.insert(out.end(), x.begin(), x.end());
        lim = std::min(lim, r.engine.limiterDb());
    }
    if (limitDb) *limitDb = lim;
    return out;
}

// What is left of the chord, against its notes played alone and summed, once each 5 ms of the sum
// is scaled to fit (a limiter's slow gain is not distortion; a bent wave is). dB below the chord.
double distortionDb(const char* id, const std::vector<int>& notes, double seconds) {
    std::vector<float> chord = play(id, notes, seconds), sum(chord.size(), 0.f);
    for (int n : notes) { std::vector<float> one = play(id, {n}, seconds); for (size_t k = 0; k < sum.size(); k++) sum[k] += one[k]; }
    const size_t w = kSr / 200;
    double err = 0, sig = 0;
    for (size_t a = 0; a < sum.size(); a += w) {
        size_t b = std::min(sum.size(), a + w);
        double xy = 0, xx = 1e-20;
        for (size_t k = a; k < b; k++) { xy += double(chord[k]) * sum[k]; xx += double(sum[k]) * sum[k]; }
        for (size_t k = a; k < b; k++) { double d = chord[k] - xy / xx * sum[k]; err += d * d; sig += double(chord[k]) * chord[k]; }
    }
    return 10 * std::log10(err / (sig + 1e-30) + 1e-30);
}

// The loudest 400 ms of one note, dBFS.
double loudness(const char* id, int note) {
    std::vector<float> x = play(id, {note}, 2.0);
    const size_t w = kSr * 4 / 10;
    double best = 0;
    for (size_t a = 0; a + w <= x.size(); a += kSr / 20) best = std::max(best, rms(x, a, a + w));
    return 20 * std::log10(best + 1e-12);
}

}  // namespace

int main() {
    const std::vector<int> chord = {48, 52, 55, 60, 64};

    // ---- a chord is its notes together ----
    // Pad and Lead are the same every time and add up exactly: nothing may come between them.
    for (const char* id : {"patrickreynolds/pad", "patrickreynolds/lead"}) {
        double d = distortionDb(id, chord, 1.5);
        CHECK(d < -60, "%s: a 5-note chord is its notes added up (left over %.1f dB; the tanh gave about -10)", id, d);
    }
    // The pianos' chords are loud enough for the limiter: it turns them down without bending them.
    for (const char* id : {"patrickreynolds/piano", "patrickreynolds/max-richter"}) {
        double d = distortionDb(id, chord, 1.5);
        CHECK(d < -30, "%s: a 5-note chord through the limiter, left over %.1f dB (the tanh gave about -7)", id, d);
    }

    // ---- one note is never limited; the loudest chord never goes over ----
    const char* six[] = {"patrickreynolds/cello", "patrickreynolds/violin", "patrickreynolds/pad",
                         "patrickreynolds/piano", "patrickreynolds/max-richter", "patrickreynolds/lead"};
    for (const char* id : six) {
        float lim = 0;
        play(id, {60}, 1.0, &lim);
        CHECK(lim > -0.01f, "%s: one note plays untouched by the limiter (%.2f dB)", id, lim);
    }
    {
        float lim = 0;
        std::vector<int> all;
        for (int n = 36; n < 36 + 15; n++) all.push_back(n);
        std::vector<float> x = play("patrickreynolds/timpani", all, 1.0, &lim, 127);
        CHECK(finite(x) && peak(x) <= 0.891f, "15 timpani at full strength stay under the ceiling (peak %.3f, limited %.1f dB)", peak(x), lim);
    }

    // ---- the six sounds are as loud as each other ----
    for (const char* id : six) {
        double l = (loudness(id, 48) + loudness(id, 60) + loudness(id, 72)) / 3;
        CHECK(std::fabs(l + 20) < 1, "%s: as loud as the others (%.1f dBFS, want -20)", id, l);
    }

    // ---- versions side by side ----
    CHECK(mpe::compareVersions("1.10.0", "1.9.2") > 0 && mpe::compareVersions("1.0", "1.0.0") < 0 &&
          mpe::compareVersions("2.0.0", "2.0.0") == 0 && mpe::compareVersions("", "0.0.1") < 0, "versions compare by number");
    int now = mpe::findInstrument("patrickreynolds/piano"), old = mpe::findInstrument("patrickreynolds/piano@1.0.0");
    CHECK(now >= 0 && mpe::instrumentAt(now).version == "1.1.0", "the piano's newest version is found by its id");
    CHECK(old >= 0 && old != now && mpe::instrumentAt(old).version == "1.0.0", "and 1.0.0 by id@version, to go back to");
    CHECK(mpe::findInstrument("keys/piano") == now && mpe::findInstrument("keys/piano@1.0.0") == old, "an old id finds the versions too");
    CHECK(mpe::findInstrument("patrickreynolds/piano@9.9.9") < 0, "a version there isn't is not found");
    std::vector<int> v = mpe::instrumentVersions(old);
    CHECK(v.size() == 2 && v[0] == now && v[1] == old, "the piano's versions, newest first");
    CHECK(mpe::isNewestVersion(now) && !mpe::isNewestVersion(old), "only the newest is the newest");
    int flute = mpe::findInstrument("patrickreynolds/flute");
    CHECK(flute >= 0 && mpe::instrumentVersions(flute).size() == 1 && mpe::isNewestVersion(flute), "a sound with one version");
    {
        float lim = 0;
        std::vector<float> x = play("patrickreynolds/piano@1.0.0", {60}, 0.5, &lim);
        CHECK(finite(x) && peak(x) > 0.01f, "the kept 1.0.0 piano plays");
    }
    return finish();
}
