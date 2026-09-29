// A/B review capture: waits for a probe matching a marker substring, then
// screenshots N frames, one per probe line (with VI settle retries).
// Usage: ares-test abcap.js rom.z64 <marker> <N> <outprefix>
//   e.g. ares-test test/abcap.js mandel.z64 "path=rsp" 8 iv_ --timeout 400
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const MARK = ares.args[1] || "[probe]";
const N = parseInt(ares.args[2] || "6", 10);
const PFX = ares.args[3] || "iv_";

try { ares.waitLog(MARK, { timeout: 600000 }); } catch (e) { console.log("mark wait failed: " + e.message); }

let shot = 0, guard = 0;
while (shot < N && guard < N * 4 + 40) {
  guard++;
  try {
    ares.waitLog("[probe]", { timeout: 600000 });
  } catch (e) { break; }
  if (guard <= 12) continue;   // let VI settle (software scanout needs many frames)
  try {
    ares.waitVI(2, { timeout: 600000 });  // VI must present AFTER the flip we probed
    const s = ares.screenshot();
    s.save("test/deep/" + PFX + shot + ".png");
    console.log("captured " + PFX + shot);
    shot++;
  } catch (e) {
    console.log("retry: " + e.message);
  }
}
console.log("abcap done shots=" + shot);
