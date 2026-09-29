// stage-J: C-button preset fly-to test — capture frames mid-fly and landed.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const p1 = ares.controller(1);
ares.wait(30);
const home = ares.screenshot();
home.save("test/hud/j_home.png");
console.log("home " + home.sha256.slice(0, 12));

// fly to TAIL preset (C-Down)
p1.hold("C-Down");
ares.waitVI(2);
p1.release("C-Down");
ares.wait(60);
const mid = ares.screenshot();
mid.save("test/hud/j_fly60.png");
console.log("fly60 " + mid.sha256.slice(0, 12));

ares.wait(200);
const landed = ares.screenshot();
landed.save("test/hud/j_tail.png");
console.log("tail  " + landed.sha256.slice(0, 12));
console.log("distinct:", new Set([home.sha256, mid.sha256, landed.sha256]).size === 3);

// Start resumes tour
p1.hold("Start"); ares.waitVI(2); p1.release("Start");
ares.wait(40);
const tour = ares.screenshot();
tour.save("test/hud/j_tour.png");
console.log("tour  " + tour.sha256.slice(0, 12));
