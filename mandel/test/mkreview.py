#!/usr/bin/env python3
"""Review video from abcap captures: RSP tour leg, CPU deep renders,
RING MORPH playback. Caption bars label each segment + per-shot index.

Sequence = capture order per window (abcap screenshots successive probe
lines, so shots are consecutive tour frames). Order: iv_ (RSP leg), then
cp_ (CPU deep), then mr_ (ring morph playback -- the payoff).
"""
import glob, os, re, subprocess, sys

B = "/home/lex/n64/mandel/test/"
DEEP = B + "deep/"
OUT = B + "review/mandel_review.mp4"
FPS = 6

WINDOWS = [
    ("iv2_", "RSP vector leg (fast path)", "abIv2"),
    ("cp2_", "CPU deep render (per octave)", "abCp2"),
    ("mr2_", "RING MORPH playback (full fps)", "abMr2"),
]

def shots(pfx, lg):
    """shot pngs + probe captions: pair each 'captured' event with the most
    recent preceding [probe] line (logs interleave in real time)."""
    txt = ""
    p = B + lg + ".log"
    if os.path.exists(p):
        txt = open(p, "rb").read().decode("utf-8", "replace")
    out = []
    last = None
    for line in txt.splitlines():
        m = re.search(r"\[probe\] f=(\d+) .*?span=(\S+) path=(\w+).*?morph=(\d+)", line)
        if m:
            last = m
            continue
        c = re.search(r"captured %s(\d+)" % pfx, line)
        if c and last:
            idx = int(c.group(1))
            png = DEEP + pfx + str(idx) + ".png"
            f, span, path, morph = last.groups()
            mode = "CPU render" if path == "cpu" else "RSP"
            if morph == "1": mode = "RING MORPH"
            if os.path.exists(png):
                out.append((png, f"f{f} span {span} [{mode}]"))
    return out

def main():
    seq = []
    for pfx, label, lg in WINDOWS:
        for p, cap in shots(pfx, lg):
            seq.append((p, label, cap))
    if len(seq) < 2:
        print("too few shots:", len(seq)); return 1
    print(f"{len(seq)} frames")

    os.makedirs(B + "review", exist_ok=True)
    inputs, filters = [], []
    for i, (p, label) in enumerate(seq):
        inputs += ["-i", p]
        idx = os.path.basename(p)[:-4]
        txt = f"{idx}  {label}".replace(":", "\\:")
        filters.append(
            f"[{i}:v]scale=640:480,"
            f"drawtext=text='{txt}':x=8:y=452:fontsize=22:fontcolor=white:"
            f"box=1:boxcolor=black@0.65:boxborderw=5")
    concat = "".join(f"[{i}]" for i in range(len(seq)))
    cmd = ["ffmpeg", "-y"] + inputs + [
        "-filter_complex", ";".join(filters) + f";{concat}concat=n={len(seq)}:v=1:a=0[outv]",
        "-map", "[outv]", "-r", str(FPS), "-c:v", "libx264",
        "-crf", "20", "-pix_fmt", "yuv420p", OUT]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2000:]); return 1
    sz = os.path.getsize(OUT)
    print(f"wrote {OUT} ({sz//1024} KB)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
