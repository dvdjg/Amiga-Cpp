#!/usr/bin/env node
// Compara dos secuencias por fase (cada frame contra el mejor frame de la otra,
// max IoU + MAD de color en esa alineacion). Gate estructural de optimizaciones.
//
// Uso:
//   node tools/analyze/phasecmp.mjs <dirA> <dirB>
//
// Ejemplo:
//   node tools/analyze/phasecmp.mjs out/run/116/A500_debug/sequence out/tmp/116_ref
import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const dirA = process.argv[2];
const dirB = process.argv[3];
if (!dirA || !dirB) {
  console.error('Uso: node tools/analyze/phasecmp.mjs <dirA> <dirB>');
  process.exit(1);
}

const load = (f) => PNG.sync.read(fs.readFileSync(f));
const bgOf = (img) => {
  const fr = new Map();
  for (let i = 0; i < img.data.length; i += 4) {
    const c = (img.data[i] << 16) | (img.data[i + 1] << 8) | (img.data[i + 2]);
    fr.set(c, (fr.get(c) || 0) + 1);
  }
  let bg = 0, b = -1;
  for (const [c, n] of fr) if (n > b) { b = n; bg = c; }
  return bg;
};
const dirOf = (d) => fs.readdirSync(d).filter((f) => /frame_\d+\.png$/.test(f)).sort().map((f) => load(`${d}/${f}`));
const cmp = (a, b) => {
  const bga = bgOf(a), bgb = bgOf(b);
  let inter = 0, uni = 0, diff = 0, n = 0;
  for (let p = 0; p < a.width * a.height; p++) {
    const i = p * 4;
    const ca = (a.data[i] << 16) | (a.data[i + 1] << 8) | (a.data[i + 2]);
    const cb = (b.data[i] << 16) | (b.data[i + 1] << 8) | (b.data[i + 2]);
    const ma = ca !== bga, mb = cb !== bgb;
    if (ma && mb) {
      inter++;
      diff += (Math.abs(a.data[i] - b.data[i]) + Math.abs(a.data[i + 1] - b.data[i + 1]) + Math.abs(a.data[i + 2] - b.data[i + 2])) / 3;
      n++;
    }
    if (ma || mb) uni++;
  }
  return { iou: uni ? inter / uni : 0, mad: n ? diff / n : 999 };
};

const da = dirOf(dirA), db = dirOf(dirB);
console.log(`A frames=${da.length} B frames=${db.length}`);
for (let i = 0; i < da.length; i++) {
  let best = { iou: -1, mad: 999 }, bi = -1;
  for (let j = 0; j < db.length; j++) {
    const r = cmp(da[i], db[j]);
    if (r.iou > best.iou) { best = r; bi = j; }
  }
  console.log(`A[${i}] -> B[${bi}] IoU=${best.iou.toFixed(3)} MAD=${best.mad.toFixed(1)}`);
}
