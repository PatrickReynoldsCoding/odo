// JNI glue: one Synth for the whole process, called from Kotlin (Native.kt).
#include <jni.h>
#include <algorithm>
#include <memory>
#include <string>
#include "Synth.h"
#include "mpe/package.h"
#include "mpe/preset.h"

static Synth& synth() { static Synth s; return s; }

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_patrickreynolds_mpeinstruments_Native_##name

FN(jboolean, nativeStart)(JNIEnv*, jobject) { return synth().start(); }
FN(void, nativeStop)(JNIEnv*, jobject) { synth().stop(); synth().stopMic(); }

FN(jboolean, nativeAttachMidi)(JNIEnv* env, jobject, jobject device, jint port) {
    return synth().attachMidi(env, device, port);
}
FN(void, nativeDetachMidi)(JNIEnv*, jobject) { synth().detachMidi(); }
FN(void, nativeTouch)(JNIEnv*, jobject, jint status, jint d1, jint d2) {
    synth().touchMidi(uint8_t(status), uint8_t(d1), uint8_t(d2));
}

FN(jint, nativeInstrumentCount)(JNIEnv*, jobject) { return mpe::instrumentCount(); }
FN(jstring, nativeInstrumentField)(JNIEnv* env, jobject, jint index, jint field) {
    if (index < 0 || index >= mpe::instrumentCount()) return env->NewStringUTF("");
    const mpe::InstrumentInfo& i = mpe::instrumentAt(index);
    // field 0, the id: the app still knows instruments by their ids from before packages
    // ("winds/flute"), which a package keeps as its first alias.
    const char* v = field == 0 ? (i.aliases.empty() ? i.id : i.aliases[0]).c_str() : field == 1 ? i.group.c_str()
                  : field == 2 ? i.name.c_str() : field == 3 ? i.description.c_str()
                  : field == 4 ? i.meta("range")     // field 4: "low high" in Hz, or ""
                  : field == 5 ? i.meta("sound")     // field 5: its own effects, `declare sound "reverb=0.3 ..."`, or ""
                  : field == 7 ? i.version.c_str()   // 7: its version ("1.1.0"), or ""
                  : field == 8 ? (mpe::isNewestVersion(index) ? "1" : "0")   // 8: the newest of its versions (older ones are kept to go back to)
                  : mpe::sustains({i.group}, i.aliases.empty() ? i.id : i.aliases[0]) ? "1" : "0";   // 6: it sustains
    return env->NewStringUTF(v ? v : "");
}

FN(jint, compareVersions)(JNIEnv* env, jobject, jstring a, jstring b) {
    const char* x = env->GetStringUTFChars(a, nullptr);
    const char* y = env->GetStringUTFChars(b, nullptr);
    int r = mpe::compareVersions(x, y);
    env->ReleaseStringUTFChars(a, x);
    env->ReleaseStringUTFChars(b, y);
    return r;
}

FN(void, nativeSetInstrument)(JNIEnv*, jobject, jint i) { synth().setInstrument(i); }
FN(void, nativeSetPartInstrument)(JNIEnv*, jobject, jint part, jint i) { synth().setInstrument(part, i); }

// The whole Performance as one float array. Layout must match Perf.toArray() in Perf.kt.
FN(void, nativeSetPerformance)(JNIEnv* env, jobject, jfloatArray arr) {
    constexpr int kHead = 11, kPart = 15, kLen = kHead + mpe::kParts * kPart;
    if (env->GetArrayLength(arr) < kLen) return;
    float a[kLen];
    env->GetFloatArrayRegion(arr, 0, kLen, a);
    mpe::Performance p;
    p.layout = mpe::Layout(int(a[0]));
    p.splitNote = int(a[1]);
    p.bpm = a[2];
    p.scaleMask = uint16_t(int(a[3]));
    p.scaleRoot = int(a[4]);
    p.assist = a[5];
    p.reverbSize = a[6];
    p.reverbDamp = a[7];
    p.delayBeats = a[8];
    p.delayFeedback = a[9];
    p.delayPingPong = a[10] > 0.5f;
    for (int k = 0; k < mpe::kParts; k++) {
        const float* b = a + kHead + k * kPart;
        mpe::PartConfig& c = p.part[k];
        c.arp = b[0] > 0.5f;
        c.order = mpe::ArpOrder(int(b[1]));
        c.rate = b[2];
        c.gate = b[3];
        c.octaves = int(b[4]);
        c.latch = b[5] > 0.5f;
        c.transpose = int(b[6]);
        c.volume = b[7];
        c.brightness = b[8];
        c.drive = b[9];
        c.pressureCurve = b[10];
        c.reverb = b[11];
        c.delay = b[12];
        c.release = b[13];
        c.width = b[14];
    }
    synth().setPerformance(p);
}

// Sounding voices into `out`, 8 floats each: part, finger, note, gate, pitch, pressure, slide, level.
FN(jint, nativeVoices)(JNIEnv* env, jobject, jfloatArray out) {
    constexpr int kMax = mpe::kParts * 16;
    mpe::VoiceView v[kMax];
    const int n = std::min(synth().voices(v, kMax), int(env->GetArrayLength(out) / 8));
    float a[kMax * 8];
    for (int i = 0; i < n; i++) {
        float* o = a + 8 * i;
        o[0] = v[i].part; o[1] = v[i].finger; o[2] = v[i].note; o[3] = v[i].gate;
        o[4] = v[i].pitch; o[5] = v[i].pressure; o[6] = v[i].slide; o[7] = v[i].level;
    }
    if (n) env->SetFloatArrayRegion(out, 0, 8 * n, a);
    return n;
}
// Held fingers into `out`, 5 floats each: channel, note, pitch, pressure, slide.
FN(jint, nativeFingers)(JNIEnv* env, jobject, jfloatArray out) {
    mpe::FingerView f[16];
    const int n = std::min(synth().fingers(f, 16), int(env->GetArrayLength(out) / 5));
    float a[16 * 5];
    for (int i = 0; i < n; i++) {
        float* o = a + 5 * i;
        o[0] = f[i].channel; o[1] = f[i].note; o[2] = f[i].pitch; o[3] = f[i].pressure; o[4] = f[i].slide;
    }
    if (n) env->SetFloatArrayRegion(out, 0, 5 * n, a);
    return n;
}
FN(void, nativeSetVolume)(JNIEnv*, jobject, jfloat v) { synth().setVolume(v); }
FN(void, nativeSetTranspose)(JNIEnv*, jobject, jint s) { synth().setTranspose(s); }
FN(void, nativeSetBendRange)(JNIEnv*, jobject, jfloat s) { synth().setBendRange(s); }
FN(void, nativeSetWatchdogMs)(JNIEnv*, jobject, jint ms) { synth().setWatchdogMs(ms); }
FN(void, nativePanic)(JNIEnv*, jobject) { synth().panic(); }
FN(void, nativeResetLatencyTuner)(JNIEnv*, jobject) { synth().resetLatencyTuner(); }

// The phone as a controller (see Synth.h).
FN(void, nativeSetTilt)(JNIEnv*, jobject, jfloat degrees) { synth().setTilt(degrees); }
FN(void, nativeSetMotion)(JNIEnv*, jobject, jfloat twist, jfloat tip, jfloat turn, jfloat shake) {
    synth().setMotion(twist, tip, turn, shake);
}
FN(void, nativeSetGyro)(JNIEnv* env, jobject, jintArray targets, jfloatArray amounts, jfloat bendSnap, jboolean bendReset) {
    int t[4] = {}; float a[4] = {};
    if (env->GetArrayLength(targets) >= 4 && env->GetArrayLength(amounts) >= 4) {
        env->GetIntArrayRegion(targets, 0, 4, reinterpret_cast<jint*>(t));
        env->GetFloatArrayRegion(amounts, 0, 4, a);
    }
    synth().setGyro(t, a, bendSnap, bendReset);
}
FN(jint, nativeSetBreath)(JNIEnv*, jobject, jint mode, jfloat sensitivity) { return synth().setBreath(mode, sensitivity); }
FN(void, nativeDebugBreath)(JNIEnv*, jobject, jfloat level) { synth().setBreathOverride(level); }
FN(void, nativeLoop)(JNIEnv*, jobject, jint track, jint op, jint layer, jfloat vol, jboolean flag) {
    synth().loop(track, op, layer, vol, flag);
}
FN(void, nativeLoopView)(JNIEnv* env, jobject, jint track, jfloatArray out) {
    float v[Synth::kLoopCount];
    synth().fillLoop(track, v);
    env->SetFloatArrayRegion(out, 0, std::min<int>(Synth::kLoopCount, env->GetArrayLength(out)), v);
}
FN(jint, nativeModifiers)(JNIEnv* env, jobject, jfloatArray out) {
    float v[Synth::kModCount];
    synth().fillModifiers(v);
    const int n = std::min<int>(Synth::kModCount, env->GetArrayLength(out));
    env->SetFloatArrayRegion(out, 0, n, v);
    return n;
}

FN(jdoubleArray, nativeStats)(JNIEnv* env, jobject) {
    double v[Synth::kStatCount];
    synth().fillStats(v);
    jdoubleArray arr = env->NewDoubleArray(Synth::kStatCount);
    env->SetDoubleArrayRegion(arr, 0, Synth::kStatCount, v);
    return arr;
}

FN(jboolean, nativeCaptureStart)(JNIEnv*, jobject, jfloat seconds) { return synth().captureStart(seconds); }
FN(jdouble, nativeCaptureProgress)(JNIEnv*, jobject) { return synth().captureProgress(); }
FN(jboolean, nativeCaptureSave)(JNIEnv* env, jobject, jstring path) {
    const char* p = env->GetStringUTFChars(path, nullptr);
    bool ok = synth().captureSave(p);
    env->ReleaseStringUTFChars(path, p);
    return ok;
}

// ---- controller packages (.odoc) -------------------------------------------------------------
// One at a time: the one on screen. Its notes go in as the touch pad's do. UI thread only.
#include "mpe/controller.h"

static std::unique_ptr<mpe::ControllerHost>& ctl() { static std::unique_ptr<mpe::ControllerHost> h; return h; }
static std::string& ctlError() { static std::string e; return e; }

// A take recording from it, or playing into it (the takes, at the end of this file).
#include <chrono>
#include "mpe/take.h"
static mpe::take::Recorder& recorder() { static mpe::take::Recorder r; return r; }
static mpe::take::Player& player() { static mpe::take::Player p; return p; }
// The take playing as it was loaded (its own touches), to lay its ghosts out again from.
static mpe::take::Take& loadedTake() { static mpe::take::Take t; return t; }
// Its ghost fingers on the controller as it is now set and sized: its own touches if that is how it
// was laid out, else laid out again (mpe::take::relayout). What it plays does not change.
static void fitGhosts(const std::vector<double>& v, float w, float h, const float centers[2]) {
    if (!ctl() || !player().loaded()) return;
    const mpe::take::Take& was = loadedTake();
    if (mpe::take::laidOutFor(was, ctl()->package().manifest, v, w, h)) { player().retouch(was.touches); return; }
    mpe::take::Take t = was;
    if (mpe::take::relayout(t, ctl()->package(), v, w, h, centers)) player().retouch(std::move(t.touches));
}

FN(jboolean, nativeCtlLoad)(JNIEnv* env, jobject, jbyteArray bytes) {
    jsize n = env->GetArrayLength(bytes);
    std::string data(size_t(n), '\0');
    env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(&data[0]));
    player().stop(nullptr);
    if (ctl()) ctl()->stop();
    ctl().reset();
    auto pkg = mpe::Package::fromBytes(data.data(), data.size(), &ctlError());
    if (!pkg) return false;
    auto host = std::make_unique<mpe::ControllerHost>([](const uint8_t* m, int len) {
        synth().touchMidi(m[0], len > 1 ? m[1] : 0, len > 2 ? m[2] : 0);
    });
    if (!host->load(*pkg, &ctlError())) return false;
    ctl() = std::move(host);
    return true;
}
FN(jstring, nativeCtlError)(JNIEnv* env, jobject) {
    return env->NewStringUTF((ctl() && !ctl()->lastError().empty() ? ctl()->lastError() : ctlError()).c_str());
}
FN(void, nativeCtlUnload)(JNIEnv*, jobject) { player().stop(nullptr); if (ctl()) ctl()->stop(); ctl().reset(); }
FN(void, nativeCtlInit)(JNIEnv*, jobject, jfloat w, jfloat h, jint flags) { if (ctl()) ctl()->init(w, h, flags); }
FN(void, nativeCtlInstrument)(JNIEnv*, jobject, jint part, jfloat center) { if (ctl()) ctl()->instrument(part, center); }
FN(void, nativeCtlTouch)(JNIEnv*, jobject, jint id, jint phase, jfloat x, jfloat y, jfloat size, jfloat force) {
    if (ctl()) ctl()->touch(id, mpe::ControllerHost::Phase(phase), x, y, size, force);
}
FN(void, nativeCtlSetting)(JNIEnv*, jobject, jint index, jdouble value) { if (ctl()) ctl()->setting(index, value); }
FN(void, nativeCtlStop)(JNIEnv*, jobject) { if (ctl()) ctl()->stop(); }
// Settings it changed itself since the last call, as index, value pairs; null if none.
FN(jdoubleArray, nativeCtlTakeSettings)(JNIEnv* env, jobject) {
    if (!ctl()) return nullptr;
    auto changes = ctl()->takeSettingChanges();
    if (changes.empty()) return nullptr;
    std::vector<double> flat;
    for (const auto& c : changes) { flat.push_back(c.index); flat.push_back(c.value); }
    jdoubleArray out = env->NewDoubleArray(jsize(flat.size()));
    env->SetDoubleArrayRegion(out, 0, jsize(flat.size()), flat.data());
    return out;
}
// A frame: time moves on; the draw list if it changed, else null.
FN(jbyteArray, nativeCtlTick)(JNIEnv* env, jobject) {
    if (!ctl()) return nullptr;
    ctl()->tick();
    if (!ctl()->takeDirty()) return nullptr;
    const auto& d = ctl()->drawList();
    jbyteArray out = env->NewByteArray(jsize(d.size()));
    env->SetByteArrayRegion(out, 0, jsize(d.size()), reinterpret_cast<const jbyte*>(d.data()));
    return out;
}

// ---- the rail's settings model (mpe/rail.h) --------------------------------------------------
// Stateless of the engine: it works from a controller's manifest, whether or not that controller is
// the one loaded. A manifest is parsed once: nativeRailOpen gives a handle for its text (the same
// text, the same handle, so there are only ever as many as there are controllers' manifests).
#include <deque>
#include <mutex>
#include "mpe/rail.h"
#include "mpe/scala.h"

namespace {
struct RailManifest { std::string text; mpe::Json json; };
std::mutex& railLock() { static std::mutex m; return m; }
std::deque<RailManifest>& railManifests() { static std::deque<RailManifest> d; return d; }   // a deque: entries never move

// The manifest for a handle (an empty one for a bad handle), and its setting `index` (Null if none).
const mpe::Json& railManifest(jint h) {
    static const mpe::Json none;
    std::lock_guard<std::mutex> l(railLock());
    auto& d = railManifests();
    return h >= 0 && size_t(h) < d.size() ? d[size_t(h)].json : none;
}
const mpe::Json& railSetting(jint h, jint index) {
    static const mpe::Json none;
    return index < 0 ? none : railManifest(h)["settings"][size_t(index)];
}
}  // namespace

// manifest: the controller.json text as UTF-8.
FN(jint, nativeRailOpen)(JNIEnv* env, jobject, jbyteArray manifest) {
    jsize n = env->GetArrayLength(manifest);
    std::string text(size_t(n), '\0');
    if (n) env->GetByteArrayRegion(manifest, 0, n, reinterpret_cast<jbyte*>(&text[0]));
    std::lock_guard<std::mutex> l(railLock());
    auto& d = railManifests();
    for (size_t i = 0; i < d.size(); i++) if (d[i].text == text) return jint(i);
    mpe::Json json = mpe::Json::parse(text);
    d.push_back({std::move(text), std::move(json)});
    return jint(d.size() - 1);
}
// rail::toJson(rail::groups(manifest, values)), as UTF-8.
FN(jbyteArray, nativeRail)(JNIEnv* env, jobject, jint h, jdoubleArray values) {
    std::vector<double> v(size_t(values ? env->GetArrayLength(values) : 0));
    if (!v.empty()) env->GetDoubleArrayRegion(values, 0, jsize(v.size()), v.data());
    std::string out = mpe::rail::toJson(mpe::rail::groups(railManifest(h), v));
    jbyteArray a = env->NewByteArray(jsize(out.size()));
    env->SetByteArrayRegion(a, 0, jsize(out.size()), reinterpret_cast<const jbyte*>(out.data()));
    return a;
}
FN(jdouble, nativeRailStepped)(JNIEnv*, jobject, jint h, jint index, jdouble value, jint dir) {
    return mpe::rail::stepped(railSetting(h, index), value, dir);
}
FN(jdouble, nativeRailFromFraction)(JNIEnv*, jobject, jint h, jint index, jdouble t) {
    return mpe::rail::fromFraction(railSetting(h, index), t);
}
FN(jdouble, nativeRailToFraction)(JNIEnv*, jobject, jint h, jint index, jdouble value) {
    return mpe::rail::toFraction(railSetting(h, index), value);
}
FN(jdouble, nativeRailChipValue)(JNIEnv*, jobject, jint h, jint index, jint k) {
    return mpe::rail::chipValue(railSetting(h, index), k);
}
// A Scala file (its bytes) for the "scala" setting `index` (mpe/scala.h), as UTF-8 JSON:
// {"error":"..."}, or {"description":"...","set":[index, value, index, value, ...]}: the settings to give it.
FN(jbyteArray, nativeRailScala)(JNIEnv* env, jobject, jint h, jint index, jbyteArray file) {
    jsize n = file ? env->GetArrayLength(file) : 0;
    std::string text(size_t(n), '\0');
    if (n) env->GetByteArrayRegion(file, 0, n, reinterpret_cast<jbyte*>(&text[0]));
    mpe::scala::Scale s;
    std::string err, out;
    auto quote = [&](const std::string& v) {
        out += '"';
        for (unsigned char c : v) {
            if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += char(c);
        }
        out += '"';
    };
    std::vector<std::pair<size_t, double>> set;
    if (mpe::scala::parse(text, &s, &err) && index >= 0)
        set = mpe::scala::settings(railManifest(h), size_t(index), s, &err);
    if (set.empty()) {
        out = "{\"error\":";
        quote(err.empty() ? std::string("This controller can't take a Scala file.") : err);
        out += "}";
    } else {
        out = "{\"description\":";
        quote(s.description);
        out += ",\"set\":[";
        for (size_t i = 0; i < set.size(); i++) {
            char b[64];
            std::snprintf(b, sizeof b, "%s%zu,%.17g", i ? "," : "", set[i].first, set[i].second);
            out += b;
        }
        out += "]}";
    }
    jbyteArray a = env->NewByteArray(jsize(out.size()));
    env->SetByteArrayRegion(a, 0, jsize(out.size()), reinterpret_cast<const jbyte*>(out.data()));
    return a;
}

// ---- preset files (docs/formats/PRESET_FORMAT.md, mpe::preset) ---------------------------------------------
// Kotlin knows a controller package as "pkg:<id>" and a sound by its id from before packages (its
// first alias, "strings/cello"); a file has the package ids. As UTF-8 bytes both ways: a name may
// have characters NewStringUTF's modified UTF-8 cannot take.

static std::string bytesIn(JNIEnv* env, jbyteArray a) {
    std::string s(size_t(env->GetArrayLength(a)), '\0');
    env->GetByteArrayRegion(a, 0, jsize(s.size()), reinterpret_cast<jbyte*>(s.data()));
    return s;
}
static jbyteArray bytesOut(JNIEnv* env, const std::string& s) {
    jbyteArray a = env->NewByteArray(jsize(s.size()));
    env->SetByteArrayRegion(a, 0, jsize(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return a;
}
static std::string soundForFile(const std::string& id) {
    int i = mpe::findInstrument(id.c_str());
    return i >= 0 ? mpe::instrumentAt(i).id : id;
}
static std::string soundForApp(const std::string& id) {
    int i = mpe::findInstrument(id.c_str());
    if (i < 0) return id;
    const mpe::InstrumentInfo& in = mpe::instrumentAt(i);
    return in.aliases.empty() ? in.id : in.aliases[0];
}

// The app's preset, in the file's shape (Kotlin's ids), to the file's text; "" if it is not one.
FN(jbyteArray, nativePresetWrite)(JNIEnv* env, jobject, jbyteArray json) {
    mpe::preset::Preset p;
    if (!mpe::preset::parse(bytesIn(env, json), p)) return bytesOut(env, "");
    for (auto& s : p.sides) {
        if (s.controller.compare(0, 4, "pkg:") == 0) s.controller.erase(0, 4);
        for (auto& q : s.parts) q.sound = soundForFile(q.sound);
    }
    return bytesOut(env, mpe::preset::write(p));
}

// A file's text to the same shape with Kotlin's ids; or "!" and why it can't be read.
FN(jbyteArray, nativePresetRead)(JNIEnv* env, jobject, jbyteArray text) {
    mpe::preset::Preset p;
    std::string err;
    if (!mpe::preset::parse(bytesIn(env, text), p, &err)) return bytesOut(env, "!" + err);
    for (auto& s : p.sides) {
        if (!s.controller.empty() && s.controller != "midi") s.controller = "pkg:" + s.controller;
        for (auto& q : s.parts) q.sound = soundForApp(q.sound);
    }
    return bytesOut(env, mpe::preset::write(p));
}

// The file name for a preset of this name.
FN(jbyteArray, nativePresetFileName)(JNIEnv* env, jobject, jbyteArray name) {
    mpe::preset::Preset p;
    p.name = bytesIn(env, name);
    return bytesOut(env, mpe::preset::fileName(p));
}

// ---- takes (docs/formats/TAKE_FORMAT.md, mpe::take) ------------------------------------------------
// Recorded from the controller package on screen, and played into it (its notes as its own notes
// go, the ghost fingers drawn over it), as odo for computers does. UI thread only, as the controller.

static double takeNowMs() {
    return double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
}
static mpe::take::Player::Sink takeSink() {
    return [](const mpe::take::Note& n) {
        if (ctl()) ctl()->injectNote(n.kind, mpe::ControllerHost::kInjectedVoice + n.voice, n.part, n.pitch, n.pressure, n.slide, n.velocity);
    };
}
static std::string takeQuote(const std::string& s) { mpe::Json j; j.type = mpe::Json::String; j.s = s; return mpe::preset::toJson(j); }

// Start recording what the controller on screen plays. sounds: its parts' sounds (the app's ids), one a
// line; setup: what plays now as a preset file's text (kept as the take's preset; "": none); volume:
// the master volume (1 the usual level; < 0: not kept).
FN(jboolean, nativeTakeRecord)(JNIEnv* env, jobject, jbyteArray sounds, jbyteArray setup, jdouble volume) {
    if (!ctl()) return false;
    if (player().playing()) player().stop(takeSink());
    std::vector<std::string> ids;
    std::string s = bytesIn(env, sounds);
    for (size_t a = 0; a < s.size();) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        std::string id = s.substr(a, b - a);
        if (!id.empty()) ids.push_back(soundForFile(id));   // the package's id, as a preset file has it
        a = b + 1;
    }
    mpe::preset::Preset p;
    bool hasSetup = mpe::preset::parse(bytesIn(env, setup), p);
    recorder().start(hasSetup ? &p : nullptr, volume);
    recorder().attach(*ctl(), 0, ids);
    return true;
}
// ms since the first finger while recording; 0 before it; -1 when not recording.
FN(jdouble, nativeTakeRecording)(JNIEnv*, jobject) { return recorder().recording() ? recorder().elapsedMs() : -1; }
// Stop recording: the take's file text, named so; "" if nothing was played.
FN(jbyteArray, nativeTakeStopRecording)(JNIEnv* env, jobject, jbyteArray name, jbyteArray madeBy) {
    mpe::take::Take t = recorder().stop();
    if (t.touches.empty()) return bytesOut(env, "");
    t.name = bytesIn(env, name);
    t.madeBy = bytesIn(env, madeBy);
    return bytesOut(env, mpe::take::write(t));
}
FN(jbyteArray, nativeTakeFileName)(JNIEnv* env, jobject, jbyteArray name) {
    mpe::take::Take t;
    t.name = bytesIn(env, name);
    return bytesOut(env, mpe::take::fileName(t));
}
// What a take file is, as JSON: {"name", "controller" ("pkg:" and its id, or ""), "written",
// "length" (ms), "sections": [...], "settings": {id: value}, "sounds": [the app's ids], "volume"
// (the master volume, 1 the usual level; -1: not given)}; or {"error": why}.
FN(jbyteArray, nativeTakeInfo)(JNIEnv* env, jobject, jbyteArray text) {
    mpe::take::Take t;
    std::string err;
    if (!mpe::take::parse(bytesIn(env, text), t, &err)) return bytesOut(env, "{\"error\":" + takeQuote(err) + "}");
    double length = t.length();
    std::vector<std::string> sections;
    bool written = !t.performed();
    if (written) {
        mpe::take::Score sc;
        if (mpe::take::parseScore(t.score, sc, nullptr, nullptr, 1, t.tempo, t.meterBeats, t.meterUnit, t.key)) {
            length = sc.ms(sc.end);
            for (const auto& s : sc.sections) sections.push_back(s.second);
        }
    } else for (const auto& s : t.sections) sections.push_back(s.name);
    std::string ctlId = t.sides.empty() || t.sides[0].controller.empty() ? "" : "pkg:" + t.sides[0].controller;
    std::string o = "{\"name\":" + takeQuote(t.name) + ",\"controller\":" + takeQuote(ctlId) +
                    ",\"written\":" + (written ? "true" : "false") + ",\"length\":" + std::to_string(length) + ",\"sections\":[";
    for (size_t i = 0; i < sections.size(); i++) o += (i ? "," : "") + takeQuote(sections[i]);
    o += "],\"settings\":{";
    if (!t.sides.empty())
        for (size_t i = 0; i < t.sides[0].settings.size(); i++) {
            mpe::Json v; v.type = mpe::Json::Number; v.n = t.sides[0].settings[i].second;
            o += (i ? "," : "") + takeQuote(t.sides[0].settings[i].first) + ":" + mpe::preset::toJson(v);
        }
    o += "},\"sounds\":[";
    if (!t.sides.empty())
        for (size_t i = 0; i < t.sides[0].sounds.size(); i++) o += (i ? "," : "") + takeQuote(soundForApp(t.sides[0].sounds[i]));
    mpe::Json vol; vol.type = mpe::Json::Number; vol.n = t.volume;
    return bytesOut(env, o + "],\"volume\":" + mpe::preset::toJson(vol) + "}");
}
// A take's whole set-up (mpe::take::presetOf) as a preset file's text, as openPresetFile takes it;
// or "!" and why it can't be read.
FN(jbyteArray, nativeTakePreset)(JNIEnv* env, jobject, jbyteArray text) {
    mpe::take::Take t;
    std::string err;
    if (!mpe::take::parse(bytesIn(env, text), t, &err)) return bytesOut(env, "!" + err);
    return bytesOut(env, mpe::preset::write(mpe::take::presetOf(t)));
}
// Play a take on the controller on screen (its settings already put on it): mode 0 listen, 1 play
// along. A take written by hand is expanded first on the controller as set (values), at this size
// (dp), for these instruments' middles. "" if it plays with every note; else what went wrong ("!"
// first if it does not play at all).
FN(jbyteArray, nativeTakePlay)(JNIEnv* env, jobject, jbyteArray text, jint mode, jdoubleArray values, jfloat w, jfloat h,
                               jfloat center0, jfloat center1, jdouble speed, jint loop) {
    if (!ctl()) return bytesOut(env, "!No controller to play it on.");
    if (recorder().recording()) recorder().stop();
    player().stop(takeSink());
    mpe::take::Take t;
    std::string err;
    if (!mpe::take::parse(bytesIn(env, text), t, &err)) return bytesOut(env, "!" + err);
    std::string note;
    if (!t.performed()) {
        std::vector<double> v(size_t(values ? env->GetArrayLength(values) : 0));
        if (!v.empty()) env->GetDoubleArrayRegion(values, 0, jsize(v.size()), v.data());
        float centers[2] = {center0, center1};
        mpe::take::Report rep;
        if (!mpe::take::prepare(t, ctl()->package(), v, w, h, centers, &rep))
            return bytesOut(env, "!" + (rep.errors.empty() ? std::string("It could not be played.") : rep.errors[0]));
        if (!rep.errors.empty())
            note = std::to_string(rep.errors.size()) + (rep.errors.size() == 1 ? " note is" : " notes are") +
                   " not on the controller as it is set: " + rep.errors[0];
    }
    loadedTake() = t;
    player().load(std::move(t));
    {
        std::vector<double> v(size_t(values ? env->GetArrayLength(values) : 0));
        if (!v.empty()) env->GetDoubleArrayRegion(values, 0, jsize(v.size()), v.data());
        float centers[2] = {center0, center1};
        fitGhosts(v, w, h, centers);
    }
    player().setSpeed(speed);
    player().loopSection(loop, takeNowMs(), takeSink());
    player().play(mode == 1 ? mpe::take::Player::PlayAlong : mpe::take::Player::Listen, takeNowMs(), takeSink());
    return bytesOut(env, note);
}
FN(void, nativeTakeStop)(JNIEnv*, jobject) { player().stop(takeSink()); }
// The controller's size or settings changed while a take plays: its ghosts follow the keys.
FN(void, nativeTakeRelayout)(JNIEnv* env, jobject, jdoubleArray values, jfloat w, jfloat h, jfloat center0, jfloat center1) {
    std::vector<double> v(size_t(values ? env->GetArrayLength(values) : 0));
    if (!v.empty()) env->GetDoubleArrayRegion(values, 0, jsize(v.size()), v.data());
    float centers[2] = {center0, center1};
    fitGhosts(v, w, h, centers);
}
FN(void, nativeTakeSpeed)(JNIEnv*, jobject, jdouble speed) { player().setSpeed(speed); }
FN(void, nativeTakeLoop)(JNIEnv*, jobject, jint section) { if (player().loaded()) player().loopSection(section, takeNowMs(), takeSink()); }
// playing (0/1), mode (0 listen, 1 play along), position ms, length ms.
FN(jdoubleArray, nativeTakeState)(JNIEnv* env, jobject) {
    double v[4] = {player().playing() ? 1.0 : 0.0, double(player().mode()), player().position(), player().length()};
    jdoubleArray a = env->NewDoubleArray(4);
    env->SetDoubleArrayRegion(a, 0, 4, v);
    return a;
}
// A frame: time moves on for a take playing; its ghost fingers over a w x h dp surface as a draw
// list (CONTROLLER_FORMAT.md's), or null when none is playing. colours: finger, trail, next, text,
// back (0xRRGGBBAA); sizes: radius, grow, trailMs, previewMs, textSize (ui/tokens.json's ghost).
FN(jbyteArray, nativeTakeTick)(JNIEnv* env, jobject, jfloat w, jfloat h, jintArray colours, jfloatArray sizes) {
    if (!player().playing()) return nullptr;
    player().update(takeNowMs(), takeSink());
    if (!player().playing()) return nullptr;
    mpe::take::GhostStyle st;
    if (env->GetArrayLength(colours) >= 5 && env->GetArrayLength(sizes) >= 5) {
        jint c[5];
        jfloat s[5];
        env->GetIntArrayRegion(colours, 0, 5, c);
        env->GetFloatArrayRegion(sizes, 0, 5, s);
        st.finger = uint32_t(c[0]); st.trail = uint32_t(c[1]); st.next = uint32_t(c[2]); st.text = uint32_t(c[3]); st.back = uint32_t(c[4]);
        st.radius = s[0]; st.grow = s[1]; st.trailMs = s[2]; st.previewMs = s[3]; st.textSize = s[4];
    }
    if (env->GetArrayLength(sizes) >= 6) {                 // the Fingers choice (GhostStyle::Look)
        jfloat look;
        env->GetFloatArrayRegion(sizes, 5, 1, &look);
        st.look = std::clamp(int(look), 0, 3);
    }
    std::vector<uint8_t> d = player().drawGhosts(0, w, h, st);
    jbyteArray out = env->NewByteArray(jsize(d.size()));
    env->SetByteArrayRegion(out, 0, jsize(d.size()), reinterpret_cast<const jbyte*>(d.data()));
    return out;
}
