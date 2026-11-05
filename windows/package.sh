#!/usr/bin/env bash
# Packs the Windows build into tux-mc-windows.zip. Runs in MSYS2 (UCRT64) after
# `make -C launcher`; GitHub Actions does this (.github/workflows/windows.yml).
#
# tux-mc-windows/
#   install.bat, install.ps1, INSTALL.txt
#   mmc-pack.json                the Prism instance (Minecraft 26.3 + Fabric)
#   theme/                       black theme: launcher style.css, Prism's "tux" theme
#   tux-mc/bin/tux-mc.exe + the GTK DLLs it needs
#   tux-mc/lib, tux-mc/share     pixbuf loaders, icons, GSettings schemas, cape images
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${MINGW_PREFIX:-/ucrt64}
OUT=$D/dist/tux-mc-windows
APP=$OUT/tux-mc
rm -rf -- "$D/dist"
mkdir -p "$APP/bin" "$APP/lib" "$APP/share/tux-mc/data" "$APP/share/glib-2.0/schemas" "$APP/share/icons"

cp "$D/launcher/tux-mc.exe" "$APP/bin/"
cp "$D"/launcher/data/*-cape.png "$APP/share/tux-mc/data/"

# pixbuf loaders (PNG, SVG for the symbolic icons); the cache gets relative
# paths so it works wherever the folder ends up
cp -r "$PREFIX/lib/gdk-pixbuf-2.0" "$APP/lib/"
cache=$(find "$APP/lib/gdk-pixbuf-2.0" -name loaders.cache)
sed -i "s|$(cygpath -m "$PREFIX")/||g; s|$PREFIX/||g" "$cache"

# every DLL the exe and the loaders pull in from MSYS2
deps() { ldd "$@" | awk '{print $3}' | grep -i "^$PREFIX/bin/" | sort -u; }
for f in $(deps "$APP/bin/tux-mc.exe" $(find "$APP/lib" -name '*.dll')); do
    cp -n "$f" "$APP/bin/"
done

# GLib starts programs (Prism, tar) through these helpers on Windows
cp "$PREFIX"/bin/gspawn-win64-helper*.exe "$APP/bin/"

cp "$PREFIX/share/glib-2.0/schemas/gschemas.compiled" "$APP/share/glib-2.0/schemas/"
cp -r "$PREFIX/share/icons/Adwaita" "$PREFIX/share/icons/hicolor" "$APP/share/icons/"

# the installer and what it puts in place
cp "$D/windows/install.bat" "$D/windows/install.ps1" "$D/windows/INSTALL.txt" "$OUT/"
cp "$D/prism/mmc-pack.json" "$OUT/"
"$D/theme/render.sh" --out "$OUT/theme"

# Windows wants CRLF in these
for f in "$OUT/install.bat" "$OUT/install.ps1" "$OUT/INSTALL.txt"; do
    sed -i 's/\r*$/\r/' "$f"
done

(cd "$D/dist" && zip -qr tux-mc-windows.zip tux-mc-windows)
du -sh "$D/dist/tux-mc-windows.zip"
