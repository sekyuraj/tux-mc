#!/usr/bin/env bash
# Renders the launcher stylesheet and the Prism Launcher theme from a palette.
#   theme/render.sh [palette.sh]      (default: theme/palettes/black.sh)
# Writes ~/.config/tux-mc/style.css and <prism>/themes/tux/, then points Prism at it.
#   theme/render.sh --out DIR [palette.sh]
# only writes DIR/style.css and DIR/prism/tux/ (for packaging), touches nothing else.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
OUTDIR=
if [[ ${1:-} == --out ]]; then
    OUTDIR=$2
    shift 2
fi
PAL=${1:-$D/palettes/black.sh}
PRISM=${PRISM_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher}
CONF=${XDG_CONFIG_HOME:-$HOME/.config}/tux-mc

before=$(compgen -v | sort)
# shellcheck source=/dev/null
source "$PAL"
vars=()
for v in $(comm -13 <(echo "$before") <(compgen -v | sort) | grep -E '^[a-z][a-z0-9_]*$'); do
    export "${v?}"
    vars+=("$v")
done
fmt=$(printf '${%s} ' "${vars[@]}")

if [[ -n $OUTDIR ]]; then
    mkdir -p "$OUTDIR/prism/tux"
    envsubst "$fmt" < "$D/tux-mc.css"       > "$OUTDIR/style.css"
    envsubst "$fmt" < "$D/prism-theme.json" > "$OUTDIR/prism/tux/theme.json"
    envsubst "$fmt" < "$D/prism-theme.css"  > "$OUTDIR/prism/tux/themeStyle.css"
    exit 0
fi

mkdir -p "$CONF" "$PRISM/themes/tux"
envsubst "$fmt" < "$D/tux-mc.css"       > "$CONF/style.css"
envsubst "$fmt" < "$D/prism-theme.json" > "$PRISM/themes/tux/theme.json"
envsubst "$fmt" < "$D/prism-theme.css"  > "$PRISM/themes/tux/themeStyle.css"

# Prism reads its cfg on start and writes it back on exit
cfg=$PRISM/prismlauncher.cfg
if pgrep -x prismlauncher >/dev/null; then
    echo "Prism is open: close it and run this again to switch it to the tux theme" >&2
elif [[ -f $cfg ]]; then
    icons=flat_white
    [[ ${dark:-1} == 0 ]] && icons=flat
    for kv in ApplicationTheme=tux IconTheme=$icons; do
        k=${kv%%=*}
        if grep -q "^$k=" "$cfg"; then
            sed -i "s/^$k=.*/$kv/" "$cfg"
        else
            sed -i "/^\[General\]/a $kv" "$cfg"
        fi
    done
else
    printf '[General]\nApplicationTheme=tux\nIconTheme=flat_white\n' > "$cfg"
fi
echo "theme: ${name:-?} -> $CONF/style.css and $PRISM/themes/tux"
