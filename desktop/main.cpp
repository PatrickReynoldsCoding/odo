// odo for computers: a basic MESH. It plays instrument packages (.odoi, compiled with libfaust)
// with controller packages (.odoc, run in wasm3) on a touch screen, from the same files as odo
// on Android. Linux and Windows (SDL3).
//
//   odo [--library DIR]... [--split N] [--controller ID]... [--sound ID]... [--log-midi] [--no-update]
//       [--version] [--self-update] [--setting [SIDE:]INDEX=VALUE]...
//       [--selftest out.bmp [--touch X,Y]... [--move DX,DY] [--expect N] [--size WxH]]
//
// Library folders hold .odoi and .odoc files (default: the library folder beside odo, and
// ~/.local/share/odo/library). It checks GitHub for a newer odo when it opens (--no-update: not);
// the update row at the bottom of the rail installs it. The rail on
// the left picks the split, then for each side its controller, sound and settings; the rest of
// the window is the controllers: whole, or split side by side, above and below, in three or in
// four, each side playing its own sound. (--controller and --sound repeat: the first is the
// first side's, the next the second's.) Keys: Tab hides the rail, F11 full screen, Esc quits.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
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
#include "tokens.h"
#include "update.h"
#include "mpe/controller.h"
#include "mpe/engine.h"
#include "mpe/faust_loader.h"
#include "mpe/package.h"

namespace fs = std::filesystem;
using draw::rgba;

namespace {

constexpr int kRate = 48000;           // instruments are tuned at 48 kHz: SDL converts if the device differs
constexpr float kRail = 230;           // the rail's width, points
constexpr float kRow = 46;

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
        std::unique_ptr<mpe::ControllerHost> host;
        std::vector<double> settings;   // the controller's, by position
        std::vector<size_t> shown;      // the ones on the rail (not "hidden"), in order
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
    std::string savedController[kSides], savedSound[kSides];
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
        }
        savedSplit = std::clamp(int(j["split"].num(0)), 0, kSplitCount - 1);
        savedActive = std::clamp(int(j["active"].num(0)), 0, kSides - 1);
        railHidden = j["railHidden"].boolean();
        savedFullscreen = j["fullscreen"].boolean();
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
    // Where side k keeps its controller's settings.
    std::string settingsKey(int k) const { return k == 0 ? controllerId(0) : controllerId(k) + "#" + std::to_string(k + 1); }

    void saveConfig() {
        if (!persist) return;
        std::error_code ec;
        fs::create_directories(fs::path(configPath).parent_path(), ec);
        std::ofstream f(configPath + ".tmp");
        f.precision(17);     // every digit: a strike zone map is a seven-digit number
        f << "{\n  \"controller\": " << quoted(controllerId(0)) << ",\n  \"sound\": " << quoted(soundId(0))
          << ",\n  \"split\": " << split << ",\n  \"active\": " << active << ",\n  \"sides\": [";
        for (int k = 0; k < kSides; k++)
            f << (k ? ", " : "") << "{\"controller\": " << quoted(controllerId(k)) << ", \"sound\": " << quoted(soundId(k)) << "}";
        f << "],\n  \"railHidden\": " << (railHidden ? "true" : "false")
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
        f << "\n  }\n}\n";
        f.close();
        fs::rename(configPath + ".tmp", configPath, ec);
    }

    void setRailHidden(bool on) { railHidden = on; saveConfig(); }
    void setFullscreen(bool on) { SDL_SetWindowFullscreen(window, on); saveConfig(); }
    bool fullscreen() const { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; }
    bool railHidden = false, eink = false, logMidi = false;

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

    // Each side's controller sends on its own MIDI channels, to its own part: a side's notes never
    // take another's channel or sound.
    void route(int k) {
        if (!sides[k].host) return;
        int per = 15 / count();
        sides[k].host->setPart(k);
        sides[k].host->setChannels(1 + k * per, per);
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

    // The engine's parts: one per side, each with its sound's effects; and each sound in its part.
    void applySounds() {
        mpe::Performance p;
        p.layout = count() > 1 ? mpe::Layout::Split : mpe::Layout::Single;
        for (int k = 0; k < count() && !lib.instruments.empty(); k++) {
            const mpe::Json& s = lib.instruments[size_t(sides[k].sound)].pkg->manifest["sound"];
            mpe::PartConfig& c = p.part[k];
            c.reverb = float(s["reverb"].num(0.15));
            c.delay = float(s["delay"].num(0));
            c.brightness = float(s["brightness"].num(1));
            c.drive = float(s["drive"].num(0));
            c.pressureCurve = float(s["pressureCurve"].num(1));
            c.release = float(s["release"].num(0));
            if (k == 0) {                        // the room is shared: the first side's
                p.reverbSize = float(s["reverbSize"].num(0.5));
                p.reverbDamp = float(s["reverbDamp"].num(0.4));
            }
        }
        engine->setPerformance(p);
        for (int k = 0; k < count() && !lib.instruments.empty(); k++) {
            Side& sd = sides[k];
            const Instrument& in = lib.instruments[size_t(sd.sound)];
            if (in.index < 0) continue;
            if (sd.engineIndex != in.index) { engine->setInstrument(k, in.index); sd.engineIndex = in.index; }
            if (sd.host) sd.host->instrument(0, centerNote(*in.pkg));
        }
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
        for (int k = 0; k < count(); k++) {
            int n = sides[k].sound;
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
            if (lib.instruments.empty() || lib.instruments[size_t(sides[k].sound)].index < 0) return false;
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
        sd.shown.clear();
        for (size_t i = 0; i < s.size(); i++) if (!s[i]["hidden"].boolean()) sd.shown.push_back(i);
        for (size_t i = 0; i < s.size(); i++) {
            auto it = mine.find(s[i]["id"].str());
            sd.settings.push_back(it != mine.end() ? it->second : s[i]["default"].num());
            if (it != mine.end()) sd.host->setting(int(i), it->second);
        }
        sd.initW = sd.initH = 0;                 // init() at the next frame, with its size
        saveConfig();
        if (!lib.instruments.empty() && lib.instruments[size_t(sd.sound)].index >= 0)
            sd.host->instrument(0, centerNote(*lib.instruments[size_t(sd.sound)].pkg));
    }

    void keepSetting(int k, size_t i, double v) {
        Side& sd = sides[k];
        sd.settings[i] = v;
        saved[settingsKey(k)][sd.host->manifest()["settings"][i]["id"].str()] = v;
    }

    void changeSetting(size_t i, int dir) {
        Side& sd = cur();
        const mpe::Json& s = sd.host->manifest()["settings"][i];
        std::string type = s["type"].str();
        double v = sd.settings[i];
        if (type == "switch") v = v != 0 ? 0 : 1;
        else if (type == "choice") {
            double n = double(s["options"].size());
            v = std::fmod(v + dir + n, n);
        } else {
            // No step (or 0): a continuous number, crossed in 20 taps, as Android's slider would.
            double lo = s["min"].num(0), hi = s["max"].num(1), step = s["step"].num(0);
            if (step <= 0) step = (hi - lo) / 20;
            v = std::clamp(v + dir * step, lo, hi);
        }
        sd.host->setting(int(i), v);
        keepSetting(active, i, v);
        saveConfig();
    }

    std::string settingValue(size_t i) {
        const mpe::Json& s = cur().host->manifest()["settings"][i];
        std::string type = s["type"].str();
        double v = cur().settings[i];
        if (type == "switch") return v != 0 ? "on" : "off";
        if (type == "choice") return s["options"][size_t(v)].str();
        char b[32];
        std::snprintf(b, sizeof b, "%g", v);
        return b;
    }

    // ---- input ----------------------------------------------------------------------------------

    // The rail's rows: the top buttons, the split, the side it changes, its controller and sound;
    // then that controller's settings.
    enum { kRowTop, kRowSplit, kRowSide, kRowController, kRowSound };

    // A tap on the rail: which row, and whether on its left (back) or right (on) half. The update
    // row is at the bottom.
    void railTap(float x, float y) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        if (updater && y > float(wh) - 76 && y < float(wh) - 40) {
            if (updater->state() == Updater::State::Available) updater->install();
            else if (updater->state() == Updater::State::Installed) { shutdown(); SDL_Quit(); updater->restart(argv); }
            else if (updater->state() == Updater::State::Failed || updater->state() == Updater::State::UpToDate) updater->check();
            return;
        }
        int row = int(y / kRow);
        if (row == kRowTop) {                            // the top row's buttons: full screen, hide
            if (x >= kRail - kButton) setRailHidden(true);
            else if (x >= kRail - 2 * kButton) setFullscreen(!fullscreen());
            return;
        }
        // "<" goes back, ">" on, the rest of the row on (a switch flips).
        int dir = x >= kRail - 2 * kButton && x < kRail - kButton ? -1 : 1;
        if (row == kRowSplit) setSplit(split + dir);
        else if (row == kRowSide) setActive(active + dir);
        else if (row == kRowController) chooseController(active, cur().controller + dir);
        else if (row == kRowSound) chooseSound(active, cur().sound + dir);
        else if (cur().host && y >= kSettingsTop && y < float(wh) - kBottom) {
            size_t r = size_t((y - kSettingsTop + settingsScroll) / settingRow(float(wh)));
            if (r < cur().shown.size()) changeSetting(cur().shown[r], dir);
        }
    }

    // A finger (or the mouse) on the rail: a tap acts when it lifts; a drag scrolls the settings.
    // phase 0 down, 1 move, 2 up. True if the rail took it.
    static constexpr int64_t kMouse = -1;
    static constexpr float kButton = 40;           // the arrows' and the top buttons' width
    struct Press { bool on = false; int64_t id = 0; float x = 0, y0 = 0, last = 0; bool dragged = false; } press;
    float settingsScroll = 0;

    void scrollSettings(float by, float h) {
        float rows = float(cur().shown.size()) * settingRow(h), room = h - kBottom - kSettingsTop;
        settingsScroll = std::clamp(settingsScroll + by, 0.f, std::max(0.f, rows - room));
    }

    bool railPress(int phase, int64_t id, float x, float y) {
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        if (phase == 0) {
            // Hidden, the rail's corner button brings it back.
            if (railHidden && x < 44 && y < 44) { setRailHidden(false); press = Press{true, id, -1, -1, -1, true}; return true; }
            if (x >= railW() || press.on) return x < railW();
            press = Press{true, id, x, y, y, false};
            return true;
        }
        if (!press.on || press.id != id) return false;
        if (phase == 1) {
            if (std::fabs(y - press.y0) > 16) press.dragged = true;
            if (press.dragged) scrollSettings(press.last - y, float(wh));
            press.last = y;
            return true;
        }
        press.on = false;
        if (!press.dragged) railTap(press.x, press.y0);
        return true;
    }

    // The controller's settings fill the rail between the sound and the lines at the bottom:
    // each row as tall as fits, down to a compact one.
    static constexpr float kSettingsTop = 5.7f * kRow, kBottom = 84;
    float settingRow(float h) {
        if (cur().shown.empty()) return kRow;
        return std::clamp((h - kBottom - kSettingsTop) / float(cur().shown.size()), 30.f, kRow);
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
                if (mx < railW()) scrollSettings(-e.wheel.y * 40, float(wh));
                return;
            }
            case SDL_EVENT_KEY_DOWN:
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

    void drawRail(float h) {
        const tokens::Palette& p = look();
        draw::Color bg = rgba(p.ink), text = rgba(p.text), dim = rgba(p.textDim), accent = rgba(p.parts[0]);
        draw::fillRect(renderer, 0, 0, kRail, h, bg);
        draw::Color key = rgba(p.panelHigh);
        // The arrows: two buttons at the right of a row.
        auto arrows = [&](float y, float h) {
            draw::fillRect(renderer, kRail - 2 * kButton + 2, y + 3, kButton - 4, h - 6, key, tokens::radius::control);
            draw::fillRect(renderer, kRail - kButton + 2, y + 3, kButton - 4, h - 6, key, tokens::radius::control);
            float size = h >= 40 ? 14 : 11;
            draw::text(renderer, kRail - 1.5f * kButton, y + h / 2 + size / 2, size, text, "<", 1);
            draw::text(renderer, kRail - 0.5f * kButton, y + h / 2 + size / 2, size, text, ">", 1);
        };
        // A value cut to fit before the arrows.
        auto fit = [&](const std::string& v, float size) {
            size_t room = size_t((kRail - 2 * kButton - 16) / size);
            return draw::textWidth(size, v) <= kRail - 2 * kButton - 16 ? v : v.substr(0, room > 2 ? room - 2 : room) + "..";
        };
        auto row = [&](int k, const std::string& label, const std::string& value, draw::Color c, bool withArrows) {
            float y = float(k) * kRow;
            draw::text(renderer, 12, y + 19, tokens::type::label, dim, label);
            draw::text(renderer, 12, y + 37, tokens::type::body, c, fit(value, tokens::type::body));
            if (withArrows) arrows(y, kRow);
        };
        draw::text(renderer, 12, 30, tokens::type::title, accent, "odo", 0, true);
        // The top row's buttons: full screen, and hide the rail.
        draw::fillRect(renderer, kRail - 2 * kButton + 2, 5, kButton - 4, kRow - 10, key, tokens::radius::control);
        draw::fillRect(renderer, kRail - kButton + 2, 5, kButton - 4, kRow - 10, key, tokens::radius::control);
        draw::strokeRect(renderer, kRail - 1.5f * kButton - 9, kRow / 2 - 7, 18, 14, fullscreen() ? accent : text, 2, 2);
        draw::text(renderer, kRail - 0.5f * kButton, kRow / 2 + 6, 12, text, "<<", 1);
        row(kRowSplit, "Split", kSplits[split].name, text, true);
        row(kRowSide, count() > 1 ? "Changing" : "Side", kSplits[split].side[active], count() > 1 ? accent : dim, count() > 1);
        Side& sd = cur();
        row(kRowController, "Controller", sd.host ? lib.controllers[size_t(sd.controller)]->name() : "none", text, true);
        std::string snd = lib.instruments.empty() ? "none" : lib.instruments[size_t(sd.sound)].pkg->name();
        bool loading = !lib.instruments.empty() && lib.instruments[size_t(sd.sound)].index < 0 && lib.instruments[size_t(sd.sound)].error.empty();
        row(kRowSound, "Sound", loading ? snd + "..." : snd, text, true);
        draw::line(renderer, 12, 5.5f * kRow, kRail - 12, 5.5f * kRow, rgba(p.line), 1);
        if (sd.host) {
            const mpe::Json& s = sd.host->manifest()["settings"];
            float rh = settingRow(h);
            scrollSettings(0, h);                // keep the scroll in range (the window may have grown)
            bool big = rh >= 40;
            for (size_t r = 0; r < sd.shown.size(); r++) {
                size_t i = sd.shown[r];
                float y = kSettingsTop + float(r) * rh - settingsScroll;
                if (y < kSettingsTop - 1 || y + rh > h - kBottom + 1) continue;   // scrolled out of the rail
                float ls = big ? tokens::type::label : 8, vs = big ? tokens::type::body : tokens::type::caption;
                draw::text(renderer, 12, y + (big ? 19 : 12), ls, dim, fit(s[i]["name"].str(), ls));
                draw::text(renderer, 12, y + (big ? 37 : 26), vs, text, fit(settingValue(i), vs));
                arrows(y, rh);
            }
            float rows = float(sd.shown.size()) * rh, room = h - kBottom - kSettingsTop;
            if (rows > room) {                   // a scroll bar: how much of the settings shows, and where
                float bar = room * room / rows, at = kSettingsTop + (room - bar) * settingsScroll / (rows - room);
                draw::fillRect(renderer, kRail - 4, at, 2, bar, rgba(tokens::alpha(p.textDim, 0x80)), 1);
            }
        }
        if (updater) {
            Updater::State us = updater->state();
            std::string line = "odo " + updater->current();
            std::string msg = us == Updater::State::Checking ? "checking for updates" : updater->message();
            bool act = us == Updater::State::Available || us == Updater::State::Installed;
            draw::text(renderer, 12, h - 62, tokens::type::caption, dim, line);
            draw::text(renderer, 12, h - 46, act ? tokens::type::header : tokens::type::caption, act ? accent : dim, msg, 0, act);
        }
        mpe::Stats st = engine->stats();
        char b[96];
        std::snprintf(b, sizeof b, "Latency %.1f ms, max %.1f", st.eventAgeAvgMs, st.eventAgeMaxMs);
        draw::text(renderer, 12, h - 30, tokens::type::caption, dim, b);
        std::snprintf(b, sizeof b, "Load %.0f%%  Voices %d", 100 * st.loadAvg, engine->activeVoices());
        draw::text(renderer, 12, h - 14, tokens::type::caption, dim, b);
    }

    void frame() {
        collectCompiled();
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
                if (!lib.instruments.empty() && lib.instruments[size_t(sd.sound)].index >= 0)
                    sd.host->instrument(0, centerNote(*lib.instruments[size_t(sd.sound)].pkg));
            }
            sd.host->tick();
            // Settings it changed itself (its editor, a button on its surface): kept as if set here.
            bool changed = false;
            for (const auto& c : sd.host->takeSettingChanges()) {
                if (size_t(c.index) >= sd.settings.size()) continue;
                keepSetting(k, size_t(c.index), c.value);
                changed = true;
            }
            if (changed) saveConfig();
        }
        SDL_SetRenderViewport(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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
            if (sd.x > railW() + 1) draw::fillRect(renderer, sd.x - kGap, sd.y, kGap, sd.h, rgba(0x000000FF));
            if (sd.y > 1) draw::fillRect(renderer, sd.x, sd.y - kGap, sd.w, kGap, rgba(0x000000FF));
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
            draw::text(renderer, 24, 30, tokens::type::header, rgba(look().text), ">>", 1);
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
    std::vector<std::string> controllerIds, soundIds;    // each --controller / --sound: the next side's
    Selftest test;
    struct Preset { int side, index; double value; };
    std::vector<Preset> presetSettings;                  // --setting [SIDE:]INDEX=VALUE
    int width = 1280, height = 720, splitArg = -1;
    bool logMidi = false, checkUpdates = true;
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
        else if (a == "--expect" && more) test.expect = std::atoi(argv[++i]);
        else if (a == "--setting" && more && std::strchr(argv[i + 1], '=')) {
            const char* s = argv[++i];
            const char *colon = std::strchr(s, ':'), *eq = std::strchr(s, '=');
            bool sided = colon && colon < eq;
            presetSettings.push_back({sided ? std::clamp(std::atoi(s) - 1, 0, kSides - 1) : 0, std::atoi(sided ? colon + 1 : s), std::atof(eq + 1)});
        }
        else if (a == "--size" && more && std::sscanf(argv[i + 1], "%dx%d", &width, &height) == 2) i++;
        else {
            std::fprintf(stderr, "usage: odo [--library DIR]... [--split N] [--controller ID]... [--sound ID]... [--log-midi] [--no-update] [--version]\n"
                                 "           [--setting [SIDE:]INDEX=VALUE]... [--selftest out.bmp [--touch X,Y]... [--move DX,DY] [--expect N] [--size WxH]]\n");
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
    app.persist = !selftestOut;
    if (app.persist) {
        app.loadConfig();
        if (app.savedFullscreen) SDL_SetWindowFullscreen(app.window, true);
    }
    // Saving waits until everything is chosen, so a half-made start doesn't overwrite the file.
    bool persist = app.persist;
    app.persist = false;
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
        }
        for (size_t i = 0; i < app.lib.controllers.size(); i++) if (app.lib.controllers[i]->id() == cid) app.sides[k].controller = int(i);
        if (k < int(controllerIds.size()) && (app.lib.controllers.empty() || app.lib.controllers[size_t(app.sides[k].controller)]->id() != cid))
            std::fprintf(stderr, "odo: no controller %s in the library\n", cid.c_str());
    }
    app.active = selftestOut ? 0 : app.savedActive;
    app.setSplit(splitArg >= 0 ? splitArg : app.savedSplit);
    for (const auto& ps : presetSettings) {
        App::Side& sd = app.sides[ps.side];
        if (sd.host && ps.index >= 0 && size_t(ps.index) < sd.settings.size()) {
            sd.settings[size_t(ps.index)] = ps.value;
            sd.host->setting(ps.index, ps.value);
        }
    }
    app.persist = persist;
    app.saveConfig();

    if (selftestOut) { int rc = selftest(app, selftestOut, test); app.shutdown(); SDL_Quit(); return rc; }
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) running = false;
            else app.handle(e);
        }
        app.frame();
    }
    app.shutdown();
    SDL_Quit();
    return 0;
}
