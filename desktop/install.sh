#!/bin/sh
# Install odo for computers for this user, with an icon in the app menu:
#   ./install.sh                 from the folder of an unpacked release (where odo is)
#   ./install.sh TARBALL         from a downloaded odo-desktop-*.tar.gz
# odo goes in ~/.local/opt/odo (where it can update itself); the menu entry and icon in
# ~/.local/share, with preset files (.odopreset) opening in odo. To remove: rm -r ~/.local/opt/odo
#   ~/.local/share/applications/odo.desktop ~/.local/share/icons/hicolor/scalable/apps/odo.svg
#   ~/.local/share/mime/packages/odo.xml
set -e
here=$(cd "$(dirname "$0")" && pwd)
dest="$HOME/.local/opt/odo"
if [ -n "$1" ]; then
    tmp=$(mktemp -d)
    tar -xzf "$1" -C "$tmp"
    src=$(ls -d "$tmp"/odo-desktop-*)
else
    src="$here"
fi
[ -x "$src/odo" ] || { echo "no odo in $src" >&2; exit 1; }
mkdir -p "$dest"
cp "$src/odo" "$dest/odo.new" && mv "$dest/odo.new" "$dest/odo"
rm -rf "$dest/library" && cp -r "$src/library" "$dest/library"
[ -f "$src/README.txt" ] && cp "$src/README.txt" "$dest/"
icon="$HOME/.local/share/icons/hicolor/scalable/apps/odo.svg"
mkdir -p "$(dirname "$icon")" "$HOME/.local/share/applications"
if [ -f "$src/odo.svg" ]; then cp "$src/odo.svg" "$icon"; elif [ -f "$here/odo.svg" ]; then cp "$here/odo.svg" "$icon"; fi
cat > "$HOME/.local/share/applications/odo.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=odo
GenericName=MESH
Comment=Play instruments expressively with your fingers on a touch screen
Exec=$dest/odo %f
MimeType=application/vnd.odo.preset+json;
Icon=odo
Terminal=false
Categories=AudioVideo;Audio;Music;
Keywords=music;instrument;MPE;touch;synth;
StartupWMClass=odo
DESKTOP
# Preset files (PRESET_FORMAT.md): known by their extension, opened with odo.
mkdir -p "$HOME/.local/share/mime/packages"
cat > "$HOME/.local/share/mime/packages/odo.xml" <<'MIME'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/vnd.odo.preset+json">
    <comment>odo preset</comment>
    <sub-class-of type="application/json"/>
    <glob pattern="*.odopreset"/>
  </mime-type>
</mime-info>
MIME
update-mime-database "$HOME/.local/share/mime" 2>/dev/null || true
xdg-mime default odo.desktop application/vnd.odo.preset+json 2>/dev/null || true
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "odo $("$dest/odo" --version | cut -d' ' -f2) installed in $dest; it is in your app menu."
