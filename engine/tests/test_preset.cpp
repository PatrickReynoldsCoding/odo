// Preset files (mpe::preset): what one odo writes, another reads back the same.
#include "check.h"
#include "mpe/preset.h"

using namespace mpe::preset;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    // A phone's preset: one side, the Grid, two full parts, the effects.
    Preset p;
    p.name = "Bass below, lead above";
    p.madeBy = "odo 0.29.0 (Android)";
    p.hasEffects = true;
    p.effects.bpm = 96;
    p.effects.delayPingPong = false;
    Side s;
    s.controller = "patrickreynolds/grid";
    s.settings = mpe::Json::parse(R"({"split": 1, "slide": 0.25, "latch": true})");
    Part a; a.sound = "patrickreynolds/bass-guitar"; a.full = true; a.transpose = -12; a.reverb = 0.05;
    a.arp.on = true; a.arp.order = 2; a.arp.rate = 8; a.arp.octaves = 2;
    Part b; b.sound = "patrickreynolds/lead"; b.full = true; b.volume = 0.7;
    s.parts = {a, b};
    p.sides = {s};

    std::string text = write(p);
    Preset q;
    std::string err;
    CHECK(parse(text, q, &err), "what is written reads back (%s)", err.c_str());
    CHECK(q.name == p.name && q.madeBy == p.madeBy, "name and madeBy");
    CHECK(q.hasEffects && near(q.effects.bpm, 96) && !q.effects.delayPingPong, "effects");
    CHECK(q.sides.size() == 1 && q.sides[0].controller == "patrickreynolds/grid", "the side's controller");
    CHECK(near(q.sides[0].settings["split"].num(), 1) && near(q.sides[0].settings["slide"].num(), 0.25)
          && q.sides[0].settings["latch"].boolean(), "the controller's settings");
    CHECK(q.sides[0].parts.size() == 2, "two parts");
    const Part& qa = q.sides[0].parts[0];
    CHECK(qa.full && qa.sound == a.sound && qa.transpose == -12 && near(qa.reverb, 0.05), "part 0's sound and settings");
    CHECK(qa.arp.on && qa.arp.order == 2 && near(qa.arp.rate, 8) && qa.arp.octaves == 2, "part 0's arp");
    CHECK(near(q.sides[0].parts[1].volume, 0.7), "part 1's volume");
    CHECK(write(q) == text, "written again, the same file");

    // A computer's: two sides, only sounds, no effects.
    const char* desk = R"({"format": "odo-preset/1", "name": "Keys over drums", "split": 2,
        "sides": [{"controller": "patrickreynolds/grid", "settings": {"split": 0}, "parts": [{"sound": "patrickreynolds/rhodes"}]},
                  {"controller": "patrickreynolds/grid", "settings": {}, "parts": [{"sound": "patrickreynolds/kit"}]}]})";
    CHECK(parse(desk, q, &err), "a computer's preset reads (%s)", err.c_str());
    CHECK(q.split == 2 && q.sides.size() == 2 && !q.hasEffects, "its split, two sides, no effects");
    CHECK(!q.sides[0].parts[0].full && near(q.sides[0].parts[0].reverb, 0.15), "a sound alone: not full, defaults");
    CHECK(write(q).find("\"transpose\"") == std::string::npos, "and written back with the sound alone");

    // A sound's version (1.1): kept, and not taken for the part's settings.
    const char* pinned = R"({"format": "odo-preset/1.1", "name": "Old piano",
        "sides": [{"parts": [{"sound": "patrickreynolds/piano", "version": "1.0.0"}, {"sound": "patrickreynolds/lead", "version": "../x"}]}]})";
    CHECK(parse(pinned, q, &err), "a preset naming a sound's version reads (%s)", err.c_str());
    CHECK(q.sides[0].parts[0].version == "1.0.0" && !q.sides[0].parts[0].full, "the version is kept; a sound and version alone: not full");
    CHECK(q.sides[0].parts[1].version.empty(), "a version that is not one is left out: the newest plays");
    CHECK(write(q).find("\"version\": \"1.0.0\"") != std::string::npos || write(q).find("\"version\":\"1.0.0\"") != std::string::npos,
          "and written back");
    CHECK(write(q).find("odo-preset/1.1") != std::string::npos, "files are odo-preset/1.1");

    // What is not a preset, or is too new.
    CHECK(!parse("not json", q, &err), "not JSON: refused (%s)", err.c_str());
    CHECK(!parse(R"({"name": "x", "sides": [{}]})", q, &err), "no format: refused");
    CHECK(!parse(R"({"format": "odo-preset/2", "name": "x", "sides": [{}]})", q, &err)
          && err.find("newer") != std::string::npos, "a newer major: refused, saying so");
    CHECK(parse(R"({"format": "odo-preset/1.4", "name": "x", "sides": [{}], "future": 1})", q, &err),
          "a newer minor, with keys it does not know: read");
    CHECK(!parse(R"({"format": "odo-preset/1", "name": "x", "sides": []})", q, &err), "no sides: refused");
    CHECK(!parse(std::string(kMaxBytes + 1, ' '), q, &err), "too big: refused");

    // Out-of-range and wrong-typed values are tamed, not trusted.
    CHECK(parse(R"({"format": "odo-preset/1", "name": "  ", "split": -3,
        "sides": [{"settings": {"a": "text", "b": 2, "c": [1]}, "parts": [{"sound": "x", "volume": 1e9, "arp": {"octaves": 99}}]}]})", q, &err),
          "odd values read");
    CHECK(q.name == "Preset" && q.split == 0, "a blank name and a negative split");
    CHECK(q.sides[0].settings.size() == 1 && near(q.sides[0].settings["b"].num(), 2), "only number/switch settings kept");
    CHECK(near(q.sides[0].parts[0].volume, 4) && q.sides[0].parts[0].arp.octaves == 4, "values clamped");
    CHECK(q.sides[0].controller.empty(), "no controller: any");

    // Numbers read back exactly: a 48-bit mask, and a short decimal stays short.
    Preset m; m.name = "m"; Side ms; ms.settings = mpe::Json::parse(R"({"notes0": 281474976710655, "x": 0.6})"); m.sides = {ms};
    CHECK(parse(write(m), q, &err) && q.sides[0].settings["notes0"].num() == 281474976710655.0, "a 48-bit mask exactly");
    CHECK(write(m).find("0.6}") != std::string::npos, "0.6 written as 0.6");

    // File names.
    Preset n; n.name = "Lead / Pad: \"big\"";
    CHECK(fileName(n) == "Lead - Pad- -big-.odopreset", "a file name without slashes or quotes (%s)", fileName(n).c_str());
    n.name = "...";
    CHECK(fileName(n) == "Preset.odopreset", "a name of dots");
    return finish();
}
