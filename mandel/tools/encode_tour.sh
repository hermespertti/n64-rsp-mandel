#!/usr/bin/env bash
# encode_tour.sh — assemble test/tour/*.png (captured every 15 sim frames)
# into a smooth 30 fps H.264 MP4 with motion interpolation.
set -euo pipefail
cd "$(dirname "$0")/.."

N=$(ls test/tour/t*.png 2>/dev/null | wc -l)
if [ "$N" -lt 10 ]; then
    echo "need >=10 tour frames in test/tour/, got $N (run test/tourcap.js first)"
    exit 1
fi

# source rate: 30 fps / 15-frame spacing = 2 fps keyed on sim time;
# minterpolate 2 -> 30 with mci blends the geometric zoom smoothly.
ffmpeg -y -framerate 2 -i test/tour/t%03d.png \
    -vf "scale=640:480:flags=lanczos,minterpolate=fps=30:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1" \
    -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p \
    docs_shots/zoom_tour.mp4

ls -la docs_shots/zoom_tour.mp4
