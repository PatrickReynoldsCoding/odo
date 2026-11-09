// Scala tunings (mpe/scala.h): reading .scl files, giving one to a controller as its settings, and
// how the rail shows it; and the Hex keyboard's tuning made from one (controllers/hex/tuning.h).
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include "check.h"
#include "mpe/rail.h"
#include "mpe/scala.h"
#include "../../controllers/hex/tuning.h"

using namespace mpe;

static bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

static void reading() {
    scala::Scale s;
    std::string err;
    // Comments anywhere, cents with a full stop, ratios, a whole number, words after the pitch, CRLF.
    const char* meantone =
        "! meantone.scl\r\n!\r\nQuarter-comma meantone\r\n 12\r\n!\r\n 76.049 C#\r\n 193.157\r\n 310.265\r\n 5/4\r\n"
        " 503.422\r\n 579.471\r\n 696.578\r\n 25/16\r\n 889.735\r\n 1006.843\r\n 1082.892\r\n 2\r\n";
    CHECK(scala::parse(meantone, &s, &err), "reads a .scl file (%s)", err.c_str());
    CHECK(s.description == "Quarter-comma meantone" && s.cents.size() == 12, "its description and 12 notes (%zu)", s.cents.size());
    CHECK(near(s.cents[0], 76.049) && near(s.cents[3], 386.3137138648348) && near(s.cents[11], 1200),
          "cents, ratios and a whole number (%.4f %.4f %.4f)", s.cents[0], s.cents[3], s.cents[11]);
    // An empty description is allowed; negative cents too.
    CHECK(scala::parse("\n2\n-10.0\n3/1\n", &s, &err) && s.description.empty() && near(s.cents[0], -10) && near(s.cents[1], 1901.955, 1e-3),
          "an empty description, negative cents, a tritave");
    struct Bad { const char* text; const char* says; };
    const Bad bad[] = {
        {"", "it needs a description line"},
        {"x\nnone\n", "how many notes"},
        {"x\n0\n", "has no notes"},
        {"x\n3\n100.0\n200.0\n", "lists 2"},
        {"x\n2\n100.0\nabc\n", "Note 2 isn't"},
        {"x\n2\n100.0\n-3/2\n", "Note 2 isn't"},
        {"x\n1\n0/1\n", "Note 1 isn't"},
        {"x\n1\n-5.0\n", "has to be above"},
        {"x\n2000\n", "up to 1024"},
    };
    for (const Bad& b : bad) {
        err.clear();
        bool ok = scala::parse(b.text, &s, &err);
        CHECK(!ok && err.find(b.says) != std::string::npos, "refused: \"%s\"", err.c_str());
    }
}

static Json hexManifest() {
    std::ifstream f(MPE_TEST_CONTROLLER_SOURCES "/hex/controller.json");
    std::stringstream ss;
    ss << f.rdbuf();
    return Json::parse(ss.str());
}

static void asSettings() {
    Json m = hexManifest();
    const Json& ss = m["settings"];
    size_t at = 0, tuning = 0;
    for (size_t i = 0; i < ss.size(); i++) {
        if (ss[i]["id"].str() == "scala") at = i;
        if (ss[i]["id"].str() == "tuning") tuning = i;
    }
    CHECK(at > 0 && ss[at]["type"].str() == "scala" && scala::capacity(ss[at]) == HT_MAX_N, "the Hex keyboard takes a Scala file of up to %d notes", HT_MAX_N);
    scala::Scale s;
    scala::parse("Bohlen-Pierce\n13\n3/1\n", &s);       // not a real one: a single note, at the tritave
    scala::parse("Bohlen-Pierce, equal\n13\n146.304\n292.608\n438.913\n585.217\n731.521\n877.825\n1024.130\n1170.434\n"
                 "1316.738\n1463.042\n1609.347\n1755.651\n3/1\n", &s);
    std::string err;
    auto set = scala::settings(m, at, s, &err);
    CHECK(set.size() == size_t(HT_MAX_N) + 1 && set.back().first == at && set.back().second == 13,
          "its settings: %d places, then the count (%zu) %s", HT_MAX_N, set.size(), err.c_str());
    std::vector<double> values(ss.size(), 0);
    for (size_t i = 0; i < ss.size(); i++) values[i] = ss[i]["default"].num(0);
    for (auto& p : set) values[p.first] = p.second;
    CHECK(near(values[at + 1], 146.304) && near(values[at + 13], 1901.955, 1e-3) && values[at + 14] == 0,
          "each note's cents in its place, the rest 0");
    CHECK(scala::describe(m, at, values) == "13 notes, to 1901.955 \xC2\xA2", "described: %s", scala::describe(m, at, values).c_str());

    // On the rail: shown with Tuning on Scala file, as a row that opens a file.
    values[tuning] = 9;
    bool found = false;
    for (const auto& r : rail::rows(m, values))
        if (r.id == "scala") found = r.widget == rail::Widget::Scala && r.display == "13 notes, to 1901.955 \xC2\xA2";
    CHECK(found, "the rail shows a Scala file row with what is loaded");
    CHECK(rail::toJson(rail::groups(m, values)).find("\"widget\":\"scala\"") != std::string::npos, "and says so to Android");
    CHECK(rail::stepped(ss[at], 13, 1) == 13, "a step doesn't change it: it comes from a file");
    values[tuning] = 0;
    for (const auto& r : rail::rows(m, values)) CHECK(r.id != "scala", "not shown for the other tunings");
    values[at] = 0;
    CHECK(scala::describe(m, at, values) == "None", "none loaded: None");
    scala::Scale twelve;
    scala::parse("12\n12\n100.0\n200.0\n300.0\n400.0\n500.0\n600.0\n700.0\n800.0\n900.0\n1000.0\n1100.0\n2/1\n", &twelve);
    for (auto& p : scala::settings(m, at, twelve)) values[p.first] = p.second;
    CHECK(scala::describe(m, at, values) == "12 notes, to 2/1 (1200 \xC2\xA2)", "an octave: %s", scala::describe(m, at, values).c_str());

    scala::Scale big;
    big.cents.assign(HT_MAX_N + 1, 10);
    err.clear();
    CHECK(scala::settings(m, at, big, &err).empty() && err.find("up to 128") != std::string::npos, "too many notes: %s", err.c_str());
}

// The Hex keyboard's tuning from a Scala file.
static void hexTuning() {
    double cents12[12], table[HT_MAX_N];
    int val[3];
    for (int k = 0; k < 12; k++) cents12[k] = 100.0 * (k + 1);
    HexTuning t;
    CHECK(ht_scala(&t, table, val, cents12, 12), "12 equal from a Scala file makes a tuning");
    int bad = 0;
    for (int key = 0; key < 12; key++)
        for (int note = -30; note < 160; note++)
            if (!near(ht_pitch(&t, key, ht_key_step(&t, key), note), ht_pitch(ht_tuning(HT_12), key, ht_key_step(ht_tuning(HT_12), key), note))) bad++;
    CHECK(bad == 0, "and plays every note in every key as the built-in 12 equal (%d differ)", bad);
    for (int l = 0; l < HL_OWN; l++) {
        int e1, n1, e2, n2;
        ht_layout_steps(&t, l, 0, 0, &e1, &n1);
        ht_layout_steps(ht_tuning(HT_12), l, 0, 0, &e2, &n2);
        CHECK(e1 == e2 && n1 == n2, "%s: the same steps (%d, %d)", HT_LAYOUTS[l].name, e1, n1);
    }
    // 31 equal from a file: the same steps as the built-in one.
    double c31[31];
    for (int k = 0; k < 31; k++) c31[k] = 1200.0 * (k + 1) / 31;
    ht_scala(&t, table, val, c31, 31);
    for (int l = 0; l < HL_OWN; l++) {
        int e1, n1, e2, n2;
        ht_layout_steps(&t, l, 0, 0, &e1, &n1);
        ht_layout_steps(ht_tuning(HT_31), l, 0, 0, &e1, &n1);
        ht_layout_steps(ht_tuning(HT_31), l, 0, 0, &e2, &n2);
        CHECK(e1 == e2 && n1 == n2, "31 from a file, %s: the built-in 31's steps", HT_LAYOUTS[l].name);
    }
    // Bohlen-Pierce: 13 to the tritave (3/1). It repeats there, the root stays put.
    double bp[13];
    for (int k = 0; k < 13; k++) bp[k] = 1901.9550008653874 * (k + 1) / 13;
    CHECK(ht_scala(&t, table, val, bp, 13), "Bohlen-Pierce makes a tuning");
    CHECK(near(ht_pitch(&t, 0, 0, 13) - ht_pitch(&t, 0, 0, 0), 19.019550008653874), "13 steps up is a tritave (%.4f semitones)",
          ht_pitch(&t, 0, 0, 13) - ht_pitch(&t, 0, 0, 0));
    CHECK(near(ht_pitch(&t, 2, ht_key_step(&t, 2), ht_root_near(&t, 2, ht_key_step(&t, 2), 62)), 62), "the key's root (D) where 12 has it");
    char name[32];
    CHECK(!ht_name(&t, 5, name), "its notes are named by step, not by letter");
    CHECK(!ht_scala(&t, table, val, bp, 0) && !ht_scala(&t, table, val, bp, HT_MAX_N + 1), "no notes, or too many: no tuning");
}

int main() {
    reading();
    asSettings();
    hexTuning();
    return finish();
}
