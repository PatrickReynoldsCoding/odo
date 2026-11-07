#include "gestures.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace gestures {
namespace {

const char* kUuid = "odo-no-gestures@odo";

const char* kMetadata = R"({
  "uuid": "odo-no-gestures@odo",
  "name": "odo: no gestures while playing",
  "description": "Turns off GNOME's three-finger swipes (overview, workspaces) and edge drags while an odo window has focus, so playing with many fingers does not leave odo. Installed and switched by odo's Block system gestures setting.",
  "shell-version": ["45", "46", "47", "48", "49", "50"],
  "version": 1
}
)";

// While odo has focus (and the overview is not open): the overview's and the workspaces' swipe
// trackers (three fingers, on the touch screen and the touchpad) and the edge drags (the
// on-screen keyboard from the bottom, leaving full screen from the top) are disabled; when focus
// goes elsewhere they are as they were, or as the shell set them meanwhile.
const char* kExtension = R"(import Clutter from 'gi://Clutter';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const APP_ID = 'odo';
const EDGE_DRAGS = ['OSK show bottom drag', 'Window unfullscreen top drag'];

export default class OdoNoGestures extends Extension {
    enable() {
        this._blocking = false;
        this._held = [];
        this._signals = [];
        this._watch(global.display, 'notify::focus-window', () => this._update());
        this._watch(Main.overview, 'showing', () => this._update());
        this._watch(Main.overview, 'hidden', () => this._update());
        this._watch(global.stage, 'captured-event', (_stage, event) => {
            if (!this._blocking)
                return Clutter.EVENT_PROPAGATE;
            const type = event.type();
            if (type !== Clutter.EventType.TOUCHPAD_SWIPE && type !== Clutter.EventType.TOUCHPAD_PINCH)
                return Clutter.EVENT_PROPAGATE;
            const fingers = event.get_touchpad_gesture_finger_count
                ? event.get_touchpad_gesture_finger_count() : event.get_gesture_finger_count();
            return fingers >= 3 ? Clutter.EVENT_STOP : Clutter.EVENT_PROPAGATE;
        });
        this._update();
    }

    disable() {
        this._release();
        for (const [obj, id] of this._signals)
            obj.disconnect(id);
        this._signals = [];
    }

    _watch(obj, signal, fn) {
        this._signals.push([obj, obj.connect(signal, fn)]);
    }

    _targets() {
        const out = [Main.overview?._swipeTracker, Main.wm?._workspaceAnimation?._swipeTracker];
        for (const action of global.stage.get_actions()) {
            if (EDGE_DRAGS.includes(action.name))
                out.push(action);
        }
        return out.filter(t => t && 'enabled' in t);
    }

    _update() {
        const app = global.display.focus_window?.get_wm_class() ?? '';
        const block = app === APP_ID && !Main.overview.visible;
        if (block === this._blocking)
            return;
        this._blocking = block;
        if (block)
            this._hold();
        else
            this._release();
    }

    // Each target off; if the shell turns one on (or off) meanwhile, that is what it goes back to.
    _hold() {
        for (const obj of this._targets()) {
            const held = {obj, wanted: obj.enabled, id: 0};
            held.id = obj.connect('notify::enabled', () => {
                if (held.setting)
                    return;
                held.wanted = obj.enabled;
                if (obj.enabled)
                    this._set(held, false);
            });
            this._set(held, false);
            this._held.push(held);
        }
    }

    _release() {
        for (const held of this._held) {
            held.obj.disconnect(held.id);
            held.obj.enabled = held.wanted;
        }
        this._held = [];
        this._blocking = false;
    }

    _set(held, on) {
        held.setting = true;
        held.obj.enabled = on;
        held.setting = false;
    }
}
)";

std::string run(const std::string& cmd) {
    std::string out;
#ifdef _WIN32
    (void)cmd;
    return out;
#else
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n);
    pclose(p);
    return out;
#endif
}

std::string dir() {
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base = data && *data ? data : std::string(home ? home : ".") + "/.local/share";
    return base + "/gnome-shell/extensions/" + kUuid;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The extension's files as odo has them; true if they had to be written (new, or changed).
bool writeFiles() {
    std::string d = dir();
    if (readFile(d + "/extension.js") == kExtension && readFile(d + "/metadata.json") == kMetadata) return false;
    std::error_code ec;
    fs::create_directories(d, ec);
    std::ofstream(d + "/metadata.json") << kMetadata;
    std::ofstream(d + "/extension.js") << kExtension;
    return true;
}

// GNOME's enabled extensions, from gsettings' "['a', 'b']" (or "@as []").
std::vector<std::string> enabledList() {
    std::string s = run("gsettings get org.gnome.shell enabled-extensions 2>/dev/null");
    std::vector<std::string> out;
    for (size_t i = s.find('\''); i != std::string::npos; ) {
        size_t j = s.find('\'', i + 1);
        if (j == std::string::npos) break;
        out.push_back(s.substr(i + 1, j - i - 1));
        i = s.find('\'', j + 1);
    }
    return out;
}

bool safe(const std::string& id) {
    for (char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '_' || c == '-')) return false;
    return !id.empty();
}

bool setEnabled(bool on) {
    std::vector<std::string> list = enabledList(), next;
    for (const auto& id : list) {
        if (!safe(id)) return false;               // something unexpected there: leave the list alone
        if (id != kUuid) next.push_back(id);
    }
    if (on) next.push_back(kUuid);
    if (next == list) return true;
    std::string v = "[";
    for (size_t i = 0; i < next.size(); i++) v += (i ? ", '" : "'") + next[i] + "'";
    v += "]";
    return std::system(("gsettings set org.gnome.shell enabled-extensions \"" + v + "\" 2>/dev/null").c_str()) == 0;
}

}  // namespace

bool supported() {
#ifdef _WIN32
    return false;
#endif
    const char* d = std::getenv("XDG_CURRENT_DESKTOP");
    return d && std::string(d).find("GNOME") != std::string::npos;
}

bool active() {
    if (!supported()) return false;
    std::string info = run(std::string("gnome-extensions info ") + kUuid + " 2>/dev/null");
    return info.find("ACTIVE") != std::string::npos;
}

std::string set(bool on) {
    if (!supported()) return on ? "Only on GNOME" : "";
    if (!on) { setEnabled(false); return ""; }
    bool fresh = writeFiles();
    if (!setEnabled(true)) return "Could not enable it";
    if (std::system("gsettings get org.gnome.shell disable-user-extensions 2>/dev/null | grep -q true") == 0)
        return "Extensions are off in GNOME";
    // GNOME Shell finds a new or changed extension only when it starts: at the next login.
    if (fresh || !active()) return "Log out and in once to start it";
    return "";
}

}  // namespace gestures
