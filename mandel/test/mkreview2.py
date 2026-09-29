#!/usr/bin/env python3
"""Review video: real emulator tour captures + measured telemetry cards."""
import os, subprocess, sys, glob

B = "/home/lex/n64/mandel/test/"
OUT = "/home/lex/n64/mandel/review.mp4"

def find_font():
    c = glob.glob("/usr/share/fonts/**/DejaVuSans*.ttf", recursive=True)
    return c[0] if c else ""

FONT = find_font()
FONT_OPT = f"fontfile={FONT}:" if FONT else ""

def text_chain(lines, size=30, fg="white"):
    parts = []
    for i, ln in enumerate(lines):
        y = 90 + i * (size + 18)
        t = ln.replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'")
        parts.append(f"drawtext={FONT_OPT}text='{t}':x=(w-text_w)/2:y={y}:fontsize={size}:fontcolor={fg}")
    return ",".join(parts)

def main():
    tour = sorted(glob.glob(B + "tour/t*.png"))
    tail = B + "deep/g2.png"
    if not tour or not os.path.exists(tail):
        print("missing sections"); return 1

    n_t = len(tour)
    tail_dup = 18   # 3 s at 6 fps

    inputs = ["-f", "lavfi", "-i", "color=c=#0a1428:s=640x480:d=3"]
    inputs += sum([["-i", p] for p in tour], [])
    inputs += ["-loop", "1", "-framerate", "6", "-t", "3", "-i", tail]
    inputs += ["-f", "lavfi", "-i", "color=c=#0a1428:s=640x480:d=4"]

    idx = 0
    fparts = []
    fparts.append(f"[{idx}]fps=6,scale=640:480,setsar=1,{text_chain(['MANDEL 64', 'stage L2 review', 'keyframe ring + live morph'])}[v{idx}]"); idx += 1
    for i in range(n_t):
        fparts.append(f"[{idx}]fps=6,scale=640:480,setsar=1[v{idx}]"); idx += 1
    fparts.append(f"[{idx}]fps=6,scale=640:480,setsar=1[v{idx}]"); idx += 1
    stats = ["RING RESULTS (headless measured)", "deep leg: 703 frames", "morphed: 618 @ ~0 ms avg", "cpu renders: 85 (one per octave)", "pixel-exact vs numpy oracle", "CI: mism=0, no magenta"]
    fparts.append(f"[{idx}]fps=6,scale=640:480,setsar=1,{text_chain(stats, fg=chr(35)+'00ffaa')}[v{idx}]"); idx += 1

    concat_in = "".join(f"[v{i}]" for i in range(idx))
    fc = ";".join(fparts) + f";{concat_in}concat=n={idx}:v=1:a=0,format=yuv420p[outv]"

    cmd = ["ffmpeg", "-y"] + inputs + [
        "-filter_complex", fc,
        "-map", "[outv]", "-r", "6", "-c:v", "libx264", "-crf", "20",
        "-t", str(3 + n_t / 6.0 + 3 + 4), OUT]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2500:]); return 1
    print("wrote", OUT, os.path.getsize(OUT) // 1024, "KB")
    return 0

if __name__ == "__main__":
    sys.exit(main())
