#!/bin/sh
# Make a release of odo for computers: desktop/dist/odo-desktop-<version>-linux-x86_64.tar.gz,
# holding odo, its library (every instrument and controller package) and a README.
#   desktop/package.sh                  every controller in controllers/
#   ODO_CONTROLLERS="tonnetz grid" desktop/package.sh   only these
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/desktop/build-release"
python3 "$root/third_party/mpe-instruments/tools/pack.py" -o "$root/third_party/mpe-instruments/build/packages" \
        --sounds "$root/app/src/main/assets/packs" >/dev/null
controllers=${ODO_CONTROLLERS:-$(cd "$root/controllers" && ls -d */ | tr -d / | grep -v -e '^build$' -e '^include$')}
"$root/controllers/build.sh" $controllers >/dev/null
cmake -S "$root/desktop" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j "$(nproc)" >/dev/null
version=$("$build/odo" --version | cut -d' ' -f2)
name="odo-desktop-$version-linux-x86_64"
stage="$root/desktop/dist/$name"
rm -rf "$stage" "$stage.tar.gz"
mkdir -p "$stage/library"
cp "$build/odo" "$root/desktop/odo.svg" "$root/desktop/install.sh" "$stage/"
# The fonts are inside odo; their licences go beside it.
mkdir -p "$stage/licenses"
cp "$root"/ui/fonts/OFL-*.txt "$stage/licenses/"
strip "$stage/odo"
cp "$root"/third_party/mpe-instruments/build/packages/*.odoi "$stage/library/"
mkdir -p "$stage/library/packs"            # the sound packs: how the sound picker groups them, as on Android
cp "$root"/app/src/main/assets/packs/*.json "$stage/library/packs/"
for c in $controllers; do
    [ -f "$root/controllers/build/$c.odoc" ] && cp "$root/controllers/build/$c.odoc" "$stage/library/"
done
cat > "$stage/README.txt" <<TXT
odo $version for computers: a MESH (Multimodal Expression Sensor Host).
Play the instruments in library/ with the controllers in library/, on a touch screen.

Run:      ./odo, or ./install.sh to put odo in your app menu (in ~/.local/opt/odo)
Needs:    Linux x86-64 with SDL 3 and Faust's libfaust (Ubuntu 26.04: sudo apt install libsdl3-0 libfaust2t64)
Updates:  odo checks GitHub for a newer version when it opens; tap the update line at the
          bottom of the rail to install it. ./odo --no-update turns the check off.
Keys:     Tab hides the rail, F11 full screen, Left/Right change the sound, Esc quits.
Rail:     the preset, split, controller and sound at the top, each opening its picker; then the
          controller's settings by group, each opening a panel. A preset of yours keeps what you
          change while it is on view. The Grid's Touch opens its own panel, with a square to play.
Presets:  share one as a file (.odopreset) from the preset picker, and open one there, by dropping
          it on the window, or (after ./install.sh) from your file manager. The same files work
          in odo on Android.
Gestures: on GNOME, the rail's Block system gestures keeps three-finger swipes (overview,
          workspaces) off odo while it has focus. It installs a small GNOME Shell extension
          (odo-no-gestures@odo); log out and in once the first time.
Fonts:    Roboto and Noto Sans Symbols, under the SIL Open Font License (licenses/).
More:     https://github.com/PatrickReynoldsCoding/odo
TXT
(cd "$root/desktop/dist" && tar -czf "$name.tar.gz" "$name")
echo "$root/desktop/dist/$name.tar.gz"
