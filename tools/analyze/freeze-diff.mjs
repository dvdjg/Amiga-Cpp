#!/usr/bin/env node
// Compara dos capturas de la demo 116 tomadas con el MISMO ángulo congelado
// (`-DFLATSHADE_FREEZE_ANGLE=N`) y dice si son idénticas píxel a píxel.
//
// Con el ángulo fijo la captura es determinista, así que esto es un gate de
// bit-exactitud real: cualquier MAD != 0 es un cambio de imagen.
//
// Uso: node tools/analyze/freeze-diff.mjs <a.png> <b.png>
import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const [fa, fb] = process.argv.slice(2);
if (!fa || !fb) { console.error('Uso: node tools/analyze/freeze-diff.mjs <a.png> <b.png>'); process.exit(2); }
const a = PNG.sync.read(fs.readFileSync(fa));
const b = PNG.sync.read(fs.readFileSync(fb));
if (a.width !== b.width || a.height !== b.height) {
  console.error(`[freeze-diff] tamaños distintos: ${a.width}x${a.height} vs ${b.width}x${b.height}`);
  process.exit(1);
}

let mad = 0, diffPx = 0, maxd = 0;
const n = a.width * a.height;
for (let i = 0; i < n; ++i) {
  const o = i * 4;
  const d = Math.abs(a.data[o] - b.data[o]) + Math.abs(a.data[o + 1] - b.data[o + 1]) +
            Math.abs(a.data[o + 2] - b.data[o + 2]);
  mad += d;
  if (d > 0) ++diffPx;
  if (d > maxd) maxd = d;
}
const meanAbsDiff = mad / n / 3; // por canal, 0..255
console.log(`[freeze-diff] ${a.width}x${a.height}  MAD/px=${meanAbsDiff.toFixed(4)}  ` +
            `px distintos=${diffPx} (${(100 * diffPx / n).toFixed(2)}%)  max=${maxd}`);
if (diffPx === 0) {
  console.log('[freeze-diff] IDENTICAS (bit-exacto).');
  process.exit(0);
}
console.log('[freeze-diff] DIFERENTES.');
process.exit(1);
