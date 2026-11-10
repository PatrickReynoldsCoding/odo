#include "mpe/controller.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include "wasm3.h"

namespace mpe {

struct ControllerHost::Wasm {
    IM3Environment env = nullptr;
    IM3Runtime runtime = nullptr;
    IM3Module module = nullptr;      // owned by the runtime once loaded
    std::string bytes;               // wasm3 reads the module from these while it runs
    IM3Function fInit = nullptr, fInstrument = nullptr, fTouch = nullptr, fTick = nullptr, fSetting = nullptr, fStop = nullptr;
    ~Wasm() {
        if (runtime) m3_FreeRuntime(runtime);
        if (env) m3_FreeEnvironment(env);
    }
};

namespace {
ControllerHost* self(IM3ImportContext ctx) { return static_cast<ControllerHost*>(ctx->userdata); }

m3ApiRawFunction(hostNoteOn) {
    m3ApiGetArg(int32_t, voice) m3ApiGetArg(int32_t, part) m3ApiGetArg(float, pitch) m3ApiGetArg(float, pressure)
    m3ApiGetArg(float, slide) m3ApiGetArg(float, velocity) m3ApiGetArg(double, t)
    self(_ctx)->noteOn(voice, part, pitch, pressure, slide, velocity, t);
    m3ApiSuccess();
}
m3ApiRawFunction(hostNoteChange) {
    m3ApiGetArg(int32_t, voice) m3ApiGetArg(float, pitch) m3ApiGetArg(float, pressure) m3ApiGetArg(float, slide)
    m3ApiGetArg(double, t)
    self(_ctx)->noteChange(voice, pitch, pressure, slide, t);
    m3ApiSuccess();
}
m3ApiRawFunction(hostNoteOff) {
    m3ApiGetArg(int32_t, voice) m3ApiGetArg(double, t)
    self(_ctx)->noteOff(voice, t);
    m3ApiSuccess();
}
m3ApiRawFunction(hostNoteVolume) {
    m3ApiGetArg(int32_t, voice) m3ApiGetArg(float, volume) m3ApiGetArg(double, t)
    self(_ctx)->noteVolume(voice, volume, t);
    m3ApiSuccess();
}
m3ApiRawFunction(hostSetSetting) {
    m3ApiGetArg(int32_t, index) m3ApiGetArg(double, value)
    self(_ctx)->settingFrom(index, value);
    m3ApiSuccess();
}
m3ApiRawFunction(hostDraw) {
    m3ApiGetArg(uint32_t, ptr) m3ApiGetArg(uint32_t, len)
    self(_ctx)->drawFrom(ptr, len);
    m3ApiSuccess();
}
}  // namespace

ControllerHost::ControllerHost(MidiSink sink) : sink_(std::move(sink)) {
    startNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

ControllerHost::~ControllerHost() { stop(); }

double ControllerHost::nowMs() const {
    if (clock_) return clock_();
    int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return double(ns - startNs_) / 1e6;
}

bool ControllerHost::loaded() const { return w_ && w_->fTouch; }

bool ControllerHost::load(const Package& pkg, std::string* error) {
    auto fail = [&](const std::string& why) { error_ = why; if (error) *error = why; w_.reset(); return false; };
    stop();
    w_.reset();
    if (!pkg.isController()) return fail(pkg.id() + ": not a controller package");
    manifest_ = pkg.manifest;
    pkg_ = pkg;
    values_.assign(pkg.manifest["settings"].size(), 0.0);
    auto w = std::make_unique<Wasm>();
    w->bytes = pkg.file("controller.wasm");
    if (w->bytes.empty()) return fail(pkg.id() + ": no controller.wasm");
    w->env = m3_NewEnvironment();
    w->runtime = m3_NewRuntime(w->env, 64 * 1024, this);
    if (!w->env || !w->runtime) return fail("wasm3: no runtime");
    M3Result r = m3_ParseModule(w->env, &w->module, reinterpret_cast<const uint8_t*>(w->bytes.data()), uint32_t(w->bytes.size()));
    if (r) return fail(pkg.id() + ": " + r);
    r = m3_LoadModule(w->runtime, w->module);
    if (r) { m3_FreeModule(w->module); return fail(pkg.id() + ": " + r); }
    struct Link { const char* name; const char* sig; M3RawCall fn; };
    const Link links[] = {
        {"note_on", "v(iiffffF)", &hostNoteOn},
        {"note_change", "v(ifffF)", &hostNoteChange},
        {"note_off", "v(iF)", &hostNoteOff},
        {"note_volume", "v(ifF)", &hostNoteVolume},
        {"draw", "v(ii)", &hostDraw},
        {"set_setting", "v(iF)", &hostSetSetting},
    };
    for (const Link& l : links) {
        r = m3_LinkRawFunctionEx(w->module, "mesh", l.name, l.sig, l.fn, this);
        if (r && r != m3Err_functionLookupFailed) return fail(pkg.id() + ": " + l.name + ": " + r);
    }
    m3_FindFunction(&w->fInit, w->runtime, "init");
    m3_FindFunction(&w->fInstrument, w->runtime, "instrument");
    m3_FindFunction(&w->fTouch, w->runtime, "touch");
    m3_FindFunction(&w->fTick, w->runtime, "tick");
    m3_FindFunction(&w->fSetting, w->runtime, "setting");
    m3_FindFunction(&w->fStop, w->runtime, "stop");
    if (!w->fTouch) return fail(pkg.id() + ": exports no touch()");
    w_ = std::move(w);
    // Its settings start at their defaults.
    const Json& s = manifest_["settings"];
    for (size_t i = 0; i < s.size(); i++) setting(int(i), s[i]["default"].num());
    return true;
}

static void report(std::string& error, M3Result r, const char* what) {
    if (r) error = std::string(what) + ": " + r;
}

void ControllerHost::init(float width, float height, int flags) {
    width_ = width;
    height_ = height;
    if (!w_ || !w_->fInit) return;
    report(error_, m3_CallV(w_->fInit, width, height, int32_t(flags), nowMs()), "init");
}

void ControllerHost::instrument(int part, float centerNote) {
    center_[part & 3] = centerNote;
    if (!w_ || !w_->fInstrument) return;
    report(error_, m3_CallV(w_->fInstrument, int32_t(part), centerNote), "instrument");
}

void ControllerHost::touch(int id, Phase phase, float x, float y, float size, float force) {
    if (!w_) return;
    double t = nowMs();
    if (taps_.touch) taps_.touch(id, int(phase), x, y, size, force, t);
    report(error_, m3_CallV(w_->fTouch, int32_t(id), int32_t(phase), x, y, size, force, t), "touch");
    flush(t);
}

void ControllerHost::tick() {
    if (!w_) return;
    double t = nowMs();
    if (w_->fTick) report(error_, m3_CallV(w_->fTick, t), "tick");
    flush(t);
}

void ControllerHost::setting(int index, double value) {
    if (index >= 0 && size_t(index) < values_.size()) {
        if (values_[size_t(index)] != value && taps_.setting) taps_.setting(index, value, nowMs());
        values_[size_t(index)] = value;
    }
    if (!w_ || !w_->fSetting) return;
    report(error_, m3_CallV(w_->fSetting, int32_t(index), value), "setting");
}

void ControllerHost::stop() {
    if (w_ && w_->fStop) report(error_, m3_CallV(w_->fStop), "stop");
    later_.clear();
    startVolume_.clear();
    for (int c = chFirst_; c < chEnd_; c++) {
        if (ch_[c].note >= 0) send(uint8_t(0x80 | c), uint8_t(ch_[c].note), 64);
        ch_[c] = Chan();
    }
}

// ---- notes to MPE ----------------------------------------------------------------------------

void ControllerHost::send(uint8_t a, uint8_t b, uint8_t c, int n) {
    uint8_t m[3] = {a, b, c};
    if (sink_) sink_(m, n);
}

void ControllerHost::noteOn(int voice, int part, float pitch, float pressure, float slide, float velocity, double t) {
    if (taps_.note) taps_.note(0, voice, part, pitch, pressure, slide, velocity, t);
    Note n{voice, part, pitch, pressure, slide, velocity, 0, t};
    if (t > nowMs() + 1) later_.push_back(n); else play(n);
}

void ControllerHost::noteChange(int voice, float pitch, float pressure, float slide, double t) {
    if (taps_.note) taps_.note(1, voice, 0, pitch, pressure, slide, 0, t);
    Note n{voice, 0, pitch, pressure, slide, 0, 1, t};
    if (t > nowMs() + 1 || std::any_of(later_.begin(), later_.end(), [&](const Note& o) { return o.voice == voice; }))
        later_.push_back(n);
    else play(n);
}

void ControllerHost::noteOff(int voice, double t) {
    if (taps_.note) taps_.note(2, voice, 0, 0, 0, 0, 0, t);
    Note n{voice, 0, 0, 0, 0, 0, 2, t};
    if (t > nowMs() + 1 || std::any_of(later_.begin(), later_.end(), [&](const Note& o) { return o.voice == voice; }))
        later_.push_back(n);
    else play(n);
}

void ControllerHost::noteVolume(int voice, float volume, double t) {
    if (taps_.note) taps_.note(3, voice, 0, 0, volume, 0, 0, t);
    Note n{voice, 0, 0, volume, 0, 0, 3, t};
    if (t > nowMs() + 1 || std::any_of(later_.begin(), later_.end(), [&](const Note& o) { return o.voice == voice; }))
        later_.push_back(n);
    else play(n);
}

void ControllerHost::flush(double now) {
    if (later_.empty()) return;
    std::stable_sort(later_.begin(), later_.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    size_t k = 0;
    while (k < later_.size() && later_[k].t <= now + 1) play(later_[k++]);
    later_.erase(later_.begin(), later_.begin() + long(k));
}

void ControllerHost::play(const Note& n) {
    constexpr float kBendRange = 48;            // the MPE default, which the engine expects
    auto shape = [&](int c, float pitch, float pressure, float slide) {
        Chan& h = ch_[c];
        int bend = int(std::lround(8192 + (pitch - float(h.note)) / kBendRange * 8192));
        bend = std::clamp(bend, 0, 16383);
        int pr = std::clamp(int(std::lround(pressure * 127)), 0, 127);
        int sl = std::clamp(int(std::lround(slide * 127)), 0, 127);
        if (pr != h.pressure) { h.pressure = pr; send(uint8_t(0xD0 | c), uint8_t(pr), 0, 2); }
        if (sl != h.slide) { h.slide = sl; send(uint8_t(0xB0 | c), 74, uint8_t(sl)); }
        if (bend != h.bend) { h.bend = bend; send(uint8_t(0xE0 | c), uint8_t(bend & 0x7F), uint8_t(bend >> 7)); }
    };
    int c = 0;
    for (int k = chFirst_; k < chEnd_; k++) if (ch_[k].voice == n.voice) c = k;
    if (n.kind == 0) {
        if (c) { send(uint8_t(0x80 | c), uint8_t(ch_[c].note), 64); ch_[c] = Chan(); }   // the same voice again
        for (int k = chFirst_; k < chEnd_ && !c; k++) if (ch_[k].voice < 0) c = k;
        if (!c) return;                                                                  // 15 notes already sound
        Chan& h = ch_[c];
        h = Chan();
        h.voice = n.voice;
        h.note = std::clamp(int(std::lround(n.pitch)), 0, 127);
        // Its volume: full, unless note_volume said otherwise before it started.
        auto pv = std::find_if(startVolume_.begin(), startVolume_.end(), [&](const auto& v) { return v.first == n.voice; });
        h.volume = pv == startVolume_.end() ? 127 : std::clamp(int(std::lround(pv->second * 127)), 0, 127);
        if (pv != startVolume_.end()) startVolume_.erase(pv);
        send(uint8_t(0xB0 | c), 7, uint8_t(h.volume));
        send(uint8_t(0xB0 | c), 9, uint8_t(std::clamp(n.part + partBase_, 0, 3) + 1));   // the part (sound) it plays
        int vel = std::clamp(int(std::lround(n.velocity * 127 / velocityScale_)), 1, 127);
        send(uint8_t(0x90 | c), uint8_t(h.note), uint8_t(vel));
        // After the note-on: the engine starts a finger's bend and pressure at zero.
        shape(c, n.pitch, n.pressure, n.slide);
    } else if (n.kind == 1) {
        if (c) shape(c, n.pitch, n.pressure, n.slide);
    } else if (n.kind == 3) {                    // volume: carried in `pressure`
        int v = std::clamp(int(std::lround(n.pressure * 127)), 0, 127);
        if (c && v != ch_[c].volume) { ch_[c].volume = v; send(uint8_t(0xB0 | c), 7, uint8_t(v)); }
        else if (!c) {                           // not started yet: its starting volume
            startVolume_.erase(std::remove_if(startVolume_.begin(), startVolume_.end(), [&](const auto& x) { return x.first == n.voice; }),
                               startVolume_.end());
            startVolume_.push_back({n.voice, n.pressure});
            if (startVolume_.size() > 64) startVolume_.erase(startVolume_.begin());
        }
    } else if (c) {
        send(uint8_t(0x80 | c), uint8_t(ch_[c].note), 64);
        ch_[c] = Chan();
    }
}

void ControllerHost::settingFrom(int index, double value) {
    if (index < 0 || size_t(index) >= manifest_["settings"].size()) { error_ = "set_setting: no setting " + std::to_string(index); return; }
    if (changes_.size() < 256) changes_.push_back({index, value});
    if (values_[size_t(index)] != value && taps_.setting) taps_.setting(index, value, nowMs());
    values_[size_t(index)] = value;
}

void ControllerHost::injectNote(int kind, int voice, int part, float pitch, float pressure, float slide, float velocity) {
    play(Note{voice, part, pitch, pressure, slide, velocity, kind, nowMs()});
}

void ControllerHost::drawFrom(uint32_t ptr, uint32_t len) {
    if (!w_) return;
    uint32_t size = 0;
    uint8_t* mem = m3_GetMemory(w_->runtime, &size, 0);
    if (!mem || uint64_t(ptr) + len > size) { error_ = "draw: outside its memory"; return; }
    draw_.assign(mem + ptr, mem + ptr + len);
    dirty_ = true;
}

}  // namespace mpe
