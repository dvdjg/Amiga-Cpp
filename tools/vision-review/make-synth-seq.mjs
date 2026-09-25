#!/usr/bin/env node
// Generador de secuencias **sintéticas** para calibrar y auto-testear el detector temporal
// (`temporal-detect.py`). Crea frames PNG con artefactos conocidos y controlados:
//
//   - `flicker`     : un cuadrado que alterna claro/oscuro cada frame (vuelve al valor → parpadeo).
//   - `corruption`  : un cuadrado que aparece a partir de un frame y **no** vuelve.
//   - `motion`      : una banda que se desplaza 1 px/frame (movimiento legítimo: NO debe marcarse).
//
// Uso: node tools/vision-review/make-synth-seq.mjs --out <dir> [--frames 8] [--width 64]
//                                               [--height 64] [--kind mix|flicker|motion]
// Salida: `<dir>/frame_000.png …` y `<dir>/expected.json` (artefactos esperados por tipo).
//
// Se usa en el auto-test `node tools/vision-review/selftest-temporal.mjs`.

import * as fs from 'node:fs';
import * as path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const arg = (n, fb) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fb; };

const outDir = arg('--out', 'out/tmp/synth-seq');
const N = parseInt(arg('--frames', '8'), 10);
const W = parseInt(arg('--width', '64'), 10);
const H = parseInt(arg('--height', '64'), 10);
const kind = arg('--kind', 'mix');

fs.mkdirSync(outDir, { recursive: true });

// Regiones (proporciones de la imagen) para cada artefacto.
const R = {
  flicker: { x0: 0.05, x1: 0.30, y0: 0.05, y1: 0.30 },
  corruption: { x0: 0.60, x1: 0.85, y0: 0.60, y1: 0.85 },
  motion: { y0: 0.45, y1: 0.58, w: 0.25, speed: 1 },
};
const inR = (x, y, r) => x >= r.x0 * W && x < r.x1 * W && y >= r.y0 * H && y < r.y1 * H;

for (let f = 0; f < N; f++) {
  const png = new PNG({ width: W, height: H });
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    let v = 100; // fondo gris
    if ((kind === 'mix' || kind === 'flicker') && inR(x, y, R.flicker)) {
      v = (f % 2 === 0) ? 220 : 40; // parpadeo: vuelve al valor
    }
    if (kind === 'mix' && inR(x, y, R.corruption) && f >= Math.floor(N / 2)) {
      v = 240; // corrupción: aparece y no vuelve
    }
    if (kind === 'mix' || kind === 'motion') {
      const mx0 = 0.05 * W + f * R.motion.speed;
      if (y >= R.motion.y0 * H && y < R.motion.y1 * H &&
          x >= mx0 && x < mx0 + R.motion.w * W) v = 160; // banda móvil 1 px/frame
    }
    png.data[i] = v; png.data[i + 1] = v; png.data[i + 2] = v; png.data[i + 3] = 255;
  }
  fs.writeFileSync(path.join(outDir, `frame_${String(f).padStart(3, '0')}.png`), PNG.sync.write(png));
}

const expected = [];
if (kind === 'mix' || kind === 'flicker') expected.push('flicker');
if (kind === 'mix') expected.push('corruption');
expected.push('motion-ignored');
fs.writeFileSync(path.join(outDir, 'expected.json'), JSON.stringify({ kind, frames: N, size: [W, H], expected }, null, 2));
console.log(`[synth] ${outDir}: ${N} frames ${W}x${H} (${kind}); esperado=${expected.join(', ')}`);
