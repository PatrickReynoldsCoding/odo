// JNI glue: one Synth for the whole process, called from Kotlin (Native.kt).
#include <jni.h>
#include <algorithm>
#include <memory>
#include <string>
#include "Synth.h"

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
                  : i.meta("sound");                 // field 5: its own effects, `declare sound "reverb=0.3 ..."`, or ""
    return env->NewStringUTF(v ? v : "");
}

FN(void, nativeSetInstrument)(JNIEnv*, jobject, jint i) { synth().setInstrument(i); }
FN(void, nativeSetPartInstrument)(JNIEnv*, jobject, jint part, jint i) { synth().setInstrument(part, i); }

// The whole Performance as one float array. Layout must match Perf.toArray() in Perf.kt.
FN(void, nativeSetPerformance)(JNIEnv* env, jobject, jfloatArray arr) {
    constexpr int kHead = 11, kPart = 14, kLen = kHead + mpe::kParts * kPart;
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

FN(jboolean, nativeCtlLoad)(JNIEnv* env, jobject, jbyteArray bytes) {
    jsize n = env->GetArrayLength(bytes);
    std::string data(size_t(n), '\0');
    env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(&data[0]));
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
FN(void, nativeCtlUnload)(JNIEnv*, jobject) { if (ctl()) ctl()->stop(); ctl().reset(); }
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
