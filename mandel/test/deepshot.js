// stage-Q deep-frame shot: first pert frame can cost 30+ s (~1850 VI at
// 60 Hz) — wait past it before shot 0, then short steps for cached frames.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const W0 = parseInt(ares.args[1] || "2000", 10);
const N  = parseInt(ares.args[2] || "3", 10);
const S  = parseInt(ares.args[3] || "120", 10);
const PFX = ares.args[4] || "ds";
ares.wait(W0);
for (let i = 0; i < N; i++) {
  const shot = ares.screenshot();
  shot.save("test/deep/" + PFX + String(i).padStart(3, "0") + ".png");
  console.log("captured " + PFX + i);
  ares.wait(S);
}
console.log("done");
