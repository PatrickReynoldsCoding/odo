#!/bin/sh
# Build and start odo for computers, playing the same packages as odo on Android:
#   desktop/run.sh [odo options]
# Packs the instruments (.odoi), builds the controllers (.odoc), builds the app, and links them
# into its library, ~/.local/share/odo/library. Needs: faust, libfaust, clang, lld, SDL3, cmake.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
lib="$HOME/.local/share/odo/library"
build="$root/desktop/build"
python3 "$root/third_party/mpe-instruments/tools/pack.py" -o "$root/third_party/mpe-instruments/build/packages" \
        --sounds "$root/app/src/main/assets/packs"
"$root/controllers/build.sh"
cmake -S "$root/desktop" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build" -j "$(nproc)"
mkdir -p "$lib/packs"
ln -sf "$root"/app/src/main/assets/packs/*.json "$lib/packs/"
for f in "$root"/third_party/mpe-instruments/build/packages/*.odoi "$root"/controllers/build/*.odoc; do
    ln -sf "$f" "$lib/"
done
mkdir -p "$build/takes"                    # the built-in takes (Takes -> BUILT IN): this checkout's takes/
ln -sf "$root"/takes/*.odotake "$build/takes/"
exec "$build/odo" --library "$lib" "$@"
