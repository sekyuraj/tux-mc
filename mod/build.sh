#!/usr/bin/env bash
# Builds the tuxmc Fabric mod with plain javac. Minecraft 26.x ships unobfuscated,
# so there's no Loom and no remapping: the classpath is Prism's own libraries.
# Launch the instance in Prism once first, so it has downloaded them.
#   ./build.sh                build, then copy into the instance's mods folder
#   ./build.sh --no-install   build only (build/tuxmc.jar)
# Env: TUXMC_INSTANCE (default 26.3), PRISM_DIR, TUXMC_CFLAGS (native lib flags),
#      TUXMC_MINGW (Windows cross compiler, default x86_64-w64-mingw32-gcc)
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
P=${PRISM_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher}
INST=${TUXMC_INSTANCE:-26.3}
MODS=$P/instances/$INST/minecraft/mods
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$D/src/main/resources/fabric.mod.json" | head -1)
OUT=$D/build
LIB=$D/lib

[[ -f $P/libraries/com/mojang/minecraft/26.3/minecraft-26.3-client.jar ]] ||
    { echo "no Minecraft 26.3 in $P/libraries: launch the $INST instance in Prism once" >&2; exit 1; }
FAPI=$(ls "$MODS"/fabric-api-*.jar 2>/dev/null | head -1 || true)
[[ -n $FAPI ]] || { echo "no fabric-api jar in $MODS (install.sh puts one there)" >&2; exit 1; }

# classpath: the 26.3 client plus every other library (other Minecraft versions left out)
mkdir -p "$LIB"
find "$P/libraries" -name '*.jar' ! -name '*natives*' \
    \( ! -path '*/com/mojang/minecraft/*' -o -path '*/com/mojang/minecraft/26.3/*' \) > "$LIB/libs.txt"
rm -rf -- "$LIB/fapi"
mkdir -p "$LIB/fapi"
unzip -q -o "$FAPI" 'META-INF/jars/*' -d "$LIB/fapi"
CP=$( (cat "$LIB/libs.txt"; ls "$LIB"/fapi/META-INF/jars/*.jar) | tr '\n' ':')

rm -rf -- "$OUT"
mkdir -p "$OUT/classes"
javac --release 25 -nowarn -encoding UTF-8 -proc:none -cp "$CP" -d "$OUT/classes" \
    $(find "$D/src/main/java" -name '*.java')
cp -r "$D/src/main/resources/." "$OUT/classes/"

# libtuxmc (native/): capes and the socket list, loaded through Java FFM
mkdir -p "$OUT/classes/natives/linux-x86_64"
cc ${TUXMC_CFLAGS:--O2 -march=x86-64-v2} -fPIC -shared -Wall -Wextra \
    -o "$OUT/classes/natives/linux-x86_64/libtuxmc.so" "$D/native/capes.c" "$D/native/net.c" -lm
# the same library for Windows, cross-compiled (needs mingw-w64)
MINGW=${TUXMC_MINGW:-x86_64-w64-mingw32-gcc}
if command -v "$MINGW" >/dev/null; then
    mkdir -p "$OUT/classes/natives/windows-x86_64"
    "$MINGW" -O2 -Wall -Wextra -shared -static-libgcc -o "$OUT/classes/natives/windows-x86_64/tuxmc.dll" \
        "$D/native/capes.c" "$D/native/net_win.c" -liphlpapi -lws2_32 -lm
else
    echo "warning: no $MINGW, the jar has no Windows library (capes and the net map are off there)" >&2
fi
(cd "$OUT/classes" && jar --create --file "$OUT/tuxmc.jar" .)
echo "built $OUT/tuxmc.jar"

[[ ${1:-} == --no-install ]] && exit 0
rm -f -- "$MODS"/tuxmc*.jar
cp "$OUT/tuxmc.jar" "$MODS/tuxmc-$VERSION.jar"
echo "installed into $MODS"
