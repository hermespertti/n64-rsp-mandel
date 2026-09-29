// stage-H input test: verify pad pan/zoom changes the view.
// Capture frames before/after stick+Z holds and compare hashes.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const p1 = ares.controller(1);
const press = (btn) => { p1.hold(btn); ares.waitVI(2); p1.release(btn); ares.waitVI(2); };

ares.wait(30);
const home = ares.screenshot();
console.log("home sha=" + home.sha256);

// zoom in with Z for ~200 simulated frames
p1.hold("Z");
ares.wait(200);
p1.release("Z");
ares.wait(10);
const zin = ares.screenshot();
console.log("z-in sha=" + zin.sha256 + " changed=" + (zin.sha256 !== home.sha256));

// pan with stick
p1.stick(0.9, 0.0);
ares.wait(120);
const panR = ares.screenshot();
console.log("pan-R sha=" + panR.sha256 + " changed=" + (panR.sha256 !== zin.sha256));
p1.stick(0, 0);

// d-pad down+right
p1.hold("Down"); p1.hold("Right");
ares.wait(80);
p1.release("Down"); p1.release("Right");
ares.wait(10);
const dpan = ares.screenshot();
console.log("dpad sha=" + dpan.sha256 + " changed=" + (dpan.sha256 !== panR.sha256));

// Start resumes the tour: after start, view should jump back to keyframe path
press("Start");
ares.wait(30);
const tour = ares.screenshot();
console.log("tour sha=" + tour.sha256);
tour.save("test/h_tour.png");
dpan.save("test/h_dpan.png");
zin.save("test/h_zin.png");
home.save("test/h_home.png");
console.log("all distinct:", new Set([home.sha256, zin.sha256, panR.sha256, dpan.sha256]).size === 4);
