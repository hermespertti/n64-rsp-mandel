// A/B review capture v3: onLog streams probe lines; each screenshot is
// paired with the probe line of the frame it shows (VI-present lag ~2).
// Usage: ares-test abcap3.js rom.z64 <marker> <N> <outprefix>
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const MARK = ares.args[1] || "[probe]";
const N = parseInt(ares.args[2] || "6", 10);
const PFX = ares.args[3] || "iv_";

let queue = [];          // probe lines as they arrive
let markSeen = false;

ares.onLog((line) => {
  const t = String(line);
  if (t.indexOf("[probe]") === 0) {
    queue.push(t);
    if (queue.length > 64) queue.shift();
    if (!markSeen && t.indexOf(MARK) >= 0) markSeen = true;
  }
});

let shot = 0, guard = 0;
while (shot < N && guard < N * 5 + 60) {
  guard++;
  const ok = ares.waitLog("[probe]", { timeout: 600000 });
  if (!ok) break;
  if (!markSeen) continue;
  if (queue.length < 14) continue;   // VI settle: ~14 frames buffered ahead
  const shown = queue[queue.length - 13];   // the frame VI is presenting now
  try {
    const s = ares.screenshot();
    s.save("test/deep/" + PFX + shot + ".png");
    console.log("SHOTS:" + PFX + shot + "|" + shown);
    shot++;
  } catch (e) {
    console.log("retry: " + e.message);
  }
}
console.log("abcap done shots=" + shot);
