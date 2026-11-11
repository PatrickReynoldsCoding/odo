// Takes (mpe/take.h): the file reads back exactly; a take recorded on the Grid plays back the same
// MIDI it made; its touches played again make the same notes; a score expands into touches that
// play the notes it names, with its gestures; the player's speed, loop and ghost fingers; and the
// example takes in takes/ check out.
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "check.h"
#include "mpe/take.h"

using namespace mpe;
using namespace mpe::take;

static std::unique_ptr<Package> controller(const std::string& name) {
    std::string err;
    auto p = Package::read(std::string(MPE_TEST_CONTROLLERS) + "/" + name + ".odoc", &err);
    if (!p) std::printf("cannot read %s: %s\n", name.c_str(), err.c_str());
    return p;
}

struct Midi { double t; std::vector<uint8_t> b; };

static void fileRoundTrip() {
    Take t;
    t.name = "Round: trip";
    t.by = "a test";
    t.about = "two lines\nof about";
    t.tempo = 96;
    t.meterBeats = 3;
    t.key = "D dorian";
    Side s;
    s.controller = "patrickreynolds/grid";
    s.controllerVersion = "2.8.0";
    s.settings = {{"cols", 3}, {"notes0", 281474976710655.0}, {"slide", 1}};
    s.sounds = {"patrickreynolds/cello"};
    s.width = 812.5f; s.height = 400; s.hasCenters = true; s.centers[0] = 48.25f; s.centers[1] = 60;
    t.sides.push_back(s);
    t.sides.push_back(s);
    t.touches.push_back({0, 0, 3, Down, 0.123456789f, 0.987654321f, 0.7f, 0});
    t.touches.push_back({8.333, 0, 3, Move, 1.0f / 3, 2.0f / 3, 0.71f, 12.5f});
    t.touches.push_back({16.667, 1, 2, Down, 0.5f, 0.5f, 1, 0});
    t.touches.push_back({100.001, 0, 3, Up, 1.0f / 3, 2.0f / 3, 0, 0});
    t.notes.push_back({0, 0, 5, On, 1, 60.0123456f, 0.3333333f, 0.5f, 0.9f});
    t.notes.push_back({8.333, 0, 5, Change, 0, 60.5f, 0.4f, 0.25f, 0});
    t.notes.push_back({9, 1, 2, Volume, 0, 0, 0.5f, 0, 0});
    t.notes.push_back({100.001, 0, 5, Off, 0, 0, 0, 0, 0});
    t.settings.push_back({50, 0, "slide", 0});
    t.sections.push_back({0, "A: the start"});
    t.says.push_back({0, 1000, "Lift your finger here, # not a comment"});
    t.score = "tempo 96\nC4:1 D4:1\n";
    std::string text = write(t);
    Take back;
    std::string err;
    std::vector<std::string> warnings;
    bool ok = parse(text, back, &err, &warnings);
    CHECK(ok && warnings.empty(), "a written take reads back (%s, %zu warnings)", err.c_str(), warnings.size());
    if (getenv("TAKE_DEBUG")) std::printf("%s\n----\n%s\n", text.c_str(), write(back).c_str());
    CHECK(write(back) == text, "and writes out the same text");
    bool same = back.touches.size() == t.touches.size() && back.notes.size() == t.notes.size();
    for (size_t i = 0; same && i < t.touches.size(); i++) {
        const Touch &a = t.touches[i], &b = back.touches[i];
        same = a.t == b.t && a.side == b.side && a.finger == b.finger && a.phase == b.phase && a.x == b.x && a.y == b.y && a.force == b.force && a.size == b.size;
    }
    for (size_t i = 0; same && i < t.notes.size(); i++) {
        const Note &a = t.notes[i], &b = back.notes[i];
        same = a.t == b.t && a.side == b.side && a.voice == b.voice && a.kind == b.kind && a.pitch == b.pitch && a.pressure == b.pressure &&
               a.slide == b.slide && a.velocity == b.velocity && (a.kind != On || a.part == b.part);
    }
    CHECK(same, "every touch and note reads back exactly (floats to the bit, times to the microsecond)");
    CHECK(back.sides.size() == 2 && back.sides[1].settings.size() == 3 && back.sides[1].settings[1].second == 281474976710655.0 &&
          back.sides[0].centers[0] == 48.25f && back.sides[0].width == 812.5f, "both sides' set-ups read back (a 48-bit setting whole)");
    CHECK(back.name == t.name && back.about == t.about && back.says.size() == 1 && back.says[0].text == t.says[0].text &&
          back.sections[0].name == "A: the start" && back.score == t.score, "the name, words, sections and score read back");

    Take x;
    CHECK(!parse("odo-take/2\nname: x\n", x, &err) && err.find("newer") != std::string::npos, "a newer major version is refused: %s", err.c_str());
    CHECK(!parse("{\"format\":\"odo-preset/1\"}", x, &err), "a preset is not a take: %s", err.c_str());
    warnings.clear();
    CHECK(parse("odo-take/1.4\nname: x\nfuture: thing\nt 0 1 down 0.5 0.5 0.5\nwiggle 3 4\n", x, &err, &warnings) && warnings.size() == 2,
          "a newer minor version reads, leaving out what it doesn't know with a warning each (%zu)", warnings.size());
    CHECK(fileName(t) == "Round- trip.odotake", "its file name: %s", fileName(t).c_str());
}

// The Grid, on its own clock, with its MIDI kept.
struct Live {
    double clock = 1000;
    std::vector<Midi> midi;
    bool keep = true;
    ControllerHost host;
    explicit Live(const Package& pkg) : host([this](const uint8_t* b, int n) { if (keep) midi.push_back({clock, std::vector<uint8_t>(b, b + n)}); }) {
        host.setClock([this] { return clock; });
        host.load(pkg);
        host.init(800, 500, 0);
        host.instrument(0, 60);
        host.instrument(1, 72);
    }
    double nextTick = 1000;
    void until(double t) {
        while (nextTick <= t) { clock = nextTick; host.tick(); nextTick += 1000.0 / 120; }
        clock = t;
    }
};

static void recordAndPlay() {
    auto grid = controller("grid");
    if (!grid) return;
    Live live(*grid);
    // Where two notes are on it, found by the layout (what a score uses too).
    take::Layout lay;
    float c[kCenters] = {60, 72, 72, 72};
    CHECK(lay.scan(*grid, live.host.values(), 800, 500, c), "the Grid's layout scans: %zu cells", lay.cells().size());
    const take::Layout::Cell* a = lay.find(60, -1, -1);
    const take::Layout::Cell* b = a ? lay.find(64, a->x, a->y) : nullptr;
    if (!a) a = &lay.cells()[0];
    if (!b) b = &lay.cells()[lay.cells().size() / 2];
    float ax = a->x, ay = a->y, bx = b->x, by = b->y;
    Recorder rec;
    rec.start();
    rec.attach(live.host, 0, {"patrickreynolds/cello"});
    live.until(1100);
    // A finger down, wiggling (vibrato); a second over it; then one sliding from one note to the other.
    live.host.touch(1, ControllerHost::Down, ax, ay, 0, 0.6f);
    for (int k = 1; k <= 60; k++) {
        live.until(1100 + 10 * k);
        live.host.touch(1, ControllerHost::Move, ax + 12 * std::sin(k * 0.6f), ay + float(k) * 0.3f, 0, 0.6f + 0.004f * k);
        if (k == 20) live.host.touch(2, ControllerHost::Down, bx, by, 0, 0.8f);
        if (k == 30) live.host.setting(0, live.host.values()[0]);   // the same: not a change
    }
    live.until(1720);
    live.host.touch(2, ControllerHost::Up, bx, by, 0, 0);
    live.host.touch(1, ControllerHost::Up, ax + 5, ay + 18, 0, 0);
    live.until(1800);
    live.host.touch(3, ControllerHost::Down, ax, ay, 0, 0.7f);
    for (int k = 1; k <= 30; k++) { live.until(1800 + 10 * k); live.host.touch(3, ControllerHost::Move, ax + (bx - ax) * k / 30, ay + (by - ay) * k / 30, 0, 0.7f); }
    live.until(2200);
    live.host.touch(3, ControllerHost::Up, bx, by, 0, 0);
    live.until(2400);
    Take take = rec.stop();
    live.keep = false;
    take.name = "Recorded";
    CHECK(take.touches.size() == 96 && take.touches[0].t == 0 && take.touches[0].phase == Down,
          "every touch was kept (%zu), the take starting at the first finger", take.touches.size());
    int ons = 0;
    for (const Note& n : take.notes) ons += n.kind == On;
    CHECK(ons >= 3 && take.notes.size() > 20, "and the notes the Grid played: %d started, %zu in all", ons, take.notes.size());
    CHECK(take.sides.size() == 1 && take.sides[0].controller == "patrickreynolds/grid" && take.sides[0].width == 800 &&
          take.sides[0].sounds.size() == 1 && take.sides[0].settings.size() == grid->manifest["settings"].size(),
          "with its set-up: the controller, every setting, the sound, the surface");

    std::string text = write(take);
    Take back;
    std::string err;
    CHECK(parse(text, back, &err), "it saves and loads (%zu bytes)", text.size());

    // Played: the same MIDI, at the same times.
    double clock = 5000;
    std::vector<Midi> out;
    ControllerHost enc([&](const uint8_t* bb, int n) { out.push_back({clock, std::vector<uint8_t>(bb, bb + n)}); });
    enc.setClock([&] { return clock; });
    Player p;
    p.load(back);
    auto sink = [&](const Note& n) { enc.injectNote(n.kind, ControllerHost::kInjectedVoice + n.voice, n.part, n.pitch, n.pressure, n.slide, n.velocity); };
    p.play(Player::Listen, clock, sink);
    while (p.playing() && clock < 20000) { clock += 0.25; p.update(clock, sink); }
    bool bytes = out.size() == live.midi.size();
    double worst = 0;
    double a0 = live.midi.empty() ? 0 : live.midi[0].t, b0 = out.empty() ? 0 : out[0].t;
    if (getenv("TAKE_DEBUG"))
        for (size_t i = 0; i < std::max(out.size(), live.midi.size()); i++) {
            auto show = [](const std::vector<Midi>& v, size_t i, double t0) {
                if (i >= v.size()) return std::string("-");
                char b[64];
                std::snprintf(b, sizeof b, "%8.3f %02x %02x %02x", v[i].t - t0, v[i].b[0], v[i].b.size() > 1 ? v[i].b[1] : 0, v[i].b.size() > 2 ? v[i].b[2] : 0);
                return std::string(b);
            };
            std::printf("%4zu  %s | %s\n", i, show(live.midi, i, a0).c_str(), show(out, i, b0).c_str());
        }
    for (size_t i = 0; bytes && i < out.size(); i++) {
        bytes = out[i].b == live.midi[i].b;
        if (!bytes) std::printf("  differs at %zu: %02x %02x / %02x %02x\n", i, out[i].b[0], out[i].b.size() > 1 ? out[i].b[1] : 0,
                                live.midi[i].b[0], live.midi[i].b.size() > 1 ? live.midi[i].b[1] : 0);
        worst = std::max(worst, std::fabs((out[i].t - b0) - (live.midi[i].t - a0)));
    }
    CHECK(bytes, "played back, the take sends the very MIDI the Grid sent (%zu messages, %zu played)", live.midi.size(), out.size());
    CHECK(worst <= 0.5, "at the same times (worst %.3f ms)", worst);

    // Its touches, given to the Grid again, make the same notes.
    Live again(*grid);
    std::vector<Note> notes;
    std::map<int, std::vector<float>> shape;    // a change to nothing new is not kept, as the recorder does
    ControllerHost::Taps taps;
    taps.note = [&](int kind, int voice, int part, float pitch, float pressure, float slide, float velocity, double t) {
        // A change or a volume to nothing new is not kept, as the recorder does.
        int key = voice * 2 + (kind == Volume);
        std::vector<float> now{pitch, pressure, slide};
        if ((kind == Change || kind == Volume) && shape.count(key) && shape[key] == now) return;
        if (kind == Off) { shape.erase(voice * 2); shape.erase(voice * 2 + 1); }
        else shape[key] = now;
        notes.push_back({t, 0, voice, kind, part, pitch, pressure, slide, velocity});
    };
    again.host.setTaps(taps);
    for (const Touch& e : back.touches) {
        again.until(1100 + e.t);
        again.host.touch(e.finger, ControllerHost::Phase(e.phase), e.x * 800, e.y * 500, e.size, e.force);
    }
    again.until(2400);
    bool sameNotes = notes.size() == back.notes.size();
    if (getenv("TAKE_DEBUG"))
        for (size_t i = 0; i < std::max(notes.size(), back.notes.size()) && i < 60; i++) {
            auto show = [](const std::vector<Note>& v, size_t i, double t0) {
                if (i >= v.size()) return std::string("-");
                char b[96];
                std::snprintf(b, sizeof b, "%8.3f v%d k%d %.3f %.3f %.3f", v[i].t - t0, v[i].voice, v[i].kind, v[i].pitch, v[i].pressure, v[i].slide);
                return std::string(b);
            };
            std::printf("%4zu  %s | %s\n", i, show(notes, i, 1100).c_str(), show(back.notes, i, 0).c_str());
        }
    for (size_t i = 0; sameNotes && i < notes.size(); i++) {
        const Note &x = notes[i], &y = back.notes[i];
        sameNotes = x.kind == y.kind && std::fabs(x.pitch - y.pitch) < 1e-3f && std::fabs(x.pressure - y.pressure) < 1e-3f &&
                    std::fabs(x.slide - y.slide) < 1e-3f && std::fabs((x.t - 1100) - y.t) < 0.01;
    }
    CHECK(sameNotes, "its touches played on the Grid again make the same notes (%zu, %zu)", notes.size(), back.notes.size());

    // Twice as fast: done in half the time. Played along: no notes, but ghosts.
    clock = 0;
    out.clear();
    p.setSpeed(2);
    p.play(Player::Listen, clock, sink);
    while (p.playing() && clock < 20000) { clock += 1; p.update(clock, sink); }
    CHECK(std::fabs(clock - (back.length() + 300) / 2) < 3, "at speed 2 it takes half as long (%.0f ms for %.0f)", clock, back.length());
    p.setSpeed(1);
    out.clear();
    clock = 0;
    p.play(Player::PlayAlong, clock, sink);
    clock = 300;
    p.update(clock, sink);
    std::vector<uint8_t> ghosts = p.drawGhosts(0, 800, 500, GhostStyle());
    CHECK(out.empty() && ghosts.size() > 100, "playing along, it plays nothing but draws the fingers (%zu bytes)", ghosts.size());
    CHECK(p.drawGhosts(1, 800, 500, GhostStyle()).size() < 40, "and nothing for fingers on another side but the progress line");
    p.stop(sink);
    CHECK(p.drawGhosts(0, 800, 500, GhostStyle()).empty(), "stopped, no ghosts");
}

static void scores() {
    Score sc;
    std::vector<std::string> errors, warnings;
    const char* s =
        "tempo 120\n"
        "meter 3/4\n"
        "key D4 dorian\n"
        "section A part\n"
        "bar 1\n"
        "^1:1 ^3:1 ^7:1    # D4 F4 C5\n"
        "bar 2\n"
        "^0:1/2 ^-1:1/2 ^8:2(vib 0.4 6, swell 0.3 0.9)\n"
        "say Lift between, softly\n"
        "bar 3\n"
        "F#4+A4:1 r:1 Bb3:1/3 C4:1/3 D4:1/3\n"
        "bar 4\n"
        "C4:2\n"
        "bar 5\n";
    bool ok = parseScore(s, sc, &errors, &warnings);
    CHECK(ok && errors.empty(), "a score parses (%s)", errors.empty() ? "" : errors[0].c_str());
    std::vector<float> want = {62, 65, 72, 60, 59, 74, 66, 69, 58, 60, 62, 60};
    std::vector<float> got;
    for (const auto& n : sc.notes) for (float p : n.pitches) got.push_back(p);
    CHECK(got == want, "names and degrees of D dorian are the right notes (^0 below the tonic, ^8 above)");
    CHECK(sc.notes.size() == 11 && std::fabs(sc.notes[4].beat - 3.5) < 1e-9 && sc.notes[5].gestures.size() == 2,
          "beats add up across bars of 3/4, gestures kept");
    CHECK(std::fabs(sc.notes[8].beat + sc.notes[8].beats - (6 + 2 + 2.0 / 3)) < 1e-9, "triplets are thirds of a beat");
    CHECK(warnings.size() == 1 && warnings[0].find("bar 5") != std::string::npos && warnings[0].find("short") != std::string::npos,
          "a bar that doesn't add up is a warning: %s", warnings.empty() ? "(none)" : warnings[0].c_str());
    CHECK(sc.sections.size() == 1 && sc.says.size() == 1 && std::fabs(sc.ms(3) - 1500) < 1e-9, "sections, words; 3 beats at 120 are 1.5 s");

    Score bad;
    errors.clear();
    CHECK(!parseScore("tempo 90\nC4:1 H4:1\nD4:x\nE4:1(wobble 2)\n^3:1\n", bad, &errors, nullptr, 10) && errors.size() == 4,
          "mistakes are found, each with its line (%zu)", errors.size());
    for (const auto& e : errors) std::printf("      %s\n", e.c_str());
    CHECK(!errors.empty() && errors[0].rfind("line 11:", 0) == 0, "line numbers count from where the score starts in the file");

    Score tp;
    parseScore("tempo 60\nC4:2\ntempo 120\nD4:2\nE4:1\n", tp, &errors, nullptr);
    CHECK(std::fabs(tp.ms(4) - 3000) < 1e-9 && std::fabs(tp.ms(5) - 3500) < 1e-9, "a tempo change in the middle is followed");
    Score pick;
    parseScore("meter 3/4\nbar 0 3\nA3:1\nbar 1\nD4:3\n", pick, &errors, nullptr);
    CHECK(pick.notes.size() == 2 && pick.notes[0].beat == 0 && pick.notes[1].beat == 1, "a pick-up (bar 0) starts the piece");
    CHECK(noteNumber("C4") == 60 && noteNumber("Bb3") == 58 && noteNumber("F#5") == 78 && noteNumber("H2") == -1 && noteName(61.25f) == "C#4+0.25",
          "note names both ways");
}

// Notes a take played: the pitch each started at, and how far each went while held.
struct Heard { float start, low, high; double t; };
static std::vector<Heard> heard(const Take& t) {
    std::vector<Heard> out;
    std::map<int, size_t> open;
    for (const Note& n : t.notes) {
        if (n.kind == On) { open[n.voice] = out.size(); out.push_back({n.pitch, n.pitch, n.pitch, n.t}); }
        else if (n.kind == Change && open.count(n.voice)) { Heard& h = out[open[n.voice]]; h.low = std::min(h.low, n.pitch); h.high = std::max(h.high, n.pitch); }
        else if (n.kind == Off) open.erase(n.voice);
    }
    return out;
}

static void authored() {
    auto grid = controller("grid");
    auto pad = controller("touchpad");
    if (!grid || !pad) return;
    const char* text =
        "odo-take/1\n"
        "name: A test tune\n"
        "tempo: 120\n"
        "settings: {\"chromatic\": 1, \"slide\": 1}\n"
        "\n"
        "score\n"
        "section Up\n"
        "C4:1 D4:1 E4:1 F4:1\n"
        "section Long\n"
        "say wiggle slowly\n"
        "G4:4(vib 0.5 5 0.1)\n"
        "C4:1(slide) E4:1(slide) G4:2\n"
        "C4+E4+G4:2\n"
        "end\n";
    for (auto* pkg : {grid.get(), pad.get()}) {
        Take t;
        std::string err;
        std::vector<std::string> warnings;
        // The touch pad as it starts plays the eight notes of a scale, F3 to F4: A3 for G4 there.
        std::string src = text;
        const bool isPad = pkg == pad.get();
        if (isPad) {
            for (size_t at; (at = src.find("G4")) != std::string::npos;) src.replace(at, 2, "A3");
            src.erase(src.find("settings:"), src.find('\n', src.find("settings:")) - src.find("settings:") + 1);
        }
        CHECK(parse(src, t, &err, &warnings) && warnings.empty() && !t.performed(), "%s: the score reads (%s)", pkg->name().c_str(), err.c_str());
        Report rep;
        float c[kCenters] = {60, 72, 72, 72};
        bool ok = prepare(t, *pkg, {}, 900, 500, c, &rep);
        for (const auto& e : rep.errors) std::printf("      %s\n", e.c_str());
        CHECK(ok && rep.errors.empty() && t.performed(), "%s: it expands and plays (%zu touches, %zu notes)", pkg->name().c_str(),
              t.touches.size(), t.notes.size());
        std::vector<float> want = {60, 62, 64, 65, isPad ? 57.f : 67.f};
        std::vector<Heard> h = heard(t);
        bool right = h.size() >= 9;
        for (size_t i = 0; right && i < want.size(); i++) right = std::fabs(h[i].start - want[i]) < 0.02f;
        CHECK(right, "%s: each note starts as written (%zu heard)", pkg->name().c_str(), h.size());
        int found = 0;
        for (const auto& p : rep.played) found += p.found && std::fabs(p.pitch - p.wanted) < 0.02f;
        if (getenv("TAKE_DEBUG"))
            for (const auto& p : rep.played)
                std::printf("      line %d wanted %.2f heard %.2f (%.2f..%.2f)%s at %.0f\n", p.line, p.wanted, p.pitch, p.low, p.high,
                            p.found ? "" : " NOT FOUND", p.t);
        CHECK(found == int(rep.played.size()) && found == 11, "%s: the report finds every note (%d of %zu)", pkg->name().c_str(), found, rep.played.size());
        CHECK(h.size() > 4 && h[4].high - h[4].low > 0.1f, "%s: the vibrato wiggles the pitch (%.2f to %.2f)", pkg->name().c_str(),
              h.size() > 4 ? h[4].low : 0, h.size() > 4 ? h[4].high : 0);
        // The slide: one finger from C4 to E4 to G4, never lifted.
        int downs = 0;
        double from = 4000, to = 6000;   // ms: beats 8 to 12
        for (const Touch& e : t.touches) downs += e.phase == Down && e.t >= from - 1 && e.t < to - 30;
        CHECK(downs == 1, "%s: the slide is one finger down, not three (%d)", pkg->name().c_str(), downs);
        CHECK(t.sections.size() == 2 && t.sections[1].t == 2000 && t.says.size() == 1 && t.says[0].t == 2000, "%s: sections and words are timed",
              pkg->name().c_str());
        // Saved, it is a performed take like any other, and loads.
        Take back;
        CHECK(parse(write(t), back, &err) && back.performed() && back.notes.size() == t.notes.size() && !back.score.empty(),
              "%s: expanded, it saves as a performed take, keeping its score", pkg->name().c_str());
    }
    // A note the controller as set doesn't have.
    Take t;
    std::string err;
    parse("odo-take/1\ntempo: 100\nsettings: {\"chromatic\": 0, \"scale\": 0, \"key\": 0}\nscore\nC4:1 C#4:1 D4:1\nend\n", t, &err);
    Report rep;
    float c[kCenters] = {60, 72, 72, 72};
    prepare(t, *grid, {}, 900, 500, c, &rep);
    bool told = false;
    for (const auto& e : rep.errors) told |= e.find("C#4") != std::string::npos && e.find("line 5") != std::string::npos;
    CHECK(told || rep.errors.empty(), "a note not on the controller as set is an error, with its line (%s)",
          rep.errors.empty() ? "the Grid had it: chromatic" : rep.errors[0].c_str());
}

static void curves() {
    auto pad = controller("touchpad");
    if (!pad) return;
    // On the touch pad, chromatic and wide: a pitch curve down two and up one, and a pressure curve.
    const char* text =
        "odo-take/1.1\n"
        "tempo: 60\n"
        "controller: patrickreynolds/touchpad\n"
        "settings: {\"scale\": 0, \"size\": 24}\n"
        "score\n"
        "G4:4(pitch 0 0.5 1 0.5 0 -1 -2 0, press 0.3 0.9 0.5 0.6)\n"
        "end\n";
    Take t;
    std::string err;
    CHECK(parse(text, t, &err) && !t.performed(), "a score with curves reads (%s)", err.c_str());
    Report rep;
    float c[kCenters] = {60, 60, 60, 60};
    CHECK(prepare(t, *pad, {}, 1000, 560, c, &rep) && rep.errors.empty(), "it plays on the touch pad (%s)",
          rep.errors.empty() ? "" : rep.errors[0].c_str());
    std::vector<Heard> h = heard(t);
    CHECK(h.size() == 1, "one note, the finger never lifted (%zu)", h.size());
    if (!h.empty()) CHECK(h[0].low < 65.3f && h[0].low > 64.7f && h[0].high > 67.7f && h[0].high < 68.3f,
                          "the pitch follows the curve, F4 to G#4 (%.2f to %.2f)", h[0].low, h[0].high);
    float lo = 1, hi = 0;
    for (const Note& n : t.notes) if (n.kind != Off) { lo = std::min(lo, n.pressure); hi = std::max(hi, n.pressure); }
    CHECK(lo < 0.4f && hi > 0.8f, "the pressure follows its curve (%.2f to %.2f)", lo, hi);
    // vel: how hard the finger lands (the note's velocity), apart from how it is held.
    {
        Take v;
        std::string e2;
        CHECK(parse("odo-take/1.1\ntempo: 60\ncontroller: patrickreynolds/touchpad\nsettings: {\"scale\": 0, \"size\": 24}\n"
                    "score\nG4:2(vel 0.2, press 0.8)\nend\n", v, &e2), "a score with vel reads (%s)", e2.c_str());
        Report r2;
        CHECK(prepare(v, *pad, {}, 1000, 560, c, &r2) && r2.errors.empty(), "it plays");
        float velocity = -1, held = 0;
        for (const Note& n : v.notes) { if (n.kind == On && velocity < 0) velocity = n.velocity; if (n.kind == Change) held = n.pressure; }
        CHECK(velocity > 0.1f && velocity < 0.3f && held > 0.7f, "vel 0.2: struck softly (%.2f), then held at its pressure (%.2f)", velocity, held);
    }
    // Bad curves are said so.
    Take bad;
    std::vector<std::string> errors;
    Score sc;
    CHECK(!parseScore("C4:1(pitch 0 30)\n", sc, &errors, nullptr) && !errors.empty(), "a pitch curve past two octaves is an error");
}

static void relaid() {
    auto hex = controller("hex");
    if (!hex) return;
    const char* text =
        "odo-take/1.1\n"
        "tempo: 100\n"
        "controller: patrickreynolds/hex\n"
        "settings: {\"chromatic\": 1, \"cols\": 6}\n"
        "score\n"
        "C4:1 E4:1 G4:1 C5:1 A3+E4:2 D4:1(press 0.4 0.9) G3:1\n"
        "end\n";
    const Json& st = hex->manifest["settings"];
    auto valuesWith = [&](double cols) {
        std::vector<double> v;
        for (size_t i = 0; i < st.size(); i++) {
            std::string id = st[i]["id"].str();
            v.push_back(id == "cols" ? cols : id == "chromatic" ? 1 : st[i]["default"].num(0));
        }
        return v;
    };
    for (bool fromNotes : {false, true}) {
        Take t;
        std::string err;
        parse(text, t, &err);
        float c[kCenters] = {60, 60, 60, 60};
        Report rep;
        CHECK(prepare(t, *hex, {}, 900, 500, c, &rep) && rep.errors.empty(), "relayout: it plays on the Hex at 900x500");
        if (fromNotes) t.score.clear();                 // as a take that was played, not written
        std::vector<double> v = valuesWith(9);
        CHECK(laidOutFor(t, hex->manifest, valuesWith(6), 900, 500) && !laidOutFor(t, hex->manifest, v, 900, 500) &&
              !laidOutFor(t, hex->manifest, valuesWith(6), 1300, 500), "relayout: a change of size or of a setting is seen");
        Take moved = t;
        CHECK(relayout(moved, *hex, v, 1300, 560, c), "relayout: laid out again at 1300x560 with 9 keys a column");
        // Every finger down lands on a key playing its note.
        mpe::take::Layout lay;
        lay.scan(*hex, v, 1300, 560, c);
        int downs = 0, right = 0;
        std::vector<float> want;
        for (const Note& n : moved.notes) if (n.kind == On) want.push_back(std::round(n.pitch));
        std::multiset<float> wanted(want.begin(), want.end());
        for (const Touch& e : moved.touches) {
            if (e.phase != Down) continue;
            downs++;
            Note got;
            if (lay.probe(e.x * 1300, e.y * 560, 0.6f, &got) && wanted.count(std::round(got.pitch))) right++;
        }
        CHECK(downs == int(want.size()) && right == downs, "relayout (%s): each finger lands on its note's key (%d of %d)",
              fromNotes ? "from its notes" : "from its score", right, downs);
        CHECK(moved.notes.size() == t.notes.size(), "relayout: what it plays is unchanged");
    }
}

// The Hex keyboard moved, sized and turned (its view settings): a take written for it still plays
// every note as written, its bends too (two fingers out of the key, then along its row, which is
// turned with it: the manifest's "rowTurn"); and one laid out unmoved follows the keys when they move.
static void hexMoved() {
    auto hex = controller("hex");
    if (!hex) return;
    const char* text =
        "odo-take/1.1\n"
        "tempo: 90\n"
        "controller: patrickreynolds/hex\n"
        "settings: {\"chromatic\": 1}\n"
        "score\n"
        "C4:2(pitch 0 0.5 -0.5 0) E4:2(bend +2 0.2 0.8) G4:2(vib 0.3 5 0.3) D4:1(press 0.3 0.9) A3+E4:2 F4:2(bend -1 0.3 0.7)\n"
        "end\n";
    const Json& st = hex->manifest["settings"];
    struct View { double x, y, s, a; };
    auto values = [&](const View& w) {
        std::vector<double> v;
        for (size_t i = 0; i < st.size(); i++) {
            std::string id = st[i]["id"].str();
            v.push_back(id == "viewX" ? w.x : id == "viewY" ? w.y : id == "viewScale" ? w.s : id == "viewTurn" ? w.a : st[i]["default"].num(0));
        }
        return v;
    };
    const View views[] = {{0, 0, 1, 0}, {0, 0, 1, 90}, {1.5, -2, 0.8, 37}, {-1, 0.5, 1.2, 210}, {0.5, 1, 1.0, 330}, {0, 0, 0.6, 120}};
    float c[kCenters] = {60, 60, 60, 60};
    for (const View& w : views) {
        Take t;
        std::string err;
        parse(text, t, &err);
        Report rep;
        bool ok = prepare(t, *hex, values(w), 1000, 560, c, &rep);
        int wrong = 0;
        for (const auto& p : rep.played) wrong += !p.asWritten();
        // The bends: C4 up and down half a semitone, E4 up to F#4, F4 down to E4 (within a tenth of a
        // semitone: a key's middle is found by touching it all over, a few dp apart).
        float cLow = 0, cHigh = 0, eHigh = 0, fLow = 0;
        for (const auto& p : rep.played) {
            if (p.wanted == 60) { cLow = p.low; cHigh = p.high; }
            if (p.wanted == 64 && p.high > eHigh) eHigh = p.high;
            if (p.wanted == 65) fLow = p.low;
        }
        CHECK(ok && rep.errors.empty() && wrong == 0 && rep.played.size() == 7,
              "the Hex moved (%.1f, %.1f), sized %.1f, turned %.0f°: all %zu notes as written (%d not)", w.x, w.y, w.s, w.a, rep.played.size(), wrong);
        CHECK(std::fabs(cLow - 59.5f) < 0.1f && std::fabs(cHigh - 60.5f) < 0.1f && std::fabs(eHigh - 66) < 0.1f && std::fabs(fLow - 64) < 0.1f,
              "  turned %.0f°: its bends as written (C4 %.2f..%.2f, E4 up to %.2f, F4 down to %.2f)", w.a, cLow, cHigh, eHigh, fLow);
    }
    // Laid out unmoved, then the keyboard moved: its fingers follow (relayout), each on its note's key.
    Take t;
    std::string err;
    parse(text, t, &err);
    Report rep;
    prepare(t, *hex, values(views[0]), 1000, 560, c, &rep);
    for (const View& w : {views[2], views[3]}) {
        std::vector<double> v = values(w);
        CHECK(laidOutFor(t, hex->manifest, values(views[0]), 1000, 560) && !laidOutFor(t, hex->manifest, v, 1000, 560),
              "moving the keyboard is seen as a change of its settings");
        Take moved = t;
        CHECK(relayout(moved, *hex, v, 1000, 560, c), "relayout: laid out again on the keyboard turned %.0f°", w.a);
        mpe::take::Layout lay;
        lay.scan(*hex, v, 1000, 560, c);
        int downs = 0, right = 0;
        std::multiset<float> wanted;
        for (const Note& n : moved.notes) if (n.kind == On) wanted.insert(std::round(n.pitch));
        for (const Touch& e : moved.touches) {
            if (e.phase != Down) continue;
            downs++;
            Note got;
            if (lay.probe(e.x * 1000, e.y * 560, 0.6f, &got) && wanted.count(std::round(got.pitch))) right++;
        }
        CHECK(downs > 0 && right == downs, "  turned %.0f°: each finger lands on a key of its notes (%d of %d)", w.a, right, downs);
    }
}

static void examples() {
    auto grid = controller("grid");
    if (!grid) return;
    std::string dir = std::string(MPE_TEST_TAKES);
    DIR* d = opendir(dir.c_str());
    int n = 0;
    if (!d) { CHECK(false, "takes/ is there (%s)", dir.c_str()); return; }
    while (dirent* e = readdir(d)) {
        std::string f = e->d_name;
        if (f.size() < 8 || f.compare(f.size() - 8, 8, kExtension) != 0) continue;
        std::ifstream in(dir + "/" + f);
        std::stringstream ss;
        ss << in.rdbuf();
        Take t;
        std::string err;
        std::vector<std::string> warnings;
        bool ok = parse(ss.str(), t, &err, &warnings);
        Report rep;
        // Each part's middle from its sound, as odo lays a take out (60 and 72 without them).
        float c[kCenters] = {60, 72, 72, 72};
        if (!t.sides.empty())
            for (size_t j = 0; j < size_t(kCenters) && j < t.sides[0].sounds.size(); j++) {
                const std::string& id = t.sides[0].sounds[j];
                std::string perr;
                auto snd = Package::read(std::string(MPE_TEST_PACKAGES) + "/" + id.substr(id.find('/') + 1) + ".odoi", &perr);
                if (!snd) continue;
                double lo = snd->manifest["range"][size_t(0)].num(), hi = snd->manifest["range"][size_t(1)].num();
                if (lo > 0 && hi > 0) c[j] = float(69 + 12 * std::log2(std::sqrt(lo * hi) / 440));
            }
        // Parts past the sounds named play the last one.
        if (!t.sides.empty() && !t.sides[0].sounds.empty())
            for (size_t j = t.sides[0].sounds.size(); j < size_t(kCenters); j++) c[j] = c[j - 1];
        auto pkg = controller(t.sides.empty() || t.sides[0].controller.empty() ? "grid" : t.sides[0].controller.substr(t.sides[0].controller.find('/') + 1));
        ok = ok && pkg && prepare(t, *pkg, {}, 1000, 560, c, &rep);
        int wrong = 0;
        for (const auto& p : rep.played) wrong += !p.asWritten(0.02f);
        for (const auto& w : warnings) std::printf("      %s\n", w.c_str());
        for (const auto& w : rep.warnings) std::printf("      %s\n", w.c_str());
        for (const auto& w : rep.errors) std::printf("      %s\n", w.c_str());
        CHECK(ok && rep.errors.empty() && rep.warnings.empty() && warnings.empty() && wrong == 0,
              "takes/%s checks: %zu notes, every one as written (%d not)", f.c_str(), rep.played.size(), wrong);
        n++;
    }
    closedir(d);
    CHECK(n >= 2, "there are example takes (%d)", n);
}

// A section looped: it comes round again and again, its notes let go of at each turn.
static void looping() {
    auto grid = controller("grid");
    if (!grid) return;
    std::ifstream in(std::string(MPE_TEST_TAKES) + "/ode-to-joy.odotake");
    std::stringstream ss;
    ss << in.rdbuf();
    Take t;
    float c[kCenters] = {60, 72, 72, 72};
    if (!parse(ss.str(), t) || !prepare(t, *grid, {}, 900, 500, c)) { CHECK(false, "ode-to-joy.odotake prepares"); return; }
    Player p;
    p.load(t);
    std::map<int, int> on;               // voice -> notes started and not let go of
    int starts = 0;
    auto sink = [&](const Note& n) { if (n.kind == On) { on[n.voice]++; starts++; } else if (n.kind == Off) on[n.voice]--; };
    p.loopSection(1, 0, sink);
    double now = 0;
    p.play(Player::Listen, now, sink);
    const double from = t.sections[1].t, to = t.sections[2].t;
    bool inside = true;
    int turns = 0;
    double last = p.position();
    while (now < 2.5 * (to - from)) {
        now += 5;
        p.update(now, sink);
        inside &= p.position() >= from && p.position() < to;
        if (p.position() < last) {
            turns++;
            bool quiet = true;
            for (const auto& kv : on) quiet &= kv.second == 0;
            CHECK(quiet, "at turn %d of the loop every note has been let go of", turns);
        }
        last = p.position();
    }
    CHECK(inside && turns == 2 && p.playing(), "section 2 plays round and round (%d turns), never outside it", turns);
    CHECK(starts > 2 * 14 && p.sectionAt(p.position()) == 1, "its notes play each time (%d started)", starts);
    p.loopSection(-1, now, sink);
    p.stop(sink);
    bool quiet = true;
    for (const auto& kv : on) quiet &= kv.second == 0;
    CHECK(quiet && !p.playing(), "stopped, nothing sounds");
}

// A take keeps a whole preset (1.1): recorded with what plays now, it saves and loads the same;
// a take of before 1.1 still loads, without one; a score may give only what it needs.
static void presets() {
    preset::Preset set;
    set.name = "Piano in a hall";
    set.split = 2;
    set.layered = true;
    set.hasEffects = true;
    set.effects.bpm = 72; set.effects.reverbSize = 0.83; set.effects.reverbDamp = 0.25;
    set.effects.delayBeats = 0.5; set.effects.delayFeedback = 0.2; set.effects.delayPingPong = false;
    for (int k = 0; k < 2; k++) {
        preset::Side sd;
        sd.controller = "patrickreynolds/grid";
        sd.settings.type = Json::Object;
        preset::Part q;
        q.sound = k ? "patrickreynolds/cello" : "patrickreynolds/piano";
        q.version = "1.0.0";
        q.full = true;
        q.transpose = -12; q.volume = 0.7; q.brightness = 0.8; q.drive = 0.1; q.pressureCurve = 1.5; q.reverb = 0.45;
        q.delay = 0.05; q.release = 1.25;
        q.arp.on = true; q.arp.order = 2; q.arp.rate = 3; q.arp.gate = 0.4; q.arp.octaves = 2; q.arp.latch = true;
        sd.parts.push_back(q);
        set.sides.push_back(sd);
    }
    auto grid = controller("grid");
    Take take;
    if (grid) {
        Live live(*grid);
        Live right(*grid);
        Recorder rec;
        rec.start(&set, 1.35);
        rec.attach(live.host, 0, {"patrickreynolds/piano"});
        rec.attach(right.host, 1, {"patrickreynolds/cello"});
        live.until(1100);
        live.host.touch(1, ControllerHost::Down, 300, 250, 0, 0.6f);
        live.until(1300);
        live.host.touch(1, ControllerHost::Up, 300, 250, 0, 0);
        live.until(1400);
        take = rec.stop();
        take.name = "With a preset";
    }
    CHECK(take.hasPreset && take.volume == 1.35, "a recording keeps what plays now as its preset, and the volume");
    std::string text = write(take);
    CHECK(text.rfind("odo-take/1.1\n", 0) == 0 && text.find("\npreset: {") != std::string::npos && text.find("\nvolume: 1.35\n") != std::string::npos,
          "it is written as odo-take/1.1, with a preset line and a volume line");
    Take back;
    std::string err;
    std::vector<std::string> warnings;
    CHECK(parse(text, back, &err, &warnings) && warnings.empty(), "it reads back (%s, %zu warnings)", err.c_str(), warnings.size());
    CHECK(write(back) == text, "and writes out the same text");
    preset::Preset a = presetOf(take), b = presetOf(back);
    a.madeBy = b.madeBy = "";
    CHECK(preset::write(a) == preset::write(b), "the preset saved is the preset loaded");
    bool same = b.split == 2 && b.layered && b.hasEffects && b.effects.reverbSize == 0.83 && b.effects.bpm == 72 && !b.effects.delayPingPong &&
                b.sides.size() == 2 && b.sides[1].parts.size() == 1;
    if (same) {
        const preset::Part& q = b.sides[1].parts[0];
        same = q.full && q.sound == "patrickreynolds/cello" && q.version == "1.0.0" && q.transpose == -12 && q.volume == 0.7 &&
               q.brightness == 0.8 && q.drive == 0.1 && q.pressureCurve == 1.5 && q.reverb == 0.45 && q.delay == 0.05 &&
               q.release == 1.25 && q.arp.on && q.arp.order == 2 && q.arp.rate == 3 && q.arp.gate == 0.4 && q.arp.octaves == 2 && q.arp.latch;
    }
    CHECK(same, "with everything: the split, layering, the reverb and delay, each part's sound, version, effects, level and arp");
    CHECK(b.name == "With a preset" && b.sides[0].controller == "patrickreynolds/grid" && grid &&
          b.sides[0].settings.size() == grid->manifest["settings"].size(), "its controller and all its settings come from the take's own lines");
    // Nothing twice: the preset line leaves out each side's controller and settings.
    size_t at = text.find("\npreset: ");
    std::string line = text.substr(at, text.find('\n', at + 1) - at);
    CHECK(line.find("\"settings\"") == std::string::npos && line.find("\"controller\"") == std::string::npos && line.find("\"name\"") == std::string::npos,
          "the preset line leaves out what the take's own lines say");

    // Before 1.1: no preset; its set-up is the controller, settings and sound it names, as before.
    Take old;
    CHECK(parse("odo-take/1\nname: Old\ncontroller: patrickreynolds/grid 2.8.0\nsettings: {\"slide\": 0}\nsound: patrickreynolds/cello\n"
                "t 0 1 down 0.5 0.5 0.5\nn 0 1 on 0 60 0.5 0.5 0.5\nt 100 1 up 0.5 0.5 0\nn 100 1 off\n", old, &err),
          "a take of odo-take/1 still loads (%s)", err.c_str());
    preset::Preset op = presetOf(old);
    CHECK(!old.hasPreset && old.volume < 0 && op.sides.size() == 1 && op.sides[0].controller == "patrickreynolds/grid" &&
          op.sides[0].settings.size() == 1 && op.sides[0].parts.size() == 1 && !op.sides[0].parts[0].full && !op.hasEffects,
          "without a preset: its preset is its controller, the settings it names and its sound (with the sound's own effects)");

    // Written by hand: only the effects and a part's, the rest from its own lines.
    Take hand;
    warnings.clear();
    CHECK(parse("odo-take/1.1\nname: Hand\ncontroller: patrickreynolds/hex\nsettings: {\"layout\": 0}\nsound: patrickreynolds/piano\n"
                "preset: {\"effects\": {\"reverbSize\": 0.9}, \"sides\": [{\"parts\": [{\"sound\": \"patrickreynolds/piano\", \"reverb\": 0.5, \"release\": 2}]}]}\n"
                "score\ntempo 60\nC4:1\nend\n", hand, &err, &warnings) && warnings.empty(), "a score may give only part of a preset (%s)", err.c_str());
    preset::Preset hp = presetOf(hand);
    CHECK(hand.hasPreset && hp.hasEffects && hp.effects.reverbSize == 0.9 && hp.effects.bpm == preset::Effects().bpm &&
          hp.sides[0].controller == "patrickreynolds/hex" && hp.sides[0].parts[0].full && hp.sides[0].parts[0].reverb == 0.5 &&
          hp.sides[0].parts[0].release == 2 && hp.sides[0].parts[0].volume == 1, "what it leaves out takes a preset's defaults");
    warnings.clear();
    parse("odo-take/1.1\nname: Bad\npreset: [1]\nvolume: 9\nscore\nC4:1\nend\n", hand, &err, &warnings);
    CHECK(warnings.size() == 2, "a preset that is not one, or a volume out of range, is left out with a warning (%zu)", warnings.size());
}

int main() {
    relaid();
    hexMoved();
    curves();
    fileRoundTrip();
    recordAndPlay();
    scores();
    authored();
    examples();
    looping();
    presets();
    return finish();
}
