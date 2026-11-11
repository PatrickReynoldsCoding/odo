// The rail's settings model (mpe/rail.h): every rule in docs/formats/CONTROLLER_FORMAT.md's "Settings on the
// rail", against the controllers' own manifests and against small made-up ones.
#include <cstring>
#include <fstream>
#include <sstream>
#include "check.h"
#include "mpe/rail.h"

using namespace mpe;
using rail::Widget;

static Json load(const char* name) {
    std::ifstream f(std::string(MPE_TEST_CONTROLLER_SOURCES "/") + name + "/controller.json");
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    Json j = Json::parse(ss.str(), &err);
    CHECK(j.type == Json::Object, "%s/controller.json reads (%s)", name, err.c_str());
    return j;
}
static Json js(const char* text) { return Json::parse(text); }
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
static const rail::Row* find(const std::vector<rail::Row>& rs, const char* id) {
    for (auto& r : rs) if (r.id == id) return &r;
    return nullptr;
}
static std::vector<double> defaults(const Json& m) {
    std::vector<double> v;
    for (size_t i = 0; i < m["settings"].size(); i++) v.push_back(m["settings"][i]["default"].num());
    return v;
}

static void switches() {
    Json s = js(R"j({"id":"bass","name":"Bass","type":"switch","default":false})j");
    CHECK(rail::widget(s) == Widget::Switch, "a switch is a Switch");
    CHECK(rail::display(s, 1) == "On" && rail::display(s, 0) == "Off", "a switch shows On/Off");
    CHECK(rail::stepped(s, 0, 1) == 1 && rail::stepped(s, 1, 1) == 0 && rail::stepped(s, 1, -1) == 0, "a step flips a switch, either way");
    CHECK(rail::fromFraction(s, 0.8) == 1 && rail::fromFraction(s, 0.2) == 0, "a switch from a fraction");
    CHECK(rail::chipValue(s, 1) == 1 && rail::chipValue(s, 5) == 1, "a switch's chip values");
    Json m = js(R"j({"settings":[{"id":"bass","name":"Bass","type":"switch","default":true}]})j");
    auto rs = rail::rows(m, {});
    CHECK(rs.size() == 1 && rs[0].value == 1 && rs[0].display == "On" && rs[0].chosen == -1, "a missing value is the default (true: On); not chips: chosen -1");
}

static void choices() {
    Json s = js(R"j({"id":"strum","name":"Strum","type":"choice","options":["Off","Fast","Slow","Harp"],"default":0})j");
    CHECK(rail::widget(s) == Widget::Chips, "a choice is Chips");
    CHECK(rail::display(s, 2) == "Slow", "a choice shows its option's name");
    CHECK(rail::display(s, 9) == "Harp" && rail::display(s, -3) == "Off", "out of range: the nearest option");
    CHECK(rail::stepped(s, 3, 1) == 0, "on from the last option wraps to the first");
    CHECK(rail::stepped(s, 0, -1) == 3, "back from the first wraps to the last");
    CHECK(rail::stepped(s, 1, 1) == 2 && rail::stepped(s, 1.4, 1) == 2, "on: the next (from the nearest)");
    CHECK(rail::chipValue(s, 2) == 2, "chip k is option k");
    CHECK(near(rail::toFraction(s, 3), 1) && near(rail::toFraction(s, 1), 1.0 / 3), "a choice as a fraction");
    CHECK(rail::fromFraction(s, 0.5) == 2 && rail::fromFraction(s, 1) == 3, "a fraction to a choice: the nearest");
    Json m = js(R"j({"settings":[{"id":"strum","name":"Strum","type":"choice","options":["Off","Fast","Slow","Harp"],"default":0}]})j");
    auto rs = rail::rows(m, {2});
    CHECK(rs[0].choices.size() == 4 && rs[0].choices[3] == "Harp" && rs[0].chosen == 2, "a choice's chips are its options, the chosen one marked");
    CHECK(rs[0].min == 0 && rs[0].max == 3 && rs[0].step == 1, "a choice's range: 0 to options-1, step 1");
    CHECK(rail::stepped(js(R"j({"type":"choice","options":[]})j"), 0, 1) == 0, "a choice without options does not crash");
}

static void numbers() {
    // 16 steps: chips; 17: a slider.
    Json s16 = js(R"j({"id":"a","name":"A","type":"number","min":0,"max":15,"step":1})j");
    Json s17 = js(R"j({"id":"a","name":"A","type":"number","min":0,"max":16,"step":1})j");
    Json s2 = js(R"j({"id":"a","name":"A","type":"number","min":0,"max":1,"step":1})j");
    Json s1 = js(R"j({"id":"a","name":"A","type":"number","min":3,"max":3,"step":1})j");
    CHECK(rail::widget(s16) == Widget::Chips, "16 values: Chips");
    CHECK(rail::widget(s17) == Widget::Slider, "17 values: a Slider");
    CHECK(rail::widget(s2) == Widget::Chips, "2 values: Chips");
    CHECK(rail::widget(s1) == Widget::Slider, "1 value: a Slider");
    Json s0 = js(R"j({"id":"g","name":"Glide","type":"number","min":0,"max":1,"step":0})j");
    Json none = js(R"j({"id":"g","name":"Glide","type":"number","min":0,"max":1})j");
    Json bare = js(R"j({"id":"g","type":"number"})j");
    CHECK(rail::widget(s0) == Widget::Slider && rail::widget(none) == Widget::Slider, "step 0 or no step: a (continuous) Slider");
    CHECK(rail::label(bare) == "g" && near(rail::stepped(bare, 0, 1), 0.05), "no name: its id; no min/max: 0 to 1");

    // Chips of a number: its formatted values, the nearest marked.
    Json m = js(R"j({"settings":[{"id":"n","name":"N","type":"number","min":0,"max":1,"step":0.25,"default":0.5}]})j");
    auto r = rail::rows(m, {0.6})[0];
    CHECK(r.widget == Widget::Chips && r.choices.size() == 5, "0..1 step 0.25: 5 chips (%zu)", r.choices.size());
    CHECK(r.choices[0] == "0" && r.choices[1] == "0.25" && r.choices[2] == "0.5" && r.choices[4] == "1",
          "the chips are the formatted values (%s %s %s)", r.choices[0].c_str(), r.choices[1].c_str(), r.choices[2].c_str());
    CHECK(r.chosen == 2, "0.6: the nearest chip, 0.5 (%d)", r.chosen);
    CHECK(near(rail::chipValue(m["settings"][size_t(0)], 3), 0.75), "chip 3 is 0.75");
    CHECK(rail::chipValue(m["settings"][size_t(0)], 99) == 1, "a chip past the end is clamped to max");

    // Stepping: by step, clamped, snapped to the grid from min.
    Json st = js(R"j({"id":"s","name":"S","type":"number","min":1,"max":10,"step":3})j");   // 1 4 7 10
    CHECK(rail::stepped(st, 4, 1) == 7 && rail::stepped(st, 4, -1) == 1, "a number steps by its step");
    CHECK(rail::stepped(st, 10, 1) == 10 && rail::stepped(st, 1, -1) == 1, "clamped at max and min (no wrap)");
    CHECK(rail::stepped(st, 5, 1) == 7, "snapped to the grid from min: 5 + 3 = 8 -> 7");
    CHECK(near(rail::stepped(s0, 0.5, 1), 0.55) && near(rail::stepped(none, 0.5, -1), 0.45), "step 0 or none: (max-min)/20 a step");
    CHECK(rail::stepped(s0, 0.99, 1) == 1 && rail::stepped(s0, 0.01, -1) == 0, "a continuous number clamps too");
    Json off = js(R"j({"id":"o","name":"O","type":"number","min":0,"max":10,"step":4})j");   // 0 4 8; max off the grid
    CHECK(rail::stepped(off, 8, 1) == 8, "max off the grid: the last grid point below it (%g)", rail::stepped(off, 8, 1));
    Json tenth = js(R"j({"id":"t","name":"T","type":"number","min":0,"max":1,"step":0.1})j");
    CHECK(rail::stepped(tenth, 0.2, 1) == 0.30000000000000004 || near(rail::stepped(tenth, 0.2, 1), 0.3), "0.2 + 0.1");
    CHECK(rail::display(tenth, rail::stepped(tenth, 0.2, 1)) == "0.3", "shown 0.3, not 0.30000000000000004");

    // Fractions.
    CHECK(rail::fromFraction(st, 0.5) == 7 || rail::fromFraction(st, 0.5) == 4, "a fraction snaps to the step (%g)", rail::fromFraction(st, 0.5));
    CHECK(rail::fromFraction(st, 0.4) == 4 && rail::fromFraction(st, 0.7) == 7, "0.4 -> 4.6 -> 4; 0.7 -> 7.3 -> 7");
    CHECK(rail::fromFraction(st, -1) == 1 && rail::fromFraction(st, 2) == 10, "a fraction is clamped to 0..1");
    CHECK(near(rail::fromFraction(s0, 0.37), 0.37), "continuous: not snapped");
    CHECK(near(rail::toFraction(st, 7), 2.0 / 3) && rail::toFraction(st, 99) == 1 && rail::toFraction(s1, 3) == 0,
          "toFraction: (v-min)/(max-min), clamped; 0 for an empty range");
}

static void formatting() {
    Json pct = js(R"j({"id":"p","name":"Top's share (%)","type":"number","min":20,"max":80,"step":5})j");
    Json db = js(R"j({"id":"d","name":"Strike range (dB)","type":"number","min":0,"max":60,"step":1})j");
    Json frac = js(R"j({"id":"f","name":"Glide","type":"number","min":0,"max":1,"step":0})j");
    Json odd = js(R"j({"id":"o","name":"Top: octave (from bottom's)","type":"number","min":-3,"max":3,"step":1})j");
    CHECK(rail::display(pct, 50) == "50%", "(%%) -> a %% suffix (%s)", rail::display(pct, 50).c_str());
    CHECK(rail::display(db, 24) == "24 dB", "(dB) -> \" dB\" (%s)", rail::display(db, 24).c_str());
    CHECK(rail::label(pct) == "Top's share" && rail::label(db) == "Strike range", "the label drops the unit");
    CHECK(rail::label(odd) == "Top: octave (from bottom's)", "other brackets stay in the label");
    CHECK(rail::display(odd, -2) == "-2" && rail::display(odd, 0) == "0", "integers (%s)", rail::display(odd, -2).c_str());
    CHECK(rail::display(odd, 1.4) == "1", "an integral step shows integers (%s)", rail::display(odd, 1.4).c_str());
    CHECK(rail::display(frac, 0.5) == "0.5" && rail::display(frac, 0.25) == "0.25" && rail::display(frac, 1) == "1",
          "fractions: up to 2 decimals, trailing zeros trimmed (%s %s %s)",
          rail::display(frac, 0.5).c_str(), rail::display(frac, 0.25).c_str(), rail::display(frac, 1).c_str());
    CHECK(rail::display(frac, 0.3333) == "0.33" && rail::display(frac, -0.001) == "0", "rounded to 2 decimals, never -0 (%s)",
          rail::display(frac, -0.001).c_str());
    Json sw = js(R"j({"id":"s","name":"Thing (%)","type":"switch"})j");
    CHECK(rail::label(sw) == "Thing (%)" && rail::display(sw, 1) == "On", "only a number takes a unit");
    Json m = js(R"j({"settings":[{"id":"p","name":"Top's share (%)","type":"number","min":20,"max":80,"step":5,"default":50}]})j");
    auto r = rail::rows(m, {})[0];
    CHECK(r.label == "Top's share" && r.display == "50%" && r.choices.front() == "20%" && r.choices.back() == "80%" && r.chosen == 6,
          "a row carries the label without, and the display and chips with, the unit");
}

static void grouping() {
    Json m = js(R"j({"settings":[
        {"id":"a","type":"switch","group":"Layout"},
        {"id":"b","type":"switch"},
        {"id":"c","type":"switch","group":"Notes"},
        {"id":"d","type":"switch","group":"Layout","hidden":true},
        {"id":"e","type":"switch","group":"Layout"},
        {"id":"f","type":"switch"},
        {"id":"g","type":"switch","group":"Hidden","hidden":true}]})j");
    auto gs = rail::groups(m, {1, 0, 1, 0, 0, 1, 0});
    CHECK(gs.size() == 3, "3 groups: hidden settings make none (%zu)", gs.size());
    if (gs.size() != 3) return;
    CHECK(gs[0].name == "Layout" && gs[1].name == "" && gs[2].name == "Notes", "groups in the order of their first setting");
    CHECK(gs[0].rows.size() == 2 && gs[0].rows[0].id == "a" && gs[0].rows[1].id == "e", "rows keep manifest order; hidden left out");
    CHECK(gs[0].rows[1].index == 4 && gs[1].rows[1].index == 5, "a row's index is its place in the manifest");
    CHECK(gs[1].rows[0].group == "" && gs[0].rows[0].group == "Layout", "a row knows its group");
    auto rs = rail::rows(m, {});
    const char* order[] = {"a", "e", "b", "f", "c"};
    bool ok = rs.size() == 5;
    for (size_t i = 0; ok && i < 5; i++) ok = rs[i].id == order[i];
    CHECK(ok, "rows(): the groups flattened (a e b f c)");
    std::string j = rail::toJson(gs);
    Json back = Json::parse(j);
    CHECK(back.type == Json::Array && back.size() == 3 && back[size_t(0)]["rows"][size_t(1)]["index"].num() == 4 &&
          back[size_t(0)]["rows"][size_t(0)]["widget"].str() == "switch" && back[size_t(0)]["rows"][size_t(0)]["display"].str() == "On",
          "toJson() reads back (%zu bytes)", j.size());
    Json q = js(R"j({"settings":[{"id":"q","name":"Say \"hi\"","type":"choice","options":["a\\b"]}]})j");
    Json qb = Json::parse(rail::toJson(rail::groups(q, {})));
    CHECK(qb[size_t(0)]["rows"][size_t(0)]["label"].str() == "Say \"hi\"" && qb[size_t(0)]["rows"][size_t(0)]["choices"][size_t(0)].str() == "a\\b",
          "toJson() escapes quotes and backslashes");
}

static size_t idx(const Json& m, const char* id) {
    const Json& ss = m["settings"];
    for (size_t i = 0; i < ss.size(); i++) if (ss[i]["id"].str() == id) return i;
    return 0;
}

static void grid() {
    Json m = load("grid");
    auto v = defaults(m);
    auto gs = rail::groups(m, v);
    std::string names;
    for (auto& g : gs) names += (names.empty() ? "" : ", ") + g.name;
    CHECK(names == "Layout, Second grid, Notes, Touch, Look, Playing, Controls", "the Grid's groups: %s", names.c_str());
    bool grouped = true, noHidden = true;
    const Json& ss = m["settings"];
    for (size_t i = 0; i < ss.size(); i++) if (ss[i]["group"].str().empty()) grouped = false;
    auto rs = rail::rows(m, v);
    for (auto& r : rs) if (ss[r.index]["hidden"].boolean()) noHidden = false;
    CHECK(grouped, "every Grid setting has a group");
    CHECK(noHidden && !find(rs, "notes0") && !find(rs, "bendSnap") && !find(rs, "dynamics"), "no hidden setting is on the rail");
    size_t shown = 0, ruledOut = 0;
    for (size_t i = 0; i < ss.size(); i++) if (!ss[i]["hidden"].boolean()) { shown++; ruledOut += !rail::shown(m, ss[i], v); }
    CHECK(rs.size() == shown - ruledOut && ruledOut == 14, "every other one is, but the 14 its mode rules out (%zu shown)", rs.size());
    int counted = 0;
    for (auto& g : gs) counted += g.hiddenNow;
    CHECK(counted == 14, "the groups count what their modes hide (%d)", counted);
    for (auto& r : rs) if (r.id != ss[r.index]["id"].str()) { CHECK(false, "%s's index is wrong", r.id.c_str()); }
    auto cols = find(rs, "cols");
    CHECK(cols && cols->widget == Widget::Chips && cols->choices.size() == 7 && cols->chosen == 1 && cols->display == "3",
          "Squares top to bottom: 7 chips, 3 chosen");
    CHECK(!find(rs, "splitSize") && !find(rs, "topOctave"), "the second grid off: none of its settings");
    auto vs = v;
    vs[idx(m, "split")] = 1;
    auto rs2 = rail::rows(m, vs);
    auto share = find(rs2, "splitSize");
    CHECK(share && share->widget == Widget::Chips && share->choices.size() == 13 && share->label == "Right grid's share" && share->display == "50%",
          "the second grid on: Right grid's share, 13 chips, 50%%");
    auto oct = find(rs2, "topOctave");
    CHECK(oct && oct->choices.front() == "-3" && oct->choices.back() == "3" && oct->chosen == 3, "Right grid: octave: -3..3, 0 chosen");
    auto spacing = find(rs, "spacing");
    CHECK(spacing && spacing->widget == Widget::Slider && spacing->label == "Spacing" && spacing->display == "9%",
          "Spacing: 29 values, a slider, 9%%");
    auto split = find(rs, "split");
    CHECK(split && split->widget == Widget::Switch && split->display == "Off" && split->group == "Second grid", "Second grid: a switch, Off");
    auto key = find(rs, "key");
    CHECK(key && key->widget == Widget::Chips && key->choices.size() == 12 && key->display == "C" && key->group == "Notes", "Key: 12 chips, C");
    CHECK(rail::stepped(ss[size_t(1)], 3, 1) == 0, "Mode: on from the last (Intervals) wraps");
    {
        auto vi = v;
        CHECK(!find(rail::rows(m, vi), "interval"), "Wrap: no interval");
        vi[idx(m, "layout")] = 3;
        auto ri = rail::rows(m, vi);
        auto iv = find(ri, "interval");
        CHECK(iv && iv->display == "5ths" && iv->choices.size() == 5 && find(ri, "scrollBar"), "Intervals: the interval (5ths) and the scroll bar");
    }
    // The modes: what each brings in.
    CHECK(find(rs, "scrollBar") && !find(rs, "scrollLock") && !find(rs, "sideLock") && !find(rs, "noteEdit"), "Up the columns: the scroll bar, no lock, no notes editor");
    auto vc = v;
    vc[idx(m, "layout")] = 1;
    auto rc = rail::rows(m, vc);
    CHECK(!find(rc, "scrollBar") && find(rc, "scrollLock") && find(rc, "sideLock"), "Along each row: the lock, no scroll bar");
    vc[idx(m, "layout")] = 2;
    rc = rail::rows(m, vc);
    CHECK(find(rc, "noteEdit") && find(rc, "scrollBar"), "Your own notes: the notes editor and the scroll bar");
    auto vl = v;
    vl[idx(m, "slide")] = 1;
    CHECK(find(rs, "vibrato") && find(rail::rows(m, vl), "vibrato"), "Slide between notes: vibrato still there (only slides go)");
    CHECK(!find(rs, "slideGlide") && find(rail::rows(m, vl), "slideGlide") && find(rail::rows(m, vl), "slideGlide")->widget == Widget::Slider, "Slide between notes: its Glide, a slider as the touch pad's");
    vl[idx(m, "latch")] = 1;
    CHECK(!find(rs, "latchMode") && find(rail::rows(m, vl), "latchMode"), "Latch on: its next-touch choice");
    auto te = find(rs, "touchEdit");
    CHECK(te && te->panel && te->widget == Widget::Switch && !find(rs, "xTarget"), "Touch: a switch that opens the Grid's panel; its settings are in it");
    std::string j = rail::toJson(gs);
    CHECK(j.find("\"panel\":true") != std::string::npos && j.find("\"hiddenNow\":") != std::string::npos, "toJson() carries panel and hiddenNow");
    // A hidden setting still steps and shows, for an app that shows it elsewhere.
    CHECK(rail::display(ss[idx(m, "dynLow")], 30) == "30%" && rail::label(ss[idx(m, "dynHigh")]) == "Loudness at the top of a square", "the loudness range: 30%%, its top's label");
}

// The Hex keyboard is the Grid's code (controllers/hex/hex.c includes grid.c), which reads its settings
// by place: its manifest must have the Grid's, in the Grid's order, before its own.
static void hex() {
    Json g = load("grid"), m = load("hex");
    const Json& gs = g["settings"];
    const Json& hs = m["settings"];
    // Its own, in hex.c's order (KEYS_EXTRA_SETTINGS): five, then a Scala file's count and its 128 places, then the slide's snap.
    std::vector<std::string> own = {"hexLayout", "tuning", "stepRight", "stepUpRight", "press", "scala"};
    for (int k = 1; k <= 128; k++) own.push_back("scala" + std::to_string(k));
    own.push_back("slideSnap");
    own.push_back("snapPoints");
    for (const char* id : {"moveKeys", "viewReset", "viewX", "viewY", "viewScale", "viewTurn", "sideMove", "place9"}) own.push_back(id);
    bool same = hs.size() == gs.size() + own.size();
    for (size_t i = 0; i < gs.size() && same; i++)
        if (hs[i]["id"].str() != gs[i]["id"].str() || hs[i]["type"].str() != gs[i]["type"].str()) {
            same = false;
            CHECK(false, "hex setting %zu is %s, the Grid's %s", i, hs[i]["id"].str().c_str(), gs[i]["id"].str().c_str());
        }
    for (size_t i = 0; i < own.size() && same; i++) same = hs[gs.size() + i]["id"].str() == own[i];
    CHECK(same, "the Hex keyboard has the Grid's %zu settings in its order, then its own %zu", gs.size(), own.size());
    auto v = defaults(m);
    auto groups = rail::groups(m, v);
    std::string names;
    for (auto& gr : groups) names += (names.empty() ? "" : ", ") + gr.name;
    CHECK(names == "Layout, Second keyboard, Notes, Touch, Look, Playing, Controls", "the Hex keyboard's groups, the Grid's: %s", names.c_str());
    auto rs = rail::rows(m, v);
    CHECK(!find(rs, "layout") && !find(rs, "chromatic") && !find(rs, "noteEdit") && !find(rs, "interval") && !find(rs, "sideLock") && !find(rs, "topNotes"),
          "none of the Grid's note modes: every key is a note of the tuning");
    auto hl = find(rs, "hexLayout");
    CHECK(hl && hl->group == "Layout" && hl->choices.size() == 4 && hl->display == "Wicki-Hayden", "Note layout: 4 chips, Wicki-Hayden");
    auto tu = find(rs, "tuning");
    CHECK(tu && tu->group == "Notes" && tu->choices.size() == 10 && tu->choices.back() == "Scala file" && tu->display == "12 equal", "Tuning: 10, 12 equal, the last Scala file");
    CHECK(!find(rs, "stepRight"), "Your own steps: not until chosen");
    auto vo = v;
    vo[idx(m, "hexLayout")] = 3;
    auto ro = rail::rows(m, vo);
    auto sr = find(ro, "stepRight");
    CHECK(sr && sr->widget == Widget::Slider && find(ro, "stepUpRight"), "Your own steps: two sliders");
    auto pr = find(rs, "press");
    CHECK(pr && pr->group == "Touch" && pr->display == "Pressure", "Press harder: Pressure");
    // Moving the keyboard takes the place of Keys top to bottom and Key size: those are hidden (kept,
    // so old presets load), the mode and putting it back are in Layout, its button in Controls, and
    // where the keyboard is, hidden.
    CHECK(!find(rs, "cols") && !find(rs, "squareSize"), "Keys top to bottom and Key size: not on the rail");
    auto mk = find(rs, "moveKeys"), vb = find(rs, "viewReset"), sm = find(rs, "sideMove");
    CHECK(mk && mk->group == "Layout" && mk->widget == Widget::Switch && mk->label == "Move the keyboard (two fingers)" && mk->display == "Off",
          "Move the keyboard (two fingers): a switch in Layout, off");
    CHECK(vb && vb->group == "Layout" && vb->widget == Widget::Switch && vb->label == "Put the keyboard back", "Put the keyboard back: in Layout");
    CHECK(sm && sm->group == "Controls" && sm->label == "Move button" && sm->display == "Top" && sm->choices.size() == 3, "Move button: Off/Top/Bottom, Top");
    for (const char* id : {"viewX", "viewY", "viewScale", "viewTurn", "place9"}) CHECK(!find(rs, id), "%s: hidden", id);
    CHECK(m["rowTurn"].str() == "viewTurn" && m["bend"].str() == "two fingers out", "its rows turn with viewTurn (for takes)");
    auto col = find(rs, "colours");
    CHECK(col && col->choices.size() == 9 && col->display == "Sharps and flats", "Colours: the Grid's and four more, Sharps and flats");
    // Its touch is its own: the Grid's slide, vibrato and slide-out settings are not on its rail;
    // its slide's snap is, the points only with it on.
    for (const char* id : {"slide", "slideOnly", "sideSlide", "sideOut", "outOn", "vibrato", "slideGlide", "xTarget"})
        CHECK(!find(rs, id), "%s: not on the Hex keyboard's rail", id);
    auto sn = find(rs, "slideSnap");
    CHECK(sn && sn->group == "Touch" && sn->widget == Widget::Switch && !find(rs, "snapPoints"), "Snap the slide to points: a switch in Touch, off");
    {
        auto vs = defaults(m);
        vs[idx(m, "slideSnap")] = 1;
        auto sp = find(rail::rows(m, vs), "snapPoints");
        CHECK(sp && sp->widget == Widget::Slider && sp->display == "4", "on: Points between keys, a slider at 4");
    }
    // The settings both have read the same, but for the word for a key.
    auto grs = rail::rows(g, defaults(g));
    for (const char* id : {"touchEdit", "latch", "sideLatch", "sideInside", "flip90", "moveControls"}) {
        auto a = find(grs, id), b = find(rs, id);
        CHECK(a && b && a->label == b->label && a->group == b->group && a->choices == b->choices, "%s: the same on both", id);
    }
    CHECK(find(rs, "insideOn") && find(rs, "insideOn")->label == "Inside a key (dynamics, tone)", "Inside a key, as the Grid's Inside a square");
}

static void tonnetzAndTouchpad() {
    Json t = load("tonnetz");
    auto gs = rail::groups(t, defaults(t));
    std::string names;
    for (auto& g : gs) names += (names.empty() ? "" : ", ") + g.name;
    CHECK(names == "Notes, Layout, Playing, Look", "the Tonnetz's groups: %s", names.c_str());
    auto rs = rail::rows(t, defaults(t));
    CHECK(rs.size() == 9 && rs[0].id == "key" && rs[1].id == "octave" && rs[2].id == "chords" && rs[3].id == "bass" && rs[4].id == "size",
          "the Tonnetz's rows: Notes first, in manifest order");
    auto size = find(rs, "size");
    CHECK(size && size->widget == Widget::Chips && size->choices.size() == 9 && size->display == "7", "Size: 9 chips, 7");
    auto lit = find(rs, "litKey");
    CHECK(lit && lit->display == "On", "Light the key's chords: On");

    Json p = load("touchpad");
    gs = rail::groups(p, defaults(p));
    names.clear();
    for (auto& g : gs) names += (names.empty() ? "" : ", ") + g.name;
    CHECK(names == "Layout, Notes, Two pads, Chords, Touch, Playing, Look", "the touch pad's groups: %s", names.c_str());
    {
        auto pr = rail::rows(p, defaults(p));
        CHECK(!find(pr, "wheelOuter") && !find(pr, "gridRow1") && !find(pr, "rightNotes"), "A scale, one pad: no chord settings, no right pad's");
        auto pv = defaults(p);
        pv[idx(p, "chordMode")] = 1;
        CHECK(find(rail::rows(p, pv), "wheelOuter") && !find(rail::rows(p, pv), "gridRow1"), "Chord wheel: the wheel's rings, not the grid's rows");
        pv[idx(p, "chordMode")] = 0; pv[idx(p, "split")] = 1; pv[idx(p, "rightNotes")] = 3;
        CHECK(find(rail::rows(p, pv), "gridRow1") && find(rail::rows(p, pv), "rightNotes"), "Two pads, the right one a chord grid: the grid's rows");
    }
    rs = rail::rows(p, defaults(p));
    auto glide = find(rs, "glide");
    CHECK(glide && glide->widget == Widget::Slider && glide->step == 0.05 && glide->display == "0", "Glide: 21 values, a slider");
    const Json& g = p["settings"][glide->index];
    CHECK(near(rail::stepped(g, 0, 1), 0.05) && rail::display(g, rail::stepped(g, 0.3, 1)) == "0.35", "Glide steps by 0.05 (0.35)");
    CHECK(near(rail::fromFraction(g, 0.33), 0.35), "Glide from a fraction: snapped to 0.05 (%g)", rail::fromFraction(g, 0.33));
    auto sz = find(rs, "size");
    CHECK(sz && sz->widget == Widget::Slider && sz->display == "8", "Size 3..24: 22 values, a slider");
    auto oct = find(rs, "octave");
    CHECK(oct && oct->widget == Widget::Chips && oct->choices.size() == 9, "Octave -4..4: 9 chips");
}

int main() {
    switches();
    choices();
    numbers();
    formatting();
    grouping();
    grid();
    hex();
    tonnetzAndTouchpad();
    return finish();
}
