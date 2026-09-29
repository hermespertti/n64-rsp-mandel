// dump raw (0,255,B) signature geometry from scanout shot
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
ares.wait(20);

const shot = ares.screenshot();
const px = new Uint8Array(shot.data);
const W = shot.width, H = shot.height;

const rows = {};
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    if (px[i] === 0 && px[i+1] === 255) {
      (rows[y] = rows[y] || []).push([x, px[i+2]]);
    }
  }
}
const ys = Object.keys(rows).map(Number).sort((a,b)=>a-b);
console.log("sig rows: " + JSON.stringify(ys.slice(0,20)));
for (const y of ys.slice(0, 8)) {
  const r = rows[y];
  console.log("y=" + y + " n=" + r.length + " x0=" + r[0][0] + " vals=" + r.map(v=>v[1]).join(","));
}

let firstM = -1, firstPal = -1; const palSamples = [];
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    const R = px[i], G = px[i+1], B = px[i+2];
    if (R > 200 && B > 200 && G < 80 && firstM < 0) firstM = y * W + x;
    const isBlack = R < 16 && G < 16 && B < 16;
    const isSig = R === 0 && G === 255;
    if (!isBlack && !isSig && !(R > 200 && B > 200 && G < 80)) {
      if (firstPal < 0) firstPal = y * W + x;
      if (palSamples.length < 12 && (y * W + x) % (W * 8) === 0) palSamples.push([x, y, R, G, B]);
    }
  }
}
console.log("firstMagenta " + (firstM >= 0 ? (firstM % W) + "," + Math.floor(firstM / W) : "-"));
console.log("firstPalette " + (firstPal >= 0 ? (firstPal % W) + "," + Math.floor(firstPal / W) : "-"));
console.log("palSamples=" + JSON.stringify(palSamples));
shot.save(ares.args[1] || "shot.png");
