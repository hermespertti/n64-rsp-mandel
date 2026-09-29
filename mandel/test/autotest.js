// stage-J AUTOTEST verification: scripted fly_req cycle (preset every 20
// game frames). wait(n) = n game frames (proven pattern). Probes emit every
// frame in AUTOTEST builds — the cx/cy/span/name trail proves the fly path.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

ares.wait(25);   // covers HOME hop + SEAHORSE hop
const s1 = ares.screenshot(); s1.save("test/hud/jt_seahorse.png");
ares.wait(25);   // covers TAIL hop (deep CPU leg, slow)
const s2 = ares.screenshot(); s2.save("test/hud/jt_tail.png");
ares.wait(25);   // VALLEY hop + Start resume → TOUR
const s3 = ares.screenshot(); s3.save("test/hud/jt_tour.png");
console.log("autotest done");
