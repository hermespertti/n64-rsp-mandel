// stage-G deep capture: ROM booted at START_FRAME near the deep keyframe;
// capture a few frames of the seahorse tail (span 5e-5, CPU double path).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N = parseInt(ares.args[1] || "3", 10);
for (let i = 0; i < N; i++) {
  ares.wait(i === 0 ? 5 : 40);
  const shot = ares.screenshot();
  shot.save("test/deep/g" + i + ".png");
  console.log("captured deep frame " + i);
}
