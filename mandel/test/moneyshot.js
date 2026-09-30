// stage-Q money shot: long CPU frames (30s pert) leave VI scanning a stable
// buffer -> shots during them come back solid; fast frames race mid-scanout.
// Fire several shots at varying offsets and let the harness pick solid ones.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N   = parseInt(ares.args[1] || "4", 10);
const S   = parseInt(ares.args[2] || "1", 10);   // sim-frames between shots
const PFX = ares.args[3] || "ms";
for (let i = 0; i < N; i++) {
  ares.wait(i === 0 ? 2 : S);
  try {
    const shot = ares.screenshot();
    shot.save("test/deep/" + PFX + String(i).padStart(3, "0") + ".png");
    console.log("captured " + PFX + i);
  } catch (e) { console.log("shot " + i + " failed: " + e); }
}
console.log("done");
