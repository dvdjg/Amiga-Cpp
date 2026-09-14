#!/usr/bin/env node
// Compara cada frame nuestro con el MEJOR frame del original por fase (max IoU),
// y reporta la MAD de color en esa alineacion. Gate estructural de optimizaciones.
//
// Uso:
//   node tools/analyze/bestphase.mjs <dir_nuestro> <dir_original> [count]
//
// Ejemplo:
//   node tools/analyze/bestphase.mjs out/run/116/A500_debug/sequence out/tmp/116_ref 4
import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const ours = process.argv[2];
const origDir = process.argv[3];
const count = parseInt(process.argv[4] || '8', 10);
if (!ours || !origDir) {
  console.error('Uso: node tools/analyze/bestphase.mjs <dir_nuestro> <dir_original> [count]');
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
const origs = fs.readdirSync(origDir).filter((f) => /frame_\d+\.png$/.test(f)).sort().map((f) => load(`${origDir}/${f}`));
if (origs.length === 0) { console.error('sin frames originales en ' + origDir); process.exit(1); }
for (let k = 0; k < count; k++) {
  const fname = `${ours}/frame_${String(k).padStart(3, '0')}.png`;
  if (!fs.existsSync(fname)) break;
  const a = load(fname);
  if (a.width !== origs[0].width) { console.log('skip (width mismatch)'); break; }
  const bga = bgOf(a);
  let best = { iou: -1, mad: 999 };
  for (const b of origs) {
    const bgb = bgOf(b);
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
    const iou = uni ? inter / uni : 0;
    if (iou > best.iou) best = { iou, mad: n ? diff / n : 999 };
  }
  console.log(`frame ${k}: bestIoU=${(best.iou * 100).toFixed(1)}%  MAD=${best.mad.toFixed(1)}`);
}
