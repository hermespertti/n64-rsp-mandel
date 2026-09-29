// A/B review capture v4: content-checked screenshots.
// - rejects pure black (VI mid-flip) via compare against black640.png
// - for motion windows: rejects frames identical to the previous accepted shot
// Usage: ares-test abcap4.js rom.z64 <marker> <N> <outprefix> <gapCheck 0|1>
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const MARK = ares.args[1] || "[probe]";
const N = parseInt(ares.args[2] || "6", 10);
const PFX = ares.args[3] || "iv_";
const GAP = parseInt(ares.args[4] || "0", 10) === 1;

let lastProbe = "";
ares.onLog((line) => {
  const t = String(line);
  if (t.indexOf("[probe]") === 0) lastProbe = t.trim();
});

let markSeen = false;
let shot = 0, guard = 0;
while (shot < N && guard < N * 60 + 400) {
  guard++;
  if (!markSeen) {
    if (!ares.waitLog(MARK, { timeout: 600000 })) break;
    markSeen = true;
    continue;
  }
  if (!ares.waitLog("[probe]", { timeout: 600000 })) break;
  try {
    const s = ares.screenshot();
    const black = s.compare("test/black640.png", 8);
    if (black.diffPixels < black.totalPixels * 0.3) continue;   // mid-flip black
    if (GAP && shot > 0) {
      const prev = s.compare("test/deep/" + PFX + (shot - 1) + ".png", 8);
      if (prev.diffPixels < prev.totalPixels * 0.02) continue;  // stale frame
    }
    s.save("test/deep/" + PFX + shot + ".png");
    console.log("SHOTS:" + PFX + shot + "|" + lastProbe);
    shot++;
  } catch (e) {
    console.log("retry: " + e.message);
  }
}
console.log("abcap4 done shots=" + shot);
