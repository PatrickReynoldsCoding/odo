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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "mpe/take.h"

using namespace mpe;
using namespace mpe::take;

#ifndef ODO_CONTROLLERS
#define ODO_CONTROLLERS "controllers/build"
#endif

namespace {

struct Options {
    std::string controller, library = ODO_CONTROLLERS, out;
    float w = 0, h = 0;
    float centers[2] = {60, 72};
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
    float w = o.w, h = o.h;
    if (w <= 0 && !t.sides.empty() && t.sides[0].width > 0) { w = t.sides[0].width; h = t.sides[0].height; }
    if (w <= 0) { w = 900; h = 500; }
    if (!t.performed()) {
        bool ok = prepare(t, *pkg, {}, w, h, o.centers, &rep);
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
            if (p.found && std::fabs(p.pitch - p.wanted) < 0.05f) continue;
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
        else if (a == "--size" && more) {
            if (std::sscanf(argv[++i], "%fx%f", &o.w, &o.h) != 2 || o.w <= 0 || o.h <= 0) return usage();
        } else if (a == "--center" && more) {
            int n = std::sscanf(argv[++i], "%f,%f", &o.centers[0], &o.centers[1]);
            if (n == 1) o.centers[1] = o.centers[0] + 12;
            if (n < 1) return usage();
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
    return usage();
}
