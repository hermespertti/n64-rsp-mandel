#!/usr/bin/env python3
"""Review video v4 (stage P): progressive deep zoom story.

  seg1  title card, 3 s
  seg2  RSP tour leg: test/tour/t%03d.png @ 6 fps  (~23 s)
  seg3  tail view FIRST frame (quarter, soft): deep/fq0.png, 2.5 s
  seg4  tail view after hold-upgrade (full res, sharp): deep/fq2.png, 3.5 s
  seg5  telemetry card, 5 s
"""
import glob, os, subprocess, sys

B = "/home/lex/n64/mandel/test/"
OUT = "/home/lex/n64/mandel/reviewP.mp4"
TMP = B + "segs/"

def find_font():
    c = glob.glob("/usr/share/fonts/**/DejaVuSans*.ttf", recursive=True)
    return c[0] if c else ""

FONT = find_font()
FOPT = f"fontfile={FONT}:" if FONT else ""

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print("FAIL:", " ".join(cmd[:8]), "...")
        print(r.stderr[-1200:])
    return r.returncode

def card(path, lines, dur, fg="white", size=30):
    dt = []
    for i, ln in enumerate(lines):
        y = 110 + i * (size + 22)
        t = ln.replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'")
        dt.append(f"drawtext={FOPT}text='{t}':x=(w-text_w)/2:y={y}:fontsize={size}:fontcolor={fg}")
    return run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
                "-i", f"color=c=0x0a1428:s=640x480:d={dur}:r=6",
                "-vf", ",".join(dt) + ",format=yuv420p",
                "-c:v", "libx264", "-crf", "20", path])

def still(path, img, dur):
    return run(["ffmpeg", "-y", "-v", "error", "-loop", "1", "-framerate", "6",
                "-t", str(dur), "-i", img, "-c:v", "libx264", "-crf", "20",
                "-pix_fmt", "yuv420p", path])

def main():
    os.makedirs(TMP, exist_ok=True)
    tour = sorted(glob.glob(B + "tour/t*.png"))
    if not tour:
        print("no tour frames"); return 1

    if card(TMP + "s1.mp4", ["MANDEL 64", "", "stage P review",
                             "progressive deep zoom",
                             "quarter-first, sharp on hold"], 3): return 1
    if run(["ffmpeg", "-y", "-v", "error", "-framerate", "6",
            "-i", B + "tour/t%03d.png", "-c:v", "libx264", "-crf", "20",
            "-pix_fmt", "yuv420p", TMP + "s2.mp4"]): return 1
    if still(TMP + "s3.mp4", B + "deep/fq0.png", 2.5): return 1
    if card(TMP + "s3b.mp4", ["first frame: quarter lattice", "830 ms"], 1.5,
            fg="0xffcc66", size=26): return 1
    if still(TMP + "s4.mp4", B + "deep/fq2.png", 3.5): return 1
    if card(TMP + "s4b.mp4", ["held 3 frames -> full-res upgrade",
                              "6.5 s once, then free forever"], 1.5,
            fg="0x66ffcc", size=26): return 1
    if card(TMP + "s5.mp4",
            ["STAGE P RESULTS  (measured, headless)", "",
             "deep first frame: 6533 ms -> 831 ms  (7.8x)",
             "held view: ~11 ms/frame (static cache)",
             "quality: fq keys, bilinear morph, AA",
             "soak: 117 probe windows, mism=0",
             "pixel-exact vs numpy oracle"], 5, fg="0x00ffaa", size=28): return 1

    lst = TMP + "list.txt"
    with open(lst, "w") as f:
        for s in ["s1", "s2", "s3", "s3b", "s4", "s4b", "s5"]:
            f.write(f"file '{TMP}{s}.mp4'\n")
    if run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0",
            "-i", lst, "-c", "copy", OUT]): return 1
    print("wrote", OUT, os.path.getsize(OUT) // 1024, "KB")
    d = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                        "-of", "csv=p=0", OUT], capture_output=True, text=True)
    print("duration", d.stdout.strip())
    return 0

if __name__ == "__main__":
    sys.exit(main())
