// stage-P deep capture, black-proof: waits for probes, screenshots, and
// RETRIES shots whose PNG came back solid-black (VI mid-flip race). A real
// mandelbrot frame compresses to way more than 20 KB; black = tiny PNG.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const N = parseInt(ares.args[1] || "3", 10);
const PFX = ares.args[2] || "g";
let shot = 0, guard = 0;
while (shot < N && guard < N * 8 + 40) {
  ares.waitLog("[probe]", { timeout: 600000 });
  guard++;
  let ok = false;
  for (let tryI = 0; tryI < 6 && !ok; tryI++) {
    try {
      const s = ares.screenshot();
      const path = "test/deep/" + PFX + shot + ".png";
      s.save(path);
      const sz = ares.fileSize ? ares.fileSize(path) : 0;
      if (sz && sz < 20480) {
        console.log("black retry " + tryI + " size=" + sz);
      } else {
        console.log("captured " + path + " size=" + (sz || "?"));
        ok = true;
      }
    } catch (e) {
      console.log("shot skipped: " + e.message);
      ares.wait(2);
    }
  }
  if (ok) shot++;
}
console.log("done shots=" + shot);
