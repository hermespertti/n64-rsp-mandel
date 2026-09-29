// stage-J debug v3: hold C-Right + stick permanently from boot; every probe
// window must report non-zero btn/sx once SI polling delivers injected input.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

const p1 = ares.controller(1);
p1.hold("C-Right");
p1.stick(0.75, 0);
ares.waitVI(4000);
console.log("done holding");
