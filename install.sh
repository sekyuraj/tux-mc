#!/usr/bin/env bash
# Installs tux-mc for the current user. What's missing gets set up: Prism
# Launcher (its AppImage, unpacked), the 26.3 Fabric instance, Fabric API, the
# tuxmc mod, the launcher, the black theme, and at the end the Microsoft sign-in.
#   ./install.sh            everything (the mod from the latest release)
#   ./install.sh --build    build the mod from source instead of downloading it
#                           (launch the instance once first, so the libraries exist)
#   ./install.sh --no-theme leave Prism's theme and the launcher style alone
# Env: PRISM_DIR (default ~/.local/share/PrismLauncher), PREFIX (default ~/.local)
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
REPO=sekyuraj/tux-mc
PRISM=${PRISM_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher}
PREFIX=${PREFIX:-$HOME/.local}
INST=26.3
MC=$PRISM/instances/$INST/minecraft
BUILD=0 THEME=1
for a in "$@"; do
    case $a in
        --build) BUILD=1 ;;
        --no-theme) THEME=0 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 1 ;;
    esac
done

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# ---- what we need
command -v curl >/dev/null || die "curl is missing"
command -v cc >/dev/null || die "a C compiler (cc) is missing"
command -v pkg-config >/dev/null || die "pkg-config is missing"
pkg-config --exists gtk4 json-glib-1.0 libcurl ||
    die "the launcher needs the gtk4, json-glib and libcurl development packages"
command -v envsubst >/dev/null || die "envsubst (gettext) is missing"
(( BUILD )) && { command -v javac >/dev/null || die "--build needs a JDK 25 (javac)"; }

# ---- Prism Launcher: if it's not installed, its AppImage, unpacked (no FUSE
# needed) into ~/.local/share/tux-mc/prism, with a prismlauncher command in PREFIX/bin
OWN=${XDG_DATA_HOME:-$HOME/.local/share}/tux-mc/prism
if ! command -v prismlauncher >/dev/null && [[ ! -x $OWN/squashfs-root/AppRun ]]; then
    say "Prism Launcher isn't installed: downloading it"
    url=$(curl -fsSL https://api.github.com/repos/PrismLauncher/PrismLauncher/releases/latest |
        grep -o '"browser_download_url": *"[^"]*x86_64\.AppImage"' | grep -v aarch64 | head -1 | cut -d'"' -f4)
    [[ -n $url ]] || die "couldn't find Prism's AppImage on GitHub"
    mkdir -p "$OWN"
    curl -fL --progress-bar -o "$OWN/PrismLauncher.AppImage" "$url"
    chmod +x "$OWN/PrismLauncher.AppImage"
    rm -rf -- "$OWN/squashfs-root"
    (cd "$OWN" && ./PrismLauncher.AppImage --appimage-extract >/dev/null)
    rm -f -- "$OWN/PrismLauncher.AppImage"
    mkdir -p "$PREFIX/bin"
    printf '#!/bin/sh\nexec "%s/squashfs-root/AppRun" "$@"\n' "$OWN" > "$PREFIX/bin/prismlauncher"
    chmod +x "$PREFIX/bin/prismlauncher"
fi
PRISM_EXE=$(command -v prismlauncher || echo "$OWN/squashfs-root/AppRun")

# Prism's settings: fetch the right Java by itself, skip its first-run questions
# (only keys that aren't set yet; Prism rewrites the file when it exits)
cfg=$PRISM/prismlauncher.cfg
if ! pgrep -x prismlauncher >/dev/null; then
    mkdir -p "$PRISM"
    [[ -f $cfg ]] || printf '[General]\n' > "$cfg"
    for kv in AutomaticJavaDownload=true AutomaticJavaSwitch=true IgnoreJavaWizard=true Language=en_US; do
        grep -q "^${kv%%=*}=" "$cfg" || sed -i "/^\[General\]/a $kv" "$cfg"
    done
fi

# ---- the Prism instance: Minecraft 26.3 + Fabric
if [[ ! -f $PRISM/instances/$INST/instance.cfg ]]; then
    say "creating the Prism instance \"$INST\" (Minecraft 26.3 + Fabric Loader)"
    mkdir -p "$MC/mods"
    cat > "$PRISM/instances/$INST/instance.cfg" <<EOF
[General]
ConfigVersion=1.2
InstanceType=OneSix
name=$INST
iconKey=default
EOF
    # Under Wayland, XWayland GL can fail on 26.x ("Could not retrieve OpenGL functions")
    if [[ -n ${WAYLAND_DISPLAY:-} ]]; then
        printf 'OverrideCommands=true\nWrapperCommand=env SDL_VIDEO_DRIVER=wayland\n' >> "$PRISM/instances/$INST/instance.cfg"
    fi
    cp "$D/prism/mmc-pack.json" "$PRISM/instances/$INST/mmc-pack.json"
fi
mkdir -p "$MC/mods"

# ---- Fabric API from Modrinth (checked against its sha512)
if ! ls "$MC/mods"/fabric-api-*.jar >/dev/null 2>&1; then
    say "downloading Fabric API for 26.3"
    json=$(curl -fsSL -G https://api.modrinth.com/v2/project/fabric-api/version \
        --data-urlencode 'loaders=["fabric"]' --data-urlencode 'game_versions=["26.3"]')
    url=$(grep -o '"url":"[^"]*\.jar"' <<<"$json" | head -1 | cut -d'"' -f4)
    sum=$(grep -o '"sha512":"[0-9a-f]*"' <<<"$json" | head -1 | cut -d'"' -f4)
    [[ -n $url && -n $sum ]] || die "no Fabric API for 26.3 on Modrinth"
    f=${url##*/}
    f=${f//%2B/+}
    curl -fsSL -o "$MC/mods/$f.part" "$url"
    echo "$sum  $MC/mods/$f.part" | sha512sum -c --quiet - || die "Fabric API download is corrupt"
    mv "$MC/mods/$f.part" "$MC/mods/$f"
fi

# ---- the mod
if (( BUILD )); then
    say "building the tuxmc mod"
    "$D/mod/build.sh"
else
    say "downloading the tuxmc mod (latest release)"
    curl -fsSL -o "$MC/mods/tuxmc.jar.part" "https://github.com/$REPO/releases/latest/download/tuxmc.jar" ||
        die "download failed; try ./install.sh --build"
    rm -f -- "$MC/mods"/tuxmc*.jar
    mv "$MC/mods/tuxmc.jar.part" "$MC/mods/tuxmc.jar"
fi

# ---- the launcher
say "building the launcher"
make -C "$D/launcher" PREFIX="$PREFIX" install >/dev/null

# ---- black theme for Prism and the launcher
if (( THEME )); then
    say "applying the black theme"
    PRISM_DIR=$PRISM "$D/theme/render.sh"
fi

# ---- the Microsoft account: tux-mc plays with the account signed in to Prism
signed_in() { [[ -f $PRISM/accounts.json ]] && grep -q '"profile"' "$PRISM/accounts.json"; }
if ! signed_in; then
    say "sign in to your Microsoft account in Prism"
    echo "   Prism opens now. Click \"Add Microsoft account\" and sign in with the account that owns"
    echo "   Minecraft (or: Accounts at the top right, Manage Accounts, Add Microsoft)."
    echo "   This waits until you're signed in; Ctrl+C skips it (tux-mc asks again when it starts)."
    setsid -f "$PRISM_EXE" --show-window >/dev/null 2>&1 || true
    until signed_in; do sleep 2; done
    name=$(grep -o '"name": *"[^"]*"' "$PRISM/accounts.json" | head -1 | cut -d'"' -f4)
    say "signed in as ${name:-your account}"
fi

say "done. Start it with: tux-mc"
[[ :$PATH: == *":$PREFIX/bin:"* ]] || echo "   ($PREFIX/bin isn't in your PATH)"
