// odo for computers: a basic MESH. It plays instrument packages (.odoi, compiled with libfaust)
// with controller packages (.odoc, run in wasm3) on a touch screen, from the same files as odo
// on Android. Linux and Windows (SDL3).
//
//   odo [--library DIR]... [--split N] [--controller ID]... [--sound ID]... [--log-midi] [--eink] [--config FILE] [--no-update]
//       [--version] [--self-update] [--setting [SIDE:]INDEX=VALUE]... [--take FILE]...
//       [--selftest out.bmp [--tap X,Y]... [--touch X,Y]... [--move DX,DY] [--expect N] [--size WxH]] (with --take: the take plays)
//
// Library folders hold .odoi and .odoc files (default: the library folder beside odo, and
// ~/.local/share/odo/library). It checks GitHub for a newer odo when it opens (--no-update: not);
// the update line at the bottom of the rail installs it. The rail on
// the left: tiles for the preset, the split, the controller and the sound (in its pack and group),
// each opening its picker beside the rail; a map of the sides when split; then the side's
// controller's settings by group, each opening a panel with all of them (those its mode leaves
// out not shown: mpe::rail's showIf), or the controller's own (the Grid's Touch); then the app's
// (--tap: a self-test's taps there, in window points, before its touches). The rest of
// the window is the controllers: whole, or split side by side, above and below, in three or in
// four, each side playing its own sound. (--controller and --sound repeat: the first is the
// first side's, the next the second's.) Presets, as Android's, keep all of that under a name; and
// on GNOME, odo can keep the desktop's three-finger swipes off it while it has focus (gestures.h).
// Takes (mpe/take.h) record what is played and play it back, ghost fingers showing how.
// Keys: Tab hides the rail, F11 full screen, Esc quits.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "draw.h"
#include "gestures.h"
#include "tokens.h"
#include "update.h"
#include "mpe/controller.h"
#include "mpe/engine.h"
#include "mpe/faust_loader.h"
#include "mpe/package.h"
#include "mpe/preset.h"
#include "mpe/rail.h"
#include "mpe/scala.h"
#include "mpe/take.h"

namespace fs = std::filesystem;
using draw::rgba;

namespace {

constexpr int kRate = 48000;           // instruments are tuned at 48 kHz: SDL converts if the device differs
constexpr float kRail = 300;           // the rail's width, points

// A chevron pointing left (dir -1) or right (dir 1), centred on (cx, cy), `size` points high.
void chevron(SDL_Renderer* r, float cx, float cy, float size, int dir, draw::Color c) {
    float hw = size * 0.3f * float(dir), hh = size / 2, w = std::max(1.5f, size / 7);
    draw::line(r, cx - hw, cy - hh, cx + hw, cy, c, w);
    draw::line(r, cx + hw, cy, cx - hw, cy + hh, c, w);
}

std::string home() { const char* h = std::getenv("HOME"); return h ? h : "."; }

// The folder odo runs from: updates go there, and its own library is beside it.
std::string exeDir() {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string(".") : p.parent_path().string();
}

// ---- the library -----------------------------------------------------------------------------

struct Instrument {
    std::unique_ptr<mpe::Package> pkg;
    int index = -1;                     // in the engine's table, once compiled
    std::string error;
    std::string pack, group;            // where odo's sound packs put it ("" if none does)
    // Versions sit side by side (docs/formats/INSTRUMENT_FORMAT.md): an older one is kept to go back to, but
    // only the newest is listed with the sounds; the sound picker offers the others under Version.
    bool newest = true;
    std::string version() const { return pkg->version(); }
    // Whether a held note keeps sounding (bowed, blown, sung, an organ or most synths) or fades on
    // its own however long it is held (struck, plucked: keys, guitars, bells, drums): mpe::sustains,
    // as odo on Android's sound picker tells.
    bool sustains() const {
        const mpe::Json& t = pkg->manifest["tags"];
        std::vector<std::string> tags;
        for (size_t i = 0; i < t.size(); i++) tags.push_back(t[i].str());
        return mpe::sustains(tags, pkg->manifest["aliases"][size_t(0)].str());
    }
};

struct Library {
    std::vector<Instrument> instruments;
    std::vector<std::unique_ptr<mpe::Package>> controllers;

    // The sounds in the order the sound picker shows them: by pack, group, then as the pack lists
    // them (odo's packs: library/packs/*.json, as on Android); any no pack has, after, by name.
    std::vector<int> soundOrder;
    struct PackGroup { std::string pack, group; std::vector<std::string> ids; int order; };
    std::vector<PackGroup> packs;

    void scanPacks(const std::string& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".json") continue;
            std::ifstream f(e.path());
            std::stringstream ss;
            ss << f.rdbuf();
            mpe::Json j = mpe::Json::parse(ss.str());
            const mpe::Json& gs = j["groups"];
            for (size_t g = 0; g < gs.size(); g++) {
                PackGroup pg{j["name"].str(), gs[g]["name"].str(), {}, int(j["order"].num(99)) * 100 + int(g)};
                for (size_t k = 0; k < gs[g]["instruments"].size(); k++) pg.ids.push_back(gs[g]["instruments"][k].str());
                if (std::none_of(packs.begin(), packs.end(), [&](const PackGroup& x) { return x.pack == pg.pack && x.group == pg.group; })) packs.push_back(pg);
            }
        }
    }

    // Instrument i's versions, newest first (just i if it has no others).
    std::vector<int> versions(int i) const {
        std::vector<int> out;
        for (size_t k = 0; k < instruments.size(); k++)
            if (instruments[k].pkg->id() == instruments[size_t(i)].pkg->id()) out.push_back(int(k));
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            return mpe::compareVersions(instruments[size_t(a)].version(), instruments[size_t(b)].version()) > 0; });
        return out;
    }
    // How a sound is named in odo's own files: its id, and "@<version>" if it is not the newest.
    std::string ref(int i) const {
        const Instrument& in = instruments[size_t(i)];
        return in.newest ? in.pkg->id() : in.pkg->id() + "@" + in.version();
    }

    void order() {
        std::sort(packs.begin(), packs.end(), [](const PackGroup& a, const PackGroup& b) { return a.order < b.order; });
        for (size_t i = 0; i < instruments.size(); i++) instruments[i].newest = versions(int(i))[0] == int(i);
        soundOrder.clear();
        std::vector<bool> placed(instruments.size());
        for (size_t i = 0; i < instruments.size(); i++) placed[i] = !instruments[i].newest;   // an older version: not listed
        for (const PackGroup& pg : packs)
            for (const std::string& id : pg.ids)
                for (size_t i = 0; i < instruments.size(); i++) {
                    const auto& m = instruments[i].pkg->manifest;
                    if (placed[i] || (m["aliases"][size_t(0)].str() != id && instruments[i].pkg->id() != id)) continue;
                    placed[i] = true;
                    instruments[i].pack = pg.pack; instruments[i].group = pg.group;
                    soundOrder.push_back(int(i));
                }
        std::vector<int> rest;
        for (size_t i = 0; i < instruments.size(); i++) if (!placed[i]) { rest.push_back(int(i)); instruments[i].pack = "More"; instruments[i].group = "Other"; }
        std::sort(rest.begin(), rest.end(), [&](int a, int b) { return instruments[size_t(a)].pkg->name() < instruments[size_t(b)].pkg->name(); });
        soundOrder.insert(soundOrder.end(), rest.begin(), rest.end());
        for (size_t i = 0; i < instruments.size(); i++)
            if (!instruments[i].newest) { const Instrument& n = instruments[size_t(versions(int(i))[0])]; instruments[i].pack = n.pack; instruments[i].group = n.group; }
    }

    void scan(const std::string& dir) {
        scanPacks(dir + "/packs");
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) { std::fprintf(stderr, "odo: no folder %s\n", dir.c_str()); return; }
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            std::string ext = f.extension().string(), err;
            if (ext != ".odoi" && ext != ".odoc") continue;
            auto pkg = mpe::Package::read(f.string(), &err);
            if (!pkg) { std::fprintf(stderr, "odo: %s: %s\n", f.c_str(), err.c_str()); continue; }
            // The same package in two library folders: the first one found is used.
            auto same = [&](const mpe::Package& o) { return o.id() == pkg->id() && o.version() == pkg->version(); };
            if (std::any_of(instruments.begin(), instruments.end(), [&](const Instrument& i) { return same(*i.pkg); }) ||
                std::any_of(controllers.begin(), controllers.end(), [&](const auto& c) { return same(*c); })) continue;
            if (pkg->isInstrument()) instruments.push_back({std::move(pkg), -1, "", "", ""});
            else controllers.push_back(std::move(pkg));
        }
    }
};

// The middle of an instrument's range, as a MIDI note (as odo's Native.Instrument.centerNote).
float centerNote(const mpe::Package& p) {
    const mpe::Json& r = p.manifest["range"];
    double lo = r[size_t(0)].num(), hi = r[size_t(1)].num();
    if (lo <= 0 || hi <= 0) return 60;
    return float(69 + 12 * std::log2(std::sqrt(lo * hi) / 440));
}


// ---- splits ------------------------------------------------------------------------------------

// The ways the window can be shared: each side its own controller playing its own sound (a part
// of the engine), up to four.
struct Split { const char* name; int sides; int cols; const char* side[4]; };
const Split kSplits[] = {
    {"Whole", 1, 1, {"Whole"}},
    {"Side by side", 2, 2, {"Left", "Right"}},
    {"Above and below", 2, 1, {"Top", "Bottom"}},
    {"Three side by side", 3, 3, {"Left", "Middle", "Right"}},
    {"Three above each other", 3, 1, {"Top", "Middle", "Bottom"}},
    {"Four", 4, 2, {"Top left", "Top right", "Bottom left", "Bottom right"}},
};
constexpr int kSplitCount = int(sizeof kSplits / sizeof kSplits[0]);
constexpr int kSides = 4;
constexpr float kGap = 6;              // between two sides

// ---- the app -----------------------------------------------------------------------------------

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::unique_ptr<mpe::Engine> engine;
    std::unique_ptr<mpe::MidiInput> midi;
    Library lib;
    std::string cache = home() + "/.cache/odo/instruments";

    // One side of the window: a controller, its settings and its sound (part k of the engine).
    struct Side {
        int controller = 0, sound = 0;
        int sound2 = -1;                // a split controller's second part's sound (the Grid's top), -1: as the first
        int engineIndex2 = -1;          // the instrument its second part plays now
        std::unique_ptr<mpe::ControllerHost> host;
        std::vector<double> settings;   // the controller's, by position
        float x = 0, y = 0, w = 0, h = 0;   // its place in the window
        float initW = 0, initH = 0;     // the size its controller was started with
        int engineIndex = -1;           // the instrument its part plays now
        // How its parts play, as the preset (or take) on gave them (each part's level, effects,
        // arp): a part plays so while its sound is the one named; otherwise with its sound's own.
        std::vector<mpe::preset::Part> parts;
    };
    // The shared effects (reverb room, delay, tempo) and layering the preset on gave; hasEffects
    // false: each sound's own room.
    bool layered = false, hasEffects = false;
    mpe::preset::Effects effects;
    Side sides[kSides];
    int split = 0, active = 0;          // the split, and the side the rail changes
    int count() const { return kSplits[split].sides; }
    Side& cur() { return sides[active]; }

    // Remembered between sessions (~/.config/odo/settings.json): each controller's settings by
    // name (a side after the first keeps its own, "id#2" and on), the split, each side's
    // controller and sound, the rail and full screen.
    std::string configPath = home() + "/.config/odo/settings.json";
    bool persist = true;
    std::map<std::string, std::map<std::string, double>> saved;
    std::string savedController[kSides], savedSound[kSides], savedSound2[kSides];
    int savedSplit = 0, savedActive = 0;
    bool savedFullscreen = false;

    void loadConfig() {
        std::ifstream f(configPath);
        if (!f) return;
        std::stringstream ss;
        ss << f.rdbuf();
        mpe::Json j = mpe::Json::parse(ss.str());
        if (j.type != mpe::Json::Object) return;
        savedController[0] = j["controller"].str();
        savedSound[0] = j["sound"].str();
        const mpe::Json& s = j["sides"];
        for (size_t k = 0; k < s.size() && k < size_t(kSides); k++) {
            if (!s[k]["controller"].str().empty()) savedController[k] = s[k]["controller"].str();
            if (!s[k]["sound"].str().empty()) savedSound[k] = s[k]["sound"].str();
            if (!s[k]["sound2"].str().empty()) savedSound2[k] = s[k]["sound2"].str();
            for (size_t j = 0; j < s[k]["parts"].size(); j++) sides[k].parts.push_back(mpe::preset::partFrom(s[k]["parts"][j]));
        }
        layered = j["layered"].boolean();
        if (j["effects"].type == mpe::Json::Object) { hasEffects = true; effects = mpe::preset::effectsFrom(j["effects"]); }
        savedSplit = std::clamp(int(j["split"].num(0)), 0, kSplitCount - 1);
        savedActive = std::clamp(int(j["active"].num(0)), 0, kSides - 1);
        railHidden = j["railHidden"].boolean();
        savedFullscreen = j["fullscreen"].boolean();
        eink = j["eink"].boolean();
        blockGestures = j["blockGestures"].boolean();
        masterVolume = std::clamp(float(j["volume"].num(1)), 0.f, 2.f);
        presetName = j["preset"].str();
        const mpe::Json& ps = j["presets"];
        for (size_t i = 0; i < ps.size(); i++) {
            Preset p;
            p.name = ps[i]["name"].str();
            if (p.name.empty()) continue;
            p.split = std::clamp(int(ps[i]["split"].num(0)), 0, kSplitCount - 1);
            const mpe::Json& sd = ps[i]["sides"];
            for (size_t k = 0; k < sd.size() && k < size_t(kSides); k++) {
                PresetSide s{sd[k]["controller"].str(), sd[k]["sound"].str(), sd[k]["sound2"].str(), {}, {}};
                for (const auto& kv : sd[k]["settings"].o) s.settings[kv.first] = kv.second.num();
                for (size_t j = 0; j < sd[k]["parts"].size(); j++) s.parts.push_back(mpe::preset::partFrom(sd[k]["parts"][j]));
                p.sides.push_back(std::move(s));
            }
            p.layered = ps[i]["layered"].boolean();
            if (ps[i]["effects"].type == mpe::Json::Object) { p.hasEffects = true; p.effects = mpe::preset::effectsFrom(ps[i]["effects"]); }
            userPresets.push_back(std::move(p));
        }
        const mpe::Json& c = j["controllers"];
        for (const auto& kv : c.o)
            for (const auto& sv : kv.second.o) saved[kv.first][sv.first] = sv.second.num();
    }

    static std::string quoted(const std::string& v) {
        std::string o = "\"";
        for (char ch : v) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; }
        return o + "\"";
    }

    std::string controllerId(int k) const {
        return lib.controllers.empty() ? "" : lib.controllers[size_t(sides[k].controller)]->id();
    }
    std::string soundId(int k) const {
        return lib.instruments.empty() ? "" : lib.ref(sides[k].sound);
    }
    std::string sound2Id(int k) const {
        return lib.instruments.empty() || sides[k].sound2 < 0 ? "" : lib.ref(sides[k].sound2);
    }
    // Where side k keeps its controller's settings.
    std::string settingsKey(int k) const { return k == 0 ? controllerId(0) : controllerId(k) + "#" + std::to_string(k + 1); }

    void saveConfig() {
        if (!loadingPreset) keepInPreset();
        if (!persist) return;
        std::error_code ec;
        fs::create_directories(fs::path(configPath).parent_path(), ec);
        std::ofstream f(configPath + ".tmp");
        f.precision(17);     // every digit: a strike zone map is a seven-digit number
        f << "{\n  \"controller\": " << quoted(controllerId(0)) << ",\n  \"sound\": " << quoted(soundId(0))
          << ",\n  \"split\": " << split << ",\n  \"active\": " << active << ",\n  \"sides\": [";
        for (int k = 0; k < kSides; k++) {
            f << (k ? ", " : "") << "{\"controller\": " << quoted(controllerId(k)) << ", \"sound\": " << quoted(soundId(k))
              << ", \"sound2\": " << quoted(sound2Id(k));
            if (!sides[k].parts.empty()) {
                f << ", \"parts\": [";
                for (size_t j = 0; j < sides[k].parts.size(); j++) f << (j ? ", " : "") << mpe::preset::toJson(mpe::preset::partJson(sides[k].parts[j]));
                f << "]";
            }
            f << "}";
        }
        f << "]";
        if (layered) f << ",\n  \"layered\": true";
        if (hasEffects) f << ",\n  \"effects\": " << mpe::preset::toJson(mpe::preset::effectsJson(effects));
        f << ",\n  \"railHidden\": " << (railHidden ? "true" : "false")
          << ",\n  \"eink\": " << (eink ? "true" : "false")
          << ",\n  \"blockGestures\": " << (blockGestures ? "true" : "false")
          << ",\n  \"volume\": " << masterVolume
          << ",\n  \"fullscreen\": " << ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "true" : "false")
          << ",\n  \"controllers\": {";
        bool first = true;
        for (const auto& kv : saved) {
            f << (first ? "\n    " : ",\n    ") << quoted(kv.first) << ": {";
            bool f2 = true;
            for (const auto& sv : kv.second) { f << (f2 ? "" : ", ") << quoted(sv.first) << ": " << sv.second; f2 = false; }
            f << "}";
            first = false;
        }
        f << "\n  },\n  \"preset\": " << quoted(presetName) << ",\n  \"presets\": [";
        for (size_t i = 0; i < userPresets.size(); i++) {
            const Preset& p = userPresets[i];
            f << (i ? ",\n    " : "\n    ") << "{\"name\": " << quoted(p.name) << ", \"split\": " << p.split << ", \"sides\": [";
            for (size_t k = 0; k < p.sides.size(); k++) {
                const PresetSide& s = p.sides[k];
                f << (k ? ", " : "") << "{\"controller\": " << quoted(s.controller) << ", \"sound\": " << quoted(s.sound)
                  << ", \"sound2\": " << quoted(s.sound2) << ", \"settings\": {";
                bool f2 = true;
                for (const auto& sv : s.settings) { f << (f2 ? "" : ", ") << quoted(sv.first) << ": " << sv.second; f2 = false; }
                f << "}";
                if (!s.parts.empty()) {
                    f << ", \"parts\": [";
                    for (size_t j = 0; j < s.parts.size(); j++) f << (j ? ", " : "") << mpe::preset::toJson(mpe::preset::partJson(s.parts[j]));
                    f << "]";
                }
                f << "}";
            }
            f << "]";
            if (p.layered) f << ", \"layered\": true";
            if (p.hasEffects) f << ", \"effects\": " << mpe::preset::toJson(mpe::preset::effectsJson(p.effects));
            f << "}";
        }
        f << (userPresets.empty() ? "]\n}\n" : "\n  ]\n}\n");
        f.close();
        fs::rename(configPath + ".tmp", configPath, ec);
    }

    void setRailHidden(bool on) { railHidden = on; saveConfig(); }
    // E-ink (as odo on Android's Look): black on white, and the controllers told so, started again.
    void setEink(bool on) {
        if (on == eink) return;
        eink = on;
        for (Side& s : sides) s.initW = s.initH = 0;
        saveConfig();
    }
    void setFullscreen(bool on) { SDL_SetWindowFullscreen(window, on); saveConfig(); }
    bool fullscreen() const { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; }
    bool railHidden = false, eink = false, logMidi = false;

    // The master volume, 0 to 2 (200%): 1 is the engine's usual level (0.8 into its soft limiter).
    float masterVolume = 1;
    void setMasterVolume(float v, bool save) {
        masterVolume = std::clamp(v, 0.f, 2.f);
        engine->setVolume(0.8f * masterVolume);
        if (save) saveConfig();
    }

    // Block system gestures (gestures.h): on, and what to tell the user about it ("" when it works).
    bool blockGestures = false;
    std::string gestureNote;
    void setBlockGestures(bool on) {
        blockGestures = on;
        gestureNote = gestures::set(on);
        saveConfig();
    }

    // ---- presets ---------------------------------------------------------------------------------

    // As odo on Android's: a preset is the split and, for each side, its controller, its sound (and
    // a split controller's second sound) and the controller's settings. A built-in one is a starting
    // point: each side's settings only those it names, the rest left as they are. Yours keep all of
    // them, and what you change while one of yours is on view is kept in it (as Android's). The one
    // on view and yours are saved with the rest of the settings.
    // A preset opened from a file (mpe::preset) may carry what this app has no settings for: each
    // part's own settings (its volume, effects, arp), layering and the shared effects, as odo on
    // Android has them. They are kept, and given back when the preset is shared, while its sounds stay.
    struct PresetSide { std::string controller, sound, sound2; std::map<std::string, double> settings;
                        std::vector<mpe::preset::Part> parts; };
    struct Preset { std::string name; int split = 0; std::vector<PresetSide> sides; bool builtIn = false;
                    bool layered = false, hasEffects = false; mpe::preset::Effects effects; };
    std::vector<Preset> userPresets;
    std::string presetName;               // the one on view, or ""
    bool loadingPreset = false;
    bool confirmDelete = false;           // "Delete" was tapped once: the next tap deletes

    static std::vector<Preset> builtInPresets() {
        auto side = [](const char* c, const char* s, std::map<std::string, double> st = {}, const char* s2 = "") {
            return PresetSide{std::string("patrickreynolds/") + c, std::string("patrickreynolds/") + s,
                              *s2 ? std::string("patrickreynolds/") + s2 : std::string(), std::move(st), {}};
        };
        auto built = [](const char* name, int split, std::vector<PresetSide> sides) {
            Preset p; p.name = name; p.split = split; p.sides = std::move(sides); p.builtIn = true; return p;
        };
        return {
            built("Grand piano", 0, {side("grid", "piano", {{"split", 0}, {"slide", 0}})}),
            built("Slide a flute", 0, {side("grid", "flute", {{"split", 0}, {"slide", 1}})}),
            built("Bass below, lead above", 0, {side("grid", "bass-guitar", {{"split", 1}, {"slide", 0}}, "lead")}),
            built("Cello chords", 0, {side("tonnetz", "cello")}),
            built("Dreamy pad", 0, {side("touchpad", "pad", {{"split", 0}})}),
            built("Keys over drums", 2, {side("grid", "rhodes", {{"split", 0}, {"slide", 0}}), side("grid", "kit", {{"split", 0}, {"slide", 0}})}),
        };
    }

    int controllerIndex(const std::string& id) const {
        for (size_t i = 0; i < lib.controllers.size(); i++) if (lib.controllers[i]->id() == id) return int(i);
        return -1;
    }
    // A sound by its id (or old id): the newest version; "id@1.0.0": that version.
    int soundIndex(const std::string& ref) const {
        size_t at = ref.find('@');
        std::string id = ref.substr(0, at), version = at == std::string::npos ? "" : ref.substr(at + 1);
        for (size_t i = 0; i < lib.instruments.size(); i++) {
            const Instrument& in = lib.instruments[i];
            if (in.pkg->id() != id && in.pkg->manifest["aliases"][size_t(0)].str() != id) continue;
            if (version.empty() ? in.newest : in.version() == version) return int(i);
        }
        return -1;
    }
    // Built-in ones whose controllers and sounds are all in the library, then yours.
    std::vector<Preset> allPresets() const {
        std::vector<Preset> out;
        for (Preset& p : builtInPresets()) {
            bool ok = true;
            for (const PresetSide& s : p.sides)
                ok = ok && controllerIndex(s.controller) >= 0 && soundIndex(s.sound) >= 0 && (s.sound2.empty() || soundIndex(s.sound2) >= 0);
            if (ok) out.push_back(std::move(p));
        }
        out.insert(out.end(), userPresets.begin(), userPresets.end());
        return out;
    }
    Preset* userPreset(const std::string& name) {
        for (Preset& p : userPresets) if (p.name == name) return &p;
        return nullptr;
    }

    // What is playing now, as a preset.
    Preset current(const std::string& name) const {
        Preset p;
        p.name = name;
        p.split = split;
        p.layered = layered; p.hasEffects = hasEffects; p.effects = effects;   // as the preset on gave them
        for (int k = 0; k < count(); k++) {
            PresetSide s{controllerId(k), soundId(k), sound2Id(k), {}, sides[k].parts};
            if (sides[k].host) {
                const mpe::Json& st = sides[k].host->manifest()["settings"];
                for (size_t i = 0; i < st.size() && i < sides[k].settings.size(); i++) s.settings[st[i]["id"].str()] = sides[k].settings[i];
            }
            p.sides.push_back(std::move(s));
        }
        return p;
    }

    // What plays now is as the preset has it (only the settings it names).
    bool matches(const Preset& p) const {
        if (p.split != split || p.sides.size() != size_t(count())) return false;
        for (int k = 0; k < count(); k++) {
            const PresetSide& s = p.sides[size_t(k)];
            if (s.controller != controllerId(k) || s.sound != soundId(k) || s.sound2 != sound2Id(k)) return false;
            if (!sides[k].host) continue;
            const mpe::Json& st = sides[k].host->manifest()["settings"];
            for (size_t i = 0; i < st.size() && i < sides[k].settings.size(); i++) {
                auto it = s.settings.find(st[i]["id"].str());
                if (it != s.settings.end() && std::fabs(it->second - sides[k].settings[i]) > 1e-9) return false;
            }
        }
        return true;
    }

    // A built-in preset on view, changed since it was loaded.
    bool presetEdited() const {
        if (presetName.empty()) return false;
        for (const Preset& p : allPresets()) if (p.name == presetName) return p.builtIn && !matches(p);
        return false;
    }

    // Something changed: kept in your preset on view.
    void keepInPreset() {
        Preset* p = userPreset(presetName);
        if (p && !matches(*p)) *p = current(p->name);
    }

    // onView false: what plays is changed, but no preset is on view (a take's settings, not one of yours).
    void loadPreset(const Preset& p, bool onView = true) {
        loadingPreset = true;
        bool keep = persist;
        persist = false;
        layered = p.layered;
        hasEffects = p.hasEffects;
        effects = p.effects;
        for (size_t k = 0; k < p.sides.size() && k < size_t(kSides); k++) {
            const PresetSide& s = p.sides[k];
            Side& sd = sides[k];
            sd.parts = s.parts;
            if (int c = controllerIndex(s.controller); c >= 0) sd.controller = c;
            if (int n = soundIndex(s.sound); n >= 0) sd.sound = n;
            sd.sound2 = s.sound2.empty() ? -1 : soundIndex(s.sound2);
            // Its settings over the ones kept for that controller (a side after the first starts
            // from the first side's, as when a controller is chosen); then it starts again with them.
            int ki = int(k);
            auto& mine = saved[settingsKey(ki)];
            if (mine.empty() && ki > 0) mine = saved[controllerId(ki)];
            for (const auto& kv : s.settings) mine[kv.first] = kv.second;
            if (sd.host) sd.host->stop();
            sd.host.reset();
        }
        for (size_t k = p.sides.size(); k < size_t(kSides); k++) sides[k].parts.clear();
        setSplit(p.split);
        presetName = onView ? p.name : "";
        confirmDelete = false;
        persist = keep;
        loadingPreset = false;
        saveConfig();
    }

    // A name not yet taken: "Cello", then "Cello 2", "Cello 3"...
    std::string freeName(std::string base, const std::string& except = "") const {
        while (!base.empty() && base.back() == ' ') base.pop_back();
        while (!base.empty() && base.front() == ' ') base.erase(base.begin());
        if (base.empty()) base = "Preset";
        auto taken = [&](const std::string& n) {
            if (n == except) return false;
            for (const Preset& p : allPresets()) if (p.name == n) return true;
            return false;
        };
        if (!taken(base)) return base;
        std::string stem = base;
        size_t sp = stem.find_last_of(' ');
        if (sp != std::string::npos && sp + 1 < stem.size() && stem.find_first_not_of("0123456789", sp + 1) == std::string::npos) stem.resize(sp);
        for (int n = 2;; n++) if (!taken(stem + " " + std::to_string(n))) return stem + " " + std::to_string(n);
    }

    // What plays now, as a name: each side's sound (and second sound), joined.
    std::string soundsName() const {
        std::string out;
        for (int k = 0; k < count() && !lib.instruments.empty(); k++)
            for (int j = 0; j < partsOf(k); j++)
                out += (out.empty() ? "" : " + ") + lib.instruments[size_t(j ? sound2Of(k) : sides[k].sound)].pkg->name();
        return out;
    }

    // Keep what plays now as a new preset of yours, after the one on view, and switch to it.
    void newPreset() {
        Preset p = current(freeName(soundsName()));
        size_t at = userPresets.size();
        for (size_t i = 0; i < userPresets.size(); i++) if (userPresets[i].name == presetName) at = i + 1;
        presetName = p.name;
        userPresets.insert(userPresets.begin() + long(at), std::move(p));
        confirmDelete = false;
        saveConfig();
    }

    void renamePreset(const std::string& from, const std::string& to) {
        Preset* p = userPreset(from);
        if (!p) return;
        p->name = freeName(to, from);
        if (presetName == from) presetName = p->name;
        saveConfig();
    }

    void deletePreset(const std::string& name) {
        userPresets.erase(std::remove_if(userPresets.begin(), userPresets.end(), [&](const Preset& p) { return p.name == name; }), userPresets.end());
        if (presetName == name) presetName.clear();
        confirmDelete = false;
        saveConfig();
    }

    // ---- preset files (docs/formats/PRESET_FORMAT.md) ------------------------------------------------------

    // A preset as a file: each side's controller, its settings and its parts (the sound, the
    // second sound, then what the preset carries from a file).
    mpe::preset::Preset toFile(const Preset& p) const {
        mpe::preset::Preset f;
        f.name = p.name;
        f.madeBy = std::string("odo ") + ODO_DESKTOP_VERSION + " (computer)";
        f.split = p.split;
        f.layered = p.layered;
        f.hasEffects = p.hasEffects;
        f.effects = p.effects;
        for (const PresetSide& s : p.sides) {
            mpe::preset::Side fs;
            fs.controller = s.controller;
            fs.settings.type = mpe::Json::Object;
            for (const auto& kv : s.settings) {
                mpe::Json v; v.type = mpe::Json::Number; v.n = kv.second;
                fs.settings.o.emplace_back(kv.first, v);
            }
            fs.parts = s.parts;
            // The sound, and its version: in a file, always (a preset plays the version it was made with).
            auto put = [&](size_t j, const std::string& ref) {
                if (fs.parts.size() <= j) fs.parts.resize(j + 1);
                int n = soundIndex(ref);
                std::string sound = ref.substr(0, ref.find('@')), version = n >= 0 ? lib.instruments[size_t(n)].version() : "";
                if (fs.parts[j].sound != sound) { fs.parts[j] = mpe::preset::Part(); fs.parts[j].sound = sound; }
                fs.parts[j].version = version;
            };
            put(0, s.sound);
            if (!s.sound2.empty()) put(1, s.sound2);
            f.sides.push_back(std::move(fs));
        }
        return f;
    }

    // A preset file's text as one of yours, after the rest, and on view. A controller or sound
    // this computer doesn't have: what that side plays now, and `note` says so.
    bool openPresetText(const std::string& text, std::string& note) {
        mpe::preset::Preset f;
        std::string err;
        if (!mpe::preset::parse(text, f, &err)) { note = err; return false; }
        usePreset(f, note, true, "Opened");
        return true;
    }

    // A preset on: `keep`, as a new preset of yours (on view), as opening a file does; or not, only
    // playing with it (a take's settings). `note`: "<said> “its name”", and what was left out.
    void usePreset(const mpe::preset::Preset& f, std::string& note, bool keep, const std::string& said) {
        Preset p;
        p.name = keep ? freeName(f.name) : f.name;
        p.split = std::clamp(f.split, 0, kSplitCount - 1);
        p.layered = f.layered;
        p.hasEffects = f.hasEffects;
        p.effects = f.effects;
        int missingCtl = 0, missingSound = 0, olderMissing = 0;
        size_t sidesNow = size_t(kSplits[p.split].sides);     // a split's sides; a file may give more
        for (size_t k = 0; k < f.sides.size() && k < sidesNow; k++) {
            const mpe::preset::Side& fs = f.sides[k];
            PresetSide s;
            int ki = int(k) < count() ? int(k) : 0;
            s.controller = fs.controller;
            if (s.controller.empty() || controllerIndex(s.controller) < 0) {
                if (!s.controller.empty()) missingCtl++;
                s.controller = controllerId(ki);
            }
            s.parts = fs.parts;
            // The sounds by their ids here; the version the file names, if this computer has it, else
            // the newest (and the note says so).
            std::vector<int> found(s.parts.size(), -1);
            for (size_t j = 0; j < s.parts.size(); j++) {
                mpe::preset::Part& q = s.parts[j];
                int n = q.version.empty() ? -1 : soundIndex(q.sound + "@" + q.version);
                if (n < 0 && !q.version.empty() && soundIndex(q.sound) >= 0 && j < 2) olderMissing++;
                if (n < 0) n = soundIndex(q.sound);
                found[j] = n;
                if (n >= 0) { q.sound = lib.instruments[size_t(n)].pkg->id(); q.version = lib.instruments[size_t(n)].version(); }
                else if (j < 2) missingSound++;
            }
            s.sound = !found.empty() && found[0] >= 0 ? lib.ref(found[0]) : soundId(ki);
            s.sound2 = found.size() > 1 && found[1] >= 0 ? lib.ref(found[1]) : "";
            for (const auto& kv : fs.settings.o) s.settings[kv.first] = kv.second.num();
            p.sides.push_back(std::move(s));
        }
        std::string name = p.name;
        if (keep) userPresets.push_back(p);
        loadPreset(p, keep);
        note = said + " \xE2\x80\x9C" + name + "\xE2\x80\x9D";
        if (missingCtl) note += missingCtl == 1 ? ". Its controller isn't on this computer: the one in use plays it"
                                                : ". Its controllers aren't on this computer: the ones in use play it";
        if (missingSound) note += std::string(". ") + std::to_string(missingSound)
                                + (missingSound == 1 ? " sound isn't" : " sounds aren't") + " here: what was playing plays instead";
        if (olderMissing) note += std::string(". ") + std::to_string(olderMissing)
                                + (olderMissing == 1 ? " sound's version isn't" : " sounds' versions aren't") + " here: the newest plays instead";
        if (f.sides.size() > sidesNow) note += ". Its sides after the " + std::string(sidesNow == 1 ? "first" : sidesNow == 2 ? "second" : sidesNow == 3 ? "third" : "fourth") + " are left out";
    }

    bool openPresetFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::string note;
        if (!in) { status = "Could not open " + path; return false; }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bool ok = openPresetText(text, note);
        status = note;
        return ok;
    }

    bool savePresetFile(const std::string& name, std::string path) {
        // Yours as kept; a built-in one as it is, or (changed since it was loaded) what plays now.
        const Preset* p = nullptr;
        std::vector<Preset> all = allPresets();
        for (const Preset& q : all) if (q.name == name) p = &q;
        Preset now = p && (!p->builtIn || matches(*p)) ? *p : current(name.empty() ? freeName(soundsName()) : name);
        if (path.size() < 10 || path.compare(path.size() - 10, 10, mpe::preset::kExtension) != 0) path += mpe::preset::kExtension;
        std::ofstream out(path, std::ios::binary);
        out << mpe::preset::write(toFile(now));
        out.close();
        status = out ? "Shared \xE2\x80\x9C" + now.name + "\xE2\x80\x9D as " + fs::path(path).filename().string()
                     : "Could not write " + path;
        return bool(out);
    }

    // The system's file dialogs answer on their own thread: what they chose waits here for the next frame.
    std::mutex dialogLock;
    std::vector<std::pair<bool, std::string>> dialogDone;   // (save?, path)
    std::string sharing;                                     // the preset a save dialog is for
    static void dialogAnswer(void* self, const char* const* files, int, bool save) {
        if (!files || !files[0]) return;                     // an error, or cancelled
        App* a = static_cast<App*>(self);
        std::lock_guard<std::mutex> g(a->dialogLock);
        a->dialogDone.emplace_back(save, files[0]);
    }
    static void openAnswer(void* self, const char* const* files, int filter) { dialogAnswer(self, files, filter, false); }
    static void saveAnswer(void* self, const char* const* files, int filter) { dialogAnswer(self, files, filter, true); }
    static constexpr SDL_DialogFileFilter kPresetFilter[] = {{"odo presets", "odopreset"}};

    void askToShare() {
        sharing = presetName;
        sharingTake.clear();
        mpe::preset::Preset f; f.name = presetName.empty() ? soundsName() : presetName;
        std::string at = (fs::path(home()) / mpe::preset::fileName(f)).string();
        SDL_ShowSaveFileDialog(saveAnswer, this, window, kPresetFilter, 1, at.c_str());
    }
    void askToOpen() { openingTake = false; openingScala = -1; SDL_ShowOpenFileDialog(openAnswer, this, window, kPresetFilter, 1, home().c_str(), false); }

    void collectDialogs() {
        std::vector<std::pair<bool, std::string>> done;
        { std::lock_guard<std::mutex> g(dialogLock); done.swap(dialogDone); }
        for (const auto& d : done) {
            if (d.first && !sharingTake.empty()) saveTakeFile(d.second);
            else if (d.first) savePresetFile(sharing, d.second);
            else if (openingScala >= 0) openScalaFile(size_t(openingScala), d.second);
            else if (openingTake) openTakeFile(d.second);
            else if (openPresetFile(d.second)) panelKey = kNone;
        }
        if (!done.empty()) { openingTake = false; openingScala = -1; }
    }

    // Compiling an instrument takes a moment: done on a worker, handed back to the main thread.
    std::thread worker;
    std::atomic<bool> compiling{false};
    std::mutex doneLock;
    int doneSound = -1, doneIndex = -1;
    std::string doneError;
    std::string status;

    std::unique_ptr<Updater> updater;
    char** argv = nullptr;                 // to start again after an update

    // Each pointer (a finger, the mouse, a self-test's touch) plays the side it went down on,
    // as that controller's touch id, until it lifts.
    struct Pointer { int side, id; };
    std::map<int64_t, Pointer> pointers;
    int nextFinger = 1;

    // Before SDL_Quit: the audio stream belongs to SDL.
    void shutdown() {
        if (recorder.recording()) toggleRecord();       // closed while recording: keep what was played
        if (worker.joinable()) worker.join();
        for (Side& s : sides) { if (s.host) s.host->stop(); s.host.reset(); }
        if (audio) SDL_DestroyAudioStream(audio);
        audio = nullptr;
    }
    ~App() { if (worker.joinable()) worker.join(); }

    float railW() const { return railHidden ? 0 : kRail; }

    static void SDLCALL audioCallback(void* self, SDL_AudioStream* stream, int additional, int) {
        App* a = static_cast<App*>(self);
        int frames = additional / int(2 * sizeof(float));
        if (frames <= 0) return;
        static thread_local std::vector<float> buf;
        buf.resize(size_t(frames) * 2);
        a->engine->render(buf.data(), frames, mpe::steadyNowNs());
        SDL_PutAudioStreamData(stream, buf.data(), int(buf.size() * sizeof(float)));
    }

    bool startAudio() {
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "128");   // small buffers: about 3 ms each
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &App::audioCallback, this);
        if (!audio) { std::fprintf(stderr, "odo: no audio: %s\n", SDL_GetError()); return false; }
        SDL_ResumeAudioStreamDevice(audio);
        return true;
    }

    // ---- sides ----------------------------------------------------------------------------------

    // Where each side is: the window right of the rail, shared as the split says.
    void layoutSides() {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        float ax = railW(), aw = float(ww) - ax, ah = float(wh);
        const Split& sp = kSplits[split];
        int cols = sp.cols, rows = (sp.sides + cols - 1) / cols;
        float cw = (aw - kGap * float(cols - 1)) / float(cols), rh = (ah - kGap * float(rows - 1)) / float(rows);
        for (int k = 0; k < sp.sides; k++) {
            Side& s = sides[k];
            s.x = ax + float(k % cols) * (cw + kGap);
            s.y = float(k / cols) * (rh + kGap);
            s.w = cw;
            s.h = rh;
        }
    }

    int sideAt(float x, float y) const {
        for (int k = 0; k < count(); k++) {
            const Side& s = sides[k];
            if (x >= s.x - kGap / 2 && x < s.x + s.w + kGap / 2 && y >= s.y - kGap / 2 && y < s.y + s.h + kGap / 2) return k;
        }
        return -1;
    }

    // How many sounds side k's controller plays now: its parts, as many as its "partsFrom" setting
    // says (the Grid's split: one, or two), or all of them; 1 to 2.
    int partsOf(int k) const {
        const Side& sd = sides[k];
        if (!sd.host) return 1;
        const mpe::Json& m = sd.host->manifest();
        int n = std::max(1, int(m["parts"].size()));
        std::string from = m["partsFrom"].str();
        if (!from.empty()) {
            const mpe::Json& st = m["settings"];
            for (size_t i = 0; i < st.size() && i < sd.settings.size(); i++)
                if (st[i]["id"].str() == from) { n = std::min(n, 1 + int(sd.settings[i])); break; }
        }
        return std::clamp(n, 1, 2);
    }
    // The engine part of side k's first sound: the sides' parts one after another (at most kParts in all).
    int partBase(int k) const {
        int b = 0;
        for (int j = 0; j < k; j++) b += partsOf(j);
        return std::min(b, mpe::kParts - 1);
    }
    int sound2Of(int k) const { return sides[k].sound2 < 0 ? sides[k].sound : sides[k].sound2; }

    // Each side's controller sends on its own MIDI channels, to its own parts: a side's notes never
    // take another's channel or sound.
    void route(int k) {
        if (!sides[k].host) return;
        int per = 15 / count();
        sides[k].host->setPart(partBase(k));
        sides[k].host->setChannels(1 + k * per, per);
    }
    // After the parts in use change: every side routed again and the sounds put in their parts.
    void reroute() {
        for (int k = 0; k < count(); k++) { route(k); sides[k].engineIndex = sides[k].engineIndex2 = -1; }
        applySounds();
        ensureLoading();
    }

    void setSplit(int n) {
        split = (n % kSplitCount + kSplitCount) % kSplitCount;
        for (int k = 0; k < kSides; k++) {
            Side& s = sides[k];
            if (k >= count()) {                  // a side no longer shown: its notes stop
                if (s.host) s.host->stop();
                s.host.reset();
                s.engineIndex = -1;
                continue;
            }
            if (!s.host) chooseController(k, s.controller);
            route(k);
            s.initW = s.initH = 0;               // started again, at its new size
        }
        active = std::min(active, count() - 1);
        pointers.clear();
        applySounds();
        ensureLoading();
        saveConfig();
    }

    void setActive(int k) { active = (k % count() + count()) % count(); settingsScroll = 0; saveConfig(); }

    // ---- sounds ---------------------------------------------------------------------------------

    // The engine's parts: one per side (two for a split Grid's), each with its sound's effects; and
    // each sound in its part.
    void applySounds() {
        mpe::Performance p;
        int total = partBase(count() - 1) + partsOf(count() - 1);
        p.layout = total > 1 ? mpe::Layout::Split : mpe::Layout::Single;
        for (int k = 0; k < count() && !lib.instruments.empty(); k++)
        for (int j = 0; j < partsOf(k); j++) {
            int part = std::min(partBase(k) + j, mpe::kParts - 1);
            const mpe::Package& pkg = *lib.instruments[size_t(j ? sound2Of(k) : sides[k].sound)].pkg;
            const mpe::Json& s = pkg.manifest["sound"];
            mpe::PartConfig& c = p.part[part];
            c.reverb = float(s["reverb"].num(0.15));
            c.delay = float(s["delay"].num(0));
            c.brightness = float(s["brightness"].num(1));
            c.drive = float(s["drive"].num(0));
            c.pressureCurve = float(s["pressureCurve"].num(1));
            c.release = float(s["release"].num(0));
            // The preset on gave this part how it plays (as odo on Android's parts): while its sound
            // is the one it named.
            const std::vector<mpe::preset::Part>& given = sides[k].parts;
            if (size_t(j) < given.size() && given[size_t(j)].full && given[size_t(j)].sound == pkg.id()) {
                const mpe::preset::Part& q = given[size_t(j)];
                c.transpose = q.transpose;
                c.volume = float(q.volume);
                c.brightness = float(q.brightness);
                c.drive = float(q.drive);
                c.pressureCurve = float(q.pressureCurve);
                c.reverb = float(q.reverb);
                c.delay = float(q.delay);
                c.release = float(q.release);
                c.arp = q.arp.on;
                c.order = mpe::ArpOrder(q.arp.order);
                c.rate = float(q.arp.rate);
                c.gate = float(q.arp.gate);
                c.octaves = q.arp.octaves;
                c.latch = q.arp.latch;
            }
            if (part == 0) {                     // the room is shared: the first side's
                p.reverbSize = float(s["reverbSize"].num(0.5));
                p.reverbDamp = float(s["reverbDamp"].num(0.4));
            }
        }
        if (hasEffects) {                        // the preset's room and clock, over the sounds'
            p.bpm = float(effects.bpm);
            p.reverbSize = float(effects.reverbSize);
            p.reverbDamp = float(effects.reverbDamp);
            p.delayBeats = float(effects.delayBeats);
            p.delayFeedback = float(effects.delayFeedback);
            p.delayPingPong = effects.delayPingPong;
        }
        engine->setPerformance(p);
        for (int k = 0; k < count() && !lib.instruments.empty(); k++) {
            Side& sd = sides[k];
            for (int j = 0; j < partsOf(k); j++) {
                const Instrument& in = lib.instruments[size_t(j ? sound2Of(k) : sd.sound)];
                if (in.index < 0) continue;
                int& now = j ? sd.engineIndex2 : sd.engineIndex;
                if (now != in.index) { engine->setInstrument(std::min(partBase(k) + j, mpe::kParts - 1), in.index); now = in.index; }
                if (sd.host) sd.host->instrument(j, centerNote(*in.pkg));
            }
            syncDrums(k);
        }
    }

    // A drum kit (a sound in the drums group: its first alias "drums/..."), as odo on Android tells.
    bool isDrums(int n) const {
        if (n < 0 || size_t(n) >= lib.instruments.size()) return false;
        return lib.instruments[size_t(n)].pkg->manifest["aliases"][size_t(0)].str().rfind("drums/", 0) == 0;
    }
    // A controller's "drumsFrom" switches follow each part's sound (the touch pad lays out drums).
    void syncDrums(int k) {
        Side& sd = sides[k];
        if (!sd.host) return;
        const mpe::Json& from = sd.host->manifest()["drumsFrom"];
        const mpe::Json& st = sd.host->manifest()["settings"];
        for (size_t j = 0; j < from.size() && j < 2; j++)
            for (size_t i = 0; i < st.size() && i < sd.settings.size(); i++) {
                if (st[i]["id"].str() != from[j].str()) continue;
                double v = isDrums(j ? sound2Of(k) : sd.sound) ? 1 : 0;
                if (sd.settings[i] != v) { sd.host->setting(int(i), v); keepSetting(k, i, v); }
            }
    }

    void chooseSound2(int k, int n) {
        if (lib.instruments.empty()) return;
        n = (n % int(lib.instruments.size()) + int(lib.instruments.size())) % int(lib.instruments.size());
        sides[k].sound2 = n;
        if (lib.instruments[size_t(n)].index >= 0) { status.clear(); applySounds(); }
        else ensureLoading();
        saveConfig();
    }

    // The sound after (dir 1) or before (-1) sound n in the sound picker's order (older versions are not in it).
    int nextSound(int n, int dir) const {
        const std::vector<int>& o = lib.soundOrder;
        if (o.empty()) return n;
        int newest = lib.versions(n)[0];
        auto it = std::find(o.begin(), o.end(), newest);
        int at = it == o.end() ? 0 : int(it - o.begin());
        return o[size_t(((at + dir) % int(o.size()) + int(o.size())) % int(o.size()))];
    }

    void chooseSound(int k, int n) {
        if (lib.instruments.empty()) return;
        n = (n % int(lib.instruments.size()) + int(lib.instruments.size())) % int(lib.instruments.size());
        sides[k].sound = n;
        if (lib.instruments[size_t(n)].index >= 0) { status.clear(); applySounds(); }
        else ensureLoading();
        saveConfig();
    }

    // Compile the first sound a side wants that isn't ready (one at a time).
    void ensureLoading() {
        if (compiling || lib.instruments.empty()) return;
        for (int k = 0; k < count(); k++)
        for (int j = 0; j < partsOf(k); j++) {
            int n = j ? sound2Of(k) : sides[k].sound;
            Instrument& in = lib.instruments[size_t(n)];
            if (in.index >= 0 || !in.error.empty()) continue;
            compiling = true;
            status = "Loading " + in.pkg->name() + "...";
            if (worker.joinable()) worker.join();
            worker = std::thread([this, n] {
                std::string err;
                int idx = mpe::loadInstrumentPackage(*lib.instruments[size_t(n)].pkg, cache, &err);
                std::lock_guard<std::mutex> g(doneLock);
                doneSound = n; doneIndex = idx; doneError = err;
                compiling = false;
            });
            return;
        }
    }

    void collectCompiled() {
        {
            std::lock_guard<std::mutex> g(doneLock);
            if (doneSound < 0) return;
            Instrument& in = lib.instruments[size_t(doneSound)];
            in.index = doneIndex;
            in.error = doneError;
            if (doneIndex < 0) status = "Could not load " + in.pkg->name() + ": " + doneError;
            else status.clear();
            doneSound = -1;
        }
        applySounds();
        ensureLoading();                         // the next side's, or one picked meanwhile
    }

    bool soundsReady() const {
        for (int k = 0; k < count(); k++)
            for (int j = 0; j < partsOf(k); j++)
                if (lib.instruments.empty() || lib.instruments[size_t(j ? sound2Of(k) : sides[k].sound)].index < 0) return false;
        return true;
    }

    // ---- controllers ----------------------------------------------------------------------------

    void chooseController(int k, int n) {
        if (lib.controllers.empty()) return;
        n = (n % int(lib.controllers.size()) + int(lib.controllers.size())) % int(lib.controllers.size());
        Side& sd = sides[k];
        if (sd.host) sd.host->stop();
        sd.controller = n;
        for (auto it = pointers.begin(); it != pointers.end();) it = it->second.side == k ? pointers.erase(it) : std::next(it);
        sd.host = std::make_unique<mpe::ControllerHost>([this](const uint8_t* b, int len) {
            if (logMidi) std::printf("midi %02x %02x %02x\n", b[0], len > 1 ? b[1] : 0, len > 2 ? b[2] : 0);
            midi->feed(b, len, mpe::steadyNowNs());
        });
        std::string err;
        if (!sd.host->load(*lib.controllers[size_t(n)], &err)) { status = err; sd.host.reset(); return; }
        route(k);
        sd.settings.clear();
        if (k == active) settingsScroll = 0;
        const mpe::Json& s = sd.host->manifest()["settings"];
        // A side after the first starts from the first side's settings for this controller.
        auto own = saved.find(settingsKey(k));
        const auto& mine = own != saved.end() ? own->second : saved[controllerId(k)];
        for (size_t i = 0; i < s.size(); i++) {
            auto it = mine.find(s[i]["id"].str());
            sd.settings.push_back(it != mine.end() ? it->second : s[i]["default"].num());
            if (it != mine.end()) sd.host->setting(int(i), it->second);
        }
        sd.initW = sd.initH = 0;                 // init() at the next frame, with its size
        saveConfig();
        reroute();                               // its parts may differ from the last controller's
    }

    void keepSetting(int k, size_t i, double v) {
        Side& sd = sides[k];
        sd.settings[i] = v;
        saved[settingsKey(k)][sd.host->manifest()["settings"][i]["id"].str()] = v;
    }

    // Setting i of the side the rail changes, to v. A slider being dragged saves when it lets go.
    void setSetting(size_t i, double v, bool save = true) {
        Side& sd = cur();
        if (!sd.host || i >= sd.settings.size()) return;
        int parts = partsOf(active);
        sd.host->setting(int(i), v);
        keepSetting(active, i, v);
        if (partsOf(active) != parts) reroute();
        if (save) saveConfig();
    }
    const mpe::Json& settingOf(size_t i) { return cur().host->manifest()["settings"][i]; }

    // ---- takes (mpe/take.h, docs/formats/TAKE_FORMAT.md) -----------------------------------------

    // A take: what was played, kept as the touches and the notes, in ~/.local/share/odo/takes;
    // or a piece written by hand (TAKE_AUTHORING.md). Played back, Listen plays it with ghost
    // fingers showing how; Play along shows only the fingers, for you to play; slower, or one
    // section round and round. The same panel, words and order as odo on Android's.
    struct TakeFile { std::string path, name, controller; double length = 0; bool written = false; std::vector<std::string> sections; };
    std::string takesDir = home() + "/.local/share/odo/takes";
    std::vector<TakeFile> takes;
    int takeOn = -1;                       // the take chosen in the panel
    mpe::take::Recorder recorder;
    mpe::take::Player player;
    std::string takeNote;                  // what happened last, for the panel
    double takeSpeed = 1;
    int takeLoop = -1;                     // the section played round and round, -1: all of it once
    bool confirmTakeDelete = false;
    bool openingTake = false;              // the open dialog is for a take file
    static constexpr double kSpeeds[3] = {0.5, 0.75, 1};

    static double nowMs() { return double(mpe::steadyNowNs()) / 1e6; }

    static bool readText(const std::string& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    }

    static std::string clockText(double ms) {
        int s = int(std::lround(ms / 1000));
        char b[16];
        std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
        return b;
    }

    // "0:38 · Grid · written by hand"
    std::string takeSub(const TakeFile& f) const {
        int c = controllerIndex(f.controller);
        std::string ctl = c >= 0 ? lib.controllers[size_t(c)]->name() : f.controller.empty() ? "any controller" : f.controller;
        return clockText(f.length) + " \xC2\xB7 " + ctl + " \xC2\xB7 " + (f.written ? "written by hand" : "recorded");
    }

    void scanTakes() {
        std::string was = takeOn >= 0 && takeOn < int(takes.size()) ? takes[size_t(takeOn)].path : "";
        takes.clear();
        takeOn = -1;
        std::error_code ec;
        fs::create_directories(takesDir, ec);
        for (const auto& e : fs::directory_iterator(takesDir, ec)) {
            if (e.path().extension() != mpe::take::kExtension) continue;
            std::string text;
            mpe::take::Take t;
            if (!readText(e.path().string(), text) || !mpe::take::parse(text, t)) continue;
            TakeFile f;
            f.path = e.path().string();
            f.name = t.name.empty() ? e.path().stem().string() : t.name;
            f.controller = t.sides.empty() ? "" : t.sides[0].controller;
            f.written = !t.performed();
            f.length = t.length();
            if (f.written) {
                mpe::take::Score sc;
                if (mpe::take::parseScore(t.score, sc, nullptr, nullptr, 1, t.tempo, t.meterBeats, t.meterUnit, t.key)) {
                    f.length = sc.ms(sc.end);
                    for (const auto& s : sc.sections) f.sections.push_back(s.second);
                }
            } else for (const auto& s : t.sections) f.sections.push_back(s.name);
            takes.push_back(std::move(f));
        }
        std::sort(takes.begin(), takes.end(), [](const TakeFile& a, const TakeFile& b) { return a.name < b.name; });
        for (size_t i = 0; i < takes.size(); i++) if (takes[i].path == was) takeOn = int(i);
    }

    void chooseTake(int i) {
        if (i == takeOn) return;
        stopTake();
        takeOn = i;
        takeLoop = -1;
        takeNote.clear();
    }

    // Its notes go into the side it was played on, as that side's controller's own notes go.
    void takeOut(const mpe::take::Note& n) {
        Side& sd = sides[n.side % count()];
        if (sd.host) sd.host->injectNote(n.kind, mpe::ControllerHost::kInjectedVoice + n.voice, n.part, n.pitch, n.pressure, n.slide, n.velocity);
    }
    mpe::take::Player::Sink takeSink() { return [this](const mpe::take::Note& n) { takeOut(n); }; }

    void stopTake() { if (player.playing()) player.stop(takeSink()); }

    // A take's whole set-up on (mpe::take::presetOf: each side's controller, settings and sounds,
    // their effects and levels, the reverb and delay, the arps, the split; and the volume), as
    // loading a preset does: `keep`, as a new preset of yours; or not, only playing with it. False
    // (and takeNote says why) if it can't be; else `said` is "<said> “…”" and what was left out.
    bool applyTake(const mpe::take::Take& t, bool keep, const std::string& said, std::string& note) {
        for (const mpe::take::Side& ts : t.sides)
            if (!ts.controller.empty() && controllerIndex(ts.controller) < 0) {
                takeNote = "It was played on " + ts.controller + ", which is not in the library.";
                return false;
            }
        mpe::preset::Preset f = mpe::take::presetOf(t);
        // A take of before 1.1 says nothing of the split: the one in use, if it has the sides.
        if (!t.hasPreset) {
            f.split = split;
            if (count() < int(t.sides.size())) for (int k = 0; k < kSplitCount; k++) if (kSplits[k].sides >= int(t.sides.size())) { f.split = k; break; }
        }
        usePreset(f, note, keep, said);
        if (t.volume >= 0) setMasterVolume(float(t.volume), true);
        layoutSides();
        return true;
    }
    bool readTake(mpe::take::Take& t) {
        if (takeOn < 0 || takeOn >= int(takes.size())) return false;
        const TakeFile& f = takes[size_t(takeOn)];
        std::string text, err;
        if (!readText(f.path, text) || !mpe::take::parse(text, t, &err)) { takeNote = err.empty() ? "Could not read " + f.path : err; return false; }
        return true;
    }
    // Use its settings: play with the take's set-up, without playing it.
    void useTakeSettings(bool keep = false) {
        mpe::take::Take t;
        stopTake();
        if (!readTake(t)) return;
        std::string note;
        if (applyTake(t, keep, keep ? "Kept as a new preset:" : "Playing with the settings of", note)) takeNote = note;
    }

    // Listen, or play along: the take's whole set-up, as a preset would put it, then (a take
    // written by hand) its score expanded on this controller at this size, and played.
    void playTake(mpe::take::Player::Mode mode) {
        if (takeOn < 0 || takeOn >= int(takes.size())) return;
        if (player.playing() && player.mode() == mode) { stopTake(); return; }
        stopTake();
        if (recorder.recording()) toggleRecord();
        mpe::take::Take t;
        if (!readTake(t)) return;
        std::string said;
        if (!applyTake(t, false, "", said)) return;
        std::string left = said.find(". ") == std::string::npos ? "" : said.substr(said.find(". ") + 2);   // what of it is not here
        Side& sd = sides[0];
        if (!sd.host) { takeNote = "No controller to play it on."; return; }
        if (!t.performed()) {
            float centers[2] = {60, 72};
            for (int j = 0; j < 2 && !lib.instruments.empty(); j++)
                centers[j] = centerNote(*lib.instruments[size_t(j && partsOf(0) > 1 ? sound2Of(0) : sd.sound)].pkg);
            mpe::take::Report rep;
            bool ok = mpe::take::prepare(t, *lib.controllers[size_t(sd.controller)], sd.settings, sd.w, sd.h, centers, &rep);
            if (!ok) { takeNote = rep.errors.empty() ? "It could not be played." : rep.errors[0]; return; }
            takeNote = rep.errors.empty() ? "" : std::to_string(rep.errors.size()) + (rep.errors.size() == 1 ? " note is" : " notes are") +
                       " not on the controller as it is set: " + rep.errors[0];
        } else takeNote.clear();
        if (!left.empty()) takeNote = takeNote.empty() ? left : left + ". " + takeNote;
        player.load(std::move(t));
        player.setSpeed(takeSpeed);
        player.loopSection(takeLoop, nowMs(), takeSink());
        player.play(mode, nowMs(), takeSink());
    }

    std::string freeTakeName() const {
        for (int n = 1;; n++) {
            std::string name = "Take " + std::to_string(n);
            if (std::none_of(takes.begin(), takes.end(), [&](const TakeFile& f) { return f.name == name; })) return name;
        }
    }

    // A take's file in the takes folder: its name, made free of the others' ("Take 2 2.odotake").
    std::string freeTakePath(const mpe::take::Take& t) const {
        std::string base = mpe::take::fileName(t);
        base.resize(base.size() - std::strlen(mpe::take::kExtension));
        std::string path = (fs::path(takesDir) / (base + mpe::take::kExtension)).string();
        std::error_code ec;
        for (int n = 2; fs::exists(path, ec); n++) path = (fs::path(takesDir) / (base + " " + std::to_string(n) + mpe::take::kExtension)).string();
        return path;
    }

    // Record: from the first finger down, every side; again: stop, and keep it as a take of yours.
    void toggleRecord() {
        if (recorder.recording()) {
            mpe::take::Take t = recorder.stop();
            if (t.touches.empty()) { takeNote = "Nothing was played, so nothing was kept."; return; }
            t.name = freeTakeName();
            t.madeBy = std::string("odo ") + ODO_DESKTOP_VERSION + " (computer)";
            std::error_code ec;
            fs::create_directories(takesDir, ec);
            std::string path = freeTakePath(t);
            std::ofstream out(path, std::ios::binary);
            out << mpe::take::write(t);
            out.close();
            if (!out) { takeNote = "Could not write " + path; return; }
            scanTakes();
            for (size_t i = 0; i < takes.size(); i++) if (takes[i].path == path) takeOn = int(i);
            takeNote = "Kept as \xE2\x80\x9C" + t.name + "\xE2\x80\x9D.";
            return;
        }
        stopTake();
        // What plays now, as a preset, and the volume: kept in the take, so it plays back as it was played.
        mpe::preset::Preset setup = toFile(current(presetName.empty() ? freeName(soundsName()) : presetName));
        recorder.start(&setup, masterVolume);
        for (int k = 0; k < count(); k++) {
            Side& sd = sides[k];
            if (!sd.host) continue;
            std::vector<std::string> sounds;
            for (int j = 0; j < partsOf(k) && !lib.instruments.empty(); j++) sounds.push_back(lib.instruments[size_t(j ? sound2Of(k) : sd.sound)].pkg->id());
            recorder.attach(*sd.host, k, sounds);
        }
        takeNote.clear();
    }

    void deleteTake() {
        if (takeOn < 0 || takeOn >= int(takes.size())) return;
        stopTake();
        std::error_code ec;
        fs::remove(takes[size_t(takeOn)].path, ec);
        takeNote = "Deleted \xE2\x80\x9C" + takes[size_t(takeOn)].name + "\xE2\x80\x9D.";
        takeOn = -1;
        scanTakes();
    }

    // A take file (from a dialog, dropped on the window, or the command line) copied in as one of yours.
    bool openTakeFile(const std::string& from) {
        std::string text, err;
        mpe::take::Take t;
        if (!readText(from, text)) { takeNote = "Could not open " + from; return false; }
        if (!mpe::take::parse(text, t, &err)) { takeNote = err; return false; }
        if (t.name.empty()) t.name = fs::path(from).stem().string();
        std::error_code ec;
        fs::create_directories(takesDir, ec);
        std::string path = freeTakePath(t);
        std::ofstream out(path, std::ios::binary);
        out << text;
        out.close();
        if (!out) { takeNote = "Could not write " + path; return false; }
        scanTakes();
        for (size_t i = 0; i < takes.size(); i++) if (takes[i].path == path) takeOn = int(i);
        takeNote = "Opened \xE2\x80\x9C" + t.name + "\xE2\x80\x9D.";
        return true;
    }
    static constexpr SDL_DialogFileFilter kTakeFilter[] = {{"odo takes", "odotake"}};

    // ---- Scala tunings (mpe/scala.h): a .scl file given to the controller as its settings -----------
    int openingScala = -1;                  // the open dialog is for this "scala" setting
    std::string scalaNote;                  // what the last one did, shown under it
    static constexpr SDL_DialogFileFilter kScalaFilter[] = {{"Scala tunings", "scl"}};
    void askToOpenScala(size_t index) {
        openingTake = false;
        openingScala = int(index);
        SDL_ShowOpenFileDialog(openAnswer, this, window, kScalaFilter, 1, home().c_str(), false);
    }
    bool openScalaFile(size_t index, const std::string& from) {
        Side& sd = cur();
        if (!sd.host || index >= sd.settings.size()) return false;
        std::string text, err;
        mpe::scala::Scale s;
        if (!readText(from, text)) { scalaNote = "Could not open " + from; return false; }
        if (!mpe::scala::parse(text, &s, &err)) { scalaNote = err; return false; }
        auto set = mpe::scala::settings(sd.host->manifest(), index, s, &err);
        if (set.empty()) { scalaNote = err; return false; }
        for (const auto& kv : set) setSetting(kv.first, kv.second, false);
        saveConfig();
        std::string what = s.description.empty() ? fs::path(from).stem().string() : s.description;
        scalaNote = "Playing \xE2\x80\x9C" + what + "\xE2\x80\x9D.";
        return true;
    }
    void askToOpenTake() { openingTake = true; openingScala = -1; SDL_ShowOpenFileDialog(openAnswer, this, window, kTakeFilter, 1, home().c_str(), false); }
    std::string sharingTake;                // the take a save dialog is for (its file)
    void askToShareTake() {
        if (takeOn < 0 || takeOn >= int(takes.size())) return;
        sharingTake = takes[size_t(takeOn)].path;
        std::string at = (fs::path(home()) / fs::path(sharingTake).filename()).string();
        SDL_ShowSaveFileDialog(saveAnswer, this, window, kTakeFilter, 1, at.c_str());
    }
    void saveTakeFile(std::string path) {
        if (path.size() < 8 || path.compare(path.size() - 8, 8, mpe::take::kExtension) != 0) path += mpe::take::kExtension;
        std::error_code ec;
        fs::copy_file(sharingTake, path, fs::copy_options::overwrite_existing, ec);
        takeNote = ec ? "Could not write " + path : "Shared as " + fs::path(path).filename().string();
        sharingTake.clear();
    }

    // The rail's line for takes: what is happening, or how many there are.
    std::string takesSummary() const {
        if (recorder.recording()) return recorder.elapsedMs() > 0 ? "Recording " + clockText(recorder.elapsedMs()) : "Recording: play something";
        if (player.playing() && takeOn >= 0 && takeOn < int(takes.size()))
            return std::string(player.mode() == mpe::take::Player::Listen ? "Listening: " : "Playing along: ") + takes[size_t(takeOn)].name;
        if (takes.empty()) return "Record what you play";
        return std::to_string(takes.size()) + (takes.size() == 1 ? " take" : " takes");
    }

    mpe::take::GhostStyle ghostStyle() const {
        const tokens::ghost::Colours& c = eink ? tokens::ghost::eink : tokens::ghost::dark;
        mpe::take::GhostStyle s;
        s.finger = c.finger; s.trail = c.trail; s.next = c.next; s.text = c.text; s.back = c.back;
        s.radius = tokens::ghost::radius; s.grow = tokens::ghost::grow; s.trailMs = tokens::ghost::trailMs;
        s.previewMs = tokens::ghost::previewMs; s.textSize = tokens::ghost::textSize;
        return s;
    }
    // ---- the rail and its panels -----------------------------------------------------------------

    // Under its top row, four tiles: the preset, the split, the controller and the sound (two, for
    // a split controller); a map of the sides when the window is split, to pick the one the rest
    // changes; then that side's controller's settings by group, each a row that opens a panel
    // beside the rail with all of the group's settings in it; then the app's. A tile opens its own
    // picker. Which settings, in which groups, and which the mode now leaves out: mpe::rail. A
    // group with a "panel" setting (the Grid's Touch) opens the controller's own panel instead.
    // Under the top row the rail scrolls; so does a long panel.
    static constexpr float kButton = 40;           // the top buttons' width
    static constexpr float kTop = 46;              // the top row: odo, full screen, hide
    static constexpr float kBottom = 84;           // the lines at the bottom: version and update, latency, load
    static constexpr float kPad = 16, kPanelHead = 58;

    enum Key { kNone = -100, kPresetKey = -1, kSplitKey = -2, kControllerKey = -3, kSoundKey = -4, kSound2Key = -5, kAppKey = -6, kGroupKey = -7, kTakesKey = -8 };
    int panelKey = kNone;
    std::string panelGroup;                        // kGroupKey: the group it shows
    float panelScroll = 0, settingsScroll = 0;
    int folded = -1;                               // a group's panel: the setting whose long list of chips is open
    bool renaming = false;                         // the preset panel: a new name being typed
    std::string editText, search;                  // that name; the sound panel's search
    int soundFilter = 0;                           // the sound panel: all, sustained, or fading ones

    // ---- the rail's boxes: drawing and tapping use this one layout ----

    struct Box {
        enum Kind { Tile, Part, Map, Cell, Eyebrow, Group, App, Takes } kind;
        int key;                                   // Tile: its panel; Part: 0 or 1; Cell: the side
        std::string text;                          // Eyebrow: its words; Group: its name
        float x, y, w, h;                          // y from the top of the scrolled part, unscrolled
    };

    static std::string upper(std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; }
    std::string groupLabel(const std::string& g) const { return g.empty() ? "Settings" : g; }

    std::vector<Box> railBoxes() {
        std::vector<Box> b;
        float x = 10, w = kRail - 20, half = (w - 8) / 2, y = 2;
        b.push_back({Box::Tile, kPresetKey, "", x, y, w, 62}); y += 70;
        b.push_back({Box::Tile, kSplitKey, "", x, y, half, 88});
        b.push_back({Box::Tile, kControllerKey, "", x + half + 8, y, half, 88}); y += 96;
        if (!lib.instruments.empty()) {
            int parts = partsOf(active);
            float th = 28 + 46 * float(parts);
            b.push_back({Box::Tile, kSoundKey, "", x, y, w, th});
            for (int j = 0; j < parts; j++) b.push_back({Box::Part, j, "", x + 6, y + 26 + 46 * float(j), w - 12, 44});
            y += th + 8;
        }
        if (count() > 1) {
            const Split& sp = kSplits[split];
            int cols = sp.cols, rows = (sp.sides + cols - 1) / cols;
            b.push_back({Box::Eyebrow, kNone, "CHANGING", x, y, w, 22}); y += 22;
            float mh = 12 + 42 * float(rows) - 4, cw = (w - 12 - 4 * float(cols - 1)) / float(cols);
            b.push_back({Box::Map, kNone, "", x, y, w, mh});
            for (int k = 0; k < sp.sides; k++)
                b.push_back({Box::Cell, k, "", x + 6 + float(k % cols) * (cw + 4), y + 6 + float(k / cols) * 42, cw, 38});
            y += mh + 6;
        }
        Side& sd = cur();
        if (sd.host) {
            std::string head = upper(lib.controllers[size_t(sd.controller)]->name());
            if (count() > 1) head += " \xC2\xB7 " + upper(kSplits[split].side[active]);
            b.push_back({Box::Eyebrow, kNone, head, x, y, w, 26}); y += 26;
            for (const auto& g : mpe::rail::groups(sd.host->manifest(), sd.settings)) { b.push_back({Box::Group, kGroupKey, g.name, x, y, w, 50}); y += 52; }
        }
        b.push_back({Box::Eyebrow, kNone, "APP", x, y, w, 26}); y += 26;
        b.push_back({Box::App, kAppKey, "", x, y, w, 50}); y += 52;
        b.push_back({Box::Takes, kTakesKey, "", x, y, w, 50});
        return b;
    }
    static float boxesHeight(const std::vector<Box>& b) { return b.empty() ? 0 : b.back().y + b.back().h + 8; }

    void scrollRail(float by, float h) {
        float room = h - kBottom - kTop;
        settingsScroll = std::clamp(settingsScroll + by, 0.f, std::max(0.f, boxesHeight(railBoxes()) - room));
    }

    const mpe::rail::Group* findGroup(const std::vector<mpe::rail::Group>& gs, const std::string& name) const {
        for (const auto& g : gs) if (g.name == name) return &g;
        return nullptr;
    }
    // A group's row on the rail: its first few values, and how many it shows now.
    std::string groupSummary(const mpe::rail::Group& g) const {
        std::string s;
        int n = 0;
        for (const auto& r : g.rows) {
            if (r.panel || n == 3) continue;
            std::string v = r.widget == mpe::rail::Widget::Switch ? r.label + (r.value != 0 ? " on" : " off") : r.display;
            s += (s.empty() ? "" : " \xC2\xB7 ") + v;
            n++;
        }
        return s.empty() ? (g.rows.empty() ? "Nothing to set in this mode" : "") : s;
    }
    // The setting that opens the controller's own panel for this group, or -1.
    static int panelRow(const mpe::rail::Group& g) {
        for (const auto& r : g.rows) if (r.panel) return int(r.index);
        return -1;
    }

    // ---- the panel beside the rail ----

    enum Act { aNone, aPanic, aSoundFilter, aLoadPreset, aNewPreset, aShare, aOpenFile, aRename, aRenameOk, aDelete, aSplit, aController, aSound, aLook, aGestures, aFlip, aChip, aFold, aSlide, aVolume, aVersion,
               aRecord, aTake, aListen, aAlong, aSpeed, aLoop, aTakeDelete, aTakeOpen, aTakeShare, aScalaOpen,
               aTakeUse, aTakeKeep };
    struct PItem {
        enum Kind { Heading, Sub, Note, Row, Tile, Chip, Switch, Fold, Label, Slider, Field } kind;
        std::string text, sub;
        float x, y, w, h;                          // y from the content's top, unscrolled
        int act = aNone, arg = 0, arg2 = 0;
        bool on = false, rule = false;             // rule: a line above it
        double frac = 0;                           // a slider's place
        int pic = 0, picArg = -1;                  // a picture: 1 a split, 2 a controller, 3 a sound's badge
        uint32_t colour = 0;                       // a heading's colour (a pack's), 0: the theme's
    };
    struct Panel {
        bool open = false;
        float x = 0, y = 0, w = 0, h = 0;
        std::string title, sub;
        std::vector<PItem> items;
        float top = 0, room = 0, contentH = 0;
        bool in(float px, float py) const { return open && px >= x && px < x + w && py >= y && py < y + h; }
        bool onDone(float px, float py) const { return in(px, py) && px >= x + w - 80 && py < y + kPanelHead; }
    };

    std::string presetSubtitle(const Preset& p) const {
        std::string ctl, snd;
        for (const PresetSide& s : p.sides) {
            int c = controllerIndex(s.controller);
            std::string cn = c >= 0 ? lib.controllers[size_t(c)]->name() : "?";
            if (ctl.find(cn) == std::string::npos) ctl += (ctl.empty() ? "" : " + ") + cn;
            int n = soundIndex(s.sound);
            snd += (snd.empty() ? "" : ", ") + (n >= 0 ? lib.instruments[size_t(n)].pkg->name() : std::string("?"));
            auto sp = s.settings.find("split");
            int n2 = s.sound2.empty() ? -1 : soundIndex(s.sound2);
            if (n2 >= 0 && sp != s.settings.end() && sp->second != 0) snd += " + " + lib.instruments[size_t(n2)].pkg->name();
        }
        return (p.split ? std::string(kSplits[p.split].name) + " \xC2\xB7 " : "") + ctl + " \xC2\xB7 " + snd;
    }

    static std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

    Panel panelLayout() {
        Panel p;
        if (panelKey == kNone || railHidden) return p;
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        Side& sd = cur();
        p.x = kRail + 8;
        p.y = 12;
        p.w = std::min(440.f, float(ww) - p.x - 8);
        float in = p.w - 2 * kPad, x = p.x + kPad, y = 0;
        std::string forSide = count() > 1 ? std::string("For the ") + lower(kSplits[split].side[active]) + " side" : "";
        auto heading = [&](const std::string& t, uint32_t colour = 0) { PItem it{PItem::Heading, t, "", x, y + 6, in, 26}; it.colour = colour; p.items.push_back(it); y += 32; };
        auto note = [&](const std::string& t) { p.items.push_back({PItem::Note, t, "", x, y, in, 24}); y += 28; };
        // Chips that wrap, as wide as their words.
        auto chips = [&](const std::vector<std::string>& names, int chosen, int act, int arg, const std::vector<int>* args = nullptr) {
            float cx = 0;
            for (size_t i = 0; i < names.size(); i++) {
                float cw = std::min(in, draw::textWidth(13, names[i]) + 24);
                if (cx > 0 && cx + cw > in) { cx = 0; y += 36; }
                PItem it{PItem::Chip, names[i], "", x + cx, y, cw, 30};
                it.act = act; it.arg = arg; it.arg2 = args ? (*args)[i] : int(i); it.on = int(i) == chosen;
                p.items.push_back(it);
                cx += cw + 6;
            }
            if (!names.empty()) y += 38;
        };
        if (panelKey == kPresetKey) {
            p.title = "Presets"; p.sub = "The sound, the split, the controllers and their settings";
            std::vector<Preset> all = allPresets();
            for (int pass = 0; pass < 2; pass++) {
                heading(pass ? "YOURS" : "BUILT IN");
                bool any = false;
                for (size_t i = 0; i < all.size(); i++) {
                    if (all[i].builtIn != (pass == 0)) continue;
                    any = true;
                    PItem it{PItem::Row, all[i].name, presetSubtitle(all[i]), x, y, in, 50};
                    it.act = aLoadPreset; it.arg = int(i); it.on = all[i].name == presetName; it.rule = true;
                    if (all[i].split) { it.pic = 1; it.picArg = all[i].split; }
                    else if (!all[i].sides.empty()) { it.pic = 2; it.picArg = controllerIndex(all[i].sides[0].controller); }
                    p.items.push_back(it);
                    y += 50;
                }
                if (!any) note("None yet: keep what is playing as one, below.");
            }
            heading("THIS PRESET");
            auto action = [&](const std::string& t, int act) { PItem it{PItem::Row, t, "", x, y, in, 40}; it.act = act; it.rule = true; p.items.push_back(it); y += 40; };
            action("+ Keep what is playing as a new preset", aNewPreset);
            action(presetName.empty() ? std::string("Share what is playing as a file\xE2\x80\xA6")
                                      : "Share \xE2\x80\x9C" + presetName + "\xE2\x80\x9D as a file\xE2\x80\xA6", aShare);
            action("Open a preset file\xE2\x80\xA6", aOpenFile);
            if (userPreset(presetName)) {
                if (renaming) {
                    p.items.push_back({PItem::Field, editText, "", x, y + 4, in, 38}); y += 46;
                    action("Save the name (Enter)", aRenameOk);
                } else action("Rename \xE2\x80\x9C" + presetName + "\xE2\x80\x9D", aRename);
                action(std::string(confirmDelete ? "Tap again to delete \xE2\x80\x9C" : "Delete \xE2\x80\x9C") + presetName + "\xE2\x80\x9D", aDelete);
            }
        } else if (panelKey == kSplitKey || panelKey == kControllerKey) {
            bool isSplit = panelKey == kSplitKey;
            p.title = isSplit ? "Split" : "Controller";
            p.sub = isSplit ? "Share the window: each side its own controller and sound" : forSide.empty() ? "What you play on" : forSide;
            int n = isSplit ? kSplitCount : int(lib.controllers.size());
            float tw = (in - 8) / 2;
            for (int k = 0; k < n; k++) {
                PItem it{PItem::Tile, "", "", x + float(k % 2) * (tw + 8), y + float(k / 2) * 104, tw, 96};
                if (isSplit) {
                    it.text = kSplits[k].name; it.sub = std::to_string(kSplits[k].sides) + (kSplits[k].sides == 1 ? " side" : " sides");
                    it.act = aSplit; it.on = k == split; it.pic = 1;
                } else {
                    it.text = lib.controllers[size_t(k)]->name();
                    std::string d = lib.controllers[size_t(k)]->manifest["description"].str();
                    it.sub = d.substr(0, d.find_first_of(":;,"));
                    it.act = aController; it.on = k == sd.controller; it.pic = 2;
                }
                it.arg = k; it.picArg = k;
                p.items.push_back(it);
            }
            y += float((n + 1) / 2) * 104;
        } else if (panelKey == kSoundKey || panelKey == kSound2Key) {
            if (lib.instruments.empty() || (panelKey == kSound2Key && partsOf(active) < 2)) { panelKey = kNone; return Panel(); }
            bool two = partsOf(active) > 1;
            const mpe::Json& parts = sd.host ? sd.host->manifest()["parts"] : mpe::Json();
            p.title = two ? "Sound: " + parts[size_t(panelKey == kSound2Key ? 1 : 0)].str() : "Sound";
            p.sub = forSide.empty() ? "Type to find one" : forSide + " \xC2\xB7 type to find one";
            int chosen = panelKey == kSound2Key ? sound2Of(active) : sd.sound;
            // The sound's versions, when it has more than one: the newest, and those kept to go back to.
            if (std::vector<int> vs = lib.versions(chosen); vs.size() > 1) {
                heading("VERSION");
                std::vector<std::string> names;
                int on = 0;
                for (size_t k = 0; k < vs.size(); k++) {
                    names.push_back(lib.instruments[size_t(vs[k])].version() + (k == 0 ? " \xC2\xB7 newest" : ""));
                    if (vs[k] == chosen) on = int(k);
                }
                chips(names, on, aVersion, 0, &vs);
                note(lib.instruments[size_t(chosen)].pkg->name() + ": older versions are kept as they were, to go back to.");
            }
            chosen = lib.versions(chosen)[0];                // the list marks the sound, whichever version plays
            p.items.push_back({PItem::Field, search, "Find a sound", x, y, in, 38}); y += 46;
            chips({"All", "Sustained", "Fading"}, soundFilter, aSoundFilter, 0);
            size_t filterChips = p.items.size();
            std::string q = lower(search), pack, group;
            std::vector<std::string> names;
            std::vector<int> ids;
            auto flush = [&] { if (!names.empty()) { std::vector<int> a = ids; chips(names, -1, aSound, 0, &a); } names.clear(); ids.clear(); };
            bool any = false;
            for (int i : lib.soundOrder) {
                const Instrument& s = lib.instruments[size_t(i)];
                if (!q.empty() && lower(s.pkg->name() + " " + s.pack + " " + s.group).find(q) == std::string::npos) continue;
                if (soundFilter && s.sustains() != (soundFilter == 1)) continue;
                any = true;
                if (s.pack != pack) { flush(); pack = s.pack; group.clear(); heading(upper(pack), packColour(pack)); }
                if (s.group != group) { flush(); group = s.group; p.items.push_back({PItem::Sub, group, "", x, y, in, 20}); y += 22; }
                names.push_back(s.pkg->name()); ids.push_back(i);
            }
            flush();
            for (size_t k = filterChips; k < p.items.size(); k++) if (p.items[k].kind == PItem::Chip) p.items[k].on = p.items[k].arg2 == chosen;
            if (!any) note(search.empty() ? std::string("None here.") : "No sound matches \xE2\x80\x9C" + search + "\xE2\x80\x9D.");
            if (soundFilter) note(soundFilter == 1 ? "Sustained: a held note keeps sounding." : "Fading: a held note dies away on its own.");
        } else if (panelKey == kAppKey) {
            p.title = "App"; p.sub = "For all of odo, not kept in presets";
            p.items.push_back({PItem::Label, "Volume", std::to_string(int(std::lround(masterVolume * 100))) + "%", x, y, in, 40}); y += 40;
            PItem v{PItem::Slider, "", "", x + 9, y, in - 18, 30};
            v.act = aVolume; v.frac = masterVolume / 2;
            p.items.push_back(v); y += 40;
            p.items.push_back({PItem::Label, "Look", eink ? "E-ink" : "Dark", x, y, in, 40}); y += 42;
            chips({"Dark", "E-ink"}, eink ? 1 : 0, aLook, 0);
            if (gestures::supported()) {
                PItem it{PItem::Switch, "Block system gestures", !blockGestures ? "GNOME's three-finger swipes stay on" : gestureNote.empty() ? "Kept off odo while it has focus" : gestureNote, x, y, in, 48};
                it.act = aGestures; it.on = blockGestures; it.rule = true;
                p.items.push_back(it); y += 50;
            }
            PItem pn{PItem::Row, "Panic: stop every note", "", x, y, in, 40};
            pn.act = aPanic; pn.rule = true;
            p.items.push_back(pn); y += 40;
        } else if (panelKey == kTakesKey) {
            // As odo on Android's Takes panel (TakesUi.kt): the same rows, words and order.
            p.title = "Takes"; p.sub = "Record what you play; play a take back to learn it";
            auto action = [&](const std::string& t, int act, bool on = false) {
                PItem it{PItem::Row, t, "", x, y, in, 40}; it.act = act; it.rule = true; it.on = on; p.items.push_back(it); y += 40;
            };
            heading("RECORD");
            if (recorder.recording())
                action(recorder.elapsedMs() > 0 ? "Stop recording \xC2\xB7 " + clockText(recorder.elapsedMs()) : "Stop recording (play something first)", aRecord, true);
            else action("Record", aRecord);
            if (!takeNote.empty()) note(takeNote);
            heading("YOURS");
            for (size_t i = 0; i < takes.size(); i++) {
                PItem it{PItem::Row, takes[i].name, takeSub(takes[i]), x, y, in, 50};
                it.act = aTake; it.arg = int(i); it.on = int(i) == takeOn; it.rule = true;
                p.items.push_back(it);
                y += 50;
            }
            if (takes.empty()) note("None yet: record one, or open a take file.");
            action("Open a take file\xE2\x80\xA6", aTakeOpen);
            if (takeOn >= 0 && takeOn < int(takes.size())) {
                const TakeFile& f = takes[size_t(takeOn)];
                heading("THIS TAKE");
                bool listening = player.playing() && player.mode() == mpe::take::Player::Listen;
                bool along = player.playing() && player.mode() == mpe::take::Player::PlayAlong;
                action(listening ? "Stop" : "Listen", aListen, listening);
                action(along ? "Stop" : "Play along", aAlong, along);
                int sp = 2;
                for (int k = 0; k < 3; k++) if (std::fabs(kSpeeds[k] - takeSpeed) < 1e-6) sp = k;
                p.items.push_back({PItem::Label, "Speed", std::to_string(int(std::lround(takeSpeed * 100))) + "%", x, y, in, 40}); y += 42;
                chips({"50%", "75%", "100%"}, sp, aSpeed, 0);
                if (!f.sections.empty()) {
                    p.items.push_back({PItem::Label, "Loop", takeLoop < 0 || takeLoop >= int(f.sections.size()) ? "All of it" : f.sections[size_t(takeLoop)],
                                       x, y, in, 40}); y += 42;
                    std::vector<std::string> names{"All of it"};
                    for (const auto& s : f.sections) names.push_back(s);
                    chips(names, takeLoop + 1, aLoop, 0);
                }
                action("Use its settings", aTakeUse);
                action("Keep its settings as a new preset", aTakeKeep);
                action("Share \xE2\x80\x9C" + f.name + "\xE2\x80\x9D as a file\xE2\x80\xA6", aTakeShare);
                action(std::string(confirmTakeDelete ? "Tap again to delete \xE2\x80\x9C" : "Delete \xE2\x80\x9C") + f.name + "\xE2\x80\x9D", aTakeDelete);
            }
        } else if (panelKey == kGroupKey) {
            if (!sd.host) { panelKey = kNone; return Panel(); }
            auto gs = mpe::rail::groups(sd.host->manifest(), sd.settings);
            const mpe::rail::Group* g = findGroup(gs, panelGroup);
            if (!g) { panelKey = kNone; return Panel(); }
            p.title = groupLabel(g->name);
            p.sub = lib.controllers[size_t(sd.controller)]->name() + (count() > 1 ? std::string(" \xC2\xB7 ") + kSplits[split].side[active] + " side" : "");
            bool first = true;
            for (const auto& r : g->rows) {
                if (r.panel) continue;
                if (r.widget == mpe::rail::Widget::Switch) {
                    PItem it{PItem::Switch, r.label, r.display, x, y, in, 48};
                    it.act = aFlip; it.arg = int(r.index); it.on = r.value != 0; it.rule = !first;
                    p.items.push_back(it); y += 50;
                } else if (r.widget == mpe::rail::Widget::Chips) {
                    size_t letters = 0;
                    for (const auto& c : r.choices) letters += c.size();
                    bool fold = r.choices.size() > 8 || letters > 56;
                    if (fold) {
                        PItem it{PItem::Fold, r.label, r.display, x, y, in, 48};
                        it.act = aFold; it.arg = int(r.index); it.on = folded == int(r.index); it.rule = !first;
                        p.items.push_back(it); y += 50;
                        if (folded == int(r.index)) chips(r.choices, r.chosen, aChip, int(r.index));
                    } else {
                        PItem it{PItem::Label, r.label, r.display, x, y, in, 42};
                        it.rule = !first;
                        p.items.push_back(it); y += 44;
                        chips(r.choices, r.chosen, aChip, int(r.index));
                    }
                } else if (r.widget == mpe::rail::Widget::Scala) {
                    PItem it{PItem::Label, r.label, r.display, x, y, in, 42};
                    it.rule = !first;
                    p.items.push_back(it); y += 44;
                    PItem o{PItem::Row, "Open a Scala file\xE2\x80\xA6", "", x, y, in, 40};
                    o.act = aScalaOpen; o.arg = int(r.index);
                    p.items.push_back(o); y += 40;
                    if (!scalaNote.empty()) note(scalaNote);
                } else {
                    PItem it{PItem::Label, r.label, r.display, x, y, in, 42};
                    it.rule = !first;
                    p.items.push_back(it); y += 42;
                    PItem s{PItem::Slider, "", "", x + 9, y, in - 18, 30};
                    s.act = aSlide; s.arg = int(r.index); s.frac = mpe::rail::toFraction(settingOf(r.index), r.value);
                    p.items.push_back(s); y += 36;
                }
                first = false;
            }
            if (g->rows.empty()) note("Nothing to set in this mode.");
            if (g->hiddenNow) note(std::to_string(g->hiddenNow) + (g->hiddenNow == 1 ? " option" : " options") + " hidden in this mode");
        }
        p.open = true;
        p.contentH = y + 8;
        p.h = std::min(kPanelHead + p.contentH + kPad, float(wh) - 24);
        p.top = p.y + kPanelHead;
        p.room = p.h - kPanelHead - kPad / 2;
        panelScroll = std::clamp(panelScroll, 0.f, std::max(0.f, p.contentH - p.room));
        return p;
    }

    // The colour of a pack's heading and its sounds' badges (ui/tokens.json, as Android's).
    static uint32_t packColour(const std::string& pack) {
        for (const auto& k : tokens::packs) if (pack == k.name) return k.colour;
        return tokens::packOther;
    }

    bool typing() const { return renaming || panelKey == kSoundKey || panelKey == kSound2Key; }

    void openPanel(int key, const std::string& group = "") {
        bool same = panelKey == key && (key != kGroupKey || panelGroup == group);
        panelKey = same ? kNone : key;
        panelGroup = group;
        panelScroll = 0;
        folded = -1;
        renaming = false;
        confirmDelete = false;
        confirmTakeDelete = false;
        search.clear();
    }

    // A tap on a group's row: its panel, or the controller's own (a "panel" switch flipped on).
    void openGroup(const std::string& name) {
        Side& sd = cur();
        if (!sd.host) return;
        auto gs = mpe::rail::groups(sd.host->manifest(), sd.settings);
        const mpe::rail::Group* g = findGroup(gs, name);
        if (!g) return;
        int own = panelRow(*g);
        if (own >= 0) { panelKey = kNone; setSetting(size_t(own), sd.settings[size_t(own)] != 0 ? 0 : 1); return; }
        openPanel(kGroupKey, name);
    }

    void panelTap(float x, float y) {
        Panel p = panelLayout();
        if (!p.open) return;
        if (p.onDone(x, y)) { panelKey = kNone; renaming = false; return; }
        if (y < p.top || y >= p.top + p.room) return;
        for (const PItem& it : p.items) {
            float iy = p.top + it.y - panelScroll;
            if (it.act == aNone || x < it.x || x >= it.x + it.w || y < iy || y >= iy + it.h) continue;
            switch (it.act) {
                case aLoadPreset: { std::vector<Preset> all = allPresets(); if (it.arg < int(all.size())) loadPreset(all[size_t(it.arg)]); break; }
                case aNewPreset: newPreset(); renaming = false; break;
                case aShare: askToShare(); break;
                case aOpenFile: askToOpen(); break;
                case aRename: renaming = true; editText = presetName; break;
                case aRenameOk: renamePreset(presetName, editText); renaming = false; break;
                case aDelete: if (confirmDelete) deletePreset(presetName); else confirmDelete = true; break;
                case aSplit: setSplit(it.arg); break;
                case aController: chooseController(active, it.arg); break;
                case aSoundFilter: soundFilter = it.arg2; panelScroll = 0; break;
                case aSound: if (panelKey == kSound2Key) chooseSound2(active, it.arg2); else chooseSound(active, it.arg2); break;
                case aVersion: if (panelKey == kSound2Key) chooseSound2(active, it.arg2); else chooseSound(active, it.arg2); break;
                case aLook: setEink(it.arg2 == 1); break;
                case aGestures: setBlockGestures(!blockGestures); break;
                case aPanic: engine->panic(); break;                  // every note stops, as on Android
                case aFlip: setSetting(size_t(it.arg), cur().settings[size_t(it.arg)] != 0 ? 0 : 1); break;
                case aChip: setSetting(size_t(it.arg), mpe::rail::chipValue(settingOf(size_t(it.arg)), it.arg2)); break;
                case aFold: folded = folded == it.arg ? -1 : it.arg; break;
                case aRecord: toggleRecord(); break;
                case aTake: chooseTake(it.arg); break;
                case aListen: playTake(mpe::take::Player::Listen); break;
                case aAlong: playTake(mpe::take::Player::PlayAlong); break;
                case aSpeed: takeSpeed = kSpeeds[std::clamp(it.arg2, 0, 2)]; player.setSpeed(takeSpeed); break;
                case aLoop: takeLoop = it.arg2 - 1; if (player.loaded()) player.loopSection(takeLoop, nowMs(), takeSink()); break;
                case aTakeDelete: if (confirmTakeDelete) deleteTake(); else confirmTakeDelete = true; break;
                case aTakeOpen: askToOpenTake(); break;
                case aScalaOpen: askToOpenScala(size_t(it.arg)); break;
                case aTakeShare: askToShareTake(); break;
                case aTakeUse: confirmTakeDelete = false; useTakeSettings(false); break;
                case aTakeKeep: confirmTakeDelete = false; useTakeSettings(true); break;
                default: break;
            }
            if (it.act != aTakeDelete) confirmTakeDelete = false;
            if (it.act != aDelete) confirmDelete = false;
            return;
        }
    }

    // A slider in the panel under x: its setting at that place.
    struct Slide { int setting = -1; float x = 0, w = 0; bool master = false; } slide;
    void panelSlide(float x, bool save) {
        if ((slide.setting < 0 && !slide.master) || slide.w <= 0) return;
        double t = std::clamp(double(x - slide.x) / double(slide.w), 0.0, 1.0);
        if (slide.master) { setMasterVolume(float(std::round(t * 40) / 20), save); return; }   // 5% steps
        setSetting(size_t(slide.setting), mpe::rail::fromFraction(settingOf(size_t(slide.setting)), t), save);
    }

    // A tap on the rail: the top row's buttons, the update line, a tile, a sound, a side, a group.
    void railTap(float x, float y) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        if (updater && y > float(wh) - 76 && y < float(wh) - 40) {
            if (updater->state() == Updater::State::Available) updater->install();
            else if (updater->state() == Updater::State::Installed) { shutdown(); SDL_Quit(); updater->restart(argv); }
            else if (updater->state() == Updater::State::Failed || updater->state() == Updater::State::UpToDate) updater->check();
            return;
        }
        if (y < kTop) {                                  // full screen, hide
            if (x >= kRail - kButton) setRailHidden(true);
            else if (x >= kRail - 2 * kButton) setFullscreen(!fullscreen());
            return;
        }
        if (y >= float(wh) - kBottom) return;
        float at = y - kTop + settingsScroll;
        std::vector<Box> boxes = railBoxes();
        // The innermost box under the tap: a sound in its tile, a side in the map.
        for (auto it = boxes.rbegin(); it != boxes.rend(); ++it) {
            const Box& b = *it;
            if (x < b.x || x >= b.x + b.w || at < b.y || at >= b.y + b.h) continue;
            switch (b.kind) {
                case Box::Part: openPanel(b.key ? kSound2Key : kSoundKey); return;
                case Box::Cell: if (b.key != active) { setActive(b.key); panelKey = panelKey == kGroupKey ? kNone : panelKey; } return;
                case Box::Tile: if (b.key != kSoundKey) openPanel(b.key); return;
                case Box::Group: openGroup(b.text); return;
                case Box::App: openPanel(kAppKey); return;
                case Box::Takes: if (panelKey != kTakesKey) scanTakes(); openPanel(kTakesKey); return;
                default: break;
            }
        }
    }

    // A finger (or the mouse) on the rail or the panel: a tap acts when it lifts, a drag scrolls
    // (the rail, or the panel), and on a slider it slides. phase 0 down, 1 move, 2 up.
    // True if the rail or the panel took it.
    static constexpr int64_t kMouse = -1;
    struct Press {
        enum Where { Rail, Panel, Corner } where = Rail;
        bool on = false;
        int64_t id = 0;
        float x = 0, y0 = 0, last = 0;
        bool dragged = false, sliding = false;
    } press;

    bool railPress(int phase, int64_t id, float x, float y) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        if (phase == 0) {
            // Hidden, the rail's corner button brings it back.
            if (railHidden && x < 44 && y < 44) { setRailHidden(false); press = Press{Press::Corner, true, id, x, y, y, true, false}; return true; }
            Panel p = panelLayout();
            bool mine = x < railW() || p.in(x, y);
            if (press.on) return mine;                   // one finger at a time on the rail and panel
            if (!mine) return false;
            press = Press{p.in(x, y) ? Press::Panel : Press::Rail, true, id, x, y, y, false, false};
            if (press.where == Press::Panel && y >= p.top && !p.onDone(x, y))
                for (const PItem& it : p.items) {
                    float iy = p.top + it.y - panelScroll;
                    if (it.kind != PItem::Slider || x < it.x - 10 || x >= it.x + it.w + 10 || y < iy - 6 || y >= iy + it.h + 6) continue;
                    slide = Slide{it.arg, it.x, it.w, it.act == aVolume};
                    press.sliding = press.dragged = true;
                    panelSlide(x, false);
                    break;
                }
            return true;
        }
        if (!press.on || press.id != id) return false;
        if (phase == 1) {
            if (press.sliding) panelSlide(x, false);
            else {
                if (std::fabs(y - press.y0) > 16) press.dragged = true;
                if (press.dragged && press.where == Press::Rail) scrollRail(press.last - y, float(wh));
                if (press.dragged && press.where == Press::Panel) { panelScroll += press.last - y; panelLayout(); }
            }
            press.last = y;
            return true;
        }
        press.on = false;
        if (press.sliding) { saveConfig(); slide = Slide(); }
        else if (!press.dragged && press.where == Press::Rail) railTap(press.x, press.y0);
        else if (!press.dragged && press.where == Press::Panel) panelTap(press.x, press.y0);
        return true;
    }

    // A pointer on the play area: down on a side, it plays that side until it lifts.
    void pointer(int64_t key, mpe::ControllerHost::Phase ph, float x, float y, float force) {
        if (ph == mpe::ControllerHost::Down) {
            layoutSides();
            int k = sideAt(x, y);
            if (k < 0 || !sides[k].host) return;
            Pointer p{k, nextFinger++};
            pointers[key] = p;
            sides[k].host->touch(p.id, ph, x - sides[k].x, y - sides[k].y, 0, force);
            return;
        }
        auto it = pointers.find(key);
        if (it == pointers.end()) return;
        Side& s = sides[it->second.side];
        if (s.host) s.host->touch(it->second.id, ph, x - s.x, y - s.y, 0, force);
        if (ph == mpe::ControllerHost::Up || ph == mpe::ControllerHost::Cancel) pointers.erase(it);
    }

    void handle(const SDL_Event& e) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        using H = mpe::ControllerHost;
        switch (e.type) {
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED: {
                float x = e.tfinger.x * float(ww), y = e.tfinger.y * float(wh);
                int64_t key = int64_t(e.tfinger.fingerID & 0x7FFFFFFFFFFFFFFFull);
                if (railPress(e.type == SDL_EVENT_FINGER_DOWN ? 0 : e.type == SDL_EVENT_FINGER_MOTION ? 1 : 2, key, x, y)) return;
                H::Phase ph = e.type == SDL_EVENT_FINGER_DOWN ? H::Down : e.type == SDL_EVENT_FINGER_MOTION ? H::Move
                            : e.type == SDL_EVENT_FINGER_UP ? H::Up : H::Cancel;
                pointer(key, ph, x, y, ph == H::Up || ph == H::Cancel ? 0 : e.tfinger.pressure);
                return;
            }
            // The mouse is one more pointer. Touches also arrive as mouse events: those are skipped.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) return;
                if (railPress(0, kMouse, e.button.x, e.button.y)) return;
                pointer(kMouse, H::Down, e.button.x, e.button.y, 0.8f);
                return;
            case SDL_EVENT_MOUSE_MOTION:
                if (e.motion.which == SDL_TOUCH_MOUSEID) return;
                if (railPress(1, kMouse, e.motion.x, e.motion.y)) return;
                pointer(kMouse, H::Move, e.motion.x, e.motion.y, 0.8f);
                return;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.which == SDL_TOUCH_MOUSEID) return;
                if (railPress(2, kMouse, e.button.x, e.button.y)) return;
                pointer(kMouse, H::Up, e.button.x, e.button.y, 0);
                return;
            case SDL_EVENT_MOUSE_WHEEL: {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                if (panelLayout().in(mx, my)) { panelScroll -= e.wheel.y * 40; panelLayout(); }
                else if (mx < railW()) scrollRail(-e.wheel.y * 40, float(wh));
                return;
            }
            case SDL_EVENT_DROP_FILE:                // a preset or take file dropped on the window
                if (!e.drop.data) return;
                if (fs::path(e.drop.data).extension() == mpe::take::kExtension) { if (openTakeFile(e.drop.data)) { panelKey = kTakesKey; panelScroll = 0; } }
                else openPresetFile(e.drop.data);
                return;
            case SDL_EVENT_TEXT_INPUT: {
                // a preset's new name, or the sound panel's search
                std::string& to = renaming ? editText : search;
                if (typing() && to.size() < 60) { to += e.text.text; panelScroll = renaming ? panelScroll : 0; }
                return;
            }
            case SDL_EVENT_KEY_DOWN:
                if (typing()) {
                    std::string& to = renaming ? editText : search;
                    if (e.key.key == SDLK_BACKSPACE && !to.empty()) {
                        size_t n = to.size() - 1;            // a whole UTF-8 character
                        while (n > 0 && (static_cast<unsigned char>(to[n]) & 0xC0) == 0x80) n--;
                        to.resize(n);
                    } else if (renaming && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) { renamePreset(presetName, editText); renaming = false; }
                    else if (e.key.key == SDLK_ESCAPE) { if (renaming) renaming = false; else panelKey = kNone; }
                    return;
                }
                if (e.key.key == SDLK_TAB) setRailHidden(!railHidden);
                else if (e.key.key == SDLK_F11) setFullscreen(!fullscreen());
                else if (e.key.key == SDLK_RIGHT) chooseSound(active, nextSound(cur().sound, 1));
                else if (e.key.key == SDLK_LEFT) chooseSound(active, nextSound(cur().sound, -1));
                return;
            default: return;
        }
    }

    // ---- drawing --------------------------------------------------------------------------------

    // The look: ui/tokens.json, the same as odo on Android's.
    const tokens::Palette& look() const { return eink ? tokens::eink : tokens::dark; }

    void clip(float x, float y, float w, float h) {
        SDL_Rect r{int(x), int(y), int(std::ceil(w)), int(std::ceil(h))};
        SDL_SetRenderClipRect(renderer, &r);
    }

    // A split, drawn: its sides as boxes in w x h; the one the rail changes filled.
    void splitPic(int n, int activeSide, float x, float y, float w, float h, draw::Color c) {
        const Split& sp = kSplits[n];
        int cols = sp.cols, rows = (sp.sides + cols - 1) / cols;
        float g = 3, cw = (w - g * float(cols - 1)) / float(cols), rh = (h - g * float(rows - 1)) / float(rows);
        for (int k = 0; k < sp.sides; k++) {
            float bx = x + float(k % cols) * (cw + g), by = y + float(k / cols) * (rh + g);
            if (k == activeSide) draw::fillRect(renderer, bx, by, cw, rh, draw::Color{c.r, c.g, c.b, 0.45f}, 3);
            draw::strokeRect(renderer, bx, by, cw, rh, c, 1.6f, 3);
        }
    }

    // A controller, drawn small (one odo does not know: its first letter in a box).
    void controllerPic(int n, float x, float y, float w, float h, draw::Color c) {
        if (n < 0 || n >= int(lib.controllers.size())) return;
        std::string id = lib.controllers[size_t(n)]->id();
        float s = std::min(w / 40, h / 28);
        float ox = x + (w - 40 * s) / 2, oy = y + (h - 28 * s) / 2;
        auto X = [&](float v) { return ox + v * s; };
        auto Y = [&](float v) { return oy + v * s; };
        if (id == "patrickreynolds/grid") {
            for (int r = 0; r < 2; r++) for (int k = 0; k < 3; k++) {
                float bx = X(5 + float(k) * 10.5f), by = Y(3 + float(r) * 11);
                if (k == 1 && r == 1) draw::fillRect(renderer, bx, by, 8.5f * s, 9 * s, c, 2 * s);
                else draw::strokeRect(renderer, bx, by, 8.5f * s, 9 * s, c, 1.6f, 2 * s);
            }
        } else if (id == "patrickreynolds/touchpad") {
            draw::strokeRect(renderer, X(3), Y(3), 34 * s, 22 * s, c, 1.6f, 3 * s);
            for (int k = 1; k < 6; k++) draw::line(renderer, X(3 + float(k) * 34 / 6), Y(3), X(3 + float(k) * 34 / 6), Y(25), c, 1);
            draw::circle(renderer, X(20), Y(12), 3 * s, c, c, 0);
        } else if (id == "patrickreynolds/tonnetz") {
            const float px[5] = {4, 12, 20, 28, 36}, py[5] = {24, 6, 24, 6, 24};
            for (int k = 0; k < 4; k++) draw::line(renderer, X(px[k]), Y(py[k]), X(px[k + 1]), Y(py[k + 1]), c, 1.6f);
            draw::line(renderer, X(4), Y(24), X(36), Y(24), c, 1.6f);
            draw::line(renderer, X(8), Y(15), X(32), Y(15), c, 1.2f);
            for (int k : {1, 2, 3}) draw::circle(renderer, X(px[k]), Y(py[k]), 2.4f * s, c, c, 0);
        } else if (id == "patrickreynolds/hex") {
            // Five hexagons, two rows nested, a dot in the middle one of the lower row.
            const float cx[5] = {9, 20, 31, 14.5f, 25.5f}, cy[5] = {18.5f, 18.5f, 18.5f, 9, 9}, r = 5.77f;
            const float hx[6] = {0, 5, 5, 0, -5, -5}, hy[6] = {-r, -r / 2, r / 2, r, r / 2, -r / 2};
            for (int k = 0; k < 5; k++)
                for (int i = 0; i < 6; i++)
                    draw::line(renderer, X(cx[k] + hx[i]), Y(cy[k] + hy[i]), X(cx[k] + hx[(i + 1) % 6]), Y(cy[k] + hy[(i + 1) % 6]), c, 1.4f);
            draw::circle(renderer, X(20), Y(18.5f), 2.4f * s, c, c, 0);
        } else {
            draw::strokeRect(renderer, X(6), Y(3), 28 * s, 22 * s, c, 1.6f, 4 * s);
            draw::text(renderer, X(20), Y(19), 12 * s, c, lib.controllers[size_t(n)]->name().substr(0, 1), 1, true);
        }
    }

    // A sound's badge: its pack's colour, the first letters of its name.
    void soundBadge(int i, float x, float y, float size) {
        const Instrument& s = lib.instruments[size_t(i)];
        const tokens::Palette& p = look();
        draw::Color c = rgba(p.eink ? 0xFFFFFFFFu : packColour(s.pack));
        draw::fillRect(renderer, x, y, size, size, c, 8);
        if (p.eink) draw::strokeRect(renderer, x, y, size, size, rgba(p.line), 2, 8);
        draw::text(renderer, x + size / 2, y + size / 2 + 4, 11, rgba(p.eink ? 0x000000FFu : 0x0E1014FFu), s.pkg->name().substr(0, 2), 1, true);
    }

    void chev(float x, float y, draw::Color c, bool down = false) {
        if (down) { draw::line(renderer, x - 4, y - 2, x, y + 2, c, 2); draw::line(renderer, x, y + 2, x + 4, y - 2, c, 2); }
        else chevron(renderer, x, y, 11, 1, c);
    }

    void drawRail(float h) {
        const tokens::Palette& p = look();
        draw::Color bg = rgba(p.ink), text = rgba(p.text), dim = rgba(p.textDim), accent = rgba(p.parts[0]);
        draw::Color tile = rgba(p.panel), key = rgba(p.panelHigh), line = rgba(p.line);
        draw::fillRect(renderer, 0, 0, kRail, h, bg);
        if (p.eink) draw::fillRect(renderer, kRail - 2, 0, 2, h, line);
        // The top row: odo, and buttons for full screen and to hide the rail.
        draw::text(renderer, 12, 30, tokens::type::title, accent, "odo", 0, true);
        draw::fillRect(renderer, kRail - 2 * kButton + 2, 5, kButton - 4, kTop - 10, key, tokens::radius::control);
        draw::fillRect(renderer, kRail - kButton + 2, 5, kButton - 4, kTop - 10, key, tokens::radius::control);
        if (p.eink) for (int b = 1; b <= 2; b++)
            draw::strokeRect(renderer, kRail - float(b) * kButton + 2, 5, kButton - 4, kTop - 10, line, 2, tokens::radius::control);
        draw::strokeRect(renderer, kRail - 1.5f * kButton - 9, kTop / 2 - 7, 18, 14, fullscreen() ? accent : text, 2, 2);
        chevron(renderer, kRail - 0.5f * kButton - 4, kTop / 2, 12, -1, text);
        chevron(renderer, kRail - 0.5f * kButton + 4, kTop / 2, 12, -1, text);

        // Under it, scrolled: the tiles, the sides, the groups, the app.
        scrollRail(0, h);
        std::vector<Box> boxes = railBoxes();
        float room = h - kBottom - kTop;
        clip(0, kTop, kRail, room);
        Side& sd = cur();
        auto gs = sd.host ? mpe::rail::groups(sd.host->manifest(), sd.settings) : std::vector<mpe::rail::Group>();
        for (const Box& b : boxes) {
            float y = kTop + b.y - settingsScroll;
            if (y + b.h < kTop || y > kTop + room) continue;
            auto eyebrow = [&](float ex, float ey, const std::string& t) { draw::text(renderer, ex, ey, 10, dim, t, 0, true); };
            switch (b.kind) {
                case Box::Tile: {
                    bool open = panelKey == b.key || (b.key == kSoundKey && panelKey == kSound2Key);
                    draw::fillRect(renderer, b.x, y, b.w, b.h, open ? key : tile, 12);
                    if (open || p.eink) draw::strokeRect(renderer, b.x, y, b.w, b.h, open && !p.eink ? accent : line, open ? 2 : 1.5f, 12);
                    if (b.key == kPresetKey) {
                        eyebrow(b.x + 12, y + 17, "PRESET");
                        draw::text(renderer, b.x + 12, y + 37, 15, text, draw::fit(presetName.empty() ? "None" : presetName, 15, b.w - 24, true), 0, true);
                        bool edited = presetEdited();
                        std::string sub;
                        for (const Preset& pr : allPresets()) if (pr.name == presetName) sub = presetSubtitle(pr);
                        if (presetName.empty()) sub = "Keep what plays as a preset";
                        draw::text(renderer, b.x + 12, y + 54, 12, edited ? accent : dim, draw::fit(edited ? "Changed since loaded" : sub, 12, b.w - 24));
                    } else if (b.key == kSplitKey) {
                        eyebrow(b.x + 12, y + 17, "SPLIT");
                        splitPic(split, count() > 1 ? active : -1, b.x + 12, y + 28, 46, 30, p.eink ? text : accent);
                        draw::text(renderer, b.x + 12, y + 78, 12, dim, draw::fit(kSplits[split].name, 12, b.w - 24));
                    } else if (b.key == kControllerKey) {
                        eyebrow(b.x + 12, y + 17, "CONTROLLER");
                        controllerPic(sd.controller, b.x + 12, y + 28, 46, 32, p.eink ? text : accent);
                        draw::text(renderer, b.x + 12, y + 78, 12, dim, draw::fit(sd.host ? lib.controllers[size_t(sd.controller)]->name() : "none", 12, b.w - 24));
                    } else if (b.key == kSoundKey) {
                        eyebrow(b.x + 12, y + 17, partsOf(active) > 1 ? "SOUNDS" : "SOUND");
                    }
                    break;
                }
                case Box::Part: {
                    int n = b.key ? sound2Of(active) : sd.sound;
                    bool open = (b.key ? kSound2Key : kSoundKey) == panelKey;
                    const Instrument& s = lib.instruments[size_t(n)];
                    soundBadge(n, b.x + 6, y + 7, 30);
                    std::string name = s.pkg->name() + (s.newest ? "" : " " + s.version()) + (s.index < 0 && s.error.empty() ? "..." : "");
                    float nx = b.x + 46;
                    if (partsOf(active) > 1 && sd.host) {
                        std::string part = sd.host->manifest()["parts"][size_t(b.key)].str() + " ";
                        draw::text(renderer, nx, y + 20, 14, dim, part);
                        nx += draw::textWidth(14, part);
                    }
                    draw::text(renderer, nx, y + 20, 14, open ? accent : text, draw::fit(name, 14, b.x + b.w - nx - 6, true), 0, true);
                    draw::text(renderer, b.x + 46, y + 36, 11, dim, draw::fit(s.pack.empty() ? "" : s.pack + " \xC2\xB7 " + s.group, 11, b.w - 52));
                    break;
                }
                case Box::Eyebrow: eyebrow(b.x + 4, y + b.h - 7, draw::fit(b.text, 10, b.w - 8, true)); break;
                case Box::Map:
                    draw::fillRect(renderer, b.x, y, b.w, b.h, tile, 12);
                    if (p.eink) draw::strokeRect(renderer, b.x, y, b.w, b.h, line, 2, 12);
                    break;
                case Box::Cell: {
                    bool on = b.key == active;
                    const Side& x = sides[b.key];
                    draw::fillRect(renderer, b.x, y, b.w, b.h, on ? (p.eink ? text : rgba(tokens::alpha(p.parts[0], 0x38))) : bg, 8);
                    draw::strokeRect(renderer, b.x, y, b.w, b.h, on ? (p.eink ? text : accent) : line, on ? 2 : 1, 8);
                    draw::Color tc = on && p.eink ? bg : text, sc = on && p.eink ? bg : dim;
                    draw::text(renderer, b.x + 8, y + 15, 11, tc, draw::fit(kSplits[split].side[b.key], 11, b.w - 14, true), 0, true);
                    std::string what = (x.host ? lib.controllers[size_t(x.controller)]->name() : std::string("none")) +
                                       (lib.instruments.empty() ? "" : " \xC2\xB7 " + lib.instruments[size_t(x.sound)].pkg->name());
                    draw::text(renderer, b.x + 8, y + 30, 10, sc, draw::fit(what, 10, b.w - 14));
                    break;
                }
                case Box::Group: {
                    const mpe::rail::Group* g = findGroup(gs, b.text);
                    if (!g) break;
                    int own = panelRow(*g);
                    bool open = (panelKey == kGroupKey && panelGroup == b.text) || (own >= 0 && sd.settings[size_t(own)] != 0);
                    if (open) draw::fillRect(renderer, b.x, y, b.w, b.h, p.eink ? text : key, 10);
                    draw::Color nc = open ? (p.eink ? bg : accent) : text, sc = open && p.eink ? bg : dim;
                    draw::text(renderer, b.x + 12, y + 21, 14, nc, draw::fit(groupLabel(g->name), 14, b.w - 60, true), 0, true);
                    draw::text(renderer, b.x + 12, y + 38, 12, sc, draw::fit(groupSummary(*g), 12, b.w - 60));
                    if (own < 0) draw::text(renderer, b.x + b.w - 26, y + 30, 11, sc, std::to_string(g->rows.size()), 2);
                    chev(b.x + b.w - 12, y + b.h / 2, sc);
                    break;
                }
                case Box::App: {
                    bool open = panelKey == kAppKey;
                    if (open) draw::fillRect(renderer, b.x, y, b.w, b.h, p.eink ? text : key, 10);
                    draw::Color nc = open ? (p.eink ? bg : accent) : text, sc = open && p.eink ? bg : dim;
                    draw::text(renderer, b.x + 12, y + 21, 14, nc, "App", 0, true);
                    std::string s = "Volume " + std::to_string(int(std::lround(masterVolume * 100))) + "% \xC2\xB7 " + std::string(eink ? "E-ink" : "Dark") + (gestures::supported() ? std::string(" \xC2\xB7 gestures ") + (blockGestures ? "blocked" : "allowed") : "");
                    draw::text(renderer, b.x + 12, y + 38, 12, sc, draw::fit(s, 12, b.w - 40));
                    chev(b.x + b.w - 12, y + b.h / 2, sc);
                    break;
                }
                case Box::Takes: {
                    bool open = panelKey == kTakesKey;
                    bool busy = recorder.recording() || player.playing();
                    if (open) draw::fillRect(renderer, b.x, y, b.w, b.h, p.eink ? text : key, 10);
                    draw::Color nc = open ? (p.eink ? bg : accent) : text, sc = open && p.eink ? bg : busy && !p.eink ? accent : dim;
                    draw::text(renderer, b.x + 12, y + 21, 14, nc, "Takes", 0, true);
                    draw::text(renderer, b.x + 12, y + 38, 12, sc, draw::fit(takesSummary(), 12, b.w - 40));
                    chev(b.x + b.w - 12, y + b.h / 2, open && p.eink ? bg : dim);
                    break;
                }
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        float all = boxesHeight(boxes);
        if (all > room) {                                // a scroll bar: how much of the rail shows, and where
            float bar = room * room / all, at = kTop + (room - bar) * settingsScroll / (all - room);
            draw::fillRect(renderer, kRail - 4, at, 2, bar, rgba(tokens::alpha(p.textDim, 0x80)), 1);
        }

        if (updater) {
            Updater::State us = updater->state();
            std::string ln = "odo " + updater->current();
            std::string msg = us == Updater::State::Checking ? "checking for updates" : updater->message();
            bool act = us == Updater::State::Available || us == Updater::State::Installed;
            float ms = act ? tokens::type::header : tokens::type::caption;
            draw::text(renderer, 12, h - 62, tokens::type::caption, dim, draw::fit(ln, tokens::type::caption, kRail - 24));
            draw::text(renderer, 12, h - 46, ms, act ? accent : dim, draw::fit(msg, ms, kRail - 24, act), 0, act);
        }
        mpe::Stats st = engine->stats();
        char b[96];
        std::snprintf(b, sizeof b, "Latency %.1f ms, max %.1f", st.eventAgeAvgMs, st.eventAgeMaxMs);
        draw::text(renderer, 12, h - 30, tokens::type::caption, dim, draw::fit(b, tokens::type::caption, kRail - 24));
        std::snprintf(b, sizeof b, "Load %.0f%%  Voices %d", 100 * st.loadAvg, engine->activeVoices());
        draw::text(renderer, 12, h - 14, tokens::type::caption, dim, draw::fit(b, tokens::type::caption, kRail - 24));
        drawPanel();
    }

    // The open panel, beside the rail over the play area: its name, what it is for and Done, then
    // its rows, tiles, chips, switches and sliders.
    void drawPanel() {
        Panel pl = panelLayout();
        if (!pl.open) return;
        const tokens::Palette& p = look();
        draw::Color text = rgba(p.text), dim = rgba(p.textDim), accent = rgba(p.parts[0]), line = rgba(p.line), bg = rgba(p.ink);
        draw::Color soft = rgba(tokens::alpha(p.line, 0xC0));
        draw::fillRect(renderer, pl.x, pl.y, pl.w, pl.h, rgba(p.panel), tokens::radius::card);
        draw::strokeRect(renderer, pl.x, pl.y, pl.w, pl.h, p.eink ? line : soft, p.eink ? 2 : 1, tokens::radius::card);
        draw::text(renderer, pl.x + kPad, pl.y + 29, 17, text, draw::fit(pl.title, 17, pl.w - 2 * kPad - 70, true), 0, true);
        draw::text(renderer, pl.x + kPad, pl.y + 47, 12, dim, draw::fit(pl.sub, 12, pl.w - 2 * kPad - 70));
        draw::text(renderer, pl.x + pl.w - kPad - 4, pl.y + 31, tokens::type::body + 1, accent, "Done", 2, true);
        clip(pl.x, pl.top, pl.w, pl.room);
        for (const PItem& it : pl.items) {
            float y = pl.top + it.y - panelScroll;
            if (y + it.h < pl.top || y > pl.top + pl.room) continue;
            if (it.rule) draw::line(renderer, it.x, y, it.x + it.w, y, rgba(tokens::alpha(p.line, 0x90)), 1);
            switch (it.kind) {
                case PItem::Heading:
                    draw::text(renderer, it.x, y + 18, 11, it.colour && !p.eink ? rgba(it.colour) : dim, it.text, 0, true);
                    break;
                case PItem::Sub: draw::text(renderer, it.x, y + 14, 12, dim, it.text); break;
                case PItem::Note: draw::text(renderer, it.x, y + 16, 12, dim, draw::fit(it.text, 12, it.w)); break;
                case PItem::Row: {
                    float tx = it.x + 2;
                    if (it.pic == 1) { splitPic(it.picArg, -1, it.x + 2, y + 12, 38, 26, it.on ? accent : dim); tx = it.x + 52; }
                    if (it.pic == 2) { controllerPic(it.picArg, it.x + 2, y + 10, 38, 30, it.on ? accent : dim); tx = it.x + 52; }
                    bool two = !it.sub.empty();
                    draw::text(renderer, tx, y + (two ? 21 : 25), 14, it.on ? accent : text, draw::fit(it.text, 14, it.x + it.w - tx, true), 0, true);
                    if (two) draw::text(renderer, tx, y + 38, 12, dim, draw::fit(it.sub, 12, it.x + it.w - tx));
                    break;
                }
                case PItem::Tile: {
                    draw::fillRect(renderer, it.x, y, it.w, it.h, bg, 12);
                    draw::strokeRect(renderer, it.x, y, it.w, it.h, it.on ? accent : line, it.on ? 2 : 1, 12);
                    draw::Color pc = it.on ? accent : dim;
                    if (it.pic == 1) splitPic(it.picArg, -1, it.x + 10, y + 10, 58, 38, pc);
                    else controllerPic(it.picArg, it.x + 10, y + 10, 58, 40, pc);
                    draw::text(renderer, it.x + 10, y + 68, 13, text, draw::fit(it.text, 13, it.w - 20, true), 0, true);
                    draw::text(renderer, it.x + 10, y + 85, 11, dim, draw::fit(it.sub, 11, it.w - 20));
                    break;
                }
                case PItem::Chip:
                    if (it.on) draw::fillRect(renderer, it.x, y, it.w, it.h, p.eink ? text : rgba(tokens::alpha(p.parts[0], 0x38)), 8);
                    draw::strokeRect(renderer, it.x, y, it.w, it.h, p.eink ? line : it.on ? accent : line, p.eink ? 2 : 1, 8);
                    draw::text(renderer, it.x + it.w / 2, y + it.h / 2 + 4.5f, 13, it.on ? (p.eink ? bg : accent) : text, draw::fit(it.text, 13, it.w - 12), 1);
                    break;
                case PItem::Switch: {
                    draw::text(renderer, it.x, y + 21, 13, text, draw::fit(it.text, 13, it.w - 60, true), 0, true);
                    draw::text(renderer, it.x, y + 37, 12, dim, draw::fit(it.sub, 12, it.w - 60));
                    float sx = it.x + it.w - 36, sy = y + it.h / 2 - 10;
                    draw::fillRect(renderer, sx, sy, 36, 20, it.on ? (p.eink ? text : accent) : line, 10);
                    if (p.eink) draw::strokeRect(renderer, sx, sy, 36, 20, line, 1.5f, 10);
                    draw::circle(renderer, it.on ? sx + 26 : sx + 10, sy + 10, 8, it.on && p.eink ? bg : rgba(0xECEFF5FFu), it.on && p.eink ? bg : rgba(0xECEFF5FFu), 0);
                    break;
                }
                case PItem::Fold:
                    draw::text(renderer, it.x, y + 21, 13, text, draw::fit(it.text, 13, it.w - 30, true), 0, true);
                    draw::text(renderer, it.x, y + 37, 12, it.on ? accent : dim, draw::fit(it.sub, 12, it.w - 30));
                    chev(it.x + it.w - 8, y + it.h / 2, dim, true);
                    break;
                case PItem::Label:
                    draw::text(renderer, it.x, y + 21, 13, text, draw::fit(it.text, 13, it.w - 90, true), 0, true);
                    draw::text(renderer, it.x + it.w, y + 21, 12, dim, draw::fit(it.sub, 12, 120), 2);
                    break;
                case PItem::Slider: {
                    float ty = y + it.h / 2, fx = it.x + float(it.frac) * it.w;
                    draw::fillRect(renderer, it.x, ty - 2, it.w, 4, p.eink ? rgba(0xBBBBBBFF) : line, 2);
                    draw::fillRect(renderer, it.x, ty - 2, fx - it.x, 4, accent, 2);
                    draw::circle(renderer, fx, ty, 9, accent, accent, 0);
                    break;
                }
                case PItem::Field: {
                    draw::fillRect(renderer, it.x, y, it.w, it.h, bg, 10);
                    draw::strokeRect(renderer, it.x, y, it.w, it.h, accent, p.eink ? 2 : 1.5f, 10);
                    bool empty = it.text.empty();
                    std::string shown = empty ? it.sub : draw::fit(it.text, 14, it.w - 28);
                    draw::text(renderer, it.x + 12, y + 24, 14, empty ? dim : text, shown);
                    float cx = it.x + 12 + (empty ? 0 : draw::textWidth(14, shown)) + 1;
                    if ((SDL_GetTicks() / 500) % 2 == 0) draw::fillRect(renderer, cx, y + 10, 2, 18, accent);
                    break;
                }
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        if (pl.contentH > pl.room) {                     // a scroll bar for a long panel
            float bar = pl.room * pl.room / pl.contentH, at = pl.top + (pl.room - bar) * panelScroll / (pl.contentH - pl.room);
            draw::fillRect(renderer, pl.x + pl.w - 6, at, 2, bar, rgba(tokens::alpha(p.textDim, 0x80)), 1);
        }
    }

    void frame() {
        collectCompiled();
        collectDialogs();
        // Typing (a preset's name, a sound's search) while its panel is open, and only then.
        if (typing() != SDL_TextInputActive(window)) { if (typing()) SDL_StartTextInput(window); else SDL_StopTextInput(window); }
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        layoutSides();
        for (int k = 0; k < count(); k++) {
            Side& sd = sides[k];
            if (!sd.host) continue;
            if (sd.w != sd.initW || sd.h != sd.initH) {
                sd.initW = sd.w; sd.initH = sd.h;
                sd.host->init(sd.w, sd.h, eink ? 1 : 0);
                for (size_t i = 0; i < sd.settings.size(); i++) sd.host->setting(int(i), sd.settings[i]);
                for (int j = 0; j < partsOf(k) && !lib.instruments.empty(); j++) {
                    const Instrument& in = lib.instruments[size_t(j ? sound2Of(k) : sd.sound)];
                    if (in.index >= 0) sd.host->instrument(j, centerNote(*in.pkg));
                }
            }
            sd.host->tick();
            // Settings it changed itself (its editor, a button on its surface): kept as if set here.
            bool changed = false;
            int parts = partsOf(k);
            for (const auto& c : sd.host->takeSettingChanges()) {
                if (size_t(c.index) >= sd.settings.size()) continue;
                keepSetting(k, size_t(c.index), c.value);
                changed = true;
            }
            if (partsOf(k) != parts) reroute();
            if (changed) saveConfig();
        }
        player.update(nowMs(), takeSink());              // a take playing: its notes due now
        SDL_SetRenderViewport(renderer, nullptr);
        draw::Color back = rgba(look().ink);        // between the sides: black, or e-ink's white
        SDL_SetRenderDrawColor(renderer, Uint8(back.r * 255), Uint8(back.g * 255), Uint8(back.b * 255), 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderClipRect(renderer, nullptr);
        bool any = false;
        for (int k = 0; k < count(); k++) {
            Side& sd = sides[k];
            if (!sd.host) continue;
            any = true;
            SDL_Rect area{int(sd.x), int(sd.y), int(sd.w), int(sd.h)};
            SDL_SetRenderViewport(renderer, &area);
            draw::list(renderer, sd.host->drawList(), sd.w, sd.h);
            if (player.playing()) draw::list(renderer, player.drawGhosts(k, sd.w, sd.h, ghostStyle()), sd.w, sd.h);
        }
        SDL_SetRenderViewport(renderer, nullptr);
        // The lines between the sides; with the rail out, the side it changes is outlined.
        for (int k = 0; k < count() && count() > 1; k++) {
            const Side& sd = sides[k];
            draw::Color gap = rgba(eink ? look().line : 0x000000FFu);
            if (sd.x > railW() + 1) draw::fillRect(renderer, sd.x - kGap, sd.y, kGap, sd.h, gap);
            if (sd.y > 1) draw::fillRect(renderer, sd.x, sd.y - kGap, sd.w, kGap, gap);
        }
        if (!railHidden && count() > 1) {
            const Side& sd = cur();
            draw::strokeRect(renderer, sd.x + 1, sd.y + 1, sd.w - 2, sd.h - 2, rgba(tokens::alpha(look().parts[0], 0xB0)), 2, 4);
        }
        if (!any) draw::text(renderer, railW() + 20, 40, 14, rgba(look().text), "No controller packages (.odoc) in the library.");
        if (!status.empty()) draw::text(renderer, railW() + 16, float(wh) - 16, tokens::type::header, rgba(look().warn), status);
        if (!railHidden) drawRail(float(wh));
        else {                                           // the corner button that brings the rail back
            draw::fillRect(renderer, 6, 6, 36, 36, rgba(tokens::alpha(look().ink, 0xCC)), 8);
            chevron(renderer, 20, 24, 12, 1, rgba(look().text));
            chevron(renderer, 28, 24, 12, 1, rgba(look().text));
        }
        SDL_RenderPresent(renderer);
    }
};

// Plays the controllers by themselves for a moment and saves what was on screen: a check that
// runs without a touch screen (SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy). Fingers go down
// at `touches` (fractions of the play area, right of the rail; by default two places on the
// Tonnetz), optionally move by `move` (fractions), and stay down while the screenshot is taken.
// Passes if at least `expect` voices sound.
struct Selftest {
    std::vector<std::pair<float, float>> touches;
    float moveX = 0, moveY = 0;
    int expect = -1;
    std::vector<std::pair<float, float>> taps;   // taps on the rail or its panel first, in window points
    bool listen = false;                         // --take: it plays (Listen) before the touches, its ghosts in the screenshot
};

int selftest(App& app, const char* out, Selftest t) {
    auto wait = [&](int ms) {
        Uint64 end = SDL_GetTicks() + Uint64(ms);
        while (SDL_GetTicks() < end) { SDL_Event e; while (SDL_PollEvent(&e)) {} app.frame(); SDL_Delay(5); }
    };
    while (!app.soundsReady()) {
        for (int k = 0; k < app.count(); k++)
            if (!app.compiling && !app.lib.instruments.empty() && !app.lib.instruments[size_t(app.sides[k].sound)].error.empty()) {
                std::fprintf(stderr, "selftest: %s\n", app.status.c_str());
                return 1;
            }
        if (app.lib.instruments.empty()) { std::fprintf(stderr, "selftest: no instruments\n"); return 1; }
        wait(20);
    }
    wait(100);
    for (size_t k = 0; k < t.taps.size(); k++) {
        app.railPress(0, int64_t(2000 + k), t.taps[k].first, t.taps[k].second);
        wait(20);
        app.railPress(2, int64_t(2000 + k), t.taps[k].first, t.taps[k].second);
        wait(50);
    }
    while (!app.soundsReady()) wait(20);         // a tap may have picked a sound still to load
    int ww, wh;
    SDL_GetWindowSize(app.window, &ww, &wh);
    float aw = float(ww) - app.railW(), ah = float(wh);
    if (t.listen) {
        // A take (--take): it plays by itself, its ghost fingers over the controller. Its settings
        // first (they may name a sound to load), then it plays.
        app.useTakeSettings();
        for (int k = 0; k < 500 && !app.soundsReady(); k++) wait(20);
        wait(100);
        app.playTake(mpe::take::Player::Listen);
        if (!app.player.playing()) { std::fprintf(stderr, "selftest: the take does not play: %s\n", app.takeNote.c_str()); return 1; }
        wait(2500);
        if (t.touches.empty()) {
            int voices = app.engine->activeVoices();
            SDL_Surface* shot = SDL_RenderReadPixels(app.renderer, nullptr);
            bool saved = shot && SDL_SaveBMP(shot, out);
            if (shot) SDL_DestroySurface(shot);
            std::printf("selftest: take \xE2\x80\x9C%s\xE2\x80\x9D at %.1f s of %.1f, %d voices sounding%s%s, screenshot %s\n",
                        app.player.take().name.c_str(), app.player.position() / 1000, app.player.length() / 1000, voices,
                        app.takeNote.empty() ? "" : ": ", app.takeNote.c_str(), saved ? out : "not saved");
            app.stopTake();
            return voices >= 1 && app.takeNote.empty() ? 0 : 1;
        }
    }
    if (t.touches.empty()) {
        // The Tonnetz at size 7: the C major triangle, and a node.
        float dx = 1.f / 7, dy = aw * dx / ah;
        t.touches = {{0.5f + dx / 2, 0.5f - dy * 0.29f}, {0.5f - dx, 0.5f}};
        if (t.expect < 0) t.expect = 4;
    }
    if (t.expect < 0) t.expect = 1;
    auto at = [&](size_t k, float ox, float oy) {
        return std::make_pair(app.railW() + (t.touches[k].first + ox) * aw, (t.touches[k].second + oy) * ah);
    };
    using H = mpe::ControllerHost;
    for (size_t k = 0; k < t.touches.size(); k++) {
        auto p = at(k, 0, 0);
        app.pointer(int64_t(1000 + k), H::Down, p.first, p.second, 0.8f);
        wait(30);
    }
    if (t.moveX != 0 || t.moveY != 0)
        for (int step = 1; step <= 10; step++) {
            for (size_t k = 0; k < t.touches.size(); k++) {
                auto p = at(k, t.moveX * float(step) / 10, t.moveY * float(step) / 10);
                app.pointer(int64_t(1000 + k), H::Move, p.first, p.second, 0.8f);
            }
            wait(20);
        }
    wait(400);
    int voices = app.engine->activeVoices();
    mpe::Stats st = app.engine->stats();
    SDL_Surface* shot = SDL_RenderReadPixels(app.renderer, nullptr);
    bool saved = shot && SDL_SaveBMP(shot, out);
    if (shot) SDL_DestroySurface(shot);
    for (size_t k = 0; k < t.touches.size(); k++) {
        auto p = at(k, t.moveX, t.moveY);
        app.pointer(int64_t(1000 + k), H::Up, p.first, p.second, 0);
    }
    wait(100);
    std::string errors;
    for (int k = 0; k < app.count(); k++)
        if (app.sides[k].host && !app.sides[k].host->lastError().empty()) errors += (errors.empty() ? "" : "; ") + app.sides[k].host->lastError();
    std::printf("selftest: %d voices sounding (want at least %d), %llu events, latency avg %.2f ms, controller errors: %s, screenshot %s\n",
                voices, t.expect, (unsigned long long)st.events, st.eventAgeAvgMs,
                errors.empty() ? "none" : errors.c_str(), saved ? out : "not saved");
    return voices >= t.expect && errors.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    const char* selftestOut = nullptr;
    const char* configArg = nullptr;                     // --config FILE: settings from and to it, even in a self-test
    std::vector<std::string> controllerIds, soundIds;    // each --controller / --sound: the next side's
    Selftest test;
    struct Preset { int side, index; double value; };
    std::vector<Preset> presetSettings;                  // --setting [SIDE:]INDEX=VALUE
    std::vector<std::string> openPresets;                // --open-preset FILE, or a bare .odopreset path (the file manager's "Open with")
    std::vector<std::string> openTakes;                  // --take FILE, or a bare .odotake path: one of your takes (a self-test plays it)
    const char* sharePreset = nullptr;                   // --share-preset FILE: the preset on view into FILE, then quit
    int width = 1280, height = 720, splitArg = -1;
    bool logMidi = false, checkUpdates = true, einkArg = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool more = i + 1 < argc;
        float x = 0, y = 0;
        if (a == "--library" && more) dirs.push_back(argv[++i]);
        else if (a == "--selftest" && more) selftestOut = argv[++i];
        else if (a == "--controller" && more) controllerIds.push_back(argv[++i]);
        else if (a == "--sound" && more) soundIds.push_back(argv[++i]);
        else if (a == "--split" && more) splitArg = std::clamp(std::atoi(argv[++i]), 0, kSplitCount - 1);
        else if (a == "--log-midi") logMidi = true;
        else if (a == "--eink") einkArg = true;
        else if (a == "--config" && more) configArg = argv[++i];
        else if (a == "--no-update") checkUpdates = false;
        else if (a == "--open-preset" && more) openPresets.push_back(argv[++i]);
        else if (a == "--share-preset" && more) sharePreset = argv[++i];
        else if (a == "--take" && more) openTakes.push_back(argv[++i]);
        else if (fs::path(a).extension() == mpe::take::kExtension) openTakes.push_back(a);
        else if (a.size() > 10 && a.compare(a.size() - 10, 10, mpe::preset::kExtension) == 0) openPresets.push_back(a);
        else if (a == "--version") { std::printf("odo %s\n", ODO_DESKTOP_VERSION); return 0; }
        else if (a == "--self-update") {
            // Without the window: check, install if newer, and say what happened.
            Updater u(ODO_DESKTOP_VERSION, exeDir());
            auto settle = [&] { while (u.state() == Updater::State::Checking || u.state() == Updater::State::Downloading) SDL_Delay(50); };
            u.check(); settle();
            if (u.state() == Updater::State::Available) { u.install(); settle(); }
            std::printf("odo %s: %s\n", ODO_DESKTOP_VERSION, u.message().c_str());
            return u.state() == Updater::State::Failed ? 1 : 0;
        }
        else if (a == "--touch" && more && std::sscanf(argv[i + 1], "%f,%f", &x, &y) == 2) { test.touches.push_back({x, y}); i++; }
        else if (a == "--move" && more && std::sscanf(argv[i + 1], "%f,%f", &x, &y) == 2) { test.moveX = x; test.moveY = y; i++; }
        else if (a == "--tap" && more && std::sscanf(argv[i + 1], "%f,%f", &x, &y) == 2) { test.taps.push_back({x, y}); i++; }
        else if (a == "--expect" && more) test.expect = std::atoi(argv[++i]);
        else if (a == "--setting" && more && std::strchr(argv[i + 1], '=')) {
            const char* s = argv[++i];
            const char *colon = std::strchr(s, ':'), *eq = std::strchr(s, '=');
            bool sided = colon && colon < eq;
            presetSettings.push_back({sided ? std::clamp(std::atoi(s) - 1, 0, kSides - 1) : 0, std::atoi(sided ? colon + 1 : s), std::atof(eq + 1)});
        }
        else if (a == "--size" && more && std::sscanf(argv[i + 1], "%dx%d", &width, &height) == 2) i++;
        else {
            std::fprintf(stderr, "usage: odo [--library DIR]... [--split N] [--controller ID]... [--sound ID]... [--log-midi] [--eink] [--config FILE] [--no-update] [--version]\n"
                                 "           [--open-preset FILE]... [--share-preset FILE] [FILE.odopreset]... [--take FILE]... [FILE.odotake]...\n"
                                 "           [--setting [SIDE:]INDEX=VALUE]... [--selftest out.bmp [--tap X,Y]... [--touch X,Y]... [--move DX,DY] [--expect N] [--size WxH]]\n");
            return 2;
        }
    }
    if (dirs.empty()) {
        std::error_code ec;
        if (fs::is_directory(exeDir() + "/library", ec)) dirs.push_back(exeDir() + "/library");
        dirs.push_back(home() + "/.local/share/odo/library");
    }

    // "odo": the window's app id, so the desktop matches it to odo.desktop (its icon in the dock).
    SDL_SetAppMetadata("odo", ODO_DESKTOP_VERSION, "odo");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   // a touch is a finger, not also a mouse
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { std::fprintf(stderr, "odo: %s\n", SDL_GetError()); return 1; }
    App app;
    for (const auto& d : dirs) app.lib.scan(d);
    app.lib.order();
    std::printf("odo: %zu instruments, %zu controllers\n", app.lib.instruments.size(), app.lib.controllers.size());

    app.engine = std::make_unique<mpe::Engine>(kRate);
    app.engine->setVolume(0.8f);              // until the saved master volume is read
    app.midi = std::make_unique<mpe::MidiInput>(*app.engine);
    app.logMidi = logMidi;
    app.argv = argv;
    if (checkUpdates && !selftestOut) {
        app.updater = std::make_unique<Updater>(ODO_DESKTOP_VERSION, exeDir());
        app.updater->check();
    }
    if (!SDL_CreateWindowAndRenderer("odo", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app.window, &app.renderer)) {
        std::fprintf(stderr, "odo: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    float density = SDL_GetWindowDisplayScale(app.window);
    SDL_SetRenderScale(app.renderer, density > 0 ? density : 1, density > 0 ? density : 1);
    app.startAudio();

    // Where you left off (not for a self-test, which neither reads nor writes it): the split, each
    // side's controller and sound, each controller's settings, the rail and full screen.
    app.persist = !selftestOut || configArg;
    if (configArg) app.configPath = configArg;
    if (app.persist) {
        app.loadConfig();
        if (app.savedFullscreen) SDL_SetWindowFullscreen(app.window, true);
    }
    // Saving waits until everything is chosen, so a half-made start doesn't overwrite the file.
    bool persist = app.persist;
    app.persist = false;
    app.loadingPreset = true;                    // nor is a half-made start kept in the preset on view
    for (int k = 0; k < kSides; k++) {
        // The command line's, else where you left off, else the flute (or the first side's) and the first controller.
        std::string cid = k < int(controllerIds.size()) ? controllerIds[size_t(k)] : app.savedController[k];
        std::string sid = k < int(soundIds.size()) ? soundIds[size_t(k)] : app.savedSound[k];
        if (cid.empty()) cid = k > 0 && !controllerIds.empty() ? controllerIds[0] : app.savedController[0];
        if (sid.empty()) sid = k > 0 && !soundIds.empty() ? soundIds[0] : app.savedSound[0];
        if (sid.empty()) sid = "patrickreynolds/flute";
        // "id" or "id@version" (a version kept to go back to); one this library hasn't: the newest.
        int n = app.soundIndex(sid);
        if (n < 0) n = app.soundIndex(sid.substr(0, sid.find('@')));
        if (n >= 0) app.sides[k].sound = n;
        if (!app.savedSound2[k].empty()) {
            int n2 = app.soundIndex(app.savedSound2[k]);
            if (n2 < 0) n2 = app.soundIndex(app.savedSound2[k].substr(0, app.savedSound2[k].find('@')));
            if (n2 >= 0) app.sides[k].sound2 = n2;
        }
        for (size_t i = 0; i < app.lib.controllers.size(); i++) if (app.lib.controllers[i]->id() == cid) app.sides[k].controller = int(i);
        if (k < int(controllerIds.size()) && (app.lib.controllers.empty() || app.lib.controllers[size_t(app.sides[k].controller)]->id() != cid))
            std::fprintf(stderr, "odo: no controller %s in the library\n", cid.c_str());
    }
    if (einkArg) app.eink = true;
    app.setMasterVolume(app.masterVolume, false);
    app.active = selftestOut ? 0 : app.savedActive;
    app.setSplit(splitArg >= 0 ? splitArg : app.savedSplit);
    for (const auto& ps : presetSettings) {
        App::Side& sd = app.sides[ps.side];
        if (sd.host && ps.index >= 0 && size_t(ps.index) < sd.settings.size()) {
            sd.settings[size_t(ps.index)] = ps.value;
            sd.host->setting(ps.index, ps.value);
        }
    }
    app.reroute();                               // a --setting may have split a controller
    app.loadingPreset = false;
    app.persist = persist;
    if (app.persist && app.blockGestures) app.gestureNote = gestures::set(true);   // and brings the extension up to date
    app.saveConfig();
    for (const auto& p : openPresets) { app.openPresetFile(p); std::printf("odo: %s\n", app.status.c_str()); }
    // Takes: in a folder of the self-test's own when testing, so it leaves yours alone.
    if (selftestOut) {
        app.takesDir = (fs::temp_directory_path() / "odo-selftest-takes").string();
        std::error_code ec;
        fs::remove_all(app.takesDir, ec);             // each self-test starts with none
    }
    app.scanTakes();
    for (const auto& f : openTakes) {
        bool ok = app.openTakeFile(f);
        std::printf("odo: %s\n", app.takeNote.c_str());
        test.listen |= ok && selftestOut;
    }
    if (sharePreset) {
        bool ok = app.savePresetFile(app.presetName, sharePreset);
        std::printf("odo: %s\n", app.status.c_str());
        app.shutdown(); SDL_Quit();
        return ok ? 0 : 1;
    }

    if (selftestOut) { int rc = selftest(app, selftestOut, test); app.shutdown(); SDL_Quit(); return rc; }
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && !app.typing())) running = false;
            else app.handle(e);
        }
        app.frame();
    }
    app.shutdown();
    SDL_Quit();
    return 0;
}
