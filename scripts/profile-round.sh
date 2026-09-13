#!/bin/bash
# Record a 20 s CPU profile of the running game on the Quest with simpleperf.
# Usage: scripts/profile-round.sh [seconds]   -> $DIST/perf-<stamp>.data
# Set ANDROID_SERIAL to pick the headset when more than one device is connected.
D=${1:-20}
PROJ=$(cd "$(dirname "$0")/.." && pwd)
DIST=${DIST:-$(cd "$PROJ/.." && pwd)/dist}
pid=$(adb shell pidof com.sgtbilko.xonoticquest | tr -d '\r ')
[ -z "$pid" ] && { echo "game not running"; exit 1; }
adb shell "/data/local/tmp/simpleperf record -p $pid -e cpu-clock -f 1000 -g --duration $D -o /data/local/tmp/perf.data" 2>&1 | tail -3
mkdir -p "$DIST"
out=$DIST/perf-$(date +%Y%m%d-%H%M).data
adb pull /data/local/tmp/perf.data "$out" >/dev/null && echo "$out"
