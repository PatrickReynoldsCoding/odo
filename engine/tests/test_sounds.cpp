// Which sounds sustain (mpe::sustains): the sound pickers' Sustained and Fading, in both apps.
#include "check.h"
#include "mpe/package.h"

int main() {
    CHECK(mpe::sustains({"strings"}, "strings/cello"), "a cello sustains");
    CHECK(mpe::sustains({"organs"}, "organs/organ"), "an organ sustains");
    CHECK(mpe::sustains({"synths"}, "synths/pad"), "a synth pad sustains");
    CHECK(!mpe::sustains({"keys"}, "keys/piano"), "a piano fades");
    CHECK(!mpe::sustains({"plucked"}, "plucked/harp"), "a harp fades");
    CHECK(!mpe::sustains({"drums"}, "drums/kit"), "a drum kit fades");
    CHECK(!mpe::sustains({"percussion"}, "percussion/taiko"), "a taiko fades");
    CHECK(!mpe::sustains({"struck"}, "struck/marimba"), "a marimba fades");
    CHECK(!mpe::sustains({"synths"}, "synths/pluck-synth"), "Pluck Synth fades, though a synth");
    CHECK(!mpe::sustains({"synths"}, "synths/fm-keys"), "FM Keys fades, though a synth");
    CHECK(mpe::sustains({}, ""), "a sound with no tags sustains");
    return finish();
}
