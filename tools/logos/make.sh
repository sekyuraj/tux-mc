#!/usr/bin/env bash
# Rebuilds the cape logos from the vector sources in src/ at 1024 px and puts
# them where the mod (assets/tuxmc/<id>.png) and the launcher (data/<id>-cape.png)
# look. Needs rsvg-convert and gdk-pixbuf. Tux, Gentoo, SteamOS and CRUX only
# exist as pictures and are left as they are.
#   tools/logos/make.sh            rebuild
#   tools/logos/make.sh --sheet    rebuild, then write sheet.png to look at
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
R=$D/../..
B=$D/build
mkdir -p "$B"
cc -O2 -o "$B/logoprep" "$D/logoprep.c" $(pkg-config --cflags --libs gdk-pixbuf-2.0)

svg() { rsvg-convert -h 2048 -a "$D/src/$1.svg" -o "$B/$1.png"; }
make_logo() {                      # id source ops...
    local id=$1 src=$2
    shift 2
    "$B/logoprep" "$B/$src.png" "$B/out-$id.png" 1024 "$@"
    cp "$B/out-$id.png" "$R/mod/src/main/resources/assets/tuxmc/$id.png"
    cp "$B/out-$id.png" "$R/launcher/data/$id-cape.png"
    echo "  $id"
}

svg openbsd;   make_logo openbsd openbsd fillin F2CA30          # Puffy, filled yellow
svg archlinux; make_logo arch archlinux color 1793D1
svg debian;    make_logo debian debian color D70A53
svg freebsd;   make_logo freebsd freebsd color EB0028 erase 1869 0 179 358   # without the ®
svg mint;      make_logo mint mint
rsvg-convert -w 2048 -h 2048 "$D/src/lfs.svg" -o "$B/lfs.png"; make_logo lfs lfs

if [[ ${1:-} == --sheet ]]; then
    cc -O2 -I"$R/mod/native" -o "$B/sheet" "$D/sheet.c" "$R/mod/native/capes.c" \
        $(pkg-config --cflags --libs gdk-pixbuf-2.0) -lm
    "$B/sheet" "$D/sheet.png" 32 "$R/mod/src/main/resources/assets/tuxmc" openbsd freebsd arch debian mint lfs
    echo "wrote $D/sheet.png"
fi
