#!/bin/bash
# Waits for the game process on the Quest and samples per-thread CPU usage every 5 s
# into $DIST/threads-<date>.txt, so a played round yields main-thread saturation data.
# Set ANDROID_SERIAL to pick the headset when more than one device is connected.
PROJ=$(cd "$(dirname "$0")/.." && pwd)
DIST=${DIST:-$(cd "$PROJ/.." && pwd)/dist}
mkdir -p "$DIST"
OUT=$DIST/threads-$(date +%Y%m%d-%H%M).txt
echo "sampler started $(date)" > "$OUT"
end=$((SECONDS + 3600))
while [ $SECONDS -lt $end ]; do
  pid=$(adb shell pidof com.sgtbilko.xonoticquest 2>/dev/null | tr -d '\r ')
  if [ -n "$pid" ]; then
    echo "=== $(date +%T) pid $pid" >> "$OUT"
    adb shell "top -H -b -n 1 -p $pid -o TID,%CPU,CMD 2>/dev/null | tail -n +5 | head -8" >> "$OUT"
    adb shell "cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq 2>/dev/null | tr '\n' ' '" >> "$OUT"
    echo >> "$OUT"
    sleep 5
  else
    sleep 10
  fi
done
echo "sampler ended $(date)" >> "$OUT"
