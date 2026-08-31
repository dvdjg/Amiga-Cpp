import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => /^frame_\d{3}\.png$/.test(f)).sort();
const load = (f) => PNG.sync.read(fs.readFileSync(path.join(dir, f)));
function charFor(r, g, b) {
  const lum = (r + g + b) / 3;
  if (lum < 24) return ' ';
  if (lum < 60) return '.';
  if (lum < 110) return 'o';
  if (lum < 180) return 'O';
  return '#';
}
// Grid 16px: por cada celda 16x16, estadístico → muestra la estructura de tiles.
function gridArt(png, cols, rows) {
  let out = '';
  for (let gy = 0; gy < rows; ++gy) {
    for (let gx = 0; gx < cols; ++gx) {
      let sum = 0, n = 0;
      for (let y = gy * 16; y < Math.min((gy + 1) * 16, png.height); ++y)
        for (let x = gx * 16; x < Math.min((gx + 1) * 16, png.width); ++x) {
          const i = (y * png.width + x) * 4;
          sum += (png.data[i] + png.data[i + 1] + png.data[i + 2]) / 3; n++;
        }
      out += charFor(sum / n, sum / n, sum / n);
    }
    out += '\n';
  }
  return out;
}
const warns = (f1, f2, tag) => {
  // número de celdas 16px cuya luminosidad cambia >40 entre frames consecutivos
  let ch = 0, total = 0;
  for (let gy = 0; gy < f1.height / 16; ++gy)
    for (let gx = 0; gx < f1.width / 16; ++gx) {
      let a = 0, b = 0, n = 0;
      for (let y = gy * 16; y < (gy + 1) * 16; ++y)
        for (let x = gx * 16; x < (gx + 1) * 16; ++x) {
          const i = (y * f1.width + x) * 4, j = (y * f2.width + x) * 4;
          a += (f1.data[i] + f1.data[i + 1] + f1.data[i + 2]) / 3;
          b += (f2.data[j] + f2.data[j + 1] + f2.data[j + 2]) / 3; n++;
        }
      if (Math.abs(a / n - b / n) > 40) ch++; total++;
    }
  console.log(`[${tag}] celdas ${ch}/${total} cambiaron >40 entre consecutivos`);
};
const p = files.map(load);
// Vista ASCII de la mitad (cada 2ª fila) de 2 frames lejanos
for (const k of [0, files.length - 1]) {
  console.log(`--- frame ${files[k]} (${p[k].width}x${p[k].height}) ---`);
  console.log(gridArt(p[k], 20, 12));
}
for (let i = 1; i < p.length; ++i) warns(p[i - 1], p[i], `F${i}`);