#!/usr/bin/env bash
# Attach JFR (CPU + allocation sampling) and a GC log to the running game.
#   tools/profile-attach.sh <out-prefix> <delay s> [seconds]
# Use with bench.sh: start this in the background first, it waits for the game.
set -u
MC=${PRISM_DIR:-$HOME/.local/share/PrismLauncher}/instances/${TUXMC_INSTANCE:-26.3}/minecraft
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")}
pid=
for _ in $(seq 600); do
    for p in $(pgrep -x java); do [[ $(readlink /proc/$p/cwd) == "$MC" ]] && pid=$p; done
    [[ -n $pid ]] && break
    sleep 0.1
done
[[ -n $pid ]] || { echo "no game" >&2; exit 1; }
sleep "$2"
"$JAVA_HOME/bin/jcmd" "$pid" VM.log output="file=$1.gc.log" what=gc decorators=uptime >/dev/null
"$JAVA_HOME/bin/jcmd" "$pid" JFR.start duration="${3:-35}s" settings=profile filename="$1.jfr" >/dev/null
echo "recording $pid -> $1.jfr"
