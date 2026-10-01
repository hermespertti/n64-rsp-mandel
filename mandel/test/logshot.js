// stage-S capture: waitLog on real probe text (guarantees painted framebuffer
// + live VI since ROM probes AFTER display_show), sequential shots proven to
// work in this JS host (loop+const form silently skipped prints).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
var PFX = ares.args[1] || "ls";
console.log("start " + PFX);
for (var i = 0; i < 3; i++) {
  var ok = ares.waitLog("[probe]", 600);
  console.log("probe-wait " + i + " -> " + ok);
  ares.clearLog();
  try {
    ares.screenshot().save("test/deep/" + PFX + i + ".png");
    console.log("saved " + PFX + i);
  } catch (e) {
    console.log("err " + i + ": " + e);
  }
}
console.log("done");
