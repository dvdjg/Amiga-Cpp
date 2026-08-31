import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => f.endsWith('.png')).sort();
const imgs = files.slice(0, 8).map(f => PNG.sync.read(fs.readFileSync(path.join(dir, f))));
const W = imgs[0].width, H = imgs[0].height;
console.log('frames', imgs.length, 'size', W + 'x' + H);
const lum = (im, x, y) => {
  const i = (y * W + x) * 4;
  return (im.data[i] + im.data[i + 1] + im.data[i + 2]) / 3;
};

// Región de correlación alejada del HUD (mitad derecha, centro vertical)
const X0 = 420, Y0 = 100, X1 = 700, Y1 = 380;

function bestShift(a, b) {
  let best = { dx: 0, dy: 0, score: 1e18 };
  for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) {
    let s = 0, cnt = 0;
    for (let y = Y0; y < Y1; y += 2) for (let x = X0; x < X1; x += 2) {
      const bx = x - dx, by = y - dy;
      if (bx < X0 || by < Y0 || bx >= X1 || by >= Y1) continue;
      s += Math.abs(lum(a, x, y) - lum(b, bx, by)); cnt++;
    }
    const score = s / cnt;
    if (score < best.score) best = { dx, dy, score };
  }
  return best;
}

// diff entre frames adyacentes alineados: el mapa cancela, el HUD (fijo) queda
function showDiff(a, b, dx, dy) {
  const step = 2;
  for (let y = 30; y < 80; y += step) {
    let row = '';
    for (let x = 70; x < 200; x += step) {
      let d = 0, cnt = 0;
      for (let yy = 0; yy < step; yy++) for (let xx = 0; xx < step; xx++) {
        const bx = x + xx + dx, by = y + yy + dy;
        if (bx < 0 || by < 0 || bx >= W || by >= H) continue;
        d += Math.abs(lum(a, x + xx, y + yy) - lum(b, bx, by)); cnt++;
      }
      row += d / cnt > 10 ? '#' : '.';
    }
    console.log(String(y).padStart(3) + ' ' + row);
  }
}

for (let i = 0; i + 1 < imgs.length && i < 3; i++) {
  const s = bestShift(imgs[i], imgs[i + 1]);
  console.log('=== frame ' + i + ' -> ' + (i + 1) + ': shift', s.dx, s.dy, 'score', s.score.toFixed(3), '===');
  showDiff(imgs[i], imgs[i + 1], s.dx, s.dy);
}
