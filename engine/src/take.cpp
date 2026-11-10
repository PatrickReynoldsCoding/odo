// Takes (mpe/take.h): the file, recording and playing. The score, the layout and expanding a
// score into touches are in take_score.cpp.
#include "mpe/take.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include "mpe/preset.h"

namespace mpe::take {

namespace {

// The shortest text that reads back as the same float.
std::string fnum(float v) {
    if (!std::isfinite(v)) v = 0;
    char b[32];
    // No exponent for an ordinary number: 400, not 4e+02.
    bool plain = v == 0 || (std::fabs(v) >= 1e-4f && std::fabs(v) < 1e9f);
    for (int d = 1; d <= 9; d++) {
        std::snprintf(b, sizeof b, "%.*g", d, double(v));
        if (plain && d < 9 && std::strchr(b, 'e')) continue;
        if (std::strtof(b, nullptr) == v) break;
    }
    return b;
}
// The shortest text that reads back as the same double (a setting can be a 48-bit mask).
std::string dnum(double v) {
    if (!std::isfinite(v)) v = 0;
    char b[40];
    bool plain = v == 0 || (std::fabs(v) >= 1e-4 && std::fabs(v) < 1e17);
    for (int d = 1; d <= 17; d++) {
        std::snprintf(b, sizeof b, "%.*g", d, v);
        if (plain && d < 17 && std::strchr(b, 'e')) continue;
        if (std::strtod(b, nullptr) == v) break;
    }
    return b;
}
// A time in ms, to the microsecond, without trailing zeros.
std::string tnum(double ms) {
    if (!std::isfinite(ms)) ms = 0;
    char b[40];
    std::snprintf(b, sizeof b, "%.3f", ms);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}
double quantize(double ms) { return std::round(ms * 1000.0) / 1000.0; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// A line, trimmed; "" for a comment (a line starting with "#"). Only a score has comments at the
// end of a line: the words of a take's other lines are taken as they are.
std::string uncomment(const std::string& line) {
    std::string t = trim(line);
    return !t.empty() && t[0] == '#' ? "" : t;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::istringstream in(s);
    std::string x;
    while (in >> x) w.push_back(x);
    return w;
}
// The rest of the line after `n` words.
std::string after(const std::string& s, int n) {
    size_t i = 0;
    for (int k = 0; k < n; k++) {
        i = s.find_first_not_of(" \t", i);
        if (i == std::string::npos) return "";
        i = s.find_first_of(" \t", i);
        if (i == std::string::npos) return "";
    }
    return trim(s.substr(i));
}

bool number(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == 0 && std::isfinite(v);
}
bool integer(const std::string& s, int& v) {
    double d;
    if (!number(s, d) || d != std::floor(d) || std::fabs(d) > 1e9) return false;
    v = int(d);
    return true;
}
// "3" (side 0) or "1:3".
bool sided(const std::string& s, int& side, int& n) {
    size_t c = s.find(':');
    side = 0;
    if (c != std::string::npos) {
        if (!integer(s.substr(0, c), side) || side < 0 || side > 15) return false;
        return integer(s.substr(c + 1), n);
    }
    return integer(s, n);
}
std::string sidedText(int side, int n) { return side ? std::to_string(side) + ":" + std::to_string(n) : std::to_string(n); }

const char* phaseWord(int p) { return p == Down ? "down" : p == Move ? "move" : p == Up ? "up" : "cancel"; }
int phaseOf(const std::string& w) {
    if (w == "down" || w == "d") return Down;
    if (w == "move" || w == "m") return Move;
    if (w == "up" || w == "u") return Up;
    if (w == "cancel" || w == "c") return Cancel;
    return -1;
}

}  // namespace

double Take::length() const {
    double l = 0;
    if (!touches.empty()) l = std::max(l, touches.back().t);
    if (!notes.empty()) l = std::max(l, notes.back().t);
    for (const Say& s : says) l = std::max(l, s.t + s.dur);
    for (const Section& s : sections) l = std::max(l, s.t);
    for (const Setting& s : settings) l = std::max(l, s.t);
    return l;
}

// ---- the file ------------------------------------------------------------------------------------

bool parse(const std::string& text, Take& out, std::string* error, std::vector<std::string>* warnings) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (text.size() > kMaxBytes) return fail("This file is too big to be a take.");
    std::vector<std::string> lines;
    {
        size_t a = 0;
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) a = 3;
        while (a <= text.size()) {
            size_t b = text.find('\n', a);
            if (b == std::string::npos) b = text.size();
            std::string l = text.substr(a, b - a);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            a = b + 1;
        }
    }
    size_t i = 0;
    while (i < lines.size() && uncomment(lines[i]).empty()) i++;
    const std::string stem = "odo-take/";
    std::string first = i < lines.size() ? uncomment(lines[i]) : "";
    if (first.compare(0, stem.size(), stem) != 0) return fail("This file is not an odo take.");
    int major = std::atoi(first.c_str() + stem.size());
    if (major < 1) return fail("This file is not an odo take.");
    if (major > 1) return fail("This take was made by a newer odo. Update odo to open it.");
    i++;

    Take t;
    auto warn = [&](size_t line, const std::string& what) {
        if (warnings) warnings->push_back("line " + std::to_string(line + 1) + ": " + what);
    };
    int side = 0;
    auto sd = [&]() -> Side& { if (t.sides.size() <= size_t(side)) t.sides.resize(size_t(side) + 1); return t.sides[size_t(side)]; };
    for (; i < lines.size(); i++) {
        std::string l = uncomment(lines[i]);
        if (l.empty()) continue;
        // The score: kept as it is written, to its "end".
        if (l == "score") {
            t.scoreLine = int(i) + 2;
            std::string s;
            size_t j = i + 1;
            for (; j < lines.size() && uncomment(lines[j]) != "end"; j++) s += lines[j] + "\n";
            if (j == lines.size()) warn(i, "the score has no \"end\": it runs to the end of the file");
            t.score += s;
            i = j;
            continue;
        }
        // A header line: "key: value".
        size_t colon = l.find(':');
        std::string key = colon == std::string::npos ? "" : l.substr(0, colon);
        bool isKey = !key.empty() && std::isalpha(static_cast<unsigned char>(key[0])) &&
                     std::all_of(key.begin(), key.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
        if (isKey) {
            std::string v = trim(l.substr(colon + 1));
            std::vector<std::string> w = words(v);
            double d = 0;
            if (key == "name") t.name = v;
            else if (key == "by") t.by = v;
            else if (key == "about") t.about += (t.about.empty() ? "" : "\n") + v;
            else if (key == "madeBy") t.madeBy = v;
            else if (key == "tempo") { if (number(v, d) && d >= 10 && d <= 600) t.tempo = d; else warn(i, "tempo: a number of beats a minute, 10 to 600"); }
            else if (key == "meter") {
                int a = 0, b = 0;
                size_t sl = v.find('/');
                if (sl != std::string::npos && integer(trim(v.substr(0, sl)), a) && integer(trim(v.substr(sl + 1)), b) && a > 0 && a <= 32 &&
                    (b == 1 || b == 2 || b == 4 || b == 8 || b == 16)) { t.meterBeats = a; t.meterUnit = b; }
                else warn(i, "meter: as 4/4, 3/4 or 6/8");
            }
            else if (key == "key") t.key = v;
            else if (key == "speed") { if (number(v, d) && d > 0.05 && d <= 4) t.speed = d; }
            else if (key == "side") { int k = 0; if (integer(v, k) && k >= 0 && k < 16) { side = k; sd(); } else warn(i, "side: 0 to 15"); }
            else if (key == "controller") { sd().controller = w.empty() ? "" : w[0]; sd().controllerVersion = w.size() > 1 ? w[1] : ""; }
            else if (key == "settings") {
                std::string perr;
                Json j = Json::parse(v, &perr);
                if (j.type != Json::Object) warn(i, "settings: a JSON object, {\"id\": value, ...}");
                for (const auto& kv : j.o)
                    if (kv.second.type == Json::Number || kv.second.type == Json::Bool) sd().settings.push_back({kv.first, kv.second.num()});
            }
            else if (key == "sound") { if (!v.empty()) sd().sounds.push_back(w[0]); }
            else if (key == "preset") {
                // A preset (PRESET_FORMAT.md) on one line; its "format", "name" and sides may be left
                // out (a score may give only the effects, or a part's).
                std::string perr;
                Json j = Json::parse(v, &perr);
                if (j.type != Json::Object) { warn(i, "preset: a JSON object on one line, as a preset file has it"); continue; }
                auto has = [&](const char* k) { for (const auto& kv : j.o) if (kv.first == k) return true; return false; };
                auto put = [&](const char* k, Json x) {
                    for (auto& kv : j.o) if (kv.first == k) { kv.second = std::move(x); return; }
                    j.o.emplace_back(k, std::move(x));
                };
                if (!has("format")) { Json f; f.type = Json::String; f.s = preset::kFormat; put("format", f); }
                if (j["sides"].type != Json::Array || j["sides"].size() == 0) {
                    Json one; one.type = Json::Object;
                    Json a; a.type = Json::Array; a.a.push_back(one);
                    put("sides", a);
                }
                preset::Preset p;
                std::string perr2;
                if (preset::parse(preset::toJson(j), p, &perr2)) { t.preset = std::move(p); t.hasPreset = true; }
                else warn(i, "preset: " + perr2);
            }
            else if (key == "volume") { if (number(v, d) && d >= 0 && d <= 2) t.volume = d; else warn(i, "volume: the master volume, 0 to 2 (1: the usual level)"); }
            else if (key == "surface") {
                double a = 0, b = 0;
                if (w.size() == 2 && number(w[0], a) && number(w[1], b) && a > 0 && b > 0) { sd().width = float(a); sd().height = float(b); }
                else warn(i, "surface: its width and height in dp");
            }
            else if (key == "centers") {
                // One to four: each part's; those left out as the one before.
                double a = 0;
                for (size_t j = 0; j < size_t(kCenters); j++) {
                    if (j < w.size() && number(w[j], a)) sd().centers[j] = float(a);
                    else if (j > 0) sd().centers[j] = sd().centers[j - 1];
                    else break;
                    sd().hasCenters = true;
                }
            }
            else warn(i, "\"" + key + ":\" is not known here: left out");
            continue;
        }
        // An event.
        std::vector<std::string> w = words(l);
        const std::string& k = w[0];
        double tm = 0;
        if (w.size() < 2 || !number(w[1], tm) || tm < 0) { warn(i, "\"" + k + "\": its time (ms) is missing: left out"); continue; }
        if (k == "t") {
            Touch e;
            e.t = tm;
            double x, y, f, sz = 0;
            if (w.size() < 7 || !sided(w[2], e.side, e.finger) || (e.phase = phaseOf(w[3])) < 0 || !number(w[4], x) || !number(w[5], y) ||
                !number(w[6], f) || (w.size() > 7 && !number(w[7], sz))) { warn(i, "a touch is: t TIME FINGER down|move|up|cancel X Y FORCE [SIZE]"); continue; }
            e.x = float(x); e.y = float(y); e.force = float(std::clamp(f, 0.0, 1.0)); e.size = float(sz);
            t.touches.push_back(e);
        } else if (k == "n") {
            Note e;
            e.t = tm;
            double a[5] = {};
            bool ok = w.size() >= 4 && sided(w[2], e.side, e.voice);
            const std::string kind = ok ? w[3] : "";
            if (ok && kind == "on") {
                int part = 0;
                ok = w.size() >= 9 && integer(w[4], part) && number(w[5], a[0]) && number(w[6], a[1]) && number(w[7], a[2]) && number(w[8], a[3]);
                e.kind = On; e.part = std::clamp(part, 0, 3);
                e.pitch = float(a[0]); e.pressure = float(a[1]); e.slide = float(a[2]); e.velocity = float(a[3]);
            } else if (ok && kind == "ch") {
                ok = w.size() >= 7 && number(w[4], a[0]) && number(w[5], a[1]) && number(w[6], a[2]);
                e.kind = Change; e.pitch = float(a[0]); e.pressure = float(a[1]); e.slide = float(a[2]);
            } else if (ok && kind == "off") {
                e.kind = Off;
            } else if (ok && kind == "vol") {
                ok = w.size() >= 5 && number(w[4], a[0]);
                e.kind = Volume; e.pressure = float(a[0]);
            } else ok = false;
            if (!ok) { warn(i, "a note is: n TIME VOICE on PART PITCH PRESSURE SLIDE VELOCITY | ch PITCH PRESSURE SLIDE | off | vol VOLUME"); continue; }
            t.notes.push_back(e);
        } else if (k == "s") {
            Setting e;
            e.t = tm;
            double v = 0;
            std::string id = w.size() > 2 ? w[2] : "";
            size_t c = id.find(':');
            if (c != std::string::npos && integer(id.substr(0, c), e.side)) id = id.substr(c + 1);
            if (id.empty() || w.size() < 4 || !number(w[3], v)) { warn(i, "a setting is: s TIME ID VALUE"); continue; }
            e.id = id; e.value = v;
            t.settings.push_back(e);
        } else if (k == "section") {
            t.sections.push_back({tm, after(l, 2)});
        } else if (k == "say") {
            double dur = 0;
            if (w.size() < 4 || !number(w[2], dur) || dur < 0) { warn(i, "words to show are: say TIME DURATION TEXT"); continue; }
            t.says.push_back({tm, dur, after(l, 3)});
        } else warn(i, "\"" + k + "\" is not known here: left out");
    }
    auto byTime = [](const auto& a, const auto& b) { return a.t < b.t; };
    std::stable_sort(t.touches.begin(), t.touches.end(), byTime);
    std::stable_sort(t.notes.begin(), t.notes.end(), byTime);
    std::stable_sort(t.settings.begin(), t.settings.end(), byTime);
    std::stable_sort(t.sections.begin(), t.sections.end(), byTime);
    std::stable_sort(t.says.begin(), t.says.end(), byTime);
    if (!t.performed() && trim(t.score).empty()) return fail("This take has nothing in it to play.");
    out = std::move(t);
    return true;
}

std::string write(const Take& t) {
    std::string o = std::string(kFormat) + "\n";
    auto field = [&](const char* k, const std::string& v) { if (!v.empty()) o += std::string(k) + ": " + v + "\n"; };
    auto oneLine = [](std::string s) { for (char& c : s) if (c == '\n' || c == '\r') c = ' '; return s; };
    field("name", oneLine(t.name));
    field("by", oneLine(t.by));
    {
        std::string a = t.about;
        size_t p = 0;
        while (!a.empty()) {
            size_t n = a.find('\n', p);
            field("about", trim(a.substr(p, n == std::string::npos ? std::string::npos : n - p)));
            if (n == std::string::npos) break;
            p = n + 1;
        }
    }
    field("madeBy", oneLine(t.madeBy));
    if (t.tempo > 0) field("tempo", dnum(t.tempo));
    if (t.meterBeats != 4 || t.meterUnit != 4 || t.tempo > 0) field("meter", std::to_string(t.meterBeats) + "/" + std::to_string(t.meterUnit));
    field("key", oneLine(t.key));
    if (t.speed != 1) field("speed", dnum(t.speed));
    if (t.volume >= 0) field("volume", dnum(t.volume));
    if (t.hasPreset) {
        // As a preset file has it, on one line, less what the take's own lines say: its name, who
        // made it, and each side's controller and settings.
        Json j = Json::parse(preset::write(t.preset));
        std::vector<std::pair<std::string, Json>> kept;
        for (auto& kv : j.o) if (kv.first != "name" && kv.first != "madeBy") kept.push_back(std::move(kv));
        j.o = std::move(kept);
        for (auto& kv : j.o)
            if (kv.first == "sides")
                for (Json& sd : kv.second.a) {
                    std::vector<std::pair<std::string, Json>> rest;
                    for (auto& f : sd.o) if (f.first != "controller" && f.first != "settings") rest.push_back(std::move(f));
                    sd.o = std::move(rest);
                }
        field("preset", preset::toJson(j));
    }
    for (size_t k = 0; k < t.sides.size(); k++) {
        const Side& s = t.sides[k];
        if (t.sides.size() > 1) field("side", std::to_string(k));
        field("controller", s.controller + (s.controllerVersion.empty() ? "" : " " + s.controllerVersion));
        if (!s.settings.empty()) {
            std::string j = "{";
            for (size_t i = 0; i < s.settings.size(); i++) {
                Json key;
                key.type = Json::String;
                key.s = s.settings[i].first;
                j += (i ? ", " : "") + preset::toJson(key) + ": " + dnum(s.settings[i].second);
            }
            field("settings", j + "}");
        }
        for (const std::string& snd : s.sounds) field("sound", snd);
        if (s.width > 0 && s.height > 0) field("surface", fnum(s.width) + " " + fnum(s.height));
        if (s.hasCenters) {
            // Those after the second only when a part there has its own (older readers take two).
            int n = 2;
            for (int j = 2; j < kCenters; j++) if (s.centers[j] != s.centers[j - 1]) n = j + 1;
            std::string v;
            for (int j = 0; j < n; j++) v += (j ? " " : "") + fnum(s.centers[j]);
            field("centers", v);
        }
    }
    if (!t.score.empty()) {
        o += "\nscore\n" + t.score;
        if (t.score.back() != '\n') o += "\n";
        o += "end\n";
    }
    if (!t.sections.empty() || !t.says.empty()) o += "\n";
    for (const Section& s : t.sections) o += "section " + tnum(s.t) + " " + oneLine(s.name) + "\n";
    for (const Say& s : t.says) o += "say " + tnum(s.t) + " " + tnum(s.dur) + " " + oneLine(s.text) + "\n";
    if (t.performed() || !t.settings.empty()) o += "\n";
    // The touches, notes and settings, merged by time: a log of what happened.
    size_t a = 0, b = 0, c = 0;
    const double inf = std::numeric_limits<double>::infinity();
    while (a < t.touches.size() || b < t.notes.size() || c < t.settings.size()) {
        double ta = a < t.touches.size() ? t.touches[a].t : inf, tb = b < t.notes.size() ? t.notes[b].t : inf;
        double tc = c < t.settings.size() ? t.settings[c].t : inf;
        if (tc <= ta && tc <= tb) {
            const Setting& e = t.settings[c++];
            o += "s " + tnum(e.t) + " " + (e.side ? std::to_string(e.side) + ":" : "") + e.id + " " + dnum(e.value) + "\n";
        } else if (ta <= tb) {
            const Touch& e = t.touches[a++];
            o += "t " + tnum(e.t) + " " + sidedText(e.side, e.finger) + " " + phaseWord(e.phase) + " " + fnum(e.x) + " " + fnum(e.y) + " " +
                 fnum(e.force) + (e.size > 0 ? " " + fnum(e.size) : "") + "\n";
        } else {
            const Note& e = t.notes[b++];
            o += "n " + tnum(e.t) + " " + sidedText(e.side, e.voice);
            if (e.kind == On) o += " on " + std::to_string(e.part) + " " + fnum(e.pitch) + " " + fnum(e.pressure) + " " + fnum(e.slide) + " " + fnum(e.velocity);
            else if (e.kind == Change) o += " ch " + fnum(e.pitch) + " " + fnum(e.pressure) + " " + fnum(e.slide);
            else if (e.kind == Off) o += " off";
            else o += " vol " + fnum(e.pressure);
            o += "\n";
        }
    }
    return o;
}

std::string fileName(const Take& t) {
    std::string n;
    for (unsigned char c : t.name) {
        if (c < 0x20 || std::string("/\\:*?\"<>|").find(char(c)) != std::string::npos) n += '-';
        else n += char(c);
    }
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && (n.front() == ' ' || n.front() == '.')) n.erase(n.begin());
    if (n.empty()) n = "Take";
    if (n.size() > 100) n.resize(100);
    return n + kExtension;
}

preset::Preset presetOf(const Take& t) {
    preset::Preset p = t.hasPreset ? t.preset : preset::Preset();
    p.name = t.name.empty() ? "Take" : t.name;
    if (p.madeBy.empty()) p.madeBy = t.madeBy;
    p.sides.resize(std::max<size_t>({p.sides.size(), t.sides.size(), 1}));
    if (p.sides.size() > size_t(preset::kMaxSides)) p.sides.resize(size_t(preset::kMaxSides));
    for (size_t k = 0; k < t.sides.size() && k < p.sides.size(); k++) {
        const Side& s = t.sides[k];
        preset::Side& ps = p.sides[k];
        if (!s.controller.empty()) ps.controller = s.controller;
        if (ps.settings.type != Json::Object) { ps.settings = Json(); ps.settings.type = Json::Object; }
        for (const auto& kv : s.settings) {
            Json v; v.type = Json::Number; v.n = kv.second;
            bool found = false;
            for (auto& o : ps.settings.o) if (o.first == kv.first) { o.second = v; found = true; }
            if (!found) ps.settings.o.emplace_back(kv.first, v);
        }
        for (size_t j = 0; j < s.sounds.size() && j < size_t(preset::kMaxParts); j++) {
            if (ps.parts.size() <= j) ps.parts.resize(j + 1);
            if (ps.parts[j].sound != s.sounds[j]) { ps.parts[j].sound = s.sounds[j]; ps.parts[j].version.clear(); }
        }
    }
    return p;
}

std::vector<double> settingsFor(const Take& t, int side, const Json& manifest, std::vector<double> values) {
    const Json& st = manifest["settings"];
    if (values.size() < st.size()) {
        size_t had = values.size();
        values.resize(st.size());
        for (size_t i = had; i < st.size(); i++) values[i] = st[i]["default"].num();
    }
    if (side < 0 || size_t(side) >= t.sides.size()) return values;
    for (const auto& kv : t.sides[size_t(side)].settings)
        for (size_t i = 0; i < st.size(); i++)
            if (st[i]["id"].str() == kv.first) values[i] = kv.second;
    return values;
}

// ---- recording -----------------------------------------------------------------------------------

bool Unchanged::keep(int voice, int kind, float pitch, float pressure, float slide) {
    std::array<float, 3> now{pitch, pressure, slide};
    if (kind == On) { shape_[voice] = now; return true; }
    if (kind == Off) { shape_.erase(voice); volume_.erase(voice); return true; }
    if (kind == Change) {
        auto it = shape_.find(voice);
        if (it != shape_.end() && it->second == now) return false;
        shape_[voice] = now;
        return true;
    }
    auto it = volume_.find(voice);                 // Volume: carried in pressure
    if (it != volume_.end() && it->second == pressure) return false;
    volume_[voice] = pressure;
    return true;
}

struct Recorder::State {
    bool on = false;
    double origin = std::numeric_limits<double>::quiet_NaN();
    int64_t originWallNs = 0;
    Take take;
    std::set<std::pair<int, int>> down, sounding;
    std::map<int, Unchanged> unchanged;  // by side
};

namespace {
int64_t wallNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

Recorder::Recorder() = default;
Recorder::~Recorder() { if (s_) s_->on = false; }

void Recorder::start(const preset::Preset* setup, double volume) {
    if (s_) s_->on = false;              // hosts still tapped for the last take tell that one, which is over
    s_ = std::make_shared<State>();
    s_->on = true;
    if (setup) { s_->take.preset = *setup; s_->take.hasPreset = true; }
    s_->take.volume = volume;
}

bool Recorder::recording() const { return s_ && s_->on; }

double Recorder::elapsedMs() const {
    if (!recording() || std::isnan(s_->origin)) return 0;
    return double(wallNs() - s_->originWallNs) / 1e6;
}

void Recorder::attach(ControllerHost& host, int side, const std::vector<std::string>& sounds) {
    if (!recording()) return;
    std::shared_ptr<State> s = s_;
    if (s->take.sides.size() <= size_t(side)) s->take.sides.resize(size_t(side) + 1);
    Side& sd = s->take.sides[size_t(side)];
    const Json& m = host.manifest();
    sd.controller = m["id"].str();
    sd.controllerVersion = m["version"].str();
    sd.settings.clear();
    const Json& st = m["settings"];
    for (size_t i = 0; i < st.size() && i < host.values().size(); i++) sd.settings.push_back({st[i]["id"].str(), host.values()[i]});
    sd.sounds = sounds;
    sd.width = host.width();
    sd.height = host.height();
    for (int j = 0; j < kCenters; j++) sd.centers[j] = host.centerNote(j);
    sd.hasCenters = true;
    ControllerHost* h = &host;          // the taps live in the host: it is there whenever they are called
    ControllerHost::Taps taps;
    taps.touch = [s, side, h](int id, int phase, float x, float y, float size, float force, double t) {
        if (!s->on) return;
        t = quantize(t);
        std::pair<int, int> f{side, id};
        if (phase == Down) {
            if (std::isnan(s->origin)) { s->origin = t; s->originWallNs = wallNs(); }
            s->down.insert(f);
        } else if (!s->down.count(f)) return;                 // down before the take started
        else if (phase == Up || phase == Cancel) s->down.erase(f);
        float w = h->width() > 0 ? h->width() : 1, hh = h->height() > 0 ? h->height() : 1;
        s->take.touches.push_back({t - s->origin, side, id, phase, x / w, y / hh, std::clamp(force, 0.f, 1.f), size});
    };
    taps.note = [s, side](int kind, int voice, int part, float pitch, float pressure, float slide, float velocity, double t) {
        if (!s->on || std::isnan(s->origin)) return;
        t = quantize(t);
        std::pair<int, int> v{side, voice};
        if (kind == On) s->sounding.insert(v);
        else if (kind != Volume && !s->sounding.count(v)) return;   // started before the take (a volume may come first)
        else if (kind == Off) s->sounding.erase(v);
        // A change to nothing new is left out (it sends nothing): a controller may send one every frame.
        if (!s->unchanged[side].keep(voice, kind, pitch, pressure, slide)) return;
        s->take.notes.push_back({std::max(0.0, t - s->origin), side, voice, kind, part, pitch, pressure, slide, velocity});
    };
    taps.setting = [s, side, h](int index, double value, double t) {
        if (!s->on) return;
        std::string id = h->manifest()["settings"][size_t(index)]["id"].str();
        if (std::isnan(s->origin)) {                           // before the first finger: how it starts
            for (auto& kv : s->take.sides[size_t(side)].settings) if (kv.first == id) kv.second = value;
            return;
        }
        s->take.settings.push_back({quantize(t) - s->origin, side, id, value});
    };
    host.setTaps(std::move(taps));
}

Take Recorder::stop() {
    if (!s_) return Take();
    std::shared_ptr<State> s = s_;
    s_.reset();
    s->on = false;
    Take t = std::move(s->take);
    auto byTime = [](const auto& a, const auto& b) { return a.t < b.t; };
    std::stable_sort(t.notes.begin(), t.notes.end(), byTime);   // a strum's notes were stamped for later
    double end = t.length();
    // Fingers still down, and notes still sounding: let go of at the end.
    for (const auto& f : s->down) {
        for (auto it = t.touches.rbegin(); it != t.touches.rend(); ++it)
            if (it->side == f.first && it->finger == f.second) {
                Touch u = *it;
                u.t = end; u.phase = Up; u.force = 0;
                t.touches.push_back(u);
                break;
            }
    }
    for (const auto& v : s->sounding) t.notes.push_back({end, v.first, v.second, Off, 0, 0, 0, 0, 0});
    std::stable_sort(t.touches.begin(), t.touches.end(), byTime);
    return t;
}

// ---- playing -------------------------------------------------------------------------------------

void Player::buildStrokes() {
    strokes_.clear();
    std::map<std::pair<int, int>, size_t> open;
    for (size_t i = 0; i < take_.touches.size(); i++) {
        const Touch& e = take_.touches[i];
        std::pair<int, int> f{e.side, e.finger};
        auto it = open.find(f);
        if (e.phase == Down || it == open.end()) {
            if (it != open.end()) { strokes_[it->second].up = e.t; open.erase(it); }
            if (e.phase == Up || e.phase == Cancel) continue;
            Stroke s;
            s.side = e.side; s.finger = e.finger; s.down = e.t; s.up = -1;
            s.events.push_back(i);
            open[f] = strokes_.size();
            strokes_.push_back(std::move(s));
            continue;
        }
        Stroke& s = strokes_[it->second];
        s.events.push_back(i);
        if (e.phase == Up || e.phase == Cancel) { s.up = e.t; open.erase(it); }
    }
    length_ = take_.length();
    for (Stroke& s : strokes_) if (s.up < 0) s.up = length_;
    // Each stroke's note: the note started on its side when it went down (the nearest, unclaimed).
    std::vector<bool> used(take_.notes.size(), false);
    for (Stroke& s : strokes_) {
        size_t lo = size_t(std::lower_bound(take_.notes.begin(), take_.notes.end(), s.down - 1,
                                            [](const Note& n, double t) { return n.t < t; }) - take_.notes.begin());
        for (size_t k = lo; k < take_.notes.size() && take_.notes[k].t <= s.down + 40; k++) {
            const Note& n = take_.notes[k];
            if (used[k] || n.kind != On || n.side != s.side) continue;
            used[k] = true;
            s.label = noteName(std::round(n.pitch));
            break;
        }
    }
}

void Player::retouch(std::vector<Touch> touches) {
    take_.touches = std::move(touches);
    std::stable_sort(take_.touches.begin(), take_.touches.end(), [](const Touch& a, const Touch& b) { return a.t < b.t; });
    buildStrokes();
}

void Player::load(Take t) {
    take_ = std::move(t);
    auto byTime = [](const auto& a, const auto& b) { return a.t < b.t; };
    std::stable_sort(take_.touches.begin(), take_.touches.end(), byTime);
    std::stable_sort(take_.notes.begin(), take_.notes.end(), byTime);
    buildStrokes();
    loaded_ = true;
    playing_ = false;
    pos_ = 0;
    next_ = 0;
    loop_ = -1;
    speed_ = 1;
    sounding_.clear();
}

void Player::seek(double pos) {
    pos_ = pos;
    next_ = size_t(std::lower_bound(take_.notes.begin(), take_.notes.end(), pos, [](const Note& n, double t) { return n.t < t; }) -
                   take_.notes.begin());
}

void Player::allOff(const Sink& sink) {
    for (const auto& v : sounding_) {
        Note n;
        n.t = pos_; n.side = v.first; n.voice = v.second; n.kind = Off;
        if (sink) sink(n);
    }
    sounding_.clear();
}

void Player::play(Mode m, double nowMs, const Sink& sink) {
    if (!loaded_) return;
    allOff(sink);
    mode_ = m;
    playing_ = true;
    last_ = nowMs;
    seek(loop_ >= 0 ? loopFrom_ : 0);
}

void Player::stop(const Sink& sink) {
    allOff(sink);
    playing_ = false;
}

void Player::setSpeed(double s) { speed_ = std::clamp(s, 0.25, 2.0); }

void Player::loopSection(int k, double nowMs, const Sink& sink) {
    if (k < 0 || size_t(k) >= take_.sections.size()) { loop_ = -1; return; }
    loop_ = k;
    loopFrom_ = take_.sections[size_t(k)].t;
    loopTo_ = size_t(k) + 1 < take_.sections.size() ? take_.sections[size_t(k) + 1].t : length_;
    if (loopTo_ <= loopFrom_ + 1) loopTo_ = length_;
    if (playing_) {
        allOff(sink);
        last_ = nowMs;
        seek(loopFrom_);
    }
}

void Player::update(double nowMs, const Sink& sink) {
    if (!playing_) return;
    double dt = std::max(0.0, nowMs - last_) * speed_;
    last_ = nowMs;
    double to = pos_ + dt;
    auto emitTo = [&](double end) {
        while (next_ < take_.notes.size() && take_.notes[next_].t < end) {
            const Note& n = take_.notes[next_++];
            if (mode_ != Listen) continue;
            std::pair<int, int> v{n.side, n.voice};
            if (n.kind == On) { if (std::find(sounding_.begin(), sounding_.end(), v) == sounding_.end()) sounding_.push_back(v); }
            else if (n.kind != Volume && std::find(sounding_.begin(), sounding_.end(), v) == sounding_.end()) continue;
            else if (n.kind == Off) sounding_.erase(std::find(sounding_.begin(), sounding_.end(), v));
            if (sink) sink(n);
        }
    };
    if (loop_ >= 0) {
        double len = loopTo_ - loopFrom_;
        while (to >= loopTo_ && len > 0) {
            emitTo(loopTo_);
            allOff(sink);
            to = loopFrom_ + (to - loopTo_);
            seek(loopFrom_);
            if (to >= loopTo_) to = loopFrom_ + std::fmod(to - loopFrom_, len);
        }
        emitTo(to);
        pos_ = to;
        return;
    }
    // The end: notes let go of after their last word; a short tail so a release is heard.
    if (to > length_ + 300) {
        emitTo(std::numeric_limits<double>::infinity());
        allOff(sink);
        playing_ = false;
        pos_ = length_;
        return;
    }
    emitTo(to);
    pos_ = to;
}

int Player::sectionAt(double pos) const {
    int k = -1;
    for (size_t i = 0; i < take_.sections.size(); i++) if (take_.sections[i].t <= pos + 0.5) k = int(i);
    return k;
}

const Say* Player::sayAt(double pos) const {
    const Say* s = nullptr;
    for (const Say& w : take_.says) if (w.t <= pos && pos < w.t + w.dur) s = &w;
    return s;
}

bool Player::at(const Stroke& s, double pos, float* x, float* y, float* force) const {
    if (s.events.empty() || pos < s.down || pos > s.up) return false;
    size_t k = 0;
    while (k + 1 < s.events.size() && take_.touches[s.events[k + 1]].t <= pos) k++;
    const Touch& a = take_.touches[s.events[k]];
    *x = a.x; *y = a.y; *force = a.force;
    if (k + 1 < s.events.size()) {
        const Touch& b = take_.touches[s.events[k + 1]];
        if (b.phase == Move && b.t > a.t) {
            float u = float((pos - a.t) / (b.t - a.t));
            *x = a.x + (b.x - a.x) * u; *y = a.y + (b.y - a.y) * u; *force = a.force + (b.force - a.force) * u;
        }
    }
    return true;
}

namespace {
// A draw list (CONTROLLER_FORMAT.md): the words of controllers/include/mesh.h.
struct List {
    std::vector<uint8_t> b;
    void w(uint32_t v) { for (int k = 0; k < 4; k++) b.push_back(uint8_t(v >> (8 * k))); }
    void f(float v) { uint32_t u; std::memcpy(&u, &v, 4); w(u); }
    static uint32_t alpha(uint32_t c, float a) {
        float base = float(c & 0xFF) / 255.f;
        int v = int(std::lround(std::clamp(a * base, 0.f, 1.f) * 255));
        return (c & 0xFFFFFF00u) | uint32_t(v);
    }
    void rect(float x, float y, float ww, float h, float r, uint32_t fill) { w(2); f(x); f(y); f(ww); f(h); f(r); w(fill); w(0); f(0); }
    void circle(float x, float y, float r, uint32_t fill, uint32_t stroke, float sw) { w(3); f(x); f(y); f(r); w(fill); w(stroke); f(sw); }
    void line(float x1, float y1, float x2, float y2, uint32_t c, float width) { w(4); f(x1); f(y1); f(x2); f(y2); w(c); f(width); }
    void glow(float x, float y, float r, uint32_t inner, uint32_t outer) { w(8); f(x); f(y); f(r); w(inner); w(outer); }
    void text(float x, float y, float size, uint32_t c, int flags, const std::string& s) {
        w(6); f(x); f(y); f(size); w(c); w(uint32_t(flags)); w(uint32_t(s.size()));
        for (size_t i = 0; i < s.size(); i += 4) {
            uint32_t v = 0;
            for (size_t k = 0; k < 4 && i + k < s.size(); k++) v |= uint32_t(uint8_t(s[i + k])) << (8 * k);
            w(v);
        }
    }
};
size_t glyphs(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}
}  // namespace

std::vector<uint8_t> Player::drawGhosts(int side, float w, float h, const GhostStyle& st) const {
    List l;
    if (!loaded_ || !playing_) return l.b;
    const double pos = pos_;
    // How far along: a thin line across the top (the looped section's part of it marked).
    if (length_ > 0) {
        if (loop_ >= 0) l.line(w * float(loopFrom_ / length_), 2, w * float(loopTo_ / length_), 2, List::alpha(st.next, 0.6f), 4);
        l.line(0, 2, w * float(std::min(1.0, pos / length_)), 2, List::alpha(st.finger, 0.8f), 4);
    }
    const float bigger = std::max(1.f, std::min(w, h) / 400.f);   // a little bigger on a big surface
    const float R = st.radius * std::min(bigger, 1.6f);
    for (const Stroke& s : strokes_) {
        if (s.side != side || st.look == GhostStyle::Off) continue;
        // Coming: where the finger goes down next.
        if (s.down > pos && s.down <= pos + st.previewMs * speed_) {
            const Touch& d = take_.touches[s.events.front()];
            float k = float((s.down - pos) / (st.previewMs * speed_));   // 1 far off, 0 now
            float x = d.x * w, y = d.y * h;
            if (st.look == GhostStyle::Dots) {
                // A small dot, filling in as it comes.
                l.circle(x, y, R * 0.32f, List::alpha(st.next, 0.9f * (1 - k)), 0, 0);
                if (!s.label.empty()) l.text(x, y - R * 0.5f, st.textSize * 0.75f, List::alpha(st.next, 1 - k), 1 | 4, s.label);
            } else if (st.look == GhostStyle::Falling) {
                // A drop falling from the top to its key, its path behind it.
                float fy = y * (1 - k);
                l.line(x, 0, x, fy, List::alpha(st.next, 0.25f * (1 - k)), std::max(2.f, R * 0.25f));
                l.rect(x - R * 0.6f, fy - R * 0.35f, R * 1.2f, R * 0.7f, R * 0.3f, List::alpha(st.next, 0.35f + 0.6f * (1 - k)));
                if (!s.label.empty()) l.text(x, fy - R * 0.55f, st.textSize * 0.8f, List::alpha(st.next, 1 - k), 1 | 4, s.label);
            } else {
                // A ring that closes in on where the finger goes down.
                l.circle(x, y, R * (1 + 1.6f * k), 0, List::alpha(st.next, 1 - k), 2.5f);
                if (!s.label.empty()) l.text(x, y + st.textSize * 0.35f, st.textSize * 0.85f, List::alpha(st.next, 1 - k), 1 | 4, s.label);
            }
            continue;
        }
        float x, y, f;
        if (!at(s, pos, &x, &y, &f)) continue;
        x *= w; y *= h;
        f = std::clamp(f, 0.f, 1.f);
        if (st.look == GhostStyle::Dots) {
            // A dot, a little bigger as it presses harder; a short trail.
            float px = x, py = y;
            for (int k = 1; k <= 6; k++) {
                double tp = pos - st.trailMs * 0.5 * k / 6;
                float tx, ty, tf;
                if (tp < s.down || !at(s, tp, &tx, &ty, &tf)) break;
                tx *= w; ty *= h;
                l.line(px, py, tx, ty, List::alpha(st.trail, 0.6f * (1 - float(k) / 7)), 2);
                px = tx; py = ty;
            }
            l.circle(x, y, R * 0.38f + st.grow * 0.35f * f, List::alpha(st.finger, 0.9f), 0, 0);
            if (!s.label.empty()) l.text(x, y - R * 0.6f, st.textSize * 0.8f, st.text, 1 | 4, s.label);
            continue;
        }
        if (st.look == GhostStyle::Falling) {
            // Held: the key lit under the finger, and above it a bar of the time it has left.
            float left = float(std::max(0.0, s.up - pos) / (st.previewMs * speed_));
            float bh = std::min(y, R * 2.5f * std::min(left, 4.f));
            if (bh > 1) l.rect(x - R * 0.18f, y - R * 0.4f - bh, R * 0.36f, bh, R * 0.18f, List::alpha(st.trail, 0.8f));
            l.rect(x - R * 0.7f, y - R * 0.4f, R * 1.4f, R * 0.8f, R * 0.35f, List::alpha(st.finger, 0.55f + 0.4f * f));
            if (!s.label.empty()) l.text(x, y + st.textSize * 0.35f, st.textSize * 0.9f, st.back | 0xFFu, 1 | 4, s.label);
            continue;
        }
        // Rings: its trail, where it was over the last moments, fading.
        const int n = 14;
        float px = x, py = y;
        for (int k = 1; k <= n; k++) {
            double tp = pos - st.trailMs * k / n;
            float tx, ty, tf;
            if (tp < s.down || !at(s, tp, &tx, &ty, &tf)) break;
            tx *= w; ty *= h;
            float a = 1 - float(k) / float(n + 1);
            l.line(px, py, tx, ty, List::alpha(st.trail, a), std::max(1.5f, R * 0.45f * a));
            px = tx; py = ty;
        }
        float r = R + st.grow * f;
        l.glow(x, y, r * 1.5f, List::alpha(st.finger, 0.45f), List::alpha(st.finger, 0));
        l.circle(x, y, r, List::alpha(st.finger, 0.18f), st.finger, 3);
        if (!s.label.empty()) l.text(x, y + st.textSize * 0.35f, st.textSize, st.text, 1 | 4, s.label);
    }
    // The section, and the words for now, at the top.
    int sec = sectionAt(pos);
    const Say* say = sayAt(pos);
    float ty = 14 + st.textSize;
    if (sec >= 0 && !take_.sections[size_t(sec)].name.empty()) {
        const std::string& name = take_.sections[size_t(sec)].name;
        float tw = float(glyphs(name)) * st.textSize * 0.42f + 16;
        l.rect(w / 2 - tw / 2, ty - st.textSize * 0.8f - 4, tw, st.textSize * 0.8f + 10, 6, st.back);
        l.text(w / 2, ty, st.textSize * 0.8f, List::alpha(st.text, 0.8f), 1, name);
        ty += st.textSize * 1.6f;
    }
    if (say && !say->text.empty()) {
        float tw = float(glyphs(say->text)) * st.textSize * 1.15f * 0.52f + 24;
        l.rect(w / 2 - tw / 2, ty - st.textSize * 1.15f - 4, tw, st.textSize * 1.15f + 14, 8, st.back);
        l.text(w / 2, ty + 2, st.textSize * 1.15f, st.text, 1 | 4, say->text);
    }
    return l.b;
}

}  // namespace mpe::take
