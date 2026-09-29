#!/usr/bin/env python3
"""Check HUD bar painted at top of captured frame."""
import sys
from collections import Counter
from PIL import Image

path = sys.argv[1] if len(sys.argv) > 1 else "test/hud/hud_home.png"
im = Image.open(path).convert("RGB")
px = im.load()
c = Counter(px[x, y] for y in range(0, 45, 3) for x in range(0, im.width, 3))
print(path, "top colors:", c.most_common(6))
white = sum(v for k, v in c.items() if k[0] > 200 and k[1] > 200 and k[2] > 200)
teal = sum(v for k, v in c.items() if k[1] > 200 and k[2] > 150 and k[0] < 100)
black = c.get((0, 0, 0), 0)
print(f"white={white} teal={teal} black={black} total={sum(c.values())}")
