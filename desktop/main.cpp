// odo for computers: a basic MESH. It plays instrument packages (.odoi, compiled with libfaust)
// with controller packages (.odoc, run in wasm3) on a touch screen, from the same files as odo
// on Android. Linux and Windows (SDL3).
//
//   odo [--library DIR]... [--split N] [--controller ID]... [--sound ID]... [--log-midi] [--eink] [--config FILE] [--no-update]
//       [--version] [--self-update] [--setting [SIDE:]INDEX=VALUE]...
//       [--selftest out.bmp [--tap X,Y]... [--touch X,Y]... [--move DX,DY] [--expect N] [--size WxH]]
//
// Library folders hold .odoi and .odoc files (default: the library folder beside odo, and
// ~/.local/share/odo/library). It checks GitHub for a newer odo when it opens (--no-update: not);
// the update line at the bottom of the rail installs it. The rail on
// the left, as odo on Android's, picks the split, then for each side its controller, sound and
// settings: a tap opens a panel beside it with chips, a slider or a list (--tap: a self-test's
// taps there, in window points, before its touches). The rest of
// the window is the controllers: whole, or split side by side, above and below, in three or in
// four, each side playing its own sound. (--controller and --sound repeat: the first is the
// first side's, the next the second's.) Presets, as Android's, keep all of that under a name; and
// on GNOME, odo can keep the desktop's three-finger swipes off it while it has focus (gestures.h).
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
#include "mpe/rail.h"

namespace fs = std::filesystem;
using draw::rgba;

namespace {

constexpr int kRate = 48000;           // instruments are tuned at 48 kHz: SDL converts if the device differs
constexpr float kRail = 230;           // the rail's width, points

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
};

struct Library {
    std::vector<Instrument> instruments;
    std::vector<std::unique_ptr<mpe::Package>> controllers;

    void scan(const std::string& dir) {
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
            if (pkg->isInstrument()) instruments.push_back({std::move(pkg), -1, ""});
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
    };
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
        }
        savedSplit = std::clamp(int(j["split"].num(0)), 0, kSplitCount - 1);
        savedActive = std::clamp(int(j["active"].num(0)), 0, kSides - 1);
        railHidden = j["railHidden"].boolean();
        savedFullscreen = j["fullscreen"].boolean();
        eink = j["eink"].boolean();
        blockGestures = j["blockGestures"].boolean();
        presetName = j["preset"].str();
        const mpe::Json& ps = j["presets"];
        for (size_t i = 0; i < ps.size(); i++) {
            Preset p;
            p.name = ps[i]["name"].str();
            if (p.name.empty()) continue;
            p.split = std::clamp(int(ps[i]["split"].num(0)), 0, kSplitCount - 1);
            const mpe::Json& sd = ps[i]["sides"];
            for (size_t k = 0; k < sd.size() && k < size_t(kSides); k++) {
                PresetSide s{sd[k]["controller"].str(), sd[k]["sound"].str(), sd[k]["sound2"].str(), {}};
                for (const auto& kv : sd[k]["settings"].o) s.settings[kv.first] = kv.second.num();
                p.sides.push_back(std::move(s));
            }
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
        return lib.instruments.empty() ? "" : lib.instruments[size_t(sides[k].sound)].pkg->id();
    }
    std::string sound2Id(int k) const {
        return lib.instruments.empty() || sides[k].sound2 < 0 ? "" : lib.instruments[size_t(sides[k].sound2)].pkg->id();
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
        for (int k = 0; k < kSides; k++)
            f << (k ? ", " : "") << "{\"controller\": " << quoted(controllerId(k)) << ", \"sound\": " << quoted(soundId(k))
              << ", \"sound2\": " << quoted(sound2Id(k)) << "}";
        f << "],\n  \"railHidden\": " << (railHidden ? "true" : "false")
          << ",\n  \"eink\": " << (eink ? "true" : "false")
          << ",\n  \"blockGestures\": " << (blockGestures ? "true" : "false")
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
                f << "}}";
            }
            f << "]}";
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
    struct PresetSide { std::string controller, sound, sound2; std::map<std::string, double> settings; };
    struct Preset { std::string name; int split = 0; std::vector<PresetSide> sides; bool builtIn = false; };
    std::vector<Preset> userPresets;
    std::string presetName;               // the one on view, or ""
    bool loadingPreset = false;
    bool confirmDelete = false;           // "Delete" was tapped once: the next tap deletes

    static std::vector<Preset> builtInPresets() {
        auto side = [](const char* c, const char* s, std::map<std::string, double> st = {}, const char* s2 = "") {
            return PresetSide{std::string("patrickreynolds/") + c, std::string("patrickreynolds/") + s,
                              *s2 ? std::string("patrickreynolds/") + s2 : std::string(), std::move(st)};
        };
        return {
            {"Grand piano", 0, {side("grid", "piano", {{"split", 0}, {"slide", 0}})}, true},
            {"Slide a flute", 0, {side("grid", "flute", {{"split", 0}, {"slide", 1}})}, true},
            {"Bass below, lead above", 0, {side("grid", "bass-guitar", {{"split", 1}, {"slide", 0}}, "lead")}, true},
            {"Cello chords", 0, {side("tonnetz", "cello")}, true},
            {"Dreamy pad", 0, {side("touchpad", "pad", {{"split", 0}})}, true},
            {"Keys over drums", 2, {side("grid", "rhodes", {{"split", 0}, {"slide", 0}}), side("grid", "kit", {{"split", 0}, {"slide", 0}})}, true},
        };
    }

    int controllerIndex(const std::string& id) const {
        for (size_t i = 0; i < lib.controllers.size(); i++) if (lib.controllers[i]->id() == id) return int(i);
        return -1;
    }
    int soundIndex(const std::string& id) const {
        for (size_t i = 0; i < lib.instruments.size(); i++) {
            const auto& p = *lib.instruments[i].pkg;
            if (p.id() == id || p.manifest["aliases"][size_t(0)].str() == id) return int(i);
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
        for (int k = 0; k < count(); k++) {
            PresetSide s{controllerId(k), soundId(k), sound2Id(k), {}};
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

    void loadPreset(const Preset& p) {
        loadingPreset = true;
        bool keep = persist;
        persist = false;
        for (size_t k = 0; k < p.sides.size() && k < size_t(kSides); k++) {
            const PresetSide& s = p.sides[k];
            Side& sd = sides[k];
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
        setSplit(p.split);
        presetName = p.name;
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
            const mpe::Json& s = lib.instruments[size_t(j ? sound2Of(k) : sides[k].sound)].pkg->manifest["sound"];
            mpe::PartConfig& c = p.part[part];
            c.reverb = float(s["reverb"].num(0.15));
            c.delay = float(s["delay"].num(0));
            c.brightness = float(s["brightness"].num(1));
            c.drive = float(s["drive"].num(0));
            c.pressureCurve = float(s["pressureCurve"].num(1));
            c.release = float(s["release"].num(0));
            if (part == 0) {                     // the room is shared: the first side's
                p.reverbSize = float(s["reverbSize"].num(0.5));
                p.reverbDamp = float(s["reverbDamp"].num(0.4));
            }
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

    // ---- the rail and its panel -----------------------------------------------------------------

    // As odo on Android: the rail lists what can be changed, each a name over its value. A tap
    // flips a switch, or opens a panel beside the rail with the setting's chips, slider or list,
    // until Done (or the row again). The controller's settings come from mpe::rail, in their
    // groups, as Android shows them; under the top row the whole rail scrolls.
    static constexpr float kButton = 40;           // the top buttons' width
    static constexpr float kTop = 46;              // the top row: odo, full screen, hide
    static constexpr float kBottom = 84;           // the lines at the bottom: version and update, latency, load
    static constexpr float kItem = 40, kHeading = 26, kToggle = 42, kDivider = 13;

    // What a row opens or flips: a controller setting by its place in the manifest (0 and up), or one of these.
    enum Key { kNone = -100, kSplitKey = -1, kControllerKey = -2, kSoundKey = -3, kSound2Key = -4, kLookKey = -5,
               kPresetKey = -6, kRenameKey = -7, kGesturesKey = -8 };

    struct RailItem {
        enum Kind { Row, Heading, Toggle, Divider } kind;
        int key;
        std::string label, value;
        bool isSwitch, on;                         // a switch, and whether it is on
        float y, h;                                // from the top of the scrolled part, unscrolled
    };

    std::string soundName(int n) const {
        const Instrument& in = lib.instruments[size_t(n)];
        return in.pkg->name() + (in.index < 0 && in.error.empty() ? "..." : "");
    }

    // The rail under its top row: the split, which side it changes, that side's controller and
    // sound (two, for a split controller), then the controller's settings. Drawing, tapping and
    // scrolling all use this one layout.
    std::vector<RailItem> railItems() {
        std::vector<RailItem> items;
        float y = 0;
        auto add = [&](RailItem::Kind k, int key, std::string label, std::string value, float h, bool sw = false, bool on = false) {
            items.push_back({k, key, std::move(label), std::move(value), sw, on, y, h});
            y += h;
        };
        Side& sd = cur();
        add(RailItem::Row, kPresetKey, "Preset", presetName.empty() ? "None" : presetName + (presetEdited() ? " (changed)" : ""), kItem);
        add(RailItem::Row, kSplitKey, "Split", kSplits[split].name, kItem);
        if (count() > 1) add(RailItem::Toggle, kNone, "", "", kToggle);
        add(RailItem::Row, kControllerKey, "Controller", sd.host ? lib.controllers[size_t(sd.controller)]->name() : "none", kItem);
        if (!lib.instruments.empty()) {
            bool two = partsOf(active) > 1;
            const mpe::Json& parts = sd.host ? sd.host->manifest()["parts"] : mpe::Json();
            add(RailItem::Row, kSoundKey, two ? "Sound: " + parts[size_t(0)].str() : "Sound", soundName(sd.sound), kItem);
            if (two) add(RailItem::Row, kSound2Key, "Sound: " + parts[size_t(1)].str(), soundName(sound2Of(active)), kItem);
        }
        add(RailItem::Row, kLookKey, "Look", eink ? "E-ink" : "Dark", kItem);
        if (gestures::supported())
            add(RailItem::Row, kGesturesKey, "Block system gestures", !blockGestures ? "Off" : gestureNote.empty() ? "On" : gestureNote,
                kItem, true, blockGestures);
        if (sd.host) {
            add(RailItem::Divider, kNone, "", "", kDivider);
            for (auto& g : mpe::rail::groups(sd.host->manifest(), sd.settings)) {
                if (!g.name.empty()) add(RailItem::Heading, kNone, g.name, "", kHeading);
                for (auto& r : g.rows) {
                    bool sw = r.widget == mpe::rail::Widget::Switch;
                    add(RailItem::Row, int(r.index), r.label, r.display, kItem, sw, sw && r.value != 0);
                }
            }
        }
        return items;
    }
    static float itemsHeight(const std::vector<RailItem>& items) { return items.empty() ? 0 : items.back().y + items.back().h; }

    float settingsScroll = 0;                      // how far the rail under its top row is scrolled
    void scrollRail(float by, float h) {
        float room = h - kBottom - kTop;
        settingsScroll = std::clamp(settingsScroll + by, 0.f, std::max(0.f, itemsHeight(railItems()) - room));
    }

    // The open panel: what it is for (kNone: none), and how far its list is scrolled.
    int panelKey = kNone;
    float panelScroll = 0;
    static constexpr float kPanelHead = 50, kPad = 14, kChipH = 32, kChipGap = 8, kListH = 36, kSliderH = 56;

    struct PanelLayout {
        bool open = false;
        float x = 0, y = 0, w = 0, h = 0;          // the card, in the window
        std::string title;
        enum Kind { Chips, List, Slider, Text } kind = Chips;
        struct Item { std::string text; float x, y, w, h; };   // y from the content's top, unscrolled
        std::vector<Item> items;
        int chosen = -1;
        float top = 0, room = 0, contentH = 0;     // where the content starts, how much of it shows, all of it
        double frac = 0;                           // a slider's place, 0..1
        std::string label, valueText;              // a slider's name and value
        float trackX = 0, trackW = 0;
        bool inCard(float px, float py) const { return open && px >= x && px < x + w && py >= y && py < y + h; }
        bool onDone(float px, float py) const { return inCard(px, py) && px >= x + w - 80 && py < y + kPanelHead; }
    };

    PanelLayout panelLayout() {
        PanelLayout p;
        if (panelKey == kNone || railHidden) return p;
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        Side& sd = cur();
        std::vector<std::string> names;
        if (panelKey == kSplitKey) {
            p.title = "Split"; p.kind = PanelLayout::List; p.chosen = split;
            for (const Split& s : kSplits) names.push_back(s.name);
        } else if (panelKey == kControllerKey) {
            p.title = "Controller"; p.kind = PanelLayout::List; p.chosen = sd.controller;
            for (const auto& c : lib.controllers) names.push_back(c->name());
        } else if (panelKey == kPresetKey) {
            // The presets, then what can be done with them.
            p.title = "Preset"; p.kind = PanelLayout::List;
            std::vector<Preset> all = allPresets();
            for (size_t i = 0; i < all.size(); i++) {
                names.push_back(all[i].name);
                if (all[i].name == presetName) p.chosen = int(i);
            }
            for (const std::string& a : presetActions()) names.push_back(a);
        } else if (panelKey == kRenameKey) {
            if (!userPreset(presetName)) { panelKey = kNone; return p; }
            p.title = "Rename"; p.kind = PanelLayout::Text;
        } else if (panelKey == kLookKey) {
            p.title = "Look"; p.kind = PanelLayout::Chips; p.chosen = eink ? 1 : 0;
            names = {"Dark", "E-ink"};
        } else if (panelKey == kSoundKey || panelKey == kSound2Key) {
            if (lib.instruments.empty() || (panelKey == kSound2Key && partsOf(active) < 2)) { panelKey = kNone; return p; }
            const mpe::Json& parts = sd.host ? sd.host->manifest()["parts"] : mpe::Json();
            bool two = partsOf(active) > 1;
            p.title = two ? "Sound: " + parts[size_t(panelKey == kSound2Key ? 1 : 0)].str() : "Sound";
            p.kind = PanelLayout::List;
            p.chosen = panelKey == kSound2Key ? sound2Of(active) : sd.sound;
            for (const Instrument& in : lib.instruments) names.push_back(in.pkg->name());
        } else {
            // A controller setting: as the rail model shows it. Gone (another controller, now
            // hidden) or a switch: no panel.
            if (!sd.host) { panelKey = kNone; return p; }
            bool found = false;
            for (const mpe::rail::Row& r : mpe::rail::rows(sd.host->manifest(), sd.settings)) {
                if (int(r.index) != panelKey || r.widget == mpe::rail::Widget::Switch) continue;
                found = true;
                p.title = r.label;
                if (r.widget == mpe::rail::Widget::Chips) { p.kind = PanelLayout::Chips; names = r.choices; p.chosen = r.chosen; }
                else {
                    p.kind = PanelLayout::Slider;
                    p.frac = mpe::rail::toFraction(settingOf(r.index), r.value);
                    p.label = r.label;
                    p.valueText = r.display;
                }
            }
            if (!found) { panelKey = kNone; return p; }
        }
        p.open = true;
        p.x = kRail + 8;
        p.y = 12;
        p.w = std::min(380.f, float(ww) - p.x - 8);
        float inner = p.w - 2 * kPad;
        if (p.kind == PanelLayout::Chips) {
            float cx = 0, cy = 0;
            for (const std::string& n : names) {
                float tw = std::min(inner, draw::textWidth(tokens::type::body, n) + 26);
                if (cx > 0 && cx + tw > inner) { cx = 0; cy += kChipH + kChipGap; }
                p.items.push_back({n, p.x + kPad + cx, cy, tw, kChipH});
                cx += tw + kChipGap;
            }
            p.contentH = names.empty() ? 0 : cy + kChipH;
        } else if (p.kind == PanelLayout::List) {
            for (size_t i = 0; i < names.size(); i++) p.items.push_back({names[i], p.x + kPad, float(i) * kListH, inner, kListH});
            p.contentH = float(names.size()) * kListH;
        } else if (p.kind == PanelLayout::Text) {
            p.contentH = 44;
        } else {
            p.contentH = kSliderH;
            p.trackX = p.x + kPad + 9;
            p.trackW = inner - 18;
        }
        p.h = std::min(kPanelHead + p.contentH + kPad, float(wh) - 24);
        p.top = p.y + kPanelHead;
        p.room = p.h - kPanelHead - kPad;
        panelScroll = std::clamp(panelScroll, 0.f, std::max(0.f, p.contentH - p.room));
        return p;
    }

    // Below the presets in their panel: keep this as a new one; for one of yours, rename or delete it.
    std::vector<std::string> presetActions() {
        std::vector<std::string> a = {"+ Keep this as a new preset"};
        if (userPreset(presetName)) {
            a.push_back("Rename \u201C" + presetName + "\u201D");
            a.push_back(confirmDelete ? "Tap again to delete \u201C" + presetName + "\u201D" : "Delete \u201C" + presetName + "\u201D");
        }
        return a;
    }

    // Renaming: the name being typed, from the keyboard (Enter or Done keeps it, Esc does not).
    std::string editText;
    bool typing() const { return panelKey == kRenameKey; }
    void startRename() {
        panelKey = kRenameKey;
        editText = presetName;
        SDL_StartTextInput(window);
    }
    void endRename(bool keep) {
        if (keep && panelKey == kRenameKey) renamePreset(presetName, editText);
        panelKey = kPresetKey;
        SDL_StopTextInput(window);
    }

    void presetTap(int n) {
        std::vector<Preset> all = allPresets();
        if (n < int(all.size())) { loadPreset(all[size_t(n)]); return; }
        n -= int(all.size());
        if (n == 0) newPreset();
        else if (n == 1) startRename();
        else if (n == 2) { if (confirmDelete) deletePreset(presetName); else confirmDelete = true; }
    }

    void openPanel(int key) {
        if (typing()) SDL_StopTextInput(window);
        confirmDelete = false;
        panelKey = panelKey == key ? kNone : key;
        panelScroll = 0;
        // A long list opens with the chosen one in sight.
        PanelLayout p = panelLayout();
        if (p.open && p.kind == PanelLayout::List && p.chosen >= 0)
            panelScroll = std::clamp(float(p.chosen) * kListH - p.room / 2 + kListH / 2, 0.f, std::max(0.f, p.contentH - p.room));
    }

    // A tap in the panel: Done, or a chip or a list's item.
    void panelTap(float x, float y) {
        PanelLayout p = panelLayout();
        if (!p.open) return;
        if (p.onDone(x, y)) { if (typing()) endRename(true); else panelKey = kNone; return; }
        if (y < p.top || y >= p.top + p.room) return;
        for (size_t i = 0; i < p.items.size(); i++) {
            const auto& it = p.items[i];
            float iy = p.top + it.y - panelScroll;
            if (x < it.x || x >= it.x + it.w || y < iy || y >= iy + it.h) continue;
            int n = int(i);
            if (panelKey == kPresetKey) presetTap(n);
            else if (panelKey == kSplitKey) { setSplit(n); panelKey = kNone; }
            else if (panelKey == kControllerKey) { chooseController(active, n); panelKey = kNone; }
            else if (panelKey == kSoundKey) chooseSound(active, n);
            else if (panelKey == kSound2Key) chooseSound2(active, n);
            else if (panelKey == kLookKey) setEink(n == 1);
            else setSetting(size_t(panelKey), mpe::rail::chipValue(settingOf(size_t(panelKey)), n));
            return;
        }
    }

    // A slider under x: the setting at that place.
    void panelSlide(const PanelLayout& p, float x, bool save) {
        if (p.kind != PanelLayout::Slider || panelKey < 0 || p.trackW <= 0) return;
        double t = std::clamp(double(x - p.trackX) / double(p.trackW), 0.0, 1.0);
        setSetting(size_t(panelKey), mpe::rail::fromFraction(settingOf(size_t(panelKey)), t), save);
    }

    // A tap on the rail: the top row's buttons, the update line, a row (a switch flips, the rest
    // open their panel) or the side toggle.
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
        for (const RailItem& it : railItems()) {
            if (at < it.y || at >= it.y + it.h) continue;
            if (it.kind == RailItem::Toggle) {           // which side the rail changes
                int n = count(), k = std::clamp(int((x - 8) / ((kRail - 16) / float(n))), 0, n - 1);
                if (k != active) { setActive(k); panelKey = kNone; }
            } else if (it.kind == RailItem::Row) {
                if (it.isSwitch && it.key == kGesturesKey) setBlockGestures(!blockGestures);
                else if (it.isSwitch) { setSetting(size_t(it.key), mpe::rail::stepped(settingOf(size_t(it.key)), cur().settings[size_t(it.key)], 1)); }
                else openPanel(it.key);
            }
            return;
        }
    }

    // A finger (or the mouse) on the rail or the panel: a tap acts when it lifts, a drag scrolls
    // (the rail, or the panel's list), and on a slider it slides. phase 0 down, 1 move, 2 up.
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
            PanelLayout p = panelLayout();
            bool mine = x < railW() || p.inCard(x, y);
            if (press.on) return mine;                   // one finger at a time on the rail and panel
            if (!mine) return false;
            press = Press{p.inCard(x, y) ? Press::Panel : Press::Rail, true, id, x, y, y, false, false};
            if (press.where == Press::Panel && p.kind == PanelLayout::Slider && y >= p.top && !p.onDone(x, y)) {
                press.sliding = press.dragged = true;
                panelSlide(p, x, false);
            }
            return true;
        }
        if (!press.on || press.id != id) return false;
        if (phase == 1) {
            if (press.sliding) panelSlide(panelLayout(), x, false);
            else {
                if (std::fabs(y - press.y0) > 16) press.dragged = true;
                if (press.dragged && press.where == Press::Rail) scrollRail(press.last - y, float(wh));
                if (press.dragged && press.where == Press::Panel) { panelScroll += press.last - y; panelLayout(); }
            }
            press.last = y;
            return true;
        }
        press.on = false;
        if (press.sliding) saveConfig();
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
                if (panelLayout().inCard(mx, my)) { panelScroll -= e.wheel.y * 40; panelLayout(); }
                else if (mx < railW()) scrollRail(-e.wheel.y * 40, float(wh));
                return;
            }
            case SDL_EVENT_TEXT_INPUT:
                if (typing() && editText.size() < 60) editText += e.text.text;
                return;
            case SDL_EVENT_KEY_DOWN:
                if (typing()) {
                    if (e.key.key == SDLK_BACKSPACE && !editText.empty()) {
                        size_t n = editText.size() - 1;      // a whole UTF-8 character
                        while (n > 0 && (static_cast<unsigned char>(editText[n]) & 0xC0) == 0x80) n--;
                        editText.resize(n);
                    } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) endRename(true);
                    else if (e.key.key == SDLK_ESCAPE) endRename(false);
                    return;
                }
                if (e.key.key == SDLK_TAB) setRailHidden(!railHidden);
                else if (e.key.key == SDLK_F11) setFullscreen(!fullscreen());
                else if (e.key.key == SDLK_RIGHT) chooseSound(active, cur().sound + 1);
                else if (e.key.key == SDLK_LEFT) chooseSound(active, cur().sound - 1);
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

    void drawRail(float h) {
        const tokens::Palette& p = look();
        draw::Color bg = rgba(p.ink), text = rgba(p.text), dim = rgba(p.textDim), accent = rgba(p.parts[0]);
        draw::fillRect(renderer, 0, 0, kRail, h, bg);
        if (p.eink) draw::fillRect(renderer, kRail - 2, 0, 2, h, rgba(p.line));
        draw::Color key = rgba(p.panelHigh);
        // The top row: odo, and buttons for full screen and to hide the rail.
        draw::text(renderer, 12, 30, tokens::type::title, accent, "odo", 0, true);
        draw::fillRect(renderer, kRail - 2 * kButton + 2, 5, kButton - 4, kTop - 10, key, tokens::radius::control);
        draw::fillRect(renderer, kRail - kButton + 2, 5, kButton - 4, kTop - 10, key, tokens::radius::control);
        if (p.eink) for (int b = 1; b <= 2; b++)
            draw::strokeRect(renderer, kRail - float(b) * kButton + 2, 5, kButton - 4, kTop - 10, rgba(p.line), 2, tokens::radius::control);
        draw::strokeRect(renderer, kRail - 1.5f * kButton - 9, kTop / 2 - 7, 18, 14, fullscreen() ? accent : text, 2, 2);
        chevron(renderer, kRail - 0.5f * kButton - 4, kTop / 2, 12, -1, text);
        chevron(renderer, kRail - 0.5f * kButton + 4, kTop / 2, 12, -1, text);

        // Under it, scrolled: the rows, as Android's rail has them.
        scrollRail(0, h);                                // keep the scroll in range (the window may have grown)
        std::vector<RailItem> items = railItems();
        float room = h - kBottom - kTop;
        clip(0, kTop, kRail, room);
        for (const RailItem& it : items) {
            float y = kTop + it.y - settingsScroll;
            if (y + it.h < kTop || y > kTop + room) continue;
            switch (it.kind) {
                case RailItem::Divider:
                    draw::line(renderer, 12, y + it.h / 2, kRail - 12, y + it.h / 2, rgba(p.line), 1);
                    break;
                case RailItem::Heading: {                // a group's name: small capitals, as Android's
                    std::string up = it.label;
                    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
                    draw::text(renderer, 14, y + it.h - 7, tokens::type::header, dim, draw::fit(up, tokens::type::header, kRail - 28, true), 0, true);
                    break;
                }
                case RailItem::Toggle: {                 // which side the rail changes, one cell each
                    int n = count();
                    float bx = 8, bw = kRail - 16, cw = bw / float(n);
                    draw::strokeRect(renderer, bx, y + 5, bw, it.h - 10, p.eink ? rgba(p.line) : dim, p.eink ? 2 : 1, 8);
                    for (int k = 0; k < n; k++) {
                        bool on = k == active;
                        float cx = bx + float(k) * cw;
                        if (on) draw::fillRect(renderer, cx + 2, y + 7, cw - 4, it.h - 14, p.eink ? rgba(p.text) : rgba(tokens::alpha(p.parts[0], 0x4D)), 6);
                        std::string name = draw::fit(kSplits[split].side[k], tokens::type::header, cw - 8, on);
                        draw::text(renderer, cx + cw / 2, y + it.h / 2 + 4.5f, tokens::type::header,
                                   on ? (p.eink ? bg : accent) : text, name, 1, on);
                    }
                    break;
                }
                case RailItem::Row: {
                    bool open = panelKey == it.key, lit = open || it.on;
                    if (open) draw::fillRect(renderer, 6, y + 1, kRail - 12, it.h - 2, p.eink ? rgba(p.text) : key, tokens::radius::control);
                    draw::Color lc = open && p.eink ? bg : lit ? accent : text, vc = open && p.eink ? bg : dim;
                    draw::text(renderer, 14, y + 17, tokens::type::header, lc, draw::fit(it.label, tokens::type::header, kRail - 28, true), 0, true);
                    draw::text(renderer, 14, y + 32, tokens::type::label, vc, draw::fit(it.value, tokens::type::label, kRail - 28));
                    break;
                }
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        float all = itemsHeight(items);
        if (all > room) {                                // a scroll bar: how much of the rail shows, and where
            float bar = room * room / all, at = kTop + (room - bar) * settingsScroll / (all - room);
            draw::fillRect(renderer, kRail - 4, at, 2, bar, rgba(tokens::alpha(p.textDim, 0x80)), 1);
        }

        if (updater) {
            Updater::State us = updater->state();
            std::string line = "odo " + updater->current();
            std::string msg = us == Updater::State::Checking ? "checking for updates" : updater->message();
            bool act = us == Updater::State::Available || us == Updater::State::Installed;
            float ms = act ? tokens::type::header : tokens::type::caption;
            draw::text(renderer, 12, h - 62, tokens::type::caption, dim, draw::fit(line, tokens::type::caption, kRail - 24));
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

    // The open panel, beside the rail over the play area: its name and Done, then the setting's
    // chips, a slider, or a list to choose from (Android's SettingPanel).
    void drawPanel() {
        PanelLayout pl = panelLayout();
        if (!pl.open) return;
        const tokens::Palette& p = look();
        draw::Color text = rgba(p.text), dim = rgba(p.textDim), accent = rgba(p.parts[0]), line = rgba(p.line);
        draw::fillRect(renderer, pl.x, pl.y, pl.w, pl.h, rgba(p.panel), tokens::radius::card);
        if (p.eink) draw::strokeRect(renderer, pl.x, pl.y, pl.w, pl.h, line, 2, tokens::radius::card);
        else draw::strokeRect(renderer, pl.x, pl.y, pl.w, pl.h, rgba(tokens::alpha(p.line, 0xC0)), 1, tokens::radius::card);
        draw::text(renderer, pl.x + kPad, pl.y + 31, 17, text, draw::fit(pl.title, 17, pl.w - 2 * kPad - 70, true), 0, true);
        draw::text(renderer, pl.x + pl.w - kPad - 6, pl.y + 31, tokens::type::body + 1, accent, "Done", 2, true);
        if (pl.kind == PanelLayout::Text) {              // the name being typed, with a cursor
            float fy = pl.top + 4, fw = pl.w - 2 * kPad;
            draw::strokeRect(renderer, pl.x + kPad, fy, fw, 36, accent, p.eink ? 2 : 1.5f, 8);
            std::string shown = draw::fit(editText, 15, fw - 24);
            draw::text(renderer, pl.x + kPad + 10, fy + 23, 15, text, shown);
            float cx = pl.x + kPad + 10 + draw::textWidth(15, shown) + 1;
            if ((SDL_GetTicks() / 500) % 2 == 0) draw::fillRect(renderer, cx, fy + 9, 2, 18, accent);
            return;
        }
        if (pl.kind == PanelLayout::Slider) {            // its name and value, then the slider
            float y = pl.top;
            draw::text(renderer, pl.x + kPad, y + 16, 14, text, draw::fit(pl.label, 14, pl.w - 2 * kPad - 70));
            draw::text(renderer, pl.x + pl.w - kPad, y + 16, 14, dim, pl.valueText, 2);
            float ty = y + 38, fx = pl.trackX + float(pl.frac) * pl.trackW;
            draw::fillRect(renderer, pl.trackX, ty - 2, pl.trackW, 4, p.eink ? rgba(0xBBBBBBFF) : line, 2);
            draw::fillRect(renderer, pl.trackX, ty - 2, fx - pl.trackX, 4, accent, 2);
            draw::circle(renderer, fx, ty, 9, accent, accent, 0);
            return;
        }
        clip(pl.x, pl.top, pl.w, pl.room);
        for (size_t i = 0; i < pl.items.size(); i++) {
            const auto& it = pl.items[i];
            float y = pl.top + it.y - panelScroll;
            if (y + it.h < pl.top || y > pl.top + pl.room) continue;
            bool on = int(i) == pl.chosen;
            if (pl.kind == PanelLayout::Chips) {         // Android's FilterChip: the chosen one tinted and edged in the accent
                if (on) draw::fillRect(renderer, it.x, y, it.w, it.h, p.eink ? text : rgba(tokens::alpha(p.parts[0], 0x38)), 8);
                draw::strokeRect(renderer, it.x, y, it.w, it.h, p.eink ? line : on ? accent : line, p.eink ? 2 : 1, 8);
                draw::text(renderer, it.x + it.w / 2, y + it.h / 2 + 4.5f, tokens::type::body,
                           on ? (p.eink ? rgba(p.ink) : accent) : text, draw::fit(it.text, tokens::type::body, it.w - 16), 1);
            } else {                                     // a list: the chosen one in the accent, bold
                if (i > 0) draw::line(renderer, it.x, y, it.x + it.w, y, rgba(tokens::alpha(p.line, 0x80)), 1);
                draw::text(renderer, it.x, y + it.h / 2 + 5.5f, 15, on ? accent : text, draw::fit(it.text, 15, it.w, on), 0, on);
            }
        }
        SDL_SetRenderClipRect(renderer, nullptr);
        if (pl.contentH > pl.room) {                     // a scroll bar for a long list
            float bar = pl.room * pl.room / pl.contentH, at = pl.top + (pl.room - bar) * panelScroll / (pl.contentH - pl.room);
            draw::fillRect(renderer, pl.x + pl.w - 6, at, 2, bar, rgba(tokens::alpha(p.textDim, 0x80)), 1);
        }
    }

    void frame() {
        collectCompiled();
        if (!typing() && SDL_TextInputActive(window)) SDL_StopTextInput(window);   // the rename panel was closed
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
    std::printf("odo: %zu instruments, %zu controllers\n", app.lib.instruments.size(), app.lib.controllers.size());

    app.engine = std::make_unique<mpe::Engine>(kRate);
    app.engine->setVolume(0.8f);
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
        for (size_t i = 0; i < app.lib.instruments.size(); i++) {
            const auto& p = *app.lib.instruments[i].pkg;
            if (p.id() == sid || p.manifest["aliases"][size_t(0)].str() == sid) app.sides[k].sound = int(i);
            if (!app.savedSound2[k].empty() && p.id() == app.savedSound2[k]) app.sides[k].sound2 = int(i);
        }
        for (size_t i = 0; i < app.lib.controllers.size(); i++) if (app.lib.controllers[i]->id() == cid) app.sides[k].controller = int(i);
        if (k < int(controllerIds.size()) && (app.lib.controllers.empty() || app.lib.controllers[size_t(app.sides[k].controller)]->id() != cid))
            std::fprintf(stderr, "odo: no controller %s in the library\n", cid.c_str());
    }
    if (einkArg) app.eink = true;
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
