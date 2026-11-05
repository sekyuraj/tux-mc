#!/usr/bin/env bash
# Makes a GitHub release of the version in VERSION: builds the tuxmc mod here
# (it needs Prism's game libraries), uploads tuxmc.jar + tuxmc.jar.sha256 and
# publishes the release. Publishing starts the linux and windows workflows,
# which attach the launcher builds; launchers with "Update by itself" on pick
# the release up once those are there.
#   tools/release.sh            release notes from the commits since the last tag
#   tools/release.sh notes.md   release notes from a file
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)
V=$(cat "$D/VERSION")
tag=v$V
grep -q "\"version\": \"$V\"" "$D/mod/src/main/resources/fabric.mod.json" ||
    { echo "fabric.mod.json doesn't say $V"; exit 1; }
git -C "$D" diff --quiet HEAD || { echo "commit first"; exit 1; }

"$D/mod/build.sh" --no-install
mkdir -p "$D/dist"
cp "$D/mod/build/tuxmc.jar" "$D/dist/tuxmc.jar"
(cd "$D/dist" && sha256sum tuxmc.jar | cut -d' ' -f1 > tuxmc.jar.sha256)

if [[ ${1:-} ]]; then notes=(--notes-file "$1")
else
    last=$(git -C "$D" describe --tags --abbrev=0 2>/dev/null || true)
    notes=(--notes "$(git -C "$D" log --format='- %s' ${last:+$last..}HEAD)")
fi
git -C "$D" push
gh release create "$tag" -R sekyuraj/tux-mc --title "$tag" "${notes[@]}" \
    "$D/dist/tuxmc.jar" "$D/dist/tuxmc.jar.sha256"
