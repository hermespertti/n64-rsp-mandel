// stage-E zoom tour capture: snapshot at intervals through the keyframe cycle
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N = parseInt(ares.args[1] || "36", 10);
const STEP = parseInt(ares.args[2] || "12", 10);  // frames between shots

for (let i = 0; i < N; i++) {
  ares.wait(STEP);
  const shot = ares.screenshot();
  shot.save("test/zoom/z" + String(i).padStart(3, "0") + ".png");
  console.log("captured z" + i);
}
console.log("DONE");
