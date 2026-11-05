#!/usr/bin/env bash
# Packs the Linux build for the launcher's updater: tux-mc-linux-x86_64.tar.gz
# and its .sha256. Runs after `make -C launcher` (GitHub Actions does this,
# .github/workflows/linux.yml).
#
# tux-mc/bin/tux-mc                  the launcher (needs gtk4, json-glib, libcurl)
# tux-mc/share/tux-mc/data/*.png     cape pictures
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=$D/dist
rm -rf -- "$OUT/tux-mc"
mkdir -p "$OUT/tux-mc/bin" "$OUT/tux-mc/share/tux-mc/data"
install -m755 "$D/launcher/tux-mc" "$OUT/tux-mc/bin/"
install -m644 "$D"/launcher/data/*-cape.png "$OUT/tux-mc/share/tux-mc/data/"
tar -C "$OUT" -czf "$OUT/tux-mc-linux-x86_64.tar.gz" tux-mc
(cd "$OUT" && sha256sum tux-mc-linux-x86_64.tar.gz | cut -d' ' -f1 > tux-mc-linux-x86_64.tar.gz.sha256)
"$OUT/tux-mc/bin/tux-mc" --version
du -sh "$OUT/tux-mc-linux-x86_64.tar.gz"
