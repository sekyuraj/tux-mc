#!/usr/bin/env bash
# Repeatable game benchmark: a fresh copy of one world, the camera turns once
# at a fixed speed after a warm-up, the mod writes the numbers and the game quits.
#   tools/bench.sh <label> [seconds] [warm-up seconds]
# Env: TUXMC_BENCH_RD=<chunks> (render distance for the run), TUXMC_BENCH_WARM_MAX (s)
#      BENCH_WORLD   world folder to copy (default: "Fall  aesthetic" of the instance)
#      TUXMC_INSTANCE (default 26.3), PRISM_DIR, PRISM (prism command)
#      BENCH_ENV     extra "K=V K=V" for the game (e.g. a config switch)
#      BENCH_VIA     prism (default) or tux-mc: START pressed in a test copy of the
#                    launcher (TUX_MC_LAUNCHER, default tux-mc), so fast start is used
# Results: ~/.cache/tux-mc-bench/<label>.txt (+ .frames, + .startup)
set -euo pipefail
label=${1:?label}
secs=${2:-40}
warm=${3:-25}
P=${PRISM_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/PrismLauncher}
INST=${TUXMC_INSTANCE:-26.3}
MC=$P/instances/$INST/minecraft
SRC=${BENCH_WORLD:-$MC/saves/Fall  aesthetic}
OUT=${XDG_CACHE_HOME:-$HOME/.cache}/tux-mc-bench
PRISM=${PRISM:-prismlauncher}
mkdir -p "$OUT"

game_pid() {
    for p in $(pgrep -x java || true); do
        tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null | grep -q -- "instances/$INST/" && { echo "$p"; return; }
        [[ $(readlink /proc/$p/cwd 2>/dev/null) == "$MC" ]] && { echo "$p"; return; }
    done
    return 0
}
[[ -z $(game_pid) ]] || { echo "the game is running: close it first" >&2; exit 1; }
pgrep -x prismlauncher >/dev/null && { echo "Prism is open: close it first (it keeps its old env)" >&2; exit 1; }

# a fresh copy of the world every run, so every run sees the same chunks
T=$OUT/world-template-$(basename "$SRC" | tr -c "A-Za-z0-9\n" _)
if [[ ! -d $T ]]; then
    echo "copying $SRC as the bench template"
    cp -a "$SRC" "$T"
fi
rm -rf -- "$MC/saves/zz-tux-bench"
cp -a "$T" "$MC/saves/zz-tux-bench"
sync -f "$MC/saves/zz-tux-bench" 2>/dev/null || true

res=$OUT/$label.txt
rm -f -- "$res" "$res.frames"
t0=$(date +%s%3N)
# shellcheck disable=SC2086
if [[ ${BENCH_VIA:-prism} == tux-mc ]]; then
    set -- "${TUX_MC_LAUNCHER:-tux-mc}"
    VIA_ENV="TUX_MC_APP_ID=dev.tux.McBench TUX_MC_PLAY=1 TUX_MC_NO_UPDATE=1 TUX_MC_WORLD=zz-tux-bench"
else
    set -- "$PRISM" --launch "$INST" --world zz-tux-bench
    VIA_ENV=
fi
env TUXMC_T0="$t0" TUXMC_BENCH="$secs" ${TUXMC_BENCH_RD:+TUXMC_BENCH_RD=$TUXMC_BENCH_RD} TUXMC_BENCH_WARM="$warm" TUXMC_BENCH_OUT="$res" ${BENCH_ENV:-} \
    $VIA_ENV "$@" >/dev/null 2>&1 &
prism=$!

# time to the java process, then wait for the game to end
pid=
for _ in $(seq 600); do
    pid=$(game_pid)
    [[ -n $pid ]] && break
    sleep 0.05
done
t_java=$(date +%s%3N)
[[ -n $pid ]] || { echo "no game process after 30 s" >&2; exit 1; }
peak=0
while kill -0 "$pid" 2>/dev/null; do
    rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status 2>/dev/null || echo 0)
    (( rss > peak )) && peak=$rss
    sleep 1
done
[[ ${BENCH_VIA:-prism} == tux-mc ]] && kill "$prism" 2>/dev/null
wait "$prism" 2>/dev/null || true
grep -h "startup" "$MC/logs/latest.log" | tail -1 | sed 's/.*tuxmc: startup //' > "$OUT/$label.startup" || true
{
    echo "click_to_java_ms $((t_java - t0))"
    echo "peak_rss_mb $((peak / 1024))"
} >> "$res" 2>/dev/null || true
rm -rf -- "$MC/saves/zz-tux-bench"
echo "== $label"
cat "$res" "$OUT/$label.startup" 2>/dev/null || echo "no result (see $MC/logs/latest.log)"
