// Packages: an instrument package compiled while running (libfaust) sounds like the same
// instrument compiled in, and a controller package (the Tonnetz) plays the notes it should.
#include <algorithm>
#include <cstring>
#include <ctime>
#include "check.h"
#include "mpe/controller.h"
#include "mpe/package.h"
#include "../../controllers/hex/tuning.h"
#include "mpe/scala.h"
#ifdef MPE_LIBFAUST
#include "mpe/faust_loader.h"
#endif

using namespace mpe;

// One finger plays `note` for a second; the left channel.
static std::vector<float> play(int index, int note) {
    Engine e(kSr);
    MidiInput in(e);
    e.setInstrument(index);
    std::vector<float> out, buf(256);
    int64_t t = 1000000000;
    auto feed = [&](uint8_t a, uint8_t b, uint8_t c, int n) { uint8_t m[3] = {a, b, c}; in.feed(m, n, t); };
    feed(0xD1, 90, 0, 2);
    feed(0x91, uint8_t(note), 20, 3);
    for (int k = 0; k < kSr / 128; k++) {
        e.render(buf.data(), 128, t);
        for (int i = 0; i < 128; i++) out.push_back(buf[2 * i]);
        t += 1000000000LL * 128 / kSr;
    }
    return out;
}

static void instrumentPackage() {
    std::string err;
    auto pkg = Package::read(MPE_TEST_PACKAGES "/flute.odoi", &err);
    CHECK(pkg != nullptr, "flute.odoi reads (%s)", err.c_str());
    if (!pkg) return;
    CHECK(pkg->isInstrument() && pkg->id() == "patrickreynolds/flute", "it is the flute (%s)", pkg->id().c_str());
    CHECK(pkg->manifest["aliases"][size_t(0)].str() == "winds/flute", "its old id is an alias");
    CHECK(pkg->has("lib/stdfaust.lib") && pkg->has("lib/mpe.lib"), "it carries its libraries");
    CHECK(pkg->manifest["sound"]["reverb"].num(-1) > 0.2, "the flute's own effects are in its manifest");
    for (int i = 0; i < instrumentCount(); i++) if (instrumentAt(i).id == "patrickreynolds/flute") {
        std::string snd = instrumentAt(i).meta("sound");
        CHECK(snd.find("reverb=") != std::string::npos && snd.find("pressureCurve=") != std::string::npos,
              "the engine hands the app the flute's own effects (\"%s\")", snd.c_str());
    }
#ifdef MPE_LIBFAUST
    int builtin = -1;
    for (int i = 0; i < instrumentCount(); i++) if (instrumentAt(i).id == "patrickreynolds/flute") { builtin = i; break; }
    int loaded = loadInstrumentPackage(*pkg, MPE_TEST_CACHE, &err);
    CHECK(loaded >= 0, "libfaust compiles the package (%s)", err.c_str());
    if (loaded < 0 || builtin < 0) return;
    CHECK(findInstrument("winds/flute") == loaded, "found by its alias");
    std::vector<float> a = play(builtin, 72), b = play(loaded, 72);
    double diff = 0, level = rms(a, 0, a.size());
    for (size_t i = 0; i < a.size(); i++) diff += double(a[i] - b[i]) * (a[i] - b[i]);
    diff = std::sqrt(diff / double(a.size()));
    CHECK(level > 0.01, "the flute sounds (rms %.3f)", level);
    CHECK(diff < level * 1e-3, "compiled in and compiled while running sound the same (difference %.2g of %.2g)", diff, level);

    auto cello = Package::read(MPE_TEST_PACKAGES "/cello.odoi", &err);
    int c = cello ? loadInstrumentPackage(*cello, MPE_TEST_CACHE, &err) : -1;
    CHECK(c >= 0 && instrumentAt(c).createPost != nullptr, "an instrument with a body (post) loads with it (%s)", err.c_str());
    if (c >= 0) CHECK(rms(play(c, 48), 0, kSr) > 0.005, "the cello sounds");
#else
    printf("(no libfaust: compiling while running not tested)\n");
#endif
}

struct Heard { int ons = 0, offs = 0; std::vector<int> notes; std::vector<int> kinds; };

static void controllerPackage() {
    std::string err;
    auto pkg = Package::read(MPE_TEST_CONTROLLERS "/tonnetz.odoc", &err);
    CHECK(pkg != nullptr, "tonnetz.odoc reads (%s)", err.c_str());
    if (!pkg) return;
    CHECK(pkg->isController() && pkg->manifest["settings"].size() == 9, "it is the Tonnetz, with its 9 settings");
    Heard h;
    ControllerHost host([&](const uint8_t* m, int n) {
        if (n == 3 && (m[0] & 0xF0) == 0x90 && m[2] > 0) { h.ons++; h.notes.push_back(m[1]); }
        if (n == 3 && (m[0] & 0xF0) == 0x80) h.offs++;
        h.kinds.push_back(m[0] & 0xF0);
    });
    CHECK(host.load(*pkg, &err), "wasm3 runs it (%s)", err.c_str());
    // 700 x 400 dp at size 7: dx = 100, the key's note (C) in the middle; the middle of the instrument is C4.
    host.init(700, 400, 0);
    host.instrument(0, 60);
    CHECK(host.takeDirty() && host.drawList().size() > 1000, "it draws (%zu bytes)", host.drawList().size());

    host.touch(1, ControllerHost::Down, 350, 200, 0, 0);          // the middle node: C
    CHECK(h.ons == 1 && h.notes.size() == 1 && h.notes[0] % 12 == 0, "a node plays its note (%d notes, first %d)", h.ons,
          h.notes.empty() ? -1 : h.notes[0]);
    {   // The engine starts a finger's bend and pressure at zero, so they must follow the note-on.
        auto on = std::find(h.kinds.begin(), h.kinds.end(), 0x90);
        CHECK(on != h.kinds.end() && std::find(on, h.kinds.end(), 0xD0) != h.kinds.end(), "its pressure comes after the note-on");
    }
    host.touch(1, ControllerHost::Up, 350, 200, 0, 0);
    CHECK(h.offs == 1, "lifting ends it");

    // The middle of the up-triangle right of C: C, G and E: C major.
    h = Heard();
    host.touch(2, ControllerHost::Down, 350 + 50, 200 - 29, 0, 0);
    std::vector<int> pcs;
    for (int n : h.notes) pcs.push_back(n % 12);
    std::sort(pcs.begin(), pcs.end());
    CHECK(h.ons == 3 && pcs == std::vector<int>({0, 4, 7}), "a triangle plays its triad (C major: %d notes)", h.ons);
    // Slide to the down-triangle next to it (E minor: E, G, B): C stops, B starts, E and G keep sounding.
    int before = h.ons;
    host.touch(2, ControllerHost::Move, 350 + 100, 200 - 58, 0, 0);
    CHECK(h.offs == 1 && h.ons == before + 1, "sliding to the next triangle changes only the note they don't share");
    host.stop();
    CHECK(h.offs == 4, "stop lets go of the three still sounding (%d note-offs in all)", h.offs);

    // Strum (setting 5 = Slow, 70 ms): the chord's notes come one after another.
    host.setting(5, 2);
    h = Heard();
    host.touch(3, ControllerHost::Down, 400, 171, 0, 0);
    int first = h.ons;
    for (int k = 0; k < 30 && h.ons < 3; k++) { host.tick(); struct timespec ts{0, 10000000}; nanosleep(&ts, nullptr); }
    CHECK(first == 1 && h.ons == 3, "a strum starts one note, then the rest in time (%d, then %d)", first, h.ons);
    CHECK(host.lastError().empty(), "no errors from the WebAssembly (%s)", host.lastError().c_str());
}

// The Hex keyboard (controllers/hex): every key plays its layout's note in its tuning, through the
// MESH, as MIDI: a note-on and a bend over 48 semitones.
struct Voice { int note = -1, bend = 8192; float pitch() const { return float(note) + float(bend - 8192) / 8192 * 48; } };

static void hexPackage() {
    std::string err;
    auto pkg = Package::read(MPE_TEST_CONTROLLERS "/hex.odoc", &err);
    CHECK(pkg != nullptr, "hex.odoc reads (%s)", err.c_str());
    if (!pkg) return;
    const Json& ss = pkg->manifest["settings"];
    auto at = [&](const char* id) { for (size_t i = 0; i < ss.size(); i++) if (ss[i]["id"].str() == id) return int(i); return -1; };
    Voice ch[16];
    int ons = 0, offs = 0, lastCh = 0, vel = 0;
    std::vector<int> pressures;
    ControllerHost host([&](const uint8_t* m, int n) {
        int c = m[0] & 0x0F;
        if (n == 3 && (m[0] & 0xF0) == 0x90 && m[2] > 0) { ons++; ch[c] = Voice(); ch[c].note = m[1]; lastCh = c; vel = m[2]; }
        if (n == 3 && (m[0] & 0xF0) == 0x80) offs++;
        if (n == 3 && (m[0] & 0xF0) == 0xE0) ch[c].bend = m[1] | (m[2] << 7);
        if (n == 2 && (m[0] & 0xF0) == 0xD0) pressures.push_back(m[1]);
    });
    CHECK(host.load(*pkg, &err), "wasm3 runs the Hex keyboard (%s)", err.c_str());
    // 452 x 400, upright (Flip 90°), not mirrored, nothing beside the keys, 4 to a row, the least
    // spacing: a key every 100 across (gap 2), rows 86.6 apart, the bottom row's middles at y 356.7.
    // The middle key (row 2, place 2) plays C4.
    host.init(452, 400, 0);
    host.instrument(0, 60);
    for (size_t i = 0; i < ss.size(); i++) host.setting(int(i), ss[i]["default"].num());
    const std::pair<const char*, double> plain[] = {{"flip90", 1}, {"flipAcross", 1}, {"cols", 4}, {"spacing", 2}, {"scrollBar", 0},
                                                    {"sideLatch", 0}, {"sideDyn", 0}, {"vibrato", 0}, {"press", 0}};
    for (auto& p : plain) host.setting(at(p.first), p.second);
    host.tick();
    CHECK(host.takeDirty() && host.drawList().size() > 2000, "it draws (%zu bytes)", host.drawList().size());

    struct Case { int tuning, layout, e, ne; };
    const Case cases[] = {{HT_12, HL_WICKI, 2, 7}, {HT_12, HL_HARMONIC, 1, 4}, {HT_12, HL_BOSANQUET, 2, 1},
                          {HT_31, HL_WICKI, 5, 18}, {HT_31, HL_BOSANQUET, 5, 2}, {HT_53, HL_HARMONIC, 3, 17},
                          {HT_19, HL_WICKI, 3, 11}, {HT_JUST, HL_WICKI, 2, 7}, {HT_MEANTONE, HL_BOSANQUET, 2, 1}};
    int id = 1;
    for (const Case& k : cases) {
        host.setting(at("tuning"), k.tuning);
        host.setting(at("hexLayout"), k.layout);
        const HexTuning* t = ht_tuning(k.tuning);
        int bad = 0;
        std::string worst;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
            float x = 51 + 100 * c + 50 * (r % 2), y = 400 - (r + 0.5f) * 86.60254f;
            int before = ons;
            host.touch(id, ControllerHost::Down, x, y, 0, 0);
            int q = c - r / 2;
            double want = ht_pitch(t, 0, 0, ht_note_at(k.e, k.ne, ht_root_near(t, 0, 0, 60), q - 1, r - 2));
            float got = ons > before ? ch[lastCh].pitch() : -1;
            if (std::fabs(got - want) > 0.01) { bad++; worst = "row " + std::to_string(r) + " place " + std::to_string(c) + ": " + std::to_string(got) + " not " + std::to_string(want); }
            host.touch(id, ControllerHost::Up, x, y, 0, 0);
            id++;
        }
        CHECK(bad == 0, "%s in %s: all 16 keys play their pitches %s", HT_LAYOUTS[k.layout].name, t->name, worst.c_str());
    }

    // A Scala file's tuning, given as settings as the apps do (mpe/scala.h): quarter-comma meantone
    // from a file plays as the built-in one; Bohlen-Pierce (13 to the tritave) as tuning.h has it.
    {
        size_t sc = size_t(at("scala"));
        auto load = [&](const char* text) {
            scala::Scale s;
            if (!scala::parse(text, &s)) return false;
            auto set = scala::settings(pkg->manifest, sc, s);
            for (auto& p : set) host.setting(int(p.first), p.second);
            return !set.empty();
        };
        std::string mt = "meantone\n12\n";
        for (int k = 1; k <= 12; k++) { char b[32]; snprintf(b, sizeof b, "%.10f\n", ht_cents(ht_tuning(HT_MEANTONE), k) + (k == 12 ? 1200 : 0)); mt += b; }
        double bpc[13];
        std::string bp = "Bohlen-Pierce\n13\n";
        for (int k = 1; k <= 13; k++) { bpc[k - 1] = 1901.9550008653874 * k / 13; char b[32]; snprintf(b, sizeof b, "%.10f\n", bpc[k - 1]); bp += b; }
        HexTuning bpt;
        double table[HT_MAX_N];
        int val[3];
        ht_scala(&bpt, table, val, bpc, 13);
        struct Sc { const char* text; const HexTuning* t; int layout; const char* name; };
        const Sc scs[] = {{mt.c_str(), ht_tuning(HT_MEANTONE), HL_BOSANQUET, "meantone"}, {bp.c_str(), &bpt, HL_WICKI, "Bohlen-Pierce"}};
        for (const Sc& k : scs) {
            CHECK(load(k.text), "a Scala file (%s) given as settings", k.name);
            host.setting(at("tuning"), HT_COUNT);        // Scala file, after the built-in tunings
            host.setting(at("hexLayout"), k.layout);
            int e, ne;
            ht_layout_steps(k.t, k.layout, 0, 0, &e, &ne);
            int bad = 0;
            std::string worst;
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
                float x = 51 + 100 * c + 50 * (r % 2), y = 400 - (r + 0.5f) * 86.60254f;
                int before = ons;
                host.touch(id, ControllerHost::Down, x, y, 0, 0);
                double want = ht_pitch(k.t, 0, 0, ht_note_at(e, ne, ht_root_near(k.t, 0, 0, 60), c - r / 2 - 1, r - 2));
                float got = ons > before ? ch[lastCh].pitch() : -1;
                if (std::fabs(got - want) > 0.01) { bad++; worst = std::to_string(got) + " not " + std::to_string(want); }
                host.touch(id, ControllerHost::Up, x, y, 0, 0);
                id++;
            }
            CHECK(bad == 0, "%s from a Scala file: all 16 keys play their pitches %s", k.name, worst.c_str());
        }
        host.setting(int(sc), 0);                         // none loaded: 12 equal
        host.touch(id, ControllerHost::Down, 251, 400 - 2.5f * 86.60254f, 0, 0);
        CHECK(std::fabs(ch[lastCh].pitch() - 60) < 0.01, "Scala file with none loaded: 12 equal (%.2f)", ch[lastCh].pitch());
        host.touch(id++, ControllerHost::Up, 251, 400 - 2.5f * 86.60254f, 0, 0);
    }

    // Your own steps: 3 to the right, 1 up to the right, in 12.
    host.setting(at("tuning"), HT_12);
    host.setting(at("hexLayout"), HL_OWN);
    host.setting(at("stepRight"), 3);
    host.setting(at("stepUpRight"), 1);
    host.touch(id, ControllerHost::Down, 251 + 100, 400 - 2.5f * 86.60254f, 0, 0);   // row 2, place 3: one right of the middle
    CHECK(std::fabs(ch[lastCh].pitch() - 63) < 0.01, "Your own steps: the key right of C4 is 3 steps up (%.2f)", ch[lastCh].pitch());
    host.touch(id++, ControllerHost::Up, 351, 400 - 2.5f * 86.60254f, 0, 0);

    // Mirrored (the Grid's way round): the notes climb right to left.
    host.setting(at("hexLayout"), HL_WICKI);
    host.setting(at("flipAcross"), 0);
    host.touch(id, ControllerHost::Down, 351, 400 - 2.5f * 86.60254f, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 58) < 0.01, "mirrored: the key right of C4 is a whole tone down (%.2f)", ch[lastCh].pitch());
    host.touch(id++, ControllerHost::Up, 351, 400 - 2.5f * 86.60254f, 0, 0);
    host.setting(at("flipAcross"), 1);

    // Pressing harder: the touch's force grows, and so does its pressure, as aftertouch. (Struck in
    // the middle of the key, the dynamics there leave room for more.)
    host.setting(at("press"), 1);
    pressures.clear();
    host.touch(id, ControllerHost::Down, 251, 400 - 2.5f * 86.60254f, 0, 0.3f);
    int first = pressures.empty() ? -1 : pressures.back();
    host.touch(id, ControllerHost::Move, 251, 400 - 2.5f * 86.60254f, 0, 0.5f);
    int harder = pressures.empty() ? -1 : pressures.back();
    CHECK(first >= 0 && harder > first + 30, "Press harder: pressure %d, then %d as the force grows", first, harder);
    host.touch(id++, ControllerHost::Up, 251, 400 - 2.5f * 86.60254f, 0, 0);
    host.setting(at("press"), 0);
    pressures.clear();
    host.touch(id, ControllerHost::Down, 251, 400 - 2.5f * 86.60254f, 0, 0.3f);
    host.touch(id, ControllerHost::Move, 251, 400 - 2.5f * 86.60254f, 0, 0.5f);
    CHECK(pressures.size() <= 1, "Press harder off: the force changes nothing (%zu pressures)", pressures.size());
    host.touch(id++, ControllerHost::Up, 251, 400 - 2.5f * 86.60254f, 0, 0);

    // Its own touch: one finger keeps its note wherever it goes (no bend, no slide between notes).
    // Upright (Flip 90°, not mirrored), the row runs left to right: C4 in the middle, D4 to its
    // right, B♭3 to its left (Wicki-Hayden: whole tones along a row). The middle key's middle is
    // (251, 183.5), the keys 100 apart.
    const float my = 400 - 2.5f * 86.60254f;
    int before = ons;
    host.touch(id, ControllerHost::Down, 251, my, 0, 0);
    host.touch(id, ControllerHost::Move, 351, my, 0, 0);
    CHECK(ons == before + 1 && std::fabs(ch[lastCh].pitch() - 60) < 0.01, "one finger slid onto D: still C4, no bend (%.2f)", ch[lastCh].pitch());
    host.touch(id++, ControllerHost::Up, 351, my, 0, 0);
    // Two fingers in C4: one note. Only one out: nothing yet. Both out: it slides, by how near the
    // finger is to the next key: on the line between C and D (50 right), C♯; at D's middle, D.
    before = ons;
    int a = id++, b = id++;
    host.touch(a, ControllerHost::Down, 245, my, 0, 0);
    host.touch(b, ControllerHost::Down, 257, my + 5, 0, 0);
    CHECK(ons == before + 1, "two fingers in a key: one note (%d)", ons - before);
    host.touch(a, ControllerHost::Move, 330, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 60) < 0.01, "one of the two out of the key: no bend yet (%.2f)", ch[lastCh].pitch());
    host.touch(b, ControllerHost::Move, 305, my + 5, 0, 0);      // both out: the one further out (a) plays it
    CHECK(std::fabs(ch[lastCh].pitch() - 61.58f) < 0.02, "both out: a slide, 0.79 of a key toward D (%.2f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Move, 301, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 61) < 0.02, "on the line between C and D: C♯ (%.2f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Move, 351, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 62) < 0.01, "at D's middle: D (%.2f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Move, 151, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 58) < 0.01, "back past C to B♭'s middle: B♭ (%.2f)", ch[lastCh].pitch());
    int slideCh = lastCh;
    // The other finger is free: it lifts and plays another key, a new note, and the slide goes on.
    host.touch(b, ControllerHost::Up, 305, my, 0, 0);
    before = ons;
    host.touch(b, ControllerHost::Down, 351, my - 2 * 86.60254f, 0, 0);
    CHECK(ons == before + 1 && lastCh != slideCh, "the freed finger plays another key: a new note");
    host.touch(b, ControllerHost::Up, 351, my - 2 * 86.60254f, 0, 0);
    host.touch(a, ControllerHost::Move, 201, my, 0, 0);
    CHECK(std::fabs(ch[slideCh].pitch() - 59) < 0.02, "the slide goes on with one finger: B (%.2f)", ch[slideCh].pitch());
    host.touch(a, ControllerHost::Up, 201, my, 0, 0);
    // Snap: 1 point, only the notes; 4 points, quarter steps between keys.
    host.setting(at("slideSnap"), 1);
    host.setting(at("snapPoints"), 1);
    a = id++; b = id++;
    host.touch(a, ControllerHost::Down, 245, my, 0, 0);
    host.touch(b, ControllerHost::Down, 257, my, 0, 0);
    host.touch(a, ControllerHost::Move, 325, my, 0, 0);
    host.touch(b, ControllerHost::Move, 310, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 62) < 0.01, "snap, 1 point: 0.74 of a key snaps to D (%.2f)", ch[lastCh].pitch());
    host.setting(at("snapPoints"), 4);
    host.touch(a, ControllerHost::Move, 314, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 61.5f) < 0.01, "snap, 4 points: 0.63 of a key snaps to 3/4 (%.2f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Up, 314, my, 0, 0);
    host.touch(b, ControllerHost::Up, 310, my, 0, 0);
    host.setting(at("slideSnap"), 0);
    // The second finger lifted before they slid: back to one finger, which plays no bend.
    a = id++; b = id++;
    host.touch(a, ControllerHost::Down, 251, my, 0, 0);
    host.touch(b, ControllerHost::Down, 255, my, 0, 0);
    host.touch(b, ControllerHost::Up, 255, my, 0, 0);
    host.touch(a, ControllerHost::Move, 351, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 60) < 0.01, "the second lifted first: one finger again, no bend (%.2f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Up, 351, my, 0, 0);
    // A just tuning slides to its own notes: at D's middle, D just (9/8 above C: 2.04 semitones).
    host.setting(at("tuning"), HT_JUST);
    a = id++; b = id++;
    host.touch(a, ControllerHost::Down, 245, my, 0, 0);
    host.touch(b, ControllerHost::Down, 257, my, 0, 0);
    host.touch(b, ControllerHost::Move, 330, my, 0, 0);
    host.touch(a, ControllerHost::Move, 351, my, 0, 0);
    CHECK(std::fabs(ch[lastCh].pitch() - 62.039f) < 0.01, "just: the slide reaches D at 9/8 (%.3f)", ch[lastCh].pitch());
    host.touch(a, ControllerHost::Up, 351, my, 0, 0);
    host.touch(b, ControllerHost::Up, 330, my, 0, 0);
    host.setting(at("tuning"), HT_12);
    host.stop();
    CHECK(host.lastError().empty(), "no errors from the WebAssembly (%s)", host.lastError().c_str());
    (void)offs; (void)vel;
}

int main() {
    instrumentPackage();
    controllerPackage();
    hexPackage();
    return finish();
}
