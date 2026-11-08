// Runs a controller package (.odoc): its WebAssembly in a sandbox (wasm3), as
// docs/formats/CONTROLLER_FORMAT.md describes. Touches and time in; MPE MIDI bytes and a draw list out.
// One thread (the UI's) calls everything.
#pragma once
#include <cstdint>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "mpe/package.h"

namespace mpe {

class ControllerHost {
public:
    // Where its notes go, as MPE MIDI messages (1 to 3 bytes each): the same as a Seaboard's.
    using MidiSink = std::function<void(const uint8_t* bytes, int n)>;

    explicit ControllerHost(MidiSink sink);
    ~ControllerHost();

    // Load its WebAssembly. False and *error if it can't be run.
    bool load(const Package& pkg, std::string* error = nullptr);
    bool loaded() const;
    const Json& manifest() const { return manifest_; }

    enum Phase { Down = 0, Move = 1, Up = 2, Cancel = 3 };
    // Its surface is width x height dp. flags: 1 = e-ink (black on white, no motion).
    void init(float width, float height, int flags);
    void instrument(int part, float centerNote);   // the middle of the instrument part `part` plays
    void touch(int id, Phase phase, float x, float y, float size, float force);
    void tick();                                    // call every frame
    void setting(int index, double value);          // the manifest's settings, by position
    void stop();                                    // let go of every note

    // The draw list it last sent, and whether it changed since the last call.
    bool takeDirty() { bool d = dirty_; dirty_ = false; return d; }
    const std::vector<uint8_t>& drawList() const { return draw_; }

    // Settings the controller changed itself (an editor or a button on its surface), oldest first:
    // the app keeps them as if they had been set on its rail. Taking them empties the list.
    struct SettingChange { int index; double value; };
    std::vector<SettingChange> takeSettingChanges() { std::vector<SettingChange> v; v.swap(changes_); return v; }

    // In a split, where this controller's notes go: the part (sound) its part 0 plays (its parts
    // follow on), and the MIDI channels it may use, first..first+count-1 (of 1..15), so two
    // controllers never share a channel.
    void setPart(int first) { partBase_ = first; }
    void setChannels(int first, int count) { stop(); chFirst_ = std::clamp(first, 1, 15); chEnd_ = std::clamp(first + count, chFirst_ + 1, 16); }

    // The engine reads note-on velocity times this (the Seaboard sends small ones): divide by it.
    void setVelocityScale(float s) { velocityScale_ = s; }
    double nowMs() const;
    const std::string& lastError() const { return error_; }

    // Called by the WebAssembly (through wasm3); not for the app.
    void noteOn(int voice, int part, float pitch, float pressure, float slide, float velocity, double t);
    void noteChange(int voice, float pitch, float pressure, float slide, double t);
    void noteOff(int voice, double t);
    void noteVolume(int voice, float volume, double t);
    void drawFrom(uint32_t ptr, uint32_t len);
    void settingFrom(int index, double value);

private:
    struct Wasm;
    struct Note { int voice, part; float pitch, pressure, slide, velocity; int kind; double t; };
    void send(uint8_t a, uint8_t b, uint8_t c, int n = 3);
    void play(const Note& n);
    void flush(double now);

    MidiSink sink_;
    std::unique_ptr<Wasm> w_;
    Json manifest_;
    std::vector<uint8_t> draw_;
    bool dirty_ = false;
    float velocityScale_ = 5;
    int partBase_ = 0, chFirst_ = 1, chEnd_ = 16;
    std::string error_;
    int64_t startNs_ = 0;
    // Its voices on MIDI channels 1..15 (2..16 to a musician), the master channel 0 kept free.
    struct Chan { int voice = -1; int note = -1; int bend = -1, pressure = -1, slide = -1, volume = 127; };
    Chan ch_[16];
    std::vector<Note> later_;                       // notes it stamped for later (a strum, an arp)
    std::vector<std::pair<int, float>> startVolume_; // note_volume for a voice that hasn't started yet
    std::vector<SettingChange> changes_;
};

}  // namespace mpe
