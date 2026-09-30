#!/usr/bin/env python3
"""Review video v4 (stage P): progressive deep zoom, honest story.

  seg1  title card, 3 s
  seg2  tour leg: test/tour/t%03d.png @ 6 fps  (~23 s)
  seg3  tail first frame (quarter, 831 ms), 2.5 s
  seg4  caption 1 s
  seg5  crossfade quarter -> full-res upgrade (fq), 3 s
  seg6  caption 1.5 s
  seg7  telemetry card, 5 s
"""
import glob, os, subprocess, sys

B = "/home/lex/n64/mandel/test/"
OUT = "/home/lex/n64/mandel/reviewP.mp4"
TMP = B + "segs/"
FPS = 6

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
        t = ln.replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'").replace("%", "\\\\%")
        dt.append(f"drawtext={FOPT}text='{t}':x=(w-text_w)/2:y={y}:fontsize={size}:fontcolor={fg}")
    return run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
                "-i", f"color=c=0x0a1428:s=640x480:d={dur}:r={FPS}",
                "-vf", ",".join(dt) + ",format=yuv420p",
                "-c:v", "libx264", "-crf", "20", path])

def main():
    os.makedirs(TMP, exist_ok=True)
    tour = sorted(glob.glob(B + "tour/t*.png"))
    if not tour:
        print("no tour frames"); return 1

    if card(TMP + "s1.mp4", ["MANDEL 64", "", "stage P review",
                             "progressive deep zoom",
                             "first frame in 0.8 s, sharp on hold"], 3): return 1
    if run(["ffmpeg", "-y", "-v", "error", "-framerate", str(FPS),
            "-i", B + "tour/t%03d.png", "-c:v", "libx264", "-crf", "20",
            "-pix_fmt", "yuv420p", TMP + "s2.mp4"]): return 1

    # tail: quarter first frame
    if run(["ffmpeg", "-y", "-v", "error", "-loop", "1", "-framerate", str(FPS),
            "-t", "2.5", "-i", B + "deep/soft001.png",
            "-vf", "format=yuv420p", "-c:v", "libx264", "-crf", "20",
            TMP + "s3.mp4"]): return 1
    if card(TMP + "s4.mp4", ["first frame: quarter lattice, 831 ms",
                             "(old full-res pass: 6533 ms)"], 1,
            fg="0xffcc66", size=26): return 1
    # crossfade quarter -> fq sharp
    if run(["ffmpeg", "-y", "-v", "error",
            "-loop", "1", "-framerate", str(FPS), "-t", "3", "-i", B + "deep/soft001.png",
            "-loop", "1", "-framerate", str(FPS), "-t", "3", "-i", B + "deep/fq004.png",
            "-filter_complex",
            "[0][1]xfade=transition=dissolve:duration=1.5:offset=1.2,format=yuv420p",
            "-c:v", "libx264", "-crf", "20", TMP + "s5.mp4"]): return 1
    if card(TMP + "s6.mp4", ["held 3 frames -> full-res upgrade",
                             "pixels differ only 0.07% - view was already right",
                             "then free forever (~11 ms/frame cache)"], 1.5,
            fg="0x66ffcc", size=24): return 1
    if card(TMP + "s7.mp4",
            ["STAGE P RESULTS  (measured, headless ares)", "",
             "deep first frame: 6533 ms -> 831 ms  (7.8x)",
             "held view display: ~11 ms/frame",
             "bilinear AA morph + fq key upgrades",
             "soak: 117 probe windows, mism=0",
             "pixel-exact vs numpy oracle"], 5, fg="0x00ffaa", size=28): return 1

    lst = TMP + "list.txt"
    with open(lst, "w") as f:
        for s in ["s1", "s2", "s3", "s4", "s5", "s6", "s7"]:
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
