// stage-I tour capture: every 15th frame through the full zoom tour cycle
// (RSP path + CPU deep path). ~90 frames for the MP4 build.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const STEP = 15;
const N = parseInt(ares.args[1] || "92", 10);
for (let i = 0; i < N; i++) {
  ares.wait(i === 0 ? 4 : STEP);
  const shot = ares.screenshot();
  shot.save("test/tour/t" + String(i).padStart(3, "0") + ".png");
  if (i % 10 === 0) console.log("tour frame " + (i * STEP));
}
console.log("done");
