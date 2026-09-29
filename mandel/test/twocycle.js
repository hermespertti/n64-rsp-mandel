// stage-L2: two full tour cycles, probe every frame (AUTOTEST build).
// Verdict data: path=cpu morph=1 fraction on cycle 2 == fluid playback.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitLog("[probe]", { timeout: 600000 });
ares.wait(2700);
console.log("two-cycle run done");
