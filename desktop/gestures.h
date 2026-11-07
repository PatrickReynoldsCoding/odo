// Keeping the desktop's own touch gestures off odo while it has focus: a three-finger swipe on the
// screen (or the touchpad) opens GNOME's overview or switches workspace, which is easy to do by
// accident while playing. GNOME takes those swipes before any app sees them, so odo installs a
// small GNOME Shell extension (odo-no-gestures@odo) that turns them off whenever an odo window has
// focus, and turns it on or off in GNOME's list of enabled extensions. Linux with GNOME only.
#pragma once
#include <string>

namespace gestures {

// GNOME is the desktop: the extension can work.
bool supported();

// The extension is installed, enabled and running in GNOME Shell now.
bool active();

// Turn the blocking on (install the extension, if it is not there or is older, and enable it) or
// off (disable it). Returns what to tell the user, "" when it simply worked: GNOME only notices a
// newly installed extension at the next login.
std::string set(bool on);

}  // namespace gestures
