// The rail's settings model (rail.h).
#include "mpe/rail.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mpe::rail {

namespace {

enum class Kind { Switch, Choice, Number };

Kind kind(const Json& s) {
    std::string t = s["type"].str();
    if (t == "switch") return Kind::Switch;
    if (t == "choice") return Kind::Choice;
    return Kind::Number;
}

struct Range { double min, max, step; };

// A setting's range. A choice: its options' indices; a switch: 0 and 1; a number: min (0), max (1),
// step (0 = continuous; a missing or negative one is 0).
Range range(const Json& s) {
    switch (kind(s)) {
    case Kind::Switch: return {0, 1, 1};
    case Kind::Choice: return {0, std::max(0.0, double(s["options"].size()) - 1), 1};
    default: break;
    }
    double lo = s["min"].num(0), hi = s["max"].num(1), step = s["step"].num(0);
    if (hi < lo) hi = lo;
    if (!(step > 0)) step = 0;
    return {lo, hi, step};
}

bool integral(double x) { return std::fabs(x - std::round(x)) < 1e-9; }

// x snapped to the step grid from lo, kept within [lo, hi].
double onGrid(const Range& r, double x) {
    x = std::clamp(x, r.min, r.max);
    if (r.step > 0) {
        double k = std::round((x - r.min) / r.step);
        x = r.min + k * r.step;
        if (x > r.max + 1e-9) x = r.min + (k - 1) * r.step;   // max off the grid: the last point below it
        x = std::clamp(x, r.min, r.max);
    }
    if (integral(x)) x = std::round(x);                       // 0.1 * 3 and the like
    return x == 0 ? 0 : x;                                    // never -0
}

// How many chips a number with a step has (its grid points up to max); 0 for a continuous one.
int gridCount(const Range& r) {
    if (r.step <= 0) return 0;
    return int(std::floor((r.max - r.min) / r.step + 1e-9)) + 1;
}

// The unit a setting's name ends in: "(%)" -> "%", "(dB)" -> " dB"; the length of that ending in *cut.
std::string unit(const std::string& name, size_t* cut = nullptr) {
    static const struct { const char* ending; const char* unit; } kUnits[] = { {"(%)", "%"}, {"(dB)", " dB"} };
    for (auto& u : kUnits) {
        std::string e = u.ending;
        if (name.size() >= e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) {
            if (cut) *cut = e.size();
            return u.unit;
        }
    }
    if (cut) *cut = 0;
    return "";
}

std::string name(const Json& s) { return s["name"].str(s["id"].str()); }

std::string number(double v, const Range& r) {
    char b[64];
    if ((r.step > 0 && integral(r.step) && integral(r.min)) || integral(v)) {
        double i = std::round(v);
        std::snprintf(b, sizeof b, "%.0f", i == 0 ? 0.0 : i);
        return b;
    }
    std::snprintf(b, sizeof b, "%.2f", v);
    std::string t = b;
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    if (t == "-0") t = "0";
    return t;
}

double valueOf(const Json& s, size_t i, const std::vector<double>& values) {
    return i < values.size() ? values[i] : s["default"].num(0);
}

Row row(const Json& s, size_t i, double v) {
    Row r;
    r.index = i;
    r.id = s["id"].str();
    r.label = label(s);
    r.group = s["group"].str();
    r.widget = widget(s);
    Range g = range(s);
    r.min = g.min; r.max = g.max; r.step = g.step;
    r.value = v;
    r.display = display(s, v);
    r.chosen = -1;
    if (r.widget == Widget::Chips) {
        if (kind(s) == Kind::Choice) {
            for (size_t k = 0; k < s["options"].size(); k++) r.choices.push_back(s["options"][k].str());
        } else {
            int n = gridCount(g);
            for (int k = 0; k < n; k++) r.choices.push_back(display(s, chipValue(s, k)));
        }
        if (!r.choices.empty())
            r.chosen = std::clamp(int(std::lround((v - g.min) / g.step)), 0, int(r.choices.size()) - 1);
    }
    return r;
}

void quote(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += char(c);
    }
    out += '"';
}

void num(std::string& out, double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.12g", std::isfinite(v) ? v : 0.0);
    out += b;
}

}  // namespace

Widget widget(const Json& s) {
    switch (kind(s)) {
    case Kind::Switch: return Widget::Switch;
    case Kind::Choice: return Widget::Chips;
    default: break;
    }
    int n = gridCount(range(s));
    return n >= 2 && n <= 16 ? Widget::Chips : Widget::Slider;
}

double snap(const Json& s, double v) {
    if (kind(s) == Kind::Switch) return v != 0 ? 1 : 0;
    return onGrid(range(s), v);
}

std::vector<Group> groups(const Json& manifest, const std::vector<double>& values) {
    std::vector<Group> out;
    const Json& ss = manifest["settings"];
    for (size_t i = 0; i < ss.size(); i++) {
        const Json& s = ss[i];
        if (s["hidden"].boolean()) continue;
        std::string g = s["group"].str();
        auto it = std::find_if(out.begin(), out.end(), [&](const Group& x) { return x.name == g; });
        if (it == out.end()) { out.push_back({g, {}}); it = out.end() - 1; }
        it->rows.push_back(row(s, i, valueOf(s, i, values)));
    }
    return out;
}

std::vector<Row> rows(const Json& manifest, const std::vector<double>& values) {
    std::vector<Row> out;
    for (auto& g : groups(manifest, values)) for (auto& r : g.rows) out.push_back(r);
    return out;
}

double stepped(const Json& s, double v, int dir) {
    Range r = range(s);
    switch (kind(s)) {
    case Kind::Switch: return v != 0 ? 0 : 1;
    case Kind::Choice: {
        int n = int(s["options"].size());
        if (n == 0) return 0;
        int k = int(std::lround(v)) + (dir > 0 ? 1 : dir < 0 ? -1 : 0);
        return double(((k % n) + n) % n);
    }
    default: break;
    }
    double by = r.step > 0 ? r.step : (r.max - r.min) / 20;
    return onGrid(r, v + (dir > 0 ? 1 : dir < 0 ? -1 : 0) * by);
}

double fromFraction(const Json& s, double t) {
    Range r = range(s);
    if (!(t >= 0)) t = 0;
    if (t > 1) t = 1;
    if (kind(s) == Kind::Switch) return t >= 0.5 ? 1 : 0;
    return onGrid(r, r.min + t * (r.max - r.min));
}

double toFraction(const Json& s, double v) {
    Range r = range(s);
    if (r.max <= r.min) return 0;
    return std::clamp((v - r.min) / (r.max - r.min), 0.0, 1.0);
}

double chipValue(const Json& s, int k) {
    Range r = range(s);
    if (kind(s) != Kind::Number) return std::clamp(double(k), r.min, r.max);
    if (r.step <= 0) return r.min;
    return onGrid(r, r.min + double(std::max(k, 0)) * r.step);
}

std::string display(const Json& s, double v) {
    switch (kind(s)) {
    case Kind::Switch: return v != 0 ? "On" : "Off";
    case Kind::Choice: {
        int n = int(s["options"].size());
        if (n == 0) return "";
        return s["options"][size_t(std::clamp(int(std::lround(v)), 0, n - 1))].str();
    }
    default: break;
    }
    return number(v, range(s)) + unit(name(s));
}

std::string label(const Json& s) {
    std::string n = name(s);
    if (kind(s) != Kind::Number) return n;
    size_t cut;
    unit(n, &cut);
    if (!cut) return n;
    n.resize(n.size() - cut);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n;
}

std::string toJson(const std::vector<Group>& gs) {
    std::string o = "[";
    for (size_t gi = 0; gi < gs.size(); gi++) {
        if (gi) o += ',';
        o += "{\"name\":"; quote(o, gs[gi].name); o += ",\"rows\":[";
        for (size_t ri = 0; ri < gs[gi].rows.size(); ri++) {
            const Row& r = gs[gi].rows[ri];
            if (ri) o += ',';
            o += "{\"index\":"; num(o, double(r.index));
            o += ",\"id\":"; quote(o, r.id);
            o += ",\"label\":"; quote(o, r.label);
            o += ",\"group\":"; quote(o, r.group);
            o += ",\"widget\":";
            o += r.widget == Widget::Switch ? "\"switch\"" : r.widget == Widget::Chips ? "\"chips\"" : "\"slider\"";
            o += ",\"choices\":[";
            for (size_t k = 0; k < r.choices.size(); k++) { if (k) o += ','; quote(o, r.choices[k]); }
            o += "],\"chosen\":"; num(o, r.chosen);
            o += ",\"min\":"; num(o, r.min);
            o += ",\"max\":"; num(o, r.max);
            o += ",\"step\":"; num(o, r.step);
            o += ",\"value\":"; num(o, r.value);
            o += ",\"display\":"; quote(o, r.display);
            o += '}';
        }
        o += "]}";
    }
    return o + "]";
}

}  // namespace mpe::rail
