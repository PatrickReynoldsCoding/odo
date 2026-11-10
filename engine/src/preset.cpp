#include "mpe/preset.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mpe::preset {

namespace {

void quote(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += char(c);
    }
    out += '"';
}

// The shortest text that reads back as the same number: a setting can be a bitmask of 48 bits
// (the Grid's own notes), which twelve digits would change.
void num(std::string& out, double v) {
    if (!std::isfinite(v)) v = 0;
    char b[32];
    for (int digits = 15; digits <= 17; digits++) {
        std::snprintf(b, sizeof b, "%.*g", digits, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    out += b;
}

void dump(std::string& out, const Json& v) {
    switch (v.type) {
        case Json::Null: out += "null"; break;
        case Json::Bool: out += v.b ? "true" : "false"; break;
        case Json::Number: num(out, v.n); break;
        case Json::String: quote(out, v.s); break;
        case Json::Array:
            out += '[';
            for (size_t i = 0; i < v.a.size(); i++) { if (i) out += ','; dump(out, v.a[i]); }
            out += ']';
            break;
        case Json::Object:
            out += '{';
            for (size_t i = 0; i < v.o.size(); i++) {
                if (i) out += ',';
                quote(out, v.o[i].first); out += ':'; dump(out, v.o[i].second);
            }
            out += '}';
            break;
    }
}

double finite(const Json& v, double def, double lo, double hi) {
    double x = v.num(def);
    return std::isfinite(x) ? std::clamp(x, lo, hi) : def;
}

bool fail(std::string* error, const char* why) { if (error) *error = why; return false; }

// A key/value pair with these spacings, as in `"name": value`.
struct Writer {
    std::string o;
    int depth = 0;
    std::vector<bool> first;
    void nl() { o += '\n'; o.append(size_t(depth) * 2, ' '); }
    void open(char c) { o += c; depth++; first.push_back(true); }
    void close(char c) { depth--; bool empty = first.back(); first.pop_back(); if (!empty) nl(); o += c; }
    void key(const char* k) {
        if (!first.back()) o += ',';
        first.back() = false;
        nl(); quote(o, k); o += ": ";
    }
    void item() { if (!first.back()) o += ','; first.back() = false; nl(); }
};

}  // namespace

namespace {
struct Obj {
    Json j;
    Obj() { j.type = Json::Object; }
    Obj& num(const char* k, double v) { Json x; x.type = Json::Number; x.n = v; j.o.emplace_back(k, x); return *this; }
    Obj& flag(const char* k, bool v) { Json x; x.type = Json::Bool; x.b = v; j.o.emplace_back(k, x); return *this; }
    Obj& str(const char* k, const std::string& v) { Json x; x.type = Json::String; x.s = v; j.o.emplace_back(k, x); return *this; }
    Obj& obj(const char* k, Json v) { j.o.emplace_back(k, std::move(v)); return *this; }
};
}  // namespace

Json partJson(const Part& q) {
    Obj o;
    o.str("sound", q.sound);
    if (!q.version.empty()) o.str("version", q.version);
    if (!q.full) return o.j;
    const Arp& a = q.arp;
    o.num("transpose", q.transpose).num("volume", q.volume).num("brightness", q.brightness).num("drive", q.drive)
     .num("pressureCurve", q.pressureCurve).num("reverb", q.reverb).num("delay", q.delay).num("release", q.release)
     .num("width", q.width)
     .obj("arp", Obj().flag("on", a.on).num("order", a.order).num("rate", a.rate).num("gate", a.gate)
                      .num("octaves", a.octaves).flag("latch", a.latch).j);
    return o.j;
}

Part partFrom(const Json& q) {
    Part part, d;
    part.sound = q["sound"].str();
    part.version = q["version"].str();
    if (part.version.size() > 32 || part.version.find_first_not_of("0123456789.") != std::string::npos) part.version.clear();
    size_t named = 0;                               // the part's sound and version: the rest is how it plays
    for (const auto& kv : q.o) named += kv.first == "sound" || kv.first == "version";
    part.full = q.o.size() > named;
    part.transpose = int(finite(q["transpose"], d.transpose, -60, 60));
    part.volume = finite(q["volume"], d.volume, 0, 4);
    part.brightness = finite(q["brightness"], d.brightness, 0, 4);
    part.drive = finite(q["drive"], d.drive, 0, 4);
    part.pressureCurve = finite(q["pressureCurve"], d.pressureCurve, 0.1, 10);
    part.reverb = finite(q["reverb"], d.reverb, 0, 1);
    part.delay = finite(q["delay"], d.delay, 0, 1);
    part.release = finite(q["release"], d.release, 0, 60);
    part.width = finite(q["width"], d.width, 0, 1);
    const Json& a = q["arp"];
    part.arp.on = a["on"].boolean(d.arp.on);
    part.arp.order = int(finite(a["order"], d.arp.order, 0, 5));
    part.arp.rate = finite(a["rate"], d.arp.rate, 0.25, 32);
    part.arp.gate = finite(a["gate"], d.arp.gate, 0.05, 1);
    part.arp.octaves = int(finite(a["octaves"], d.arp.octaves, 1, 4));
    part.arp.latch = a["latch"].boolean(d.arp.latch);
    return part;
}

Json effectsJson(const Effects& e) {
    return Obj().num("bpm", e.bpm).num("reverbSize", e.reverbSize).num("reverbDamp", e.reverbDamp)
                .num("delayBeats", e.delayBeats).num("delayFeedback", e.delayFeedback).flag("delayPingPong", e.delayPingPong).j;
}

Effects effectsFrom(const Json& fx) {
    Effects e, d;
    e.bpm = finite(fx["bpm"], d.bpm, 20, 400);
    e.reverbSize = finite(fx["reverbSize"], d.reverbSize, 0, 1);
    e.reverbDamp = finite(fx["reverbDamp"], d.reverbDamp, 0, 1);
    e.delayBeats = finite(fx["delayBeats"], d.delayBeats, 0, 8);
    e.delayFeedback = finite(fx["delayFeedback"], d.delayFeedback, 0, 1);
    e.delayPingPong = fx["delayPingPong"].boolean(d.delayPingPong);
    return e;
}

std::string toJson(const Json& v) { std::string o; dump(o, v); return o; }

bool parse(const std::string& text, Preset& out, std::string* error) {
    if (text.size() > kMaxBytes) return fail(error, "This file is too big to be a preset.");
    std::string perr;
    Json j = Json::parse(text, &perr);
    if (j.type != Json::Object) return fail(error, "This file is not an odo preset.");
    const std::string fmt = j["format"].str();
    const std::string stem = "odo-preset/";
    if (fmt.compare(0, stem.size(), stem) != 0) return fail(error, "This file is not an odo preset.");
    int major = std::atoi(fmt.c_str() + stem.size());
    if (major < 1) return fail(error, "This file is not an odo preset.");
    if (major > 1) return fail(error, "This preset was made by a newer odo. Update odo to open it.");

    Preset p;
    p.name = j["name"].str();
    p.name.erase(0, p.name.find_first_not_of(" \t\n"));
    p.name.erase(p.name.find_last_not_of(" \t\n") + 1);
    if (p.name.size() > 200) p.name.resize(200);
    if (p.name.empty()) p.name = "Preset";
    p.madeBy = j["madeBy"].str();
    p.split = int(finite(j["split"], 0, 0, 64));
    p.layered = j["layered"].boolean();

    if (j["effects"].type == Json::Object) { p.hasEffects = true; p.effects = effectsFrom(j["effects"]); }

    const Json& sides = j["sides"];
    for (size_t k = 0; k < sides.size() && k < size_t(kMaxSides); k++) {
        const Json& s = sides[k];
        if (s.type != Json::Object) continue;
        Side side;
        side.controller = s["controller"].str();
        side.settings.type = Json::Object;
        for (const auto& kv : s["settings"].o)
            if (kv.second.type == Json::Number && std::isfinite(kv.second.n)) side.settings.o.push_back(kv);
            else if (kv.second.type == Json::Bool) side.settings.o.push_back(kv);
        const Json& parts = s["parts"];
        for (size_t i = 0; i < parts.size() && i < size_t(kMaxParts); i++) {
            if (parts[i].type != Json::Object) continue;
            Part part = partFrom(parts[i]);
            side.parts.push_back(std::move(part));
        }
        p.sides.push_back(std::move(side));
    }
    if (p.sides.empty()) return fail(error, "This preset has nothing in it to play.");
    out = std::move(p);
    return true;
}

std::string write(const Preset& p) {
    Writer w;
    w.open('{');
    w.key("format"); quote(w.o, kFormat);
    w.key("name"); quote(w.o, p.name);
    if (!p.madeBy.empty()) { w.key("madeBy"); quote(w.o, p.madeBy); }
    w.key("split"); num(w.o, p.split);
    w.key("layered"); w.o += p.layered ? "true" : "false";
    if (p.hasEffects) { w.key("effects"); w.o += toJson(effectsJson(p.effects)); }
    w.key("sides"); w.open('[');
    for (const Side& s : p.sides) {
        w.item(); w.open('{');
        w.key("controller"); quote(w.o, s.controller);
        w.key("settings"); w.o += s.settings.type == Json::Object ? toJson(s.settings) : "{}";
        w.key("parts"); w.open('[');
        for (const Part& q : s.parts) { w.item(); w.o += toJson(partJson(q)); }
        w.close(']');
        w.close('}');
    }
    w.close(']');
    w.close('}');
    return w.o + "\n";
}

std::string fileName(const Preset& p) {
    std::string n;
    for (unsigned char c : p.name) {
        if (c < 0x20 || std::string("/\\:*?\"<>|").find(char(c)) != std::string::npos) n += '-';
        else n += char(c);
    }
    while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
    while (!n.empty() && (n.front() == ' ' || n.front() == '.')) n.erase(n.begin());
    if (n.empty()) n = "Preset";
    if (n.size() > 100) n.resize(100);
    return n + kExtension;
}

}  // namespace mpe::preset
