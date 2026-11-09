// The Hex keyboard's notes (controllers/hex/tuning.h): each layout's steps in each tuning, the pitch of
// every key, names, scales. The controller itself is played in test_packages; this is its maths alone.
#include <cmath>
#include <cstring>
#include <string>
#include "check.h"
#include "../../controllers/hex/tuning.h"

static bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

// Each layout's steps (to the right, up to the right) in each tuning, worked out by hand from the
// tuning's nearest whole steps to 2, 3 and 5.
static void layoutSteps() {
    struct Want { int tuning, layout, e, ne; };
    const Want want[] = {
        {HT_12, HL_WICKI, 2, 7},      {HT_12, HL_HARMONIC, 1, 4},  {HT_12, HL_BOSANQUET, 2, 1},
        {HT_19, HL_WICKI, 3, 11},     {HT_19, HL_HARMONIC, 1, 6},  {HT_19, HL_BOSANQUET, 3, 1},
        {HT_22, HL_WICKI, 4, 13},     {HT_22, HL_HARMONIC, 1, 7},  {HT_22, HL_BOSANQUET, 4, 3},
        {HT_31, HL_WICKI, 5, 18},     {HT_31, HL_HARMONIC, 2, 10}, {HT_31, HL_BOSANQUET, 5, 2},
        {HT_41, HL_WICKI, 7, 24},     {HT_41, HL_HARMONIC, 2, 13}, {HT_41, HL_BOSANQUET, 7, 4},
        {HT_53, HL_WICKI, 9, 31},     {HT_53, HL_HARMONIC, 3, 17}, {HT_53, HL_BOSANQUET, 9, 5},
        {HT_JUST, HL_WICKI, 2, 7},    {HT_JUST, HL_HARMONIC, 1, 4}, {HT_JUST, HL_BOSANQUET, 2, 1},
    };
    for (const Want& w : want) {
        int e, ne;
        ht_layout_steps(ht_tuning(w.tuning), w.layout, 0, 0, &e, &ne);
        CHECK(e == w.e && ne == w.ne, "%s in %s: %d to the right, %d up to the right (want %d, %d)",
              HT_LAYOUTS[w.layout].name, ht_tuning(w.tuning)->name, e, ne, w.e, w.ne);
    }
    // Hexatone's presets for the same EDOs: right, and down to the right (Bosanquet's diatonic semitone).
    const int hexatone[][3] = {{HT_12, 2, 1}, {HT_19, 3, 2}, {HT_22, 4, 1}, {HT_31, 5, 3}, {HT_41, 7, 3}, {HT_53, 9, 4}};
    for (auto& h : hexatone) {
        int e, ne;
        ht_layout_steps(ht_tuning(h[0]), HL_BOSANQUET, 0, 0, &e, &ne);
        CHECK(e == h[1] && e - ne == h[2], "Bosanquet in %s is Hexatone's: %d right, %d down-right", ht_tuning(h[0])->name, e, e - ne);
    }
    int e, ne;
    ht_layout_steps(ht_tuning(HT_31), HL_OWN, 3, -8, &e, &ne);
    CHECK(e == 3 && ne == -8, "Your own steps are taken as they are");
    // What each reaches: Harmonic table in 31 only every other step.
    ht_layout_steps(ht_tuning(HT_31), HL_HARMONIC, 0, 0, &e, &ne);
    CHECK(ht_gcd(e, ne) == 2, "Harmonic table in 31 equal reaches every other step");
    for (int t = 0; t < HT_COUNT; t++) for (int l = 0; l < HL_OWN; l++) {
        ht_layout_steps(ht_tuning(t), l, 0, 0, &e, &ne);
        if (!(t == HT_31 && l == HL_HARMONIC)) CHECK(ht_gcd(e, ne) == 1, "%s in %s reaches every step", HT_LAYOUTS[l].name, ht_tuning(t)->name);
    }
}

// Pitches: the key's root where 12-tone has it; the rest tuned from it.
static void pitches() {
    const HexTuning* t12 = ht_tuning(HT_12);
    for (int n = 0; n < 128; n++) if (!near(ht_pitch(t12, 0, 0, n), n)) { CHECK(false, "12 equal: note %d is %g", n, ht_pitch(t12, 0, 0, n)); break; }
    CHECK(near(ht_pitch(t12, 9, 9, 69), 69), "12 equal is MIDI's notes, in any key");
    const HexTuning* t31 = ht_tuning(HT_31);
    CHECK(ht_key_step(t31, 0) == 0 && ht_key_step(t31, 2) == 5 && ht_key_step(t31, 4) == 10 && ht_key_step(t31, 1) == 2 && ht_key_step(t31, 3) == 8,
          "31 equal: C 0, C♯ 2, D 5, E♭ 8, E 10");
    CHECK(near(ht_pitch(t31, 0, 0, 5 * 31 + 10), 60 + 10 * 12.0 / 31), "31 equal: E4 is 387 cents over C4 (%.4f)", ht_pitch(t31, 0, 0, 5 * 31 + 10));
    CHECK(near(ht_pitch(t31, 2, 5, 5 * 31 + 5), 62), "31 equal in D: D4 is MIDI's D4");
    CHECK(near(ht_pitch(t31, 2, 5, 5 * 31 + 5 + 18), 62 + 18 * 12.0 / 31), "31 equal in D: its fifth, 18 steps up");
    CHECK(near(ht_pitch(t31, 2, 5, 5 * 31 + 4), 62 - 12.0 / 31), "31 equal in D: a step under D4 is in the octave below");
    CHECK(ht_root_near(t31, 2, 5, 61.7) == 5 + 31 * 5, "the root nearest 61.7 in 31 equal, D: D4");

    const HexTuning* ji = ht_tuning(HT_JUST);
    CHECK(near(ht_pitch(ji, 0, 0, 64), 63.863137, 1e-5), "just: E over C is 5/4 (%.5f)", ht_pitch(ji, 0, 0, 64));
    CHECK(near(ht_pitch(ji, 0, 0, 67), 67.019550, 1e-5), "just: G over C is 3/2");
    CHECK(near(ht_pitch(ji, 9, 9, 73), 72.863137, 1e-5), "just in A: C♯ is 5/4 over A");
    CHECK(near(ht_pitch(ji, 9, 9, 57 + 12), 69), "just in A: A is A");
    CHECK(near(ht_pitch(ji, 0, 0, 52), 51.863137, 1e-5), "just: an octave down is the same note");
    const HexTuning* py = ht_tuning(HT_PYTHAGOREAN);
    CHECK(near(ht_pitch(py, 0, 0, 64), 64.0782, 1e-3) && near(ht_pitch(py, 0, 0, 61), 60.90225, 1e-4),
          "Pythagorean: E 81/64 (%.4f), D♭ 256/243", ht_pitch(py, 0, 0, 64));
    const HexTuning* mt = ht_tuning(HT_MEANTONE);
    CHECK(near(ht_pitch(mt, 0, 0, 64), 63.863137, 1e-4) && near(ht_pitch(mt, 0, 0, 67), 66.965784, 1e-4),
          "quarter-comma meantone: E is 5/4, G 696.6 cents (%.4f)", ht_pitch(mt, 0, 0, 67));

    // Every key of each layout in each tuning: its note is the steps it is from the key in the middle,
    // and its pitch that many steps above (or the table's).
    int bad = 0, total = 0;
    for (int t = 0; t < HT_COUNT; t++) for (int l = 0; l < HL_OWN; l++) for (int key = 0; key < 12; key++) {
        const HexTuning* tu = ht_tuning(t);
        int e, ne, ks = ht_key_step(tu, key), root = ht_root_near(tu, key, ks, 60);
        ht_layout_steps(tu, l, 0, 0, &e, &ne);
        for (int r = -3; r <= 3; r++) for (int q = -4; q <= 4; q++) {
            int note = ht_note_at(e, ne, root, q, r), steps = q * e + r * ne;
            double want = key + 12.0 * std::round((60.0 - key) / 12.0) + 12.0 * std::floor(double(steps) / tu->n)
                        + ht_cents(tu, ((steps % tu->n) + tu->n) % tu->n) / 100;
            total++;
            if (!near(ht_pitch(tu, key, ks, note), want, 1e-6)) bad++;
            // Isomorphic: the key to the right is always the same interval up.
            double step = ht_pitch(tu, key, ks, ht_note_at(e, ne, root, q + 1, r)) - ht_pitch(tu, key, ks, note);
            if (!tu->table && tu->fifth == 0 && !near(step, e * 12.0 / tu->n)) bad++;
        }
    }
    CHECK(bad == 0, "every key of every layout in every tuning and key plays its pitch (%d keys, %d wrong)", total, bad);
}

static std::string name(int tuning, int note) { char b[32]; return ht_name(ht_tuning(tuning), note, b) ? b : "?"; }

static void namesAndScales() {
    CHECK(name(HT_12, 61) == "C\xE2\x99\xAF" && name(HT_12, 70) == "B\xE2\x99\xAD", "12: C♯, B♭ (the Grid's names)");
    CHECK(name(HT_31, 2) == "C\xE2\x99\xAF" && name(HT_31, 3) == "D\xE2\x99\xAD" && name(HT_31, 31 + 5) == "D",
          "31: C♯ (%s), D♭ (%s), D", name(HT_31, 2).c_str(), name(HT_31, 3).c_str());
    CHECK(name(HT_31, 1) == "D\xE2\x99\xAD\xE2\x99\xAD", "31: a step over C is D double flat (%s)", name(HT_31, 1).c_str());
    CHECK(name(HT_19, 1) == "C\xE2\x99\xAF" && name(HT_19, 2) == "D\xE2\x99\xAD", "19: C♯ then D♭");
    CHECK(name(HT_53, 1) == "B\xE2\x99\xAF" && name(HT_53, 2) == "?", "53: a comma over C is B♯ (12 fifths up); two have no short name (%s)",
          name(HT_53, 2).c_str());
    // Chain of fifths: 31 equal's naturals, sharps and flats.
    const HexTuning* t31 = ht_tuning(HT_31);
    CHECK(ht_chain(t31, 18) == 1 && ht_chain(t31, 13) == -1 && ht_chain(t31, 2) == 7 && ht_chain(t31, 3) == -5, "31: G +1, F -1, C♯ +7, D♭ -5");
    // A major scale in 31 equal: its steps.
    int major = 0xAB5, in = 0;
    std::string steps;
    for (int i = 0; i < 31; i++) if (ht_in_scale(t31, major, i)) { in++; steps += std::to_string(i) + " "; }
    CHECK(in == 7 && steps == "0 5 10 13 18 23 28 ", "31 equal's major scale: %s", steps.c_str());
    int all = 0;
    for (int i = 0; i < 53; i++) all += ht_in_scale(ht_tuning(HT_53), 0xFFF, i);
    CHECK(all == 53, "Chromatic in 53 equal: every step");
    CHECK(ht_in_scale(ht_tuning(HT_JUST), major, 4) && !ht_in_scale(ht_tuning(HT_JUST), major, 3), "just, major: E in, E♭ out");
}

int main() {
    layoutSteps();
    pitches();
    namesAndScales();
    return finish();
}
