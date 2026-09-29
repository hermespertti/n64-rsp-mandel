// ci.js — CI probe: run frames, read ISV probe lines + screenshot, report.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

// let the ROM produce several probe windows (frame%60 each)
ares.wait(parseInt(ares.args[1] || "240", 10));
const shot = ares.screenshot();
const px = new Uint8Array(shot.data);
const W = shot.width, H = shot.height;

// independent pixel check: verify-paint is bright magenta (255,0,255)
let magenta = 0, total = 0;
for (let y = 0; y < H; y += 2)
  for (let x = 0; x < W; x += 2) {
    const i = (y * W + x) * 4;
    total++;
    if (px[i] > 200 && px[i + 1] < 60 && px[i + 2] > 200) magenta++;
  }
console.log("PIC mismatch=" + magenta + " / " + total);
if (magenta === 0) console.log("CI_PASS");
else console.log("CI_FAIL magenta=" + magenta);
