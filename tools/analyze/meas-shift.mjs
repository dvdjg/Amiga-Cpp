import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => f.endsWith('.png')).sort();
const A = PNG.sync.read(fs.readFileSync(path.join(dir, files[0])));
const B = PNG.sync.read(fs.readFileSync(path.join(dir, files[1])));
const W = A.width, H = A.height;
const lum = (im, x, y) => { const i = (y * W + x) * 4; return (im.data[i] + im.data[i + 1] + im.data[i + 2]) / 3; };
// correlación en la mitad derecha (lejos del HUD izquierdo)
const X0 = 300, Y0 = 100, X1 = 700, Y1 = 400;
let best = { dx: 0, dy: 0, score: 1e18 };
for (let dy = -2; dy <= 2; dy++) for (let dx = -6; dx <= 6; dx++) {
  let s = 0, cnt = 0;
  for (let y = Y0; y < Y1; y += 2) for (let x = X0; x < X1; x += 2) {
    const bx = x - dx, by = y - dy;
    if (bx < X0 || by < Y0 || bx >= X1 || by >= Y1) continue;
    s += Math.abs(lum(A, x, y) - lum(B, bx, by)); cnt++;
  }
  if (s / cnt < best.score) best = { dx, dy, score: s / cnt };
}
console.log('frame0->1 shift (dx,dy) score', best.dx, best.dy, best.score.toFixed(2));
console.log('(1px por frame con 20ms a 50fps; 2px sugiere doble scroll)');
