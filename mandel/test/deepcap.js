// stage-G deep capture: ROM booted at START_FRAME near the deep keyframe;
// capture a few frames of the seahorse tail (span 5e-5, CPU double path).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N = parseInt(ares.args[1] || "3", 10);
const PFX = ares.args[2] || "g";
// Probe fires post-flip now, but the first two flips still race the runner;
// skip early probes and tolerate screenshot failures until VI is stable.
let shot = 0, guard = 0;
while (shot < N && guard < N + 60) {
  ares.waitLog("[probe]", { timeout: 600000 });
  guard++;
  if (guard <= 2) continue;
  try {
    const s = ares.screenshot();
    s.save("test/deep/" + PFX + shot + ".png");
    console.log("captured deep frame " + shot);
    shot++;
  } catch (e) {
    console.log("shot skipped: " + e.message);
  }
}
