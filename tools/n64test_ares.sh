#!/usr/bin/env bash
# n64test_ares.sh — headless harness using Ares (accuracy-focused, homebrew mode)
# usage: tools/n64test_ares.sh rom.z64 [seconds]
#   runs rom under xvfb+Ares for <seconds>, grabs a screenshot at the end,
#   auto-kills, scrapes log for exceptions/debug lines.
set -u
ROM="${1:?rom required}"
SECS="${2:-20}"
OUT="shots/$(basename "${ROM%.z64}")_ares"
mkdir -p "$OUT"

xvfb-run -a -n 99 -s "-screen 0 800x600x24" \
  ares --setting Developer/HomebrewMode=True --no-file-prompt \
  "$ROM" > "$OUT/ares.log" 2>&1 &
APID=$!
sleep "$SECS"
DISPLAY=:99 import -window root "$OUT/final.png" 2>/dev/null
kill "$APID" 2>/dev/null
sleep 1
pkill -9 -f "ares .*$(basename "$ROM")" 2>/dev/null

OK=0; [ -f "$OUT/final.png" ] && OK=1
echo "== ares $ROM secs=$SECS shot=$OK"
grep -aiE "exception|assert|panic|error" "$OUT/ares.log" | grep -viE "gamemode|stalled compile|wayland" | head -10
echo "-- log tail --"; tail -3 "$OUT/ares.log"
