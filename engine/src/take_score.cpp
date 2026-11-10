// Takes written by hand (mpe/take.h, docs/formats/TAKE_AUTHORING.md): the score, where its notes are
// on a controller (Layout), and expanding it into touches and playing those into notes.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include "mpe/take.h"

namespace mpe::take {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

bool number(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == 0 && std::isfinite(v);
}

// A duration: "1", "0.5", "1/3", "3/2", "1+1/2".
bool duration(const std::string& s, double& v) {
    v = 0;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find('+', a);
        std::string term = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        size_t sl = term.find('/');
        double x, y = 1;
        if (sl == std::string::npos) { if (!number(term, x)) return false; }
        else if (!number(term.substr(0, sl), x) || !number(term.substr(sl + 1), y) || y == 0) return false;
        v += x / y;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v > 0;
}

// The scales a key can name, as semitones from its tonic.
const std::map<std::string, std::vector<int>>& scales() {
    static const std::map<std::string, std::vector<int>> m = {
        {"major", {0, 2, 4, 5, 7, 9, 11}}, {"ionian", {0, 2, 4, 5, 7, 9, 11}},
        {"minor", {0, 2, 3, 5, 7, 8, 10}}, {"aeolian", {0, 2, 3, 5, 7, 8, 10}},
        {"dorian", {0, 2, 3, 5, 7, 9, 10}}, {"phrygian", {0, 1, 3, 5, 7, 8, 10}},
        {"lydian", {0, 2, 4, 6, 7, 9, 11}}, {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
        {"locrian", {0, 1, 3, 5, 6, 8, 10}},
        {"harmonic-minor", {0, 2, 3, 5, 7, 8, 11}}, {"melodic-minor", {0, 2, 3, 5, 7, 9, 11}},
        {"pentatonic", {0, 2, 4, 7, 9}}, {"major-pentatonic", {0, 2, 4, 7, 9}},
        {"minor-pentatonic", {0, 3, 5, 7, 10}}, {"blues", {0, 3, 5, 6, 7, 10}},
        {"chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    };
    return m;
}

struct Key { int tonic = 60; std::vector<int> scale = {0, 2, 4, 5, 7, 9, 11}; bool set = false; };

// "D4 dorian", "F# minor" (octave 4 if not given), "C".
bool parseKey(const std::string& text, Key& k, std::string* why) {
    std::istringstream in(text);
    std::string tonic, mode;
    in >> tonic >> mode;
    if (tonic.empty()) { *why = "key: a tonic and a mode, as \"D4 dorian\""; return false; }
    int n = noteNumber(tonic);
    if (n < 0) n = noteNumber(tonic + "4");
    if (n < 0) { *why = "key: \"" + tonic + "\" is not a note (C4, F#3, Bb)"; return false; }
    mode = lower(mode.empty() ? "major" : mode);
    auto it = scales().find(mode);
    if (it == scales().end()) {
        *why = "key: \"" + mode + "\" is not a mode odo knows (major, minor, dorian, phrygian, lydian, mixolydian, locrian, "
               "harmonic-minor, melodic-minor, pentatonic, minor-pentatonic, blues, chromatic)";
        return false;
    }
    k.tonic = n;
    k.scale = it->second;
    k.set = true;
    return true;
}

// A pitch: a note name ("F#4"), or a degree of the key ("^3", "^5#", "^-1", "^8").
bool pitch(const std::string& s, const Key& key, float& out, std::string* why) {
    if (s.size() > 1 && s[0] == '^') {
        if (!key.set) { *why = "\"" + s + "\": a degree needs a key first (key D4 dorian)"; return false; }
        size_t i = 1;
        int sign = 1;
        if (s[i] == '-' || s[i] == '+') { sign = s[i] == '-' ? -1 : 1; i++; }
        size_t d0 = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
        if (i == d0) { *why = "\"" + s + "\": a degree is ^ and a number (^1 the tonic, ^8 an octave up)"; return false; }
        int deg = sign * std::atoi(s.substr(d0, i - d0).c_str());
        int acc = 0;
        for (; i < s.size(); i++) {
            if (s[i] == '#') acc++;
            else if (s[i] == 'b') acc--;
            else { *why = "\"" + s + "\": after a degree only # or b"; return false; }
        }
        int n = int(key.scale.size()), idx = deg - 1;
        int oct = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
        int step = idx - oct * n;
        out = float(key.tonic + 12 * oct + key.scale[size_t(step)] + acc);
        return true;
    }
    int n = noteNumber(s);
    if (n < 0) { *why = "\"" + s + "\" is not a note: a name with its octave (C4, F#3, Bb5), a degree (^3) or r for a rest"; return false; }
    out = float(n);
    return true;
}

// Splits a line into tokens at spaces, keeping "(...)" whole.
std::vector<std::string> tokens(const std::string& s, std::string* why) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (depth < 0) { *why = "a \")\" with no \"(\""; return {}; }
        if ((c == ' ' || c == '\t') && depth == 0) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (depth != 0) { *why = "a \"(\" with no \")\""; return {}; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

constexpr size_t kMaxPts = 256;   // values in a press, tone or pitch curve

bool gestures(const std::string& inside, const Key& key, std::vector<Gesture>& out, std::string* why) {
    std::vector<std::string> items;
    {
        std::string cur;
        for (char c : inside) { if (c == ',' || c == ';') { items.push_back(cur); cur.clear(); } else cur += c; }
        items.push_back(cur);
    }
    for (size_t k = 0; k < items.size(); k++) {
        std::string it = trim(items[k]);
        if (it.empty()) continue;
        std::istringstream in(it);
        std::vector<std::string> w;
        std::string x;
        while (in >> x) w.push_back(x);
        const std::string kind = lower(w[0]);
        std::vector<double> a;
        auto nums = [&](size_t from, size_t most) {
            for (size_t i = from; i < w.size(); i++) {
                double v;
                if (!number(w[i], v)) { *why = "(" + it + "): \"" + w[i] + "\" is not a number"; return false; }
                a.push_back(v);
            }
            if (a.size() > most) { *why = "(" + it + "): too many numbers"; return false; }
            return true;
        };
        Gesture g;
        if (kind == "vib" || kind == "vibrato") {
            if (!nums(1, 3)) return false;
            g.kind = Gesture::Vibrato;
            g.a = float(a.size() > 0 ? a[0] : 0.3); g.b = float(a.size() > 1 ? a[1] : 5.5); g.c = float(a.size() > 2 ? a[2] : 0.2);
            if (g.a < 0 || g.a > 3 || g.b <= 0 || g.b > 20 || g.c < 0 || g.c >= 1) { *why = "(" + it + "): vib DEPTH(0..3) RATE(Hz) FROM(0..1)"; return false; }
        } else if (kind == "bend") {
            if (w.size() < 2) { *why = "(bend): bend TO [FROM UNTIL], TO as +2, -1 or a note"; return false; }
            g.kind = Gesture::Bend;
            double to;
            if ((w[1][0] == '+' || w[1][0] == '-' || std::isdigit(static_cast<unsigned char>(w[1][0]))) && number(w[1], to)) g.a = float(to);
            else {
                float p;
                if (!pitch(w[1], key, p, why)) { *why = "(" + it + "): " + *why; return false; }
                g.a = p; g.absolute = true;
            }
            if (!nums(2, 2)) return false;
            g.b = float(a.size() > 0 ? a[0] : 0.15);
            g.c = float(a.size() > 1 ? a[1] : std::min(1.0, g.b + 0.35));
            if (g.b < 0 || g.c > 1 || g.c <= g.b) { *why = "(" + it + "): FROM and UNTIL are parts of the note, 0 <= FROM < UNTIL <= 1"; return false; }
        } else if (kind == "swell") {
            if (!nums(1, 3) || a.size() < 2) { if (why->empty()) *why = "(" + it + "): swell FROM TO [END], pressures 0..1"; return false; }
            g.kind = Gesture::Swell;
            g.a = float(a[0]); g.b = float(a[1]); g.c = float(a.size() > 2 ? a[2] : -1);
        } else if (kind == "press") {
            if (!nums(1, kMaxPts) || a.empty()) { if (why->empty()) *why = "(" + it + "): press P [P...], 0..1"; return false; }
            g.kind = Gesture::Press; g.a = float(a[0]);
            for (double v : a) g.pts.push_back(float(std::clamp(v, 0.0, 1.0)));
        } else if (kind == "tone") {
            if (!nums(1, kMaxPts) || a.empty()) { if (why->empty()) *why = "(" + it + "): tone FROM [TO...], 0..1"; return false; }
            g.kind = Gesture::Tone; g.a = float(a[0]); g.b = float(a.size() > 1 ? a[1] : a[0]);
            for (double v : a) g.pts.push_back(float(std::clamp(v, 0.0, 1.0)));
        } else if (kind == "pitch") {
            if (!nums(1, kMaxPts) || a.empty()) { if (why->empty()) *why = "(" + it + "): pitch D [D...], semitones from the note"; return false; }
            g.kind = Gesture::Pitch;
            for (double v : a) {
                if (v < -24 || v > 24) { *why = "(" + it + "): pitch: semitones from the note, -24..24"; return false; }
                g.pts.push_back(float(v));
            }
        } else if (kind == "slide" || kind == "glide") {
            if (!nums(1, 1)) return false;
            g.kind = Gesture::Slide; g.a = float(a.empty() ? 0.25 : a[0]);
            if (g.a <= 0 || g.a > 1) { *why = "(" + it + "): slide [PART], the part of the note spent sliding, 0..1"; return false; }
        } else if (kind == "stacc" || kind == "staccato") {
            g.kind = Gesture::Staccato;
        } else if (kind == "say") {
            g.kind = Gesture::Say;
            g.text = trim(it.substr(it.find(w[0]) + w[0].size()));
            // The words run on to the end, commas and all.
            for (size_t j = k + 1; j < items.size(); j++) g.text += "," + items[j];
            g.text = trim(g.text);
            out.push_back(g);
            return true;
        } else if (kind == "at") {
            if (!nums(1, 2) || a.size() != 2) { if (why->empty()) *why = "(" + it + "): at X Y, 0..1 across and down"; return false; }
            g.kind = Gesture::At; g.a = float(a[0]); g.b = float(a[1]);
        } else if (kind == "part") {
            if (!nums(1, 1) || a.size() != 1) { if (why->empty()) *why = "(" + it + "): part N"; return false; }
            g.kind = Gesture::Part; g.a = float(a[0]);
        } else {
            *why = "(" + it + "): \"" + w[0] + "\" is not a gesture (vib, bend, swell, press, tone, pitch, slide, stacc, say, at, part)";
            return false;
        }
        out.push_back(g);
    }
    return true;
}

}  // namespace

int noteNumber(const std::string& s) {
    if (s.empty()) return -1;
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    char c = char(std::toupper(static_cast<unsigned char>(s[0])));
    if (c < 'A' || c > 'G') return -1;
    int n = base[c - 'A'];
    size_t i = 1;
    for (; i < s.size() && (s[i] == '#' || s[i] == 'b'); i++) n += s[i] == '#' ? 1 : -1;
    if (i >= s.size()) return -1;
    char* end = nullptr;
    long oct = std::strtol(s.c_str() + i, &end, 10);
    if (!end || *end || oct < -1 || oct > 9) return -1;
    int v = n + 12 * int(oct + 1);
    return v >= 0 && v <= 127 ? v : -1;
}

std::string noteName(float p) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int n = int(std::lround(p));
    std::string s = std::string(names[((n % 12) + 12) % 12]) + std::to_string(n / 12 - (n < 0 ? 1 : 0) - 1);
    float off = p - float(n);
    if (std::fabs(off) >= 0.005f) {
        char b[16];
        std::snprintf(b, sizeof b, "%+.2f", double(off));
        s += b;
    }
    return s;
}

double Score::ms(double beat) const {
    double t = 0, at = 0, bpm = tempo;
    for (const auto& tp : tempos) {
        if (tp.first >= beat) break;
        if (tp.first > at) { t += (tp.first - at) * 60000.0 / bpm; at = tp.first; }
        bpm = tp.second;
    }
    return t + (beat - at) * 60000.0 / bpm;
}

bool parseScore(const std::string& text, Score& out, std::vector<std::string>* errors, std::vector<std::string>* warnings,
                int firstLine, double defaultTempo, int meterBeats, int meterUnit, const std::string& keyText) {
    Score sc;
    if (defaultTempo > 0) sc.tempo = defaultTempo;
    if (meterBeats > 0 && meterUnit > 0) { sc.meterBeats = meterBeats; sc.meterUnit = meterUnit; }
    sc.tempos.push_back({0, sc.tempo});
    Key key;
    std::string why;
    if (!keyText.empty() && !parseKey(keyText, key, &why) && warnings) warnings->push_back("key: " + why);
    struct Voice { double at = 0; bool any = false; int part = -1; float press = -1, tone = -1; };
    std::map<std::string, Voice> voices;
    std::string vname = "main";
    bool ok = true;
    auto err = [&](int line, const std::string& what) { ok = false; if (errors) errors->push_back("line " + std::to_string(line) + ": " + what); };
    auto warn = [&](int line, const std::string& what) { if (warnings) warnings->push_back("line " + std::to_string(line) + ": " + what); };

    std::istringstream in(text);
    std::string raw;
    int line = firstLine - 1;
    while (std::getline(in, raw)) {
        line++;
        // Comments: "#" at the start, or " # ".
        std::string l = trim(raw);
        if (!l.empty() && l[0] == '#') continue;
        size_t c = l.find(" # ");
        if (c != std::string::npos) l = trim(l.substr(0, c));
        if (l.size() >= 2 && l.compare(l.size() - 2, 2, " #") == 0) l = trim(l.substr(0, l.size() - 2));
        if (l.empty()) continue;
        Voice& v = voices[vname];
        std::istringstream ws(l);
        std::string cmd;
        ws >> cmd;
        std::string rest = trim(l.substr(cmd.size()));
        std::string lc = lower(cmd);
        double d = 0;
        if (lc == "tempo") {
            if (!number(rest, d) || d < 10 || d > 600) { err(line, "tempo: beats (quarter notes) a minute, 10 to 600"); continue; }
            if (v.at <= 0 && sc.tempos.size() == 1) { sc.tempo = d; sc.tempos[0].second = d; }
            else sc.tempos.push_back({v.at, d});
        } else if (lc == "meter") {
            size_t sl = rest.find('/');
            double a = 0, b = 0;
            if (sl == std::string::npos || !number(trim(rest.substr(0, sl)), a) || !number(trim(rest.substr(sl + 1)), b) || a < 1 || a > 32 ||
                (b != 1 && b != 2 && b != 4 && b != 8 && b != 16)) { err(line, "meter: as 4/4, 3/4 or 6/8"); continue; }
            sc.meterBeats = int(a); sc.meterUnit = int(b);
        } else if (lc == "key") {
            if (!parseKey(rest, key, &why)) err(line, why);
        } else if (lc == "voice") {
            if (rest.empty() || rest.find(' ') != std::string::npos) { err(line, "voice: one word, its name"); continue; }
            vname = rest;
        } else if (lc == "part") {
            if (!number(rest, d) || d < 0 || d > 3) { err(line, "part: 0 to 3 (a split controller's second part is 1)"); continue; }
            v.part = int(d);
        } else if (lc == "press") {
            if (!number(rest, d) || d < 0 || d > 1) { err(line, "press: 0 to 1"); continue; }
            v.press = float(d);
        } else if (lc == "tone") {
            if (!number(rest, d) || d < 0 || d > 1) { err(line, "tone: 0 to 1"); continue; }
            v.tone = float(d);
        } else if (lc == "bar") {
            std::istringstream bs(rest);
            std::string a, b;
            bs >> a >> b;
            double bar, beat = 1;
            if (!number(a, bar) || bar != std::floor(bar) || bar < 0 || (!b.empty() && (!duration(b, beat) || beat < 1))) {
                err(line, "bar: its number (from 1; 0 for a pick-up), and a beat in it if not the first (bar 5 3)");
                continue;
            }
            double target = (bar - 1) * sc.barBeats() + (beat - 1);
            if (v.any && std::fabs(target - v.at) > 1e-6) {
                char b2[200];
                double diff = v.at - target;
                std::snprintf(b2, sizeof b2, "bar %g%s: voice %s is at beat %g of the piece, %g %s; it goes on from the bar",
                              bar, b.empty() ? "" : (" beat " + b).c_str(), vname.c_str(), v.at, std::fabs(diff),
                              diff > 0 ? "beats past it (too much in the bar before?)" : "beats short of it (a missing note or rest?)");
                warn(line, b2);
            }
            v.at = target;
        } else if (lc == "section") {
            if (rest.empty()) { err(line, "section: its name"); continue; }
            sc.sections.push_back({v.at, rest});
        } else if (lc == "say") {
            sc.says.push_back({v.at, rest});
        } else {
            // Notes: PITCH[+PITCH...]:DURATION[(GESTURES)] ...
            std::vector<std::string> tk = tokens(l, &why);
            if (tk.empty()) { err(line, why); continue; }
            for (const std::string& t : tk) {
                size_t colon = t.find(':');
                size_t paren = t.find('(');
                if (colon == std::string::npos || (paren != std::string::npos && paren < colon)) {
                    err(line, "\"" + t + "\": a note is PITCH:BEATS (D4:1, ^3:1/2, r:2, D4+F4+A4:2), and \"" + cmd +
                              "\" is not a word a score knows (tempo, meter, key, voice, part, press, tone, bar, section, say)");
                    break;
                }
                std::string ps = t.substr(0, colon);
                std::string ds = t.substr(colon + 1, paren == std::string::npos ? std::string::npos : paren - colon - 1);
                double beats;
                if (!duration(ds, beats)) { err(line, "\"" + t + "\": its length in beats (1, 0.5, 1/3, 3/2, 1+1/2) is wrong"); break; }
                ScoreNote n;
                n.beat = v.at; n.beats = beats; n.voice = vname; n.line = line;
                bool rest = lower(ps) == "r" || lower(ps) == "rest";
                if (!rest) {
                    size_t a = 0;
                    bool good = true;
                    while (a <= ps.size()) {
                        size_t b = ps.find('+', a);
                        // "^+2" is a degree, not a chord's join.
                        if (b != std::string::npos && b == a + 1 && ps[a] == '^') b = ps.find('+', b + 1);
                        float p;
                        if (!pitch(ps.substr(a, b == std::string::npos ? std::string::npos : b - a), key, p, &why)) { err(line, why); good = false; break; }
                        n.pitches.push_back(p);
                        if (b == std::string::npos) break;
                        a = b + 1;
                    }
                    if (!good) break;
                }
                if (paren != std::string::npos) {
                    if (t.back() != ')') { err(line, "\"" + t + "\": the gestures' \")\" should end it"); break; }
                    if (!gestures(t.substr(paren + 1, t.size() - paren - 2), key, n.gestures, &why)) { err(line, why); break; }
                }
                if (v.part >= 0 && std::none_of(n.gestures.begin(), n.gestures.end(), [](const Gesture& g) { return g.kind == Gesture::Part; })) {
                    Gesture g; g.kind = Gesture::Part; g.a = float(v.part); n.gestures.push_back(g);
                }
                if (v.press >= 0 && std::none_of(n.gestures.begin(), n.gestures.end(), [](const Gesture& g) { return g.kind == Gesture::Press || g.kind == Gesture::Swell; })) {
                    Gesture g; g.kind = Gesture::Press; g.a = v.press; n.gestures.push_back(g);
                }
                if (v.tone >= 0 && std::none_of(n.gestures.begin(), n.gestures.end(), [](const Gesture& g) { return g.kind == Gesture::Tone; })) {
                    Gesture g; g.kind = Gesture::Tone; g.a = g.b = v.tone; n.gestures.push_back(g);
                }
                v.at += beats;
                v.any = true;
                if (!rest) sc.notes.push_back(std::move(n));
            }
        }
    }
    // A pick-up (bar 0) starts the piece: everything moves on by it.
    double first = 0;
    for (const auto& n : sc.notes) first = std::min(first, n.beat);
    for (const auto& s : sc.sections) first = std::min(first, s.first);
    for (const auto& s : sc.says) first = std::min(first, s.first);
    if (first < 0) {
        for (auto& n : sc.notes) n.beat -= first;
        for (auto& s : sc.sections) s.first -= first;
        for (auto& s : sc.says) s.first -= first;
        for (size_t k = 1; k < sc.tempos.size(); k++) sc.tempos[k].first -= first;
    }
    for (const auto& kv : voices) sc.end = std::max(sc.end, kv.second.at - first);
    auto byBeat = [](const ScoreNote& a, const ScoreNote& b) { return a.beat < b.beat; };
    std::stable_sort(sc.notes.begin(), sc.notes.end(), byBeat);
    std::stable_sort(sc.tempos.begin(), sc.tempos.end());
    std::stable_sort(sc.sections.begin(), sc.sections.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::stable_sort(sc.says.begin(), sc.says.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (sc.notes.empty() && ok) err(firstLine, "the score has no notes");
    out = std::move(sc);
    return ok;
}

// ---- the layout ----------------------------------------------------------------------------------

// A copy of the controller, on a clock of its own, that tells what a touch starts.
struct Layout::Sim {
    std::unique_ptr<ControllerHost> host;
    double clock = 0;
    bool listening = false, heard = false;
    Note got;
    std::vector<double> values;
};

Layout::Layout() = default;
Layout::~Layout() = default;
Layout::Layout(Layout&&) noexcept = default;
Layout& Layout::operator=(Layout&&) noexcept = default;

bool Layout::probe(float x, float y, float force, Note* out) {
    if (!sim_) return false;
    Sim& s = *sim_;
    ControllerHost& h = *s.host;
    s.clock += 1000;                     // far apart: no two touches are a double tap
    s.heard = false;
    s.listening = true;
    h.touch(7, ControllerHost::Down, x, y, 0, force);
    if (!s.heard) { s.clock += 1; h.tick(); }
    s.listening = false;
    s.clock += 30;
    h.touch(7, ControllerHost::Up, x, y, 0, 0);
    // A button on its surface may have changed one of its settings: put it back.
    for (const auto& c : h.takeSettingChanges())
        if (size_t(c.index) < s.values.size()) h.setting(c.index, s.values[size_t(c.index)]);
    if (s.heard && out) *out = s.got;
    return s.heard;
}

bool Layout::scan(const Package& pkg, const std::vector<double>& values, float width, float height, const float centers[2],
                  std::string* error) {
    sim_ = std::make_unique<Sim>();
    cells_.clear();
    responses_.clear();
    w_ = width; h_ = height;
    Sim& s = *sim_;
    s.host = std::make_unique<ControllerHost>(nullptr);
    Sim* sp = &s;
    s.host->setClock([sp] { return sp->clock; });
    if (!s.host->load(pkg, error)) { sim_.reset(); return false; }
    ControllerHost::Taps taps;
    taps.note = [sp](int kind, int voice, int part, float pitch, float pressure, float slide, float velocity, double) {
        if (!sp->listening || sp->heard || kind != On) return;
        sp->heard = true;
        sp->got.voice = voice; sp->got.part = part; sp->got.pitch = pitch; sp->got.pressure = pressure; sp->got.slide = slide;
        sp->got.velocity = velocity;
    };
    s.host->setTaps(taps);
    s.values = settingsFor(Take(), 0, pkg.manifest, values);
    for (size_t i = 0; i < s.values.size(); i++) s.host->setting(int(i), s.values[i]);
    s.clock = 1000;
    s.host->init(width, height, 0);
    s.host->instrument(0, centers[0]);
    s.host->instrument(1, centers[1]);
    s.host->tick();

    // Touch it all over, a few dp apart.
    const float step = std::max(4.f, std::min(width, height) / 40.f);
    const int nx = std::max(1, int(width / step)), ny = std::max(1, int(height / step));
    const float sx = width / float(nx), sy = height / float(ny);
    struct P { int note = -1, part = 0; float pitch = 0; };
    std::vector<P> grid(size_t(nx * ny));
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            Note n;
            if (!probe((float(i) + 0.5f) * sx, (float(j) + 0.5f) * sy, 0.7f, &n)) continue;
            P& p = grid[size_t(j * nx + i)];
            p.note = int(std::lround(n.pitch));
            p.part = n.part;
            p.pitch = n.pitch;
        }
    // Each patch of touches starting the same note is a cell.
    std::vector<int> seen(grid.size(), 0);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            size_t k0 = size_t(j * nx + i);
            if (grid[k0].note < 0 || seen[k0]) continue;
            std::vector<size_t> todo{k0}, mine;
            seen[k0] = 1;
            while (!todo.empty()) {
                size_t k = todo.back();
                todo.pop_back();
                mine.push_back(k);
                int ci = int(k) % nx, cj = int(k) / nx;
                const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; d++) {
                    int a = ci + di[d], b = cj + dj[d];
                    if (a < 0 || b < 0 || a >= nx || b >= ny) continue;
                    size_t q = size_t(b * nx + a);
                    if (seen[q] || grid[q].note != grid[k0].note || grid[q].part != grid[k0].part) continue;
                    seen[q] = 1;
                    todo.push_back(q);
                }
            }
            Cell c;
            c.note = grid[k0].note;
            c.part = grid[k0].part;
            c.probes = int(mine.size());
            c.x0 = width; c.y0 = height; c.x1 = 0; c.y1 = 0;
            float best = 1e9f;
            for (size_t k : mine) best = std::min(best, std::fabs(grid[k].pitch - float(c.note)));
            // Its middle: where it plays the note truest (all of a square key; the middle of a strip).
            double mx = 0, my = 0;
            int m = 0;
            for (size_t k : mine) {
                float x = (float(int(k) % nx) + 0.5f) * sx, y = (float(int(k) / nx) + 0.5f) * sy;
                c.x0 = std::min(c.x0, x - sx / 2); c.x1 = std::max(c.x1, x + sx / 2);
                c.y0 = std::min(c.y0, y - sy / 2); c.y1 = std::max(c.y1, y + sy / 2);
                if (std::fabs(grid[k].pitch - float(c.note)) <= best + 0.08f) { mx += x; my += y; m++; }
            }
            c.x = float(mx / m); c.y = float(my / m);
            // Where the pitch moves with the finger across a key (a touch pad gliding freely), the
            // middle found on the probe grid can be a little off the note: find where it is exact.
            Note at;
            if (probe(c.x, c.y, 0.7f, &at) && std::fabs(at.pitch - float(c.note)) > 0.005f) {
                float a = c.x0, b = c.x1;
                Note na, nb;
                if (probe(a, c.y, 0.7f, &na) && probe(b, c.y, 0.7f, &nb) &&
                    (na.pitch - float(c.note)) * (nb.pitch - float(c.note)) < 0) {
                    const bool rising = nb.pitch > na.pitch;
                    for (int it = 0; it < 14; it++) {
                        float mid = (a + b) / 2;
                        Note nm;
                        if (!probe(mid, c.y, 0.7f, &nm)) break;
                        if ((nm.pitch < float(c.note)) == rising) a = mid; else b = mid;
                    }
                    c.x = (a + b) / 2;
                }
            }
            cells_.push_back(c);
        }
    responses_.assign(cells_.size(), Response());
    if (cells_.empty() && error) *error = "no notes found on " + pkg.name() + " at this size";
    return !cells_.empty();
}

const Layout::Cell* Layout::find(int note, float x, float y, int part) const {
    if (x < 0 || y < 0) { x = w_ / 2; y = h_ / 2; }
    int most = 0;
    for (const Cell& c : cells_) if (c.note == note && (part < 0 || c.part == part)) most = std::max(most, c.probes);
    const Cell* best = nullptr;
    float bestCost = 1e30f;
    for (const Cell& c : cells_) {
        if (c.note != note || (part < 0 ? false : c.part != part)) continue;
        float d = std::hypot(c.x - x, c.y - y);
        if (c.probes * 2 < most) d += std::min(w_, h_) * 0.5f;   // cut by an edge or a button: a whole one is better
        if (d < bestCost) { bestCost = d; best = &c; }
    }
    return best;
}

Layout::Response Layout::response(const Cell& c) {
    size_t k = size_t(&c - cells_.data());
    if (k >= cells_.size()) return Response();
    Response& r = responses_[k];
    if (r.known) return r;
    r.known = true;
    // Probes over the middle of the cell: how pressure and slide follow the place.
    float hw = (c.x1 - c.x0) / 2, hh = (c.y1 - c.y0) / 2;
    double sx = 0, sy = 0, sp = 0, ss = 0, sxx = 0, syy = 0, sxy = 0, sxp = 0, syp = 0, sxs = 0, sys = 0;
    int n = 0;
    const float f[5] = {-0.6f, -0.3f, 0, 0.3f, 0.6f};
    for (float fy : f)
        for (float fx : f) {
            Note got;
            float dx = fx * hw, dy = fy * hh;
            if (!probe(c.x + dx, c.y + dy, 0.7f, &got) || int(std::lround(got.pitch)) != c.note) continue;
            n++;
            sx += dx; sy += dy; sp += got.pressure; ss += got.slide;
            sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
            sxp += dx * got.pressure; syp += dy * got.pressure; sxs += dx * got.slide; sys += dy * got.slide;
        }
    if (n < 3) {
        Note got;
        if (probe(c.x, c.y, 0.7f, &got)) { r.p0 = got.pressure; r.s0 = got.slide; }
    } else {
        // Least squares: v = v0 + a dx + b dy.
        double mx = sx / n, my = sy / n, mp = sp / n, ms = ss / n;
        double cxx = sxx / n - mx * mx, cyy = syy / n - my * my, cxy = sxy / n - mx * my;
        double cxp = sxp / n - mx * mp, cyp = syp / n - my * mp, cxs = sxs / n - mx * ms, cys = sys / n - my * ms;
        double det = cxx * cyy - cxy * cxy;
        if (std::fabs(det) > 1e-9) {
            r.pdx = float((cxp * cyy - cyp * cxy) / det); r.pdy = float((cyp * cxx - cxp * cxy) / det);
            r.sdx = float((cxs * cyy - cys * cxy) / det); r.sdy = float((cys * cxx - cxs * cxy) / det);
        }
        r.p0 = float(mp - r.pdx * mx - r.pdy * my);
        r.s0 = float(ms - r.sdx * mx - r.sdy * my);
    }
    Note lo, hi;
    if (probe(c.x, c.y, 0.2f, &lo) && probe(c.x, c.y, 1.0f, &hi)) r.force = std::fabs(hi.pressure - lo.pressure) > 0.05f;
    return r;
}

// ---- a score to touches --------------------------------------------------------------------------

namespace {

float smooth(float u) { u = std::clamp(u, 0.f, 1.f); return u * u * (3 - 2 * u); }

// A curve's value a part u (0..1) of the way through: its points evenly from start to end.
float along(const std::vector<float>& pts, float u, float otherwise) {
    if (pts.empty()) return otherwise;
    if (pts.size() == 1) return pts[0];
    float q = std::clamp(u, 0.f, 1.f) * float(pts.size() - 1);
    size_t j = std::min(size_t(q), pts.size() - 2);
    return pts[j] + (pts[j + 1] - pts[j]) * (q - float(j));
}

// Where in a cell to put the finger for this pressure and tone (either < 0: don't care).
void offsetFor(const Layout::Response& r, float press, float tone, float hw, float hh, float* dx, float* dy) {
    *dx = *dy = 0;
    // Rows: what the place should change.
    float a[2][2], b[2];
    int n = 0;
    if (press >= 0 && !r.force && std::hypot(r.pdx, r.pdy) * std::max(hw, hh) > 0.02f) { a[n][0] = r.pdx; a[n][1] = r.pdy; b[n] = press - r.p0; n++; }
    if (tone >= 0 && std::hypot(r.sdx, r.sdy) * std::max(hw, hh) > 0.02f) { a[n][0] = r.sdx; a[n][1] = r.sdy; b[n] = tone - r.s0; n++; }
    if (n == 2) {
        float det = a[0][0] * a[1][1] - a[0][1] * a[1][0];
        float norm = std::hypot(a[0][0], a[0][1]) * std::hypot(a[1][0], a[1][1]);
        if (std::fabs(det) > 0.2f * norm) {
            *dx = (b[0] * a[1][1] - b[1] * a[0][1]) / det;
            *dy = (a[0][0] * b[1] - a[1][0] * b[0]) / det;
            n = 0;
        } else n = 1;                    // the two move together: pressure wins
    }
    if (n == 1) {
        float g = a[0][0] * a[0][0] + a[0][1] * a[0][1];
        *dx = a[0][0] * b[0] / g;
        *dy = a[0][1] * b[0] / g;
    }
    *dx = std::clamp(*dx, -0.8f * hw, 0.8f * hw);
    *dy = std::clamp(*dy, -0.8f * hh, 0.8f * hh);
}

}  // namespace

bool expand(const Score& score, Layout& layout, Take& out, Report* report) {
    Report local;
    Report& rep = report ? *report : local;
    out.touches.clear();
    out.sections.clear();
    out.says.clear();
    const float W = layout.width(), H = layout.height();
    if (W <= 0 || H <= 0 || layout.cells().empty()) { rep.errors.push_back("the controller has no notes to play here"); return false; }

    // First, the cell for each note's each pitch: near where that voice's finger was.
    struct Place { const Layout::Cell* cell = nullptr; float x = 0, y = 0; };
    std::vector<std::vector<Place>> places(score.notes.size());
    std::map<std::string, std::pair<float, float>> last;
    auto placeOf = [&](float pitch, float nx, float ny, int part, Place& p) {
        int n0 = int(std::floor(pitch + 1e-4f));
        float frac = pitch - float(n0);
        const Layout::Cell* c = layout.find(n0, nx, ny, part);
        if (!c) return false;
        p.cell = c; p.x = c->x; p.y = c->y;
        if (frac > 0.01f) {                   // between two notes
            if (const Layout::Cell* d = layout.find(n0 + 1, c->x, c->y, part)) { p.x += (d->x - c->x) * frac; p.y += (d->y - c->y) * frac; }
        }
        return true;
    };
    for (size_t i = 0; i < score.notes.size(); i++) {
        const ScoreNote& n = score.notes[i];
        int part = -1;
        float atX = -1, atY = -1;
        for (const Gesture& g : n.gestures) {
            if (g.kind == Gesture::Part) part = int(g.a);
            if (g.kind == Gesture::At) { atX = g.a * W; atY = g.b * H; }
        }
        auto it = last.find(n.voice);
        float nx = it == last.end() ? -1 : it->second.first, ny = it == last.end() ? -1 : it->second.second;
        if (atX >= 0) { nx = atX; ny = atY; }
        places[i].resize(n.pitches.size());
        for (size_t k = 0; k < n.pitches.size(); k++) {
            Place& p = places[i][k];
            if (!placeOf(n.pitches[k], nx, ny, part, p)) {
                rep.errors.push_back("line " + std::to_string(n.line) + ": " + noteName(n.pitches[k]) +
                                     " is not on the controller as it is set (its notes, key and scale, or its octave): left out");
                continue;
            }
            if (atX >= 0 && k == 0) { p.x = atX; p.y = atY; }
            if (k == 0) { nx = p.x; ny = p.y; }   // a chord's others near its first
        }
        for (const Place& p : places[i]) if (p.cell) { last[n.voice] = {p.x, p.y}; break; }
    }

    // Then each note's touches, a finger each pitch.
    std::map<std::string, std::vector<size_t>> byVoice;
    for (size_t i = 0; i < score.notes.size(); i++) byVoice[score.notes[i].voice].push_back(i);
    std::map<size_t, size_t> nextOf;           // note -> the next note of its voice
    for (auto& kv : byVoice) for (size_t k = 0; k + 1 < kv.second.size(); k++) nextOf[kv.second[k]] = kv.second[k + 1];
    std::map<size_t, int> carried;             // note -> the finger sliding into it
    int nextFinger = 1;
    const double step = 10;                    // ms between moves
    auto emit = [&](double t, int finger, int phase, float x, float y, float force) {
        Touch e;
        e.t = std::round(t * 1000) / 1000; e.side = 0; e.finger = finger; e.phase = phase;
        e.x = std::clamp(x, 0.f, W) / W; e.y = std::clamp(y, 0.f, H) / H; e.force = std::clamp(force, 0.f, 1.f);
        out.touches.push_back(e);
    };
    for (size_t i = 0; i < score.notes.size(); i++) {
        const ScoreNote& n = score.notes[i];
        const double t0 = score.ms(n.beat), t1 = score.ms(n.beat + n.beats), dur = t1 - t0;
        const Gesture *vib = nullptr, *swell = nullptr, *press = nullptr, *tone = nullptr, *slide = nullptr, *curve = nullptr;
        bool stacc = false;
        std::vector<const Gesture*> bends;
        for (const Gesture& g : n.gestures) {
            switch (g.kind) {
                case Gesture::Vibrato: vib = &g; break;
                case Gesture::Swell: swell = &g; break;
                case Gesture::Press: press = &g; break;
                case Gesture::Tone: tone = &g; break;
                case Gesture::Slide: slide = &g; break;
                case Gesture::Staccato: stacc = true; break;
                case Gesture::Bend: bends.push_back(&g); break;
                case Gesture::Pitch: curve = &g; break;
                case Gesture::Say: out.says.push_back({t0, dur, g.text}); break;
                default: break;
            }
        }
        auto nx = nextOf.find(i);
        bool slides = slide && nx != nextOf.end() && !places[nx->second].empty() && places[nx->second][0].cell;
        for (size_t k = 0; k < n.pitches.size(); k++) {
            const Place& p = places[i][k];
            if (!p.cell) continue;
            Report::Played pl;
            pl.line = n.line; pl.wanted = n.pitches[k]; pl.t = t0; pl.end = t1;
            if (curve) { pl.curved = true; pl.slack = 1e9f; for (float d : curve->pts) pl.slack = std::min(pl.slack, std::fabs(d)); }
            rep.played.push_back(pl);
            const Layout::Cell& c = *p.cell;
            Layout::Response r = layout.response(c);
            float hw = (c.x1 - c.x0) / 2, hh = (c.y1 - c.y0) / 2;
            // Pressure and tone through the note (u 0..1); < 0: the controller's own.
            auto pressAt = [&](float u) -> float {
                if (swell) {
                    if (swell->c >= 0) return u < 0.5f ? swell->a + (swell->b - swell->a) * u * 2 : swell->b + (swell->c - swell->b) * (u - 0.5f) * 2;
                    return swell->a + (swell->b - swell->a) * u;
                }
                return press ? along(press->pts, u, press->a) : -1.f;
            };
            auto toneAt = [&](float u) -> float {
                if (!tone) return -1.f;
                return tone->pts.size() > 2 ? along(tone->pts, u, tone->a) : tone->a + (tone->b - tone->a) * u;
            };
            // A pitch curve: where each of its points is, the finger going between them.
            std::vector<std::pair<float, float>> curvePlaces;
            if (curve) {
                for (float d : curve->pts) {
                    Place q;
                    if (std::fabs(d) < 1e-4f) q = p;
                    else if (!placeOf(n.pitches[k] + d, p.x, p.y, c.part, q)) {
                        rep.warnings.push_back("line " + std::to_string(n.line) + ": the pitch curve goes to " + noteName(n.pitches[k] + d) +
                                               ", which has no place: kept to the note");
                        q = p;
                    }
                    curvePlaces.push_back({q.x - p.x, q.y - p.y});
                }
            }
            // Vibrato: along the way to the next note up, as far as its depth.
            float vdx = 1, vdy = 0, perSemi = std::max(hw, hh) * 2;
            if (const Layout::Cell* up = layout.find(c.note + 1, c.x, c.y, c.part)) {
                float d = std::hypot(up->x - c.x, up->y - c.y);
                if (d > 1) { vdx = (up->x - c.x) / d; vdy = (up->y - c.y) / d; perSemi = d; }
            }
            float reach = std::fabs(vdx) * hw + std::fabs(vdy) * hh;
            // The bends' targets.
            std::vector<std::pair<float, float>> targets;
            for (const Gesture* g : bends) {
                float to = g->absolute ? g->a : n.pitches[k] + g->a;
                Place q;
                if (std::fabs(to - n.pitches[k]) < 1e-4f) q = p;
                else if (!placeOf(to, p.x, p.y, c.part, q)) {
                    rep.warnings.push_back("line " + std::to_string(n.line) + ": the bend to " + noteName(to) + " has no place to go: left out");
                    q = p;
                }
                targets.push_back({q.x, q.y});
            }
            auto at = [&](double t, float* x, float* y, float* force) {
                float u = dur > 0 ? float((t - t0) / dur) : 0;
                float pr = pressAt(u), tn = toneAt(u);
                float dx, dy;
                offsetFor(r, pr, tn, hw, hh, &dx, &dy);
                float bx = p.x, by = p.y;
                if (!curvePlaces.empty()) {
                    float q = std::clamp(u, 0.f, 1.f) * float(curvePlaces.size() - 1);
                    size_t j = std::min(size_t(q), curvePlaces.size() - 1), j2 = std::min(j + 1, curvePlaces.size() - 1);
                    float fr = q - float(j);
                    bx += curvePlaces[j].first + (curvePlaces[j2].first - curvePlaces[j].first) * fr;
                    by += curvePlaces[j].second + (curvePlaces[j2].second - curvePlaces[j].second) * fr;
                }
                for (size_t b = 0; b < bends.size(); b++) {
                    float s = smooth((u - bends[b]->b) / (bends[b]->c - bends[b]->b));
                    bx += (targets[b].first - bx) * s;
                    by += (targets[b].second - by) * s;
                }
                *x = bx + dx; *y = by + dy;
                if (vib && u >= vib->c) {
                    double since = (t - (t0 + vib->c * dur)) / 1000.0;
                    float env = smooth(float(since / 0.25));
                    float amp = std::min(vib->a * perSemi, reach * 0.9f) * env;
                    float o = amp * float(std::sin(2 * M_PI * vib->b * since));
                    *x += vdx * o; *y += vdy * o;
                }
                *force = pr >= 0 ? pr : 0.7f;
            };
            bool moving = vib || swell || tone || curve || (press && press->pts.size() > 1) || !bends.empty() || (slides && k == 0);
            double lift = stacc ? t0 + dur * 0.5 : t1 - std::min(25.0, dur * 0.25);
            int finger;
            auto carry = carried.find(i);
            bool continuing = k == 0 && carry != carried.end();
            float x, y, f;
            if (continuing) finger = carry->second;
            else {
                finger = nextFinger++;
                at(t0, &x, &y, &f);
                emit(t0, finger, Down, x, y, f);
            }
            if (k == 0 && slides) {
                // Glide into the next note without lifting.
                const Place& q = places[nx->second][0];
                double g0 = t1 - dur * double(slide->a);
                float gx = 0, gy = 0, gf = 0;
                for (double t = continuing ? t0 : t0 + step; t < g0; t += step) { at(t, &x, &y, &f); emit(t, finger, Move, x, y, f); }
                at(g0, &gx, &gy, &gf);
                // There a little before the next note's time: a controller may follow a moment late.
                const double g1 = t1 - std::min(40.0, (t1 - g0) * 0.3);
                for (double t = g0; t < t1; t += step) {
                    float s = smooth(float((t - g0) / (g1 - g0)));
                    emit(t, finger, Move, gx + (q.x - gx) * s, gy + (q.y - gy) * s, gf);
                }
                carried[nx->second] = finger;
                continue;
            }
            if (moving || continuing)
                for (double t = continuing ? t0 : t0 + step; t < lift; t += step) { at(t, &x, &y, &f); emit(t, finger, Move, x, y, f); }
            at(lift, &x, &y, &f);
            emit(lift, finger, Up, x, y, 0);
        }
    }
    // In time order; at the same time, a finger lifts before another goes down.
    auto rank = [](int ph) { return ph == Up || ph == Cancel ? 0 : ph == Down ? 1 : 2; };
    std::stable_sort(out.touches.begin(), out.touches.end(), [&](const Touch& a, const Touch& b) {
        return a.t < b.t || (a.t == b.t && rank(a.phase) < rank(b.phase));
    });
    double end = score.ms(score.end);
    for (const auto& s : score.sections) out.sections.push_back({score.ms(s.first), s.second});
    // Words: until the next words, or the next section, or the end.
    for (size_t k = 0; k < score.says.size(); k++) {
        double t = score.ms(score.says[k].first), until = end;
        if (k + 1 < score.says.size()) until = std::min(until, score.ms(score.says[k + 1].first));
        for (const auto& s : score.sections) if (s.first > score.says[k].first) { until = std::min(until, score.ms(s.first)); break; }
        if (until > t) out.says.push_back({t, until - t, score.says[k].second});
    }
    std::stable_sort(out.says.begin(), out.says.end(), [](const Say& a, const Say& b) { return a.t < b.t; });
    return rep.errors.empty();
}

bool render(Take& t, const Package& pkg, const std::vector<double>& values, float width, float height, const float centers[2],
            std::string* error) {
    double clock = 0;
    ControllerHost host(nullptr);
    host.setClock([&clock] { return clock; });
    if (!host.load(pkg, error)) return false;
    std::vector<Note> notes;
    Unchanged unchanged;                           // as the recorder: what says nothing new is left out
    ControllerHost::Taps taps;
    taps.note = [&](int kind, int voice, int part, float pitch, float pressure, float slide, float velocity, double tm) {
        if (!unchanged.keep(voice, kind, pitch, pressure, slide)) return;
        notes.push_back({std::round(std::max(0.0, tm) * 1000) / 1000, 0, voice, kind, part, pitch, pressure, slide, velocity});
    };
    host.setTaps(taps);
    std::vector<double> v = settingsFor(t, 0, pkg.manifest, values);
    for (size_t i = 0; i < v.size(); i++) host.setting(int(i), v[i]);
    host.init(width, height, 2);             // no one sees it: it need not draw
    host.instrument(0, centers[0]);
    host.instrument(1, centers[1]);
    // Its touches and settings, in time, with a frame every 1/120 s between.
    const double frame = 1000.0 / 120;
    double nextTick = 0;
    size_t s = 0;
    const Json& st = pkg.manifest["settings"];
    auto until = [&](double tm) {
        while (nextTick <= tm) { clock = nextTick; host.tick(); nextTick += frame; }
        clock = tm;
        while (s < t.settings.size() && t.settings[s].t <= tm) {
            const Setting& e = t.settings[s++];
            if (e.side != 0) continue;
            for (size_t i = 0; i < st.size(); i++) if (st[i]["id"].str() == e.id) host.setting(int(i), e.value);
        }
    };
    for (const Touch& e : t.touches) {
        if (e.side != 0) continue;
        until(e.t);
        host.touch(e.finger, ControllerHost::Phase(e.phase), e.x * width, e.y * height, e.size, e.force);
    }
    until(t.length() + 1000);
    host.stop();
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    t.notes.erase(std::remove_if(t.notes.begin(), t.notes.end(), [](const Note& n) { return n.side == 0; }), t.notes.end());
    t.notes.insert(t.notes.end(), notes.begin(), notes.end());
    std::stable_sort(t.notes.begin(), t.notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    return true;
}

bool prepare(Take& t, const Package& pkg, std::vector<double> values, float width, float height, const float centers[2],
             Report* report) {
    Report local;
    Report& rep = report ? *report : local;
    if (t.performed()) return true;
    Score sc;
    if (!parseScore(t.score, sc, &rep.errors, &rep.warnings, t.scoreLine > 0 ? t.scoreLine : 1, t.tempo, t.meterBeats, t.meterUnit, t.key))
        return false;
    if (t.sides.empty()) t.sides.resize(1);
    Side& side = t.sides[0];
    if (!side.controller.empty() && side.controller != pkg.id())
        rep.warnings.push_back("written for " + side.controller + ", played on " + pkg.id() + ": its settings for that one are left out");
    if (!side.controller.empty() && side.controller != pkg.id()) side.settings.clear();
    side.controller = pkg.id();
    side.controllerVersion = pkg.version();
    float c[2] = {centers[0], centers[1]};
    // The middles of the instruments playing now (not the take's): the layout must be the one on screen.
    std::vector<double> v = settingsFor(t, 0, pkg.manifest, std::move(values));
    Layout layout;
    std::string err;
    if (!layout.scan(pkg, v, width, height, c, &err)) { rep.errors.push_back(err); return false; }
    expand(sc, layout, t, &rep);
    if (t.touches.empty()) { rep.errors.push_back("none of its notes could be played"); return false; }
    if (!render(t, pkg, v, width, height, c, &err)) { rep.errors.push_back(err); return false; }
    // What was heard for each note: a moment after it starts (a slid-into note has arrived by
    // then), the sounding voice nearest the pitch wanted; then how far that voice went while held.
    std::vector<size_t> order(rep.played.size());
    for (size_t k = 0; k < order.size(); k++) order[k] = k;
    auto when = [&](const Report::Played& p) { return p.t + std::min(60.0, (p.end - p.t) * 0.3); };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return when(rep.played[a]) < when(rep.played[b]); });
    std::map<int, float> sounding;      // voice -> its pitch now
    size_t k = 0;
    for (size_t o : order) {
        Report::Played& p = rep.played[o];
        const double at = when(p);
        for (; k < t.notes.size() && t.notes[k].t <= at; k++) {
            const Note& n = t.notes[k];
            if (n.kind == On || (n.kind == Change && sounding.count(n.voice))) sounding[n.voice] = n.pitch;
            else if (n.kind == Off) sounding.erase(n.voice);
        }
        int voice = -1;
        for (const auto& s : sounding)
            if (voice < 0 || std::fabs(s.second - p.wanted) < std::fabs(sounding[voice] - p.wanted)) voice = s.first;
        if (voice < 0) continue;
        p.found = true;
        p.pitch = p.low = p.high = sounding[voice];
        for (size_t j = k; j < t.notes.size() && t.notes[j].t < p.end; j++) {
            const Note& n = t.notes[j];
            if (n.voice != voice) continue;
            if (n.kind == Off || n.kind == On) break;
            if (n.kind != Change) continue;
            p.low = std::min(p.low, n.pitch);
            p.high = std::max(p.high, n.pitch);
            // A controller that glides gets there a moment late: the nearest it came counts.
            if (std::fabs(n.pitch - p.wanted) < std::fabs(p.pitch - p.wanted)) p.pitch = n.pitch;
        }
    }
    // The set-up it was made with: the whole of it, so the file says how it was played.
    side.settings.clear();
    const Json& st = pkg.manifest["settings"];
    for (size_t i = 0; i < st.size() && i < v.size(); i++) side.settings.push_back({st[i]["id"].str(), v[i]});
    side.width = width; side.height = height;
    side.centers[0] = c[0]; side.centers[1] = c[1]; side.hasCenters = true;
    return true;
}

}  // namespace mpe::take

namespace mpe::take {

bool laidOutFor(const Take& t, const Json& manifest, const std::vector<double>& values, float width, float height) {
    if (t.sides.empty()) return true;
    const Side& s = t.sides[0];
    if (s.width > 0 && (std::fabs(s.width - width) > 1 || std::fabs(s.height - height) > 1)) return false;
    const Json& st = manifest["settings"];
    for (const auto& kv : s.settings)
        for (size_t i = 0; i < st.size() && i < values.size(); i++)
            if (st[i]["id"].str() == kv.first && std::fabs(values[i] - kv.second) > 1e-9) return false;
    return true;
}

namespace {

// A recorded take's side-0 fingers from its notes: each held note a finger on the key for it
// (near where the hand last was), moving with its bend, pressure and tone.
std::vector<Touch> fromNotes(const Take& t, Layout& layout) {
    std::vector<Touch> out;
    const float W = layout.width(), H = layout.height();
    struct Held { int finger; const Layout::Cell* cell; float x, y; };
    std::map<int, Held> held;              // voice -> its finger
    int nextFinger = 1;
    float lastX = -1, lastY = -1;
    auto placeFor = [&](const Layout::Cell& c, const Note& n, float* x, float* y) {
        float bx = c.x, by = c.y;
        float d = n.pitch - float(c.note);
        if (std::fabs(d) > 0.01f) {        // bent: toward the keys it bends to
            int step = d > 0 ? 1 : -1;
            const Layout::Cell* from = &c;
            float left = std::fabs(d);
            int k = c.note;
            while (left > 1e-3f) {
                const Layout::Cell* to = layout.find(k + step, from->x, from->y, c.part);
                if (!to) break;
                float f = std::min(1.f, left);
                bx = from->x + (to->x - from->x) * f;
                by = from->y + (to->y - from->y) * f;
                if (left <= 1) break;
                from = to; k += step; left -= 1;
            }
        }
        Layout::Response r = layout.response(c);
        float dx, dy;
        offsetFor(r, n.pressure, n.slide, (c.x1 - c.x0) / 2, (c.y1 - c.y0) / 2, &dx, &dy);
        *x = bx + dx; *y = by + dy;
    };
    auto emit = [&](double tm, int finger, int phase, float x, float y, float force) {
        Touch e;
        e.t = tm; e.side = 0; e.finger = finger; e.phase = phase;
        e.x = std::clamp(x, 0.f, W) / W; e.y = std::clamp(y, 0.f, H) / H; e.force = std::clamp(force, 0.f, 1.f);
        out.push_back(e);
    };
    std::vector<Note> notes;
    for (const Note& n : t.notes) if (n.side == 0) notes.push_back(n);
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    for (const Note& n : notes) {
        auto it = held.find(n.voice);
        if (n.kind == On) {
            if (it != held.end()) { emit(n.t, it->second.finger, Up, it->second.x, it->second.y, 0); held.erase(it); }
            int base = int(std::lround(n.pitch));
            const Layout::Cell* c = layout.find(base, lastX, lastY, n.part);
            if (!c) c = layout.find(base, lastX, lastY, -1);
            if (!c) continue;
            Held h{nextFinger++, c, 0, 0};
            placeFor(*c, n, &h.x, &h.y);
            emit(n.t, h.finger, Down, h.x, h.y, n.pressure);
            lastX = h.x; lastY = h.y;
            held[n.voice] = h;
        } else if (n.kind == Change && it != held.end()) {
            placeFor(*it->second.cell, n, &it->second.x, &it->second.y);
            emit(n.t, it->second.finger, Move, it->second.x, it->second.y, n.pressure);
        } else if (n.kind == Off && it != held.end()) {
            emit(n.t, it->second.finger, Up, it->second.x, it->second.y, 0);
            held.erase(it);
        }
    }
    for (auto& kv : held) emit(t.length(), kv.second.finger, Up, kv.second.x, kv.second.y, 0);
    return out;
}

}  // namespace

bool relayout(Take& t, const Package& pkg, const std::vector<double>& values, float width, float height, const float centers[2],
              Report* report) {
    Report local;
    Report& rep = report ? *report : local;
    Layout layout;
    std::string err;
    float c[2] = {centers[0], centers[1]};
    if (!layout.scan(pkg, values, width, height, c, &err)) { rep.errors.push_back(err); return false; }
    std::vector<Touch> fresh;
    Score sc;
    if (!t.score.empty() &&
        parseScore(t.score, sc, nullptr, nullptr, t.scoreLine > 0 ? t.scoreLine : 1, t.tempo, t.meterBeats, t.meterUnit, t.key)) {
        Take tmp;
        expand(sc, layout, tmp, &rep);
        fresh = std::move(tmp.touches);
    } else {
        fresh = fromNotes(t, layout);
    }
    t.touches.erase(std::remove_if(t.touches.begin(), t.touches.end(), [](const Touch& e) { return e.side == 0; }), t.touches.end());
    t.touches.insert(t.touches.end(), fresh.begin(), fresh.end());
    auto rank = [](int ph) { return ph == Up || ph == Cancel ? 0 : ph == Down ? 1 : 2; };
    std::stable_sort(t.touches.begin(), t.touches.end(), [&](const Touch& a, const Touch& b) {
        return a.t < b.t || (a.t == b.t && rank(a.phase) < rank(b.phase));
    });
    return true;
}

}  // namespace mpe::take
