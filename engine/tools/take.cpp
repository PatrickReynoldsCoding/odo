// odo-take: checks a take (.odotake) and expands a score into the touches odo would play, on a
// controller package, as odo does: docs/formats/TAKE_FORMAT.md and TAKE_AUTHORING.md. Run it
// through tools/take, which builds it.
//
//   odo-take check FILE...          read it, expand its score, play it through the controller,
//                                   and say whether every note sounds as written (exit 1 if not)
//   odo-take expand FILE [-o OUT]   the take with its touches and notes (a performed take), to OUT or stdout
//   odo-take notes FILE             the notes it plays, one a line: time, bar and beat, name, bends
//   odo-take touches FILE           its touches, as the file has them
// Options:
//   --controller FILE.odoc          the controller (default: the take's, from --library)
//   --library DIR                   where controllers are (default: controllers/build)
//   --size WxH                      the surface, dp (default: the take's, or 900x500)
//   --center N[,M]                  the middle of each part's instrument, a MIDI note (default 60,72)
//   --setting ID=VALUE              a setting of the controller (repeats), over the take's
//   --sounds DIR                    instrument packages (.odoi): each part's middle from its sound, as odo
//                                   does (default: third_party/mpe-instruments/build/packages, if there)
//   odo-take render FILE -o OUT.wav [--tail S]   (built with MPE_LIBFAUST) what it sounds like: its notes
//                                   played through its sounds, with its preset's effects, 48 kHz stereo
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "mpe/take.h"
#ifdef MPE_LIBFAUST
#include "mpe/controller.h"
#include "mpe/engine.h"
#include "mpe/faust_loader.h"
#endif

using namespace mpe;
using namespace mpe::take;

#ifndef ODO_SOUNDS
#define ODO_SOUNDS ""
#endif
#ifndef ODO_CONTROLLERS
#define ODO_CONTROLLERS "controllers/build"
#endif

namespace {

struct Options {
    std::string controller, library = ODO_CONTROLLERS, out, sounds = ODO_SOUNDS;
    float w = 0, h = 0, tail = 3;
    bool centersGiven = false;
    float centers[kCenters] = {60, 72, 72, 72};
    std::vector<std::pair<std::string, double>> settings;
};

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string clock(double ms) {
    char b[32];
    int s = int(ms / 1000);
    std::snprintf(b, sizeof b, "%d:%02d.%03d", s / 60, s % 60, int(std::fmod(ms, 1000)));
    return b;
}

// An instrument package by its id ("patrickreynolds/cello" -> DIR/cello.odoi); null if not there.
std::unique_ptr<Package> soundPackage(const Options& o, const std::string& id) {
    if (o.sounds.empty() || id.empty()) return nullptr;
    std::string err;
    return Package::read(o.sounds + "/" + id.substr(id.find('/') + 1) + ".odoi", &err);
}

// The middle of an instrument's range, a MIDI note: what odo tells a controller (odo for computers' centerNote).
float centerNote(const Package& p) {
    const Json& r = p.manifest["range"];
    double lo = r[size_t(0)].num(), hi = r[size_t(1)].num();
    if (lo <= 0 || hi <= 0) return 60;
    return float(69 + 12 * std::log2(std::sqrt(lo * hi) / 440));
}

// The take, read and (if written by hand) expanded on its controller. False: said why on stderr.
bool load(const std::string& path, const Options& o, Take& t, Report& rep, std::unique_ptr<Package>& pkg, bool quiet = false) {
    std::string text, err;
    if (!readFile(path, text)) { std::fprintf(stderr, "%s: cannot read it\n", path.c_str()); return false; }
    std::vector<std::string> warnings;
    if (!parse(text, t, &err, &warnings)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str()); return false; }
    for (const auto& w : warnings) rep.warnings.push_back(w);
    std::string id = t.sides.empty() ? "" : t.sides[0].controller;
    std::string file = o.controller;
    if (file.empty()) {
        std::string name = id.empty() ? "grid" : id.substr(id.find('/') + 1);
        file = o.library + "/" + name + ".odoc";
    }
    pkg = Package::read(file, &err);
    if (!pkg) { std::fprintf(stderr, "%s: the controller %s: %s\n", path.c_str(), file.c_str(), err.c_str()); return false; }
    if (!pkg->isController()) { std::fprintf(stderr, "%s: %s is not a controller package\n", path.c_str(), file.c_str()); return false; }
    // Settings from the command line go over the take's.
    if (!o.settings.empty()) {
        if (t.sides.empty()) t.sides.resize(1);
        for (const auto& kv : o.settings) {
            bool had = false;
            for (auto& s : t.sides[0].settings) if (s.first == kv.first) { s.second = kv.second; had = true; }
            if (!had) t.sides[0].settings.push_back(kv);
        }
    }
    // Each part's middle: from the command line, else from its sound (as odo does), else 60 and 72.
    float centers[kCenters] = {o.centers[0], o.centers[1], o.centers[2], o.centers[3]};
    if (!o.centersGiven && !o.sounds.empty() && !t.sides.empty())
        for (size_t j = 0; j < size_t(kCenters) && j < t.sides[0].sounds.size(); j++) {
            std::unique_ptr<Package> snd = soundPackage(o, t.sides[0].sounds[j]);
            if (snd) centers[j] = centerNote(*snd);
        }
    // Parts past the sounds named play the last one (as odo tells a controller).
    if (!o.centersGiven && !t.sides.empty() && !t.sides[0].sounds.empty())
        for (size_t j = t.sides[0].sounds.size(); j < size_t(kCenters); j++) centers[j] = centers[j - 1];
    float w = o.w, h = o.h;
    if (w <= 0 && !t.sides.empty() && t.sides[0].width > 0) { w = t.sides[0].width; h = t.sides[0].height; }
    if (w <= 0) { w = 900; h = 500; }
    if (!t.performed()) {
        bool ok = prepare(t, *pkg, {}, w, h, centers, &rep);
        if (!ok && !quiet) {
            for (const auto& e : rep.errors) std::fprintf(stderr, "%s: %s\n", path.c_str(), e.c_str());
            return false;
        }
    }
    return true;
}

std::string barBeat(const Take& t, double ms) {
    if (t.tempo <= 0) return "";
    double beat = ms / (60000.0 / t.tempo);
    double bar = t.meterBeats * 4.0 / t.meterUnit;
    char b[32];
    std::snprintf(b, sizeof b, "bar %d beat %.2f", int(beat / bar) + 1, std::fmod(beat, bar) + 1);
    return b;
}

int check(const std::vector<std::string>& files, const Options& o) {
    int bad = 0;
    for (const auto& f : files) {
        Take t;
        Report rep;
        std::unique_ptr<Package> pkg;
        bool authored = false;
        {
            std::string text;
            readFile(f, text);
            Take peek;
            authored = parse(text, peek) && !peek.performed();
        }
        if (!load(f, o, t, rep, pkg, true) && !pkg) { bad++; continue; }
        std::printf("%s: %s", f.c_str(), t.name.empty() ? "(no name)" : t.name.c_str());
        if (t.tempo > 0) std::printf(", %d/%d at %g", t.meterBeats, t.meterUnit, t.tempo);
        std::printf(", %s long\n", clock(t.length()).c_str());
        const Side* sd = t.sides.empty() ? nullptr : &t.sides[0];
        std::printf("  on %s %s at %gx%g dp: %zu touches, %zu note events, %zu sections, %zu words\n", pkg->id().c_str(),
                    pkg->version().c_str(), sd ? double(sd->width) : 0.0, sd ? double(sd->height) : 0.0, t.touches.size(),
                    t.notes.size(), t.sections.size(), t.says.size());
        for (const auto& w : rep.warnings) std::printf("  warning: %s\n", w.c_str());
        for (const auto& e : rep.errors) std::printf("  error: %s\n", e.c_str());
        int wrong = 0;
        for (const auto& p : rep.played) {
            if (p.asWritten()) continue;
            wrong++;
            if (!p.found) std::printf("  line %d: %s was not heard at all (%s)\n", p.line, noteName(p.wanted).c_str(), barBeat(t, p.t).c_str());
            else std::printf("  line %d: wanted %s, heard %s (%s): %s\n", p.line, noteName(p.wanted).c_str(), noteName(p.pitch).c_str(),
                             barBeat(t, p.t).c_str(),
                             std::fabs(p.pitch - p.wanted) < 1 ? "a slide or bend that does not get there? (a Slide setting off?)"
                                                               : "the controller as set plays another note there");
        }
        if (authored) {
            if (!wrong && rep.errors.empty()) std::printf("  all %zu notes play as written.\n", rep.played.size());
            else std::printf("  %d of %zu notes do not play as written.\n", wrong + int(rep.errors.size()), rep.played.size() + rep.errors.size());
        } else {
            std::printf("  a performed take: its notes play as they were recorded.\n");
        }
        if (wrong || !rep.errors.empty()) bad++;
    }
    return bad ? 1 : 0;
}

int expandCmd(const std::string& file, const Options& o) {
    Take t;
    Report rep;
    std::unique_ptr<Package> pkg;
    if (!load(file, o, t, rep, pkg)) return 1;
    for (const auto& w : rep.warnings) std::fprintf(stderr, "%s: warning: %s\n", file.c_str(), w.c_str());
    std::string text = write(t);
    if (o.out.empty()) { std::fwrite(text.data(), 1, text.size(), stdout); return 0; }
    std::ofstream f(o.out, std::ios::binary);
    f << text;
    if (!f) { std::fprintf(stderr, "cannot write %s\n", o.out.c_str()); return 1; }
    std::fprintf(stderr, "%s: %zu touches, %zu note events\n", o.out.c_str(), t.touches.size(), t.notes.size());
    return 0;
}

int notesCmd(const std::string& file, const Options& o) {
    Take t;
    Report rep;
    std::unique_ptr<Package> pkg;
    if (!load(file, o, t, rep, pkg)) return 1;
    // Each note: when it starts and ends, its pitch at the start, how far it bends, its pressure.
    struct Open { size_t row; };
    struct Row { double t0, t1; int side, voice; float pitch, low, high, pmin, pmax; };
    std::vector<Row> rows;
    std::map<std::pair<int, int>, size_t> open;
    for (const Note& n : t.notes) {
        std::pair<int, int> v{n.side, n.voice};
        if (n.kind == On) { open[v] = rows.size(); rows.push_back({n.t, -1, n.side, n.voice, n.pitch, n.pitch, n.pitch, n.pressure, n.pressure}); }
        else if (!open.count(v)) continue;
        else if (n.kind == Change) {
            Row& r = rows[open[v]];
            r.low = std::min(r.low, n.pitch); r.high = std::max(r.high, n.pitch);
            r.pmin = std::min(r.pmin, n.pressure); r.pmax = std::max(r.pmax, n.pressure);
        } else if (n.kind == Off) { rows[open[v]].t1 = n.t; open.erase(v); }
    }
    std::printf("%-10s %-10s %-20s %-8s %-18s %s\n", "start", "length", "where", "note", "pitch went", "pressure");
    for (const Row& r : rows) {
        char went[40] = "";
        if (r.high - r.low > 0.02f) std::snprintf(went, sizeof went, "%s..%s", noteName(r.low).c_str(), noteName(r.high).c_str());
        std::printf("%-10s %-10s %-20s %-8s %-18s %.2f..%.2f\n", clock(r.t0).c_str(), r.t1 >= 0 ? clock(r.t1 - r.t0).c_str() : "held",
                    barBeat(t, r.t0).c_str(), noteName(r.pitch).c_str(), went, r.pmin, r.pmax);
    }
    return 0;
}

int touchesCmd(const std::string& file, const Options& o) {
    Take t;
    Report rep;
    std::unique_ptr<Package> pkg;
    if (!load(file, o, t, rep, pkg)) return 1;
    Take only;
    only.touches = t.touches;
    std::string text = write(only);
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}

int usage() {
    std::fprintf(stderr,
        "odo-take check FILE...           check a take: every note as written? (exit 1 if not)\n"
        "odo-take expand FILE [-o OUT]    a take written by hand, expanded into touches and notes\n"
        "odo-take notes FILE              the notes it plays\n"
        "odo-take touches FILE            its touches\n"
        "  --controller FILE.odoc  --library DIR  --size WxH  --center N[,M]  --setting ID=VALUE\n");
    return 2;
}

}  // namespace

#ifdef MPE_LIBFAUST
// What it sounds like: each side's notes played through a controller host of its own (on its own
// channels and parts, as odo for computers routes them) into one engine with the take's sounds
// and preset, faster than real time. 48 kHz, 16-bit stereo WAV.
int renderCmd(const std::string& file, const Options& o) {
    if (o.out.empty()) { std::fprintf(stderr, "render: -o OUT.wav\n"); return 2; }
    Take t;
    Report rep;
    std::unique_ptr<Package> ctl;
    if (!load(file, o, t, rep, ctl)) return 1;
    for (const auto& e : rep.errors) std::fprintf(stderr, "%s: %s\n", file.c_str(), e.c_str());
    const int kSr = 48000;
    Engine engine(kSr);
    MidiInput in(engine);
    double clock = 0;
    int64_t nowNs = 1000000000;
    auto sink = [&](const uint8_t* b, int n) { in.feed(b, n, nowNs); };
    const preset::Preset setup = presetOf(t);
    Performance perf;
    std::vector<std::unique_ptr<ControllerHost>> hosts;
    std::vector<int> base;
    int part = 0;
    const int sides = std::max<int>(1, int(t.sides.size()));
    std::string cache = "/tmp/odo-take-render-cache";
    for (int k = 0; k < sides; k++) {
        const Side* sd = k < int(t.sides.size()) ? &t.sides[size_t(k)] : nullptr;
        auto h = std::make_unique<ControllerHost>(sink);
        h->setClock([&clock] { return clock; });
        h->setPart(part);
        int per = 15 / sides;
        h->setChannels(1 + k * per, per);
        hosts.push_back(std::move(h));
        base.push_back(part);
        size_t n = sd ? std::max<size_t>(1, sd->sounds.size()) : 1;
        for (size_t j = 0; j < n && part < kParts; j++, part++) {
            std::string id = sd && j < sd->sounds.size() ? sd->sounds[j] : "";
            std::unique_ptr<Package> snd = soundPackage(o, id);
            if (!snd) { std::fprintf(stderr, "%s: the sound %s is not in %s\n", file.c_str(), id.c_str(), o.sounds.c_str()); return 1; }
            std::string err;
            int index = loadInstrumentPackage(*snd, cache, &err);
            if (index < 0) { std::fprintf(stderr, "%s: %s: %s\n", file.c_str(), id.c_str(), err.c_str()); return 1; }
            engine.setInstrument(part, index);
            // The sound's own effects, then the preset's part over them (as odo does).
            const Json& sm = snd->manifest["sound"];
            PartConfig& c = perf.part[part];
            c.reverb = float(sm["reverb"].num(0.15));
            c.delay = float(sm["delay"].num(0));
            c.brightness = float(sm["brightness"].num(1));
            c.drive = float(sm["drive"].num(0));
            c.pressureCurve = float(sm["pressureCurve"].num(1));
            c.release = float(sm["release"].num(0));
            c.width = float(sm["width"].num(0));
            if (part == 0) { perf.reverbSize = float(sm["reverbSize"].num(0.5)); perf.reverbDamp = float(sm["reverbDamp"].num(0.4)); }
            if (size_t(k) < setup.sides.size() && j < setup.sides[size_t(k)].parts.size()) {
                const preset::Part& q = setup.sides[size_t(k)].parts[j];
                if (q.full) {
                    c.transpose = q.transpose; c.volume = float(q.volume); c.brightness = float(q.brightness);
                    c.drive = float(q.drive); c.pressureCurve = float(q.pressureCurve); c.reverb = float(q.reverb);
                    c.delay = float(q.delay); c.release = float(q.release); c.width = float(q.width);
                    c.arp = q.arp.on; c.order = ArpOrder(q.arp.order); c.rate = float(q.arp.rate); c.gate = float(q.arp.gate);
                    c.octaves = q.arp.octaves; c.latch = q.arp.latch;
                }
            }
        }
    }
    perf.layout = part > 1 ? mpe::Layout::Split : mpe::Layout::Single;
    if (setup.hasEffects) {
        perf.bpm = float(setup.effects.bpm); perf.reverbSize = float(setup.effects.reverbSize);
        perf.reverbDamp = float(setup.effects.reverbDamp); perf.delayBeats = float(setup.effects.delayBeats);
        perf.delayFeedback = float(setup.effects.delayFeedback); perf.delayPingPong = setup.effects.delayPingPong;
    }
    engine.setPerformance(perf);
    engine.setVolume(0.8f * float(t.volume >= 0 ? t.volume : 1));
    // Let the engine take its instruments and performance (they hand over at the next block).
    std::vector<float> buf(2 * 64), out;
    for (int i = 0; i < 8; i++) { engine.render(buf.data(), 64, nowNs); nowNs += 1000000000LL * 64 / kSr; engine.collectRetired(); }
    std::vector<Note> notes = t.notes;
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    const double end = t.length() + o.tail * 1000;
    size_t next = 0;
    const int block = 64;
    for (double ms = 0; ms < end; ms += 1000.0 * block / kSr) {
        clock = ms;
        for (; next < notes.size() && notes[next].t <= ms; next++) {
            const Note& n = notes[next];
            if (n.side < 0 || size_t(n.side) >= hosts.size()) continue;
            hosts[size_t(n.side)]->injectNote(n.kind, ControllerHost::kInjectedVoice + n.voice, n.part, n.pitch, n.pressure, n.slide, n.velocity);
        }
        engine.render(buf.data(), block, nowNs);
        nowNs += 1000000000LL * block / kSr;
        out.insert(out.end(), buf.begin(), buf.end());
    }
    for (auto& h : hosts) h->stop();
    // 16-bit WAV.
    std::ofstream f(o.out, std::ios::binary);
    if (!f) { std::fprintf(stderr, "render: cannot write %s\n", o.out.c_str()); return 1; }
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = uint32_t(out.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(kSr); u32(kSr * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    float peak = 0;
    for (float v : out) {
        peak = std::max(peak, std::fabs(v));
        int16_t q = int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767));
        f.write(reinterpret_cast<const char*>(&q), 2);
    }
    std::printf("%s: %s, %.1f s, %zu notes, peak %.2f\n", file.c_str(), o.out.c_str(), double(out.size() / 2) / kSr, notes.size(), double(peak));
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    Options o;
    std::vector<std::string> files;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        bool more = i + 1 < argc;
        if (a == "--controller" && more) o.controller = argv[++i];
        else if (a == "--library" && more) o.library = argv[++i];
        else if ((a == "-o" || a == "--out") && more) o.out = argv[++i];
        else if (a == "--sounds" && more) o.sounds = argv[++i];
        else if (a == "--tail" && more) o.tail = float(std::atof(argv[++i]));
        else if (a == "--size" && more) {
            if (std::sscanf(argv[++i], "%fx%f", &o.w, &o.h) != 2 || o.w <= 0 || o.h <= 0) return usage();
        } else if (a == "--center" && more) {
            int n = std::sscanf(argv[++i], "%f,%f", &o.centers[0], &o.centers[1]);
            if (n == 1) o.centers[1] = o.centers[0] + 12;
            o.centers[2] = o.centers[3] = o.centers[1];
            if (n < 1) return usage();
            o.centersGiven = true;
        } else if (a == "--setting" && more) {
            std::string s = argv[++i];
            size_t eq = s.find('=');
            if (eq == std::string::npos) return usage();
            std::string v = s.substr(eq + 1);
            o.settings.push_back({s.substr(0, eq), v == "true" ? 1.0 : v == "false" ? 0.0 : std::atof(v.c_str())});
        } else if (!a.empty() && a[0] == '-') return usage();
        else files.push_back(a);
    }
    if (files.empty()) return usage();
    if (cmd == "check") return check(files, o);
    if (cmd == "expand") return expandCmd(files[0], o);
    if (cmd == "notes") return notesCmd(files[0], o);
    if (cmd == "touches") return touchesCmd(files[0], o);
#ifdef MPE_LIBFAUST
    if (cmd == "render") return renderCmd(files[0], o);
#endif
    return usage();
}
