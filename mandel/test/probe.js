// mandel RSP probe — decode (0,255,byte) signature from software VI scanout
// fb 320x240 RGBA32 -> shot 640x480 (2x). Probe = 144 bytes at fb rows 0..2, 48 bytes/row.
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

ares.wait(20);
const shot = ares.screenshot();
const px = new Uint8Array(shot.data);
const W = shot.width;
const at = (x, y) => { const i = (y * W + x) * 4; return [px[i], px[i+1], px[i+2]]; };

// probe byte at fb col x, row r -> shot (x*2, r*2)
function rowBytes(r, n) {
  const out = [];
  for (let x = 0; x < n; x++) out.push(at(x * 2, r * 2)[2]);
  return out;
}
const r0 = rowBytes(0, 48), r1 = rowBytes(1, 48), r2 = rowBytes(2, 48);
const u16le = (arr, off) => arr[off] | (arr[off + 1] << 8);
const s16 = (v) => (v >= 0x8000 ? v - 0x10000 : v);

console.log("r0: nc=" + u16le(r0, 0) + " esc=" + [0,1,2,3].map(i => u16le(r0, 16 + i*2)) +
      " one=" + [0,1,2,3].map(i => u16le(r0, 32 + i*2)));
console.log("r1: cr=" + [0,1,2,3].map(i => s16(u16le(r1, i*2))) +
      " ci=" + [0,1,2,3].map(i => s16(u16le(r1, 16 + i*2))) +
      " S=" + [0,1,2,3].map(i => u16le(r1, 32 + i*2)));
console.log("r2: mask=" + [0,1,2,3].map(i => "0x" + u16le(r2, i*2).toString(16)) +
      " cnt=" + [0,1,2,3].map(i => u16le(r2, 16 + i*2)) +
      " outlast=" + [0,1,2,3].map(i => u16le(r2, 32 + i*2)));

// picture mismatch stats (rows 3..239 of fb)
let magenta = 0, total = 0;
for (let y = 6; y < shot.height; y += 2)
  for (let x = 0; x < shot.width; x += 2) {
    const c = at(x, y); total++;
    if (c[0] > 200 && c[2] > 200 && c[1] < 80) magenta++;
  }
console.log("PIC magenta=" + magenta + " / " + total);
shot.save(ares.args[1] || "shot.png");
