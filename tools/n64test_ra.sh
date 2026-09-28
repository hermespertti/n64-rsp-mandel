#!/usr/bin/env bash
# n64test.sh — headless test harness for libdragon ROMs (RetroArch + mupen64plus-next)
# usage: tools/n64test.sh rom.z64 [frames]
#   runs rom for <frames> emulated frames under xvfb (null audio),
#   captures a screenshot at the last frame, scrapes stdout for
#   libdragon debug output / exceptions. Self-terminating.
set -u
ROM="${1:?rom required}"
FRAMES="${2:-180}"
OUT="shots/$(basename "${ROM%.z64}")"
mkdir -p "$OUT"
SS="$OUT/f$FRAMES.png"

stdbuf -oL -eL xvfb-run -a -s "-screen 0 800x600x24" \
  retroarch -L /usr/lib/libretro/mupen64plus_next_libretro.so \
  --config "$(dirname "$0")/ra-headless.cfg" \
  --max-frames "$FRAMES" --max-frames-ss --max-frames-ss-path "$SS" \
  --verbose --log-file "$OUT/ra.log" \
  "$ROM" > "$OUT/stdout.log" 2>&1
RC=$?

OK=0; [ -f "$SS" ] && OK=1
echo "== $ROM rc=$RC frame=$FRAMES shot=$OK"
grep -aiE "\[n64\]|assert|panic|exception|Bad instruction|AddrErr" "$OUT/stdout.log" "$OUT/ra.log" 2>/dev/null | head -20
echo "-- log tail --"; tail -4 "$OUT/stdout.log" 2>/dev/null
