import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => /^frame_\d{3}\.png$/.test(f)).sort();
const load = (f) => PNG.sync.read(fs.readFileSync(path.join(dir, f)));
// Localiza la banda del playfield (filas con contenido no-negro) en la 1ª frame.
const p0 = load(files[0]);
const rowLum = (p, y) => { let s = 0; for (let x = 0; x < p.width; x += 4) { const i = (y * p.width + x) * 4; s += (p.data[i] + p.data[i + 1] + p.data[i + 2]) / 3; } return s / Math.ceil(p.width / 4); };
let top = -1, bot = -1;
for (let y = 0; y < p0.height; y++) { const l = rowLum(p0, y); if (l > 16) { if (top < 0) top = y; bot = y; } }
console.log(`playfield filas: ${top}..${bot} (${bot - top + 1}px)`);
const rowsTotal = bot - top + 1;
const bands = 16;
// Para cada frame: luminancia de la fila inferior (22% final del playfield) y de la
// franja de staging (por encima del playfield), y nº de filas del playfield negras.
for (const f of files) {
  const p = load(f);
  const band = {};
  for (let b = 0; b < bands; b++) { let s = 0, n = 0; const y0 = top + Math.floor(b * rowsTotal / bands), y1 = top + Math.floor((b + 1) * rowsTotal / bands); for (let y = y0; y < y1; y++) { s += rowLum(p, y); n++; } band[b] = s / n; }
  const blackRows = (() => { let c = 0; for (let y = top; y <= bot; y++) if (rowLum(p, y) < 12) c++; return c; })();
  console.log(`${f}: bottom22=${band[bands - 1].toFixed(0)} mid=${band[Math.floor(bands / 2)].toFixed(0)} top=${band[0].toFixed(0)} filasNegras=${blackRows}`);
}