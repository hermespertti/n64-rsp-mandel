// stage-J debug v5: learn the fork's button-name → SI-bit mapping.
// Hold each directional name for ~150 VI (≥1 probe window), then release.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const p1 = ares.controller(1);
const seq = ["Up", "Down", "Left", "Right", "C-Up", "C-Down", "C-Left", "C-Right"];
for (const b of seq) {
  p1.hold(b);
  ares.waitVI(150);
  p1.release(b);
  ares.waitVI(5);
  console.log("held " + b);
}
console.log("done");
