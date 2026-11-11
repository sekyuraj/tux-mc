#!/usr/bin/env bash
# Installs tux-mc for the current user. What's missing gets set up: Prism
# Launcher (its AppImage, unpacked), the 26.3 Fabric instance, Fabric API, the
# tuxmc mod, the launcher, the black theme, and at the end the Microsoft sign-in.
#   ./install.sh            everything (the mod from the latest release)
#   ./install.sh --build    build the mod from source instead of downloading it
#                           (launch the instance once first, so the libraries exist)
#   ./install.sh --no-theme leave Prism's theme and the launcher style alone
#   ./install.sh --no-perf  only Fabric API + Sodium, not the rest of the performance mods
# Downloads run in parallel and are checked (sha512 / sha256); what's already there
# is kept, so running it again only fetches what's missing.
# Env: PRISM_DIR (default ~/.local/share/PrismLauncher), PREFIX (default ~/.local)
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
REPO=sekyuraj/tux-mc
PRISM=${PRISM_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher}
PREFIX=${PREFIX:-$HOME/.local}
INST=26.3
MC=$PRISM/instances/$INST/minecraft
BUILD=0 THEME=1 PERF=1
for a in "$@"; do
    case $a in
        --build) BUILD=1 ;;
        --no-theme) THEME=0 ;;
        --no-perf) PERF=0 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
# It downloads in the background while the rest is set up; unpacked once, kept after.
OWN=${XDG_DATA_HOME:-$HOME/.local/share}/tux-mc/prism
prism_get() {
    local url=https://github.com/PrismLauncher/PrismLauncher/releases/latest/download/PrismLauncher-Linux-x86_64.AppImage
    mkdir -p "$OWN"
    curl -fsSL --retry 3 -o "$OWN/PrismLauncher.AppImage.part" "$url" ||
        { echo "error: Prism Launcher download failed" >&2; return 1; }
    mv "$OWN/PrismLauncher.AppImage.part" "$OWN/PrismLauncher.AppImage"
    chmod +x "$OWN/PrismLauncher.AppImage"
    rm -rf -- "$OWN/squashfs-root"
    (cd "$OWN" && ./PrismLauncher.AppImage --appimage-extract >/dev/null) ||
        { echo "error: couldn't unpack the Prism AppImage" >&2; return 1; }
    rm -f -- "$OWN/PrismLauncher.AppImage"
    mkdir -p "$PREFIX/bin"
    printf '#!/bin/sh\nexec "%s/squashfs-root/AppRun" "$@"\n' "$OWN" > "$PREFIX/bin/prismlauncher"
    chmod +x "$PREFIX/bin/prismlauncher"
    say "got Prism Launcher"
}
prism_pid=
if ! command -v prismlauncher >/dev/null && [[ ! -x $OWN/squashfs-root/AppRun ]]; then
    say "Prism Launcher isn't installed: downloading it"
    prism_get & prism_pid=$!
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

# ---- mods from Modrinth, all at once, each checked against its sha512 and only
# moved into mods/ when complete (an interrupted run leaves no half jars behind).
# A mod that's already there (any version) is left alone.
#   Fabric API, Sodium: needed (tuxmc's render distance slider goes with Sodium's)
#   the rest: the performance set tux-mc is tested with on 26.3 (--no-perf skips it)
#     Lithium (game logic), FerriteCore (memory), ImmediatelyFast (HUD/text drawing),
#     Entity Culling (hidden entities), More Culling + Cloth Config (block faces),
#     BadOptimizations (lightmap/sky caching), Krypton (network stack)
# Left out on purpose: ScalableLux and C2ME (alpha for 26.3; they touch world data),
# Dynamic FPS (lowers fps in the background, a power saver, not a speed-up).
have_jar() { [[ -n $(find "$MC/mods" -maxdepth 1 -iname "$1*.jar" -print -quit) ]]; }
modrinth() {                     # slug title jar-prefix
    have_jar "$3" && return 0
    local json url sum f
    json=$(curl -fsSL --retry 3 -G "https://api.modrinth.com/v2/project/$1/version" \
        --data-urlencode 'loaders=["fabric"]' --data-urlencode 'game_versions=["26.3"]') ||
        { echo "error: can't reach Modrinth for $2" >&2; return 1; }
    url=$(grep -o '"url":"[^"]*\.jar"' <<<"$json" | head -1 | cut -d'"' -f4)
    sum=$(grep -o '"sha512":"[0-9a-f]*"' <<<"$json" | head -1 | cut -d'"' -f4)
    [[ -n $url && -n $sum ]] || { echo "error: no $2 for 26.3 on Modrinth" >&2; return 1; }
    f=${url##*/}
    f=${f//%2B/+}
    curl -fsSL --retry 3 -o "$MC/mods/$f.part" "$url" || { echo "error: $2 download failed" >&2; return 1; }
    if ! echo "$sum  $MC/mods/$f.part" | sha512sum -c --quiet - >/dev/null 2>&1; then
        rm -f -- "$MC/mods/$f.part"
        echo "error: $2 download is corrupt" >&2
        return 1
    fi
    mv "$MC/mods/$f.part" "$MC/mods/$f"
    say "got $2"
}
MODS=("fabric-api|Fabric API|fabric-api-" "sodium|Sodium|sodium-")
(( PERF )) && MODS+=("lithium|Lithium|lithium-" "ferrite-core|FerriteCore|ferritecore-"
    "immediatelyfast|ImmediatelyFast|ImmediatelyFast-" "entityculling|Entity Culling|entityculling-"
    "cloth-config|Cloth Config|cloth-config-" "moreculling|More Culling|moreculling-"
    "badoptimizations|BadOptimizations|BadOptimizations-" "krypton|Krypton|krypton-")

# ---- the tuxmc mod: the release jar, checked against the release's .sha256; skipped
# when the one in mods/ is already that jar
tuxmc_jar() {
    local base=https://github.com/$REPO/releases/latest/download sum have
    sum=$(curl -fsSL --retry 3 "$base/tuxmc.jar.sha256" | cut -d' ' -f1) ||
        { echo "error: can't get the tuxmc release; try ./install.sh --build" >&2; return 1; }
    for have in "$MC/mods"/tuxmc*.jar; do
        [[ -f $have && $(sha256sum "$have" | cut -d' ' -f1) == "$sum" ]] && return 0
    done
    curl -fsSL --retry 3 -o "$MC/mods/tuxmc.jar.part" "$base/tuxmc.jar" ||
        { echo "error: tuxmc download failed; try ./install.sh --build" >&2; return 1; }
    if [[ $(sha256sum "$MC/mods/tuxmc.jar.part" | cut -d' ' -f1) != "$sum" ]]; then
        rm -f -- "$MC/mods/tuxmc.jar.part"
        echo "error: tuxmc download is corrupt" >&2
        return 1
    fi
    rm -f -- "$MC/mods"/tuxmc*.jar
    mv "$MC/mods/tuxmc.jar.part" "$MC/mods/tuxmc.jar"
    say "got the tuxmc mod"
}

say "fetching mods and building the launcher (in parallel)"
jobs_pids=() jobs_names=()
for m in "${MODS[@]}"; do
    IFS='|' read -r slug title prefix <<<"$m"
    modrinth "$slug" "$title" "$prefix" & jobs_pids+=($!) jobs_names+=("$title")
done
if (( ! BUILD )); then
    tuxmc_jar & jobs_pids+=($!) jobs_names+=("the tuxmc mod")
fi
# the launcher compiles meanwhile (one object per file, all cores)
make -C "$D/launcher" -j"$(nproc 2>/dev/null || echo 4)" PREFIX="$PREFIX" install >/dev/null &
jobs_pids+=($!) jobs_names+=("the launcher build")
[[ -n $prism_pid ]] && jobs_pids+=("$prism_pid") jobs_names+=("Prism Launcher")
failed=()
for i in "${!jobs_pids[@]}"; do
    wait "${jobs_pids[$i]}" || failed+=("${jobs_names[$i]}")
done
(( ${#failed[@]} == 0 )) || die "these didn't work: ${failed[*]} (run ./install.sh again; finished parts are kept)"

if (( BUILD )); then
    say "building the tuxmc mod"
    "$D/mod/build.sh"
fi

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
