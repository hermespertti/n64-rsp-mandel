// stage-G deep capture: ROM booted at START_FRAME near the deep keyframe;
// capture a few frames of the seahorse tail (span 5e-5, CPU double path).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N = parseInt(ares.args[1] || "3", 10);
// Deep CPU frames exceed any sane VI-wait; gate on the ROM's own probe line.
// First shot after TWO probes (probe N+1 means frame N has flipped to VI);
// after that one probe per frame is safe.
for (let i = 0; i < N; i++) {
  ares.waitLog("[probe]", { timeout: 600000 });
  if (i === 0) ares.waitLog("[probe]", { timeout: 600000 });
  const shot = ares.screenshot();
  shot.save("test/deep/g" + i + ".png");
  console.log("captured deep frame " + i);
}
