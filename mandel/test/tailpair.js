// stage-P tail-pair capture: same proven wait-stepping as tourcap.js
// (waitLog-based shots raced the VI and came back black). ROM is booted
// via START_FRAME into the tail view; STEP sim frames between shots.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const STEP = parseInt(ares.args[1] || "2", 10);
const N = parseInt(ares.args[2] || "6", 10);
const PFX = ares.args[3] || "qtr";
for (let i = 0; i < N; i++) {
  ares.wait(i === 0 ? 4 : STEP);
  const shot = ares.screenshot();
  shot.save("test/deep/" + PFX + String(i).padStart(3, "0") + ".png");
  if (i % 2 === 0) console.log("captured " + PFX + i + " sim-frame-ish");
}
console.log("done");
