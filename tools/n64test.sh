#!/usr/bin/env bash
# n64test.sh — headless emulator harness for libdragon ROMs
# usage: tools/n64test.sh rom.z64 [shotframes]
#   runs rom under xvfb + mupen64plus, captures PNGs at given frame numbers
#   (emulator auto-quits after the last one), scrapes emulator stdout
#   (libdragon debug()/printf land there).
set -u
ROM="${1:?rom required}"
SHOTS="${2:-60,120,180}"                # comma-separated frame numbers to capture
OUT="shots/$(basename "${ROM%.z64}")"
mkdir -p "$OUT"
CFG="$OUT/mupen64plus.cfg"
if [ ! -f "$CFG" ]; then
  # generate default config into $OUT (no --nosaveoptions here, or it won't save!)
  mupen64plus --configdir "$OUT" rom_notexist.z64 >/dev/null 2>&1 || true
fi
if [ -f "$CFG" ]; then
  sed -i 's|^VideoPlugin *= *".*"|VideoPlugin = "mupen64plus-video-glide64mk2.so"|' "$CFG"
  sed -i 's|^VSync *= *.*|VSync = 0|; s|^VSyncInterval *= *.*|VSyncInterval = 0|' "$CFG"
  echo "[cfg] plugin line:"; grep -E "^VideoPlugin" "$CFG" | head -2
else
  echo "[cfg] WARN: $CFG missing, using built-in defaults"
fi

stdbuf -oL -eL xvfb-run -a -s "-screen 0 640x480x24" \
  mupen64plus --configdir "$OUT" --sshotdir "$OUT" --testshots "$SHOTS" \
  --noosd --nosaveoptions \
  "$ROM" > "$OUT/stdout.log" 2>&1
RC=$?

PNGS=$(ls "$OUT"/*.png 2>/dev/null | wc -l)
echo "== $ROM rc=$RC shots=$PNGS"
grep -E "\[n64\]|assert|panic|Exception" "$OUT/stdout.log" | head -20
echo "-- log tail --"; tail -3 "$OUT/stdout.log"
