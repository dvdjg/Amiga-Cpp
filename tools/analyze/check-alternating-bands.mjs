#!/usr/bin/env node
// Check automático de BANDAS ALTERNAS (flicker 1-de-cada-2 frames) en una
// secuencia por paso: por cada banda horizontal de 16 filas, mide la luminancia
// media por frame y detecta alternancias fuertes (baja en un frame, vuelve en el
// siguiente) repetidas — la firma del bug de doble-buffer con estructura
// divergente de la 110 (DEMO_VISUAL_DEBUG §6.4.1; el modelo de visión no lo caza).
//
// Uso: node tools/analyze/check-alternating-bands.mjs <dirSeq> [--band 16] [--min-alt 3] [--drop 25]
import * as fs from 'node:fs';
import * as path from 'node:path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const argv = process.argv.slice(2);
const positional = [];
let band = 16, minAlt = 3, drop = 25;
for (let i = 0; i < argv.length; ++i) {
  const a = argv[i];
  if (a === '--band') band = parseInt(argv[++i], 10);
  else if (a === '--min-alt') minAlt = parseInt(argv[++i], 10);
  else if (a === '--drop') drop = parseInt(argv[++i], 10);
  else positional.push(a);
}
const [dir] = positional;
if (!dir) { console.error('uso: check-alternating-bands.mjs <dirSeq> [--band 16] [--min-alt 3] [--drop 25]'); process.exit(2); }

const files = fs.readdirSync(dir).filter((f) => /^frame_\d+_f\d+\.png$/.test(f)).sort();
if (files.length < 6) { console.error('secuencia demasiado corta'); process.exit(2); }

// Luminancia media por banda en cada frame.
const perFrame = [];
let bands = 0;
for (const f of files) {
  const img = readPng(path.join(dir, f));
  bands = Math.ceil(img.height / band);
  const luma = new Array(bands).fill(0);
  const cnt = new Array(bands).fill(0);
  for (let y = 0; y < img.height; ++y) {
    const b = Math.floor(y / band);
    for (let x = 0; x < img.width; x += 4) {
      const p = pixel(img, x, y);
      luma[b] += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
      cnt[b]++;
    }
  }
  perFrame.push({ name: f, luma: luma.map((v, i) => v / Math.max(1, cnt[i])) });
}

let flagged = 0;
for (let b = 0; b < bands; ++b) {
  let alts = 0;
  for (let i = 2; i < perFrame.length; ++i) {
    const d1 = perFrame[i - 1].luma[b] - perFrame[i - 2].luma[b];
    const d2 = perFrame[i].luma[b] - perFrame[i - 1].luma[b];
    if (d1 * d2 < 0 && Math.abs(d1) > drop && Math.abs(d2) > drop) alts++;
  }
  if (alts >= minAlt) {
    flagged++;
    console.log(`[bands] BANDA ALTERNA y=${b * band}..${(b + 1) * band - 1} alternancias=${alts} (>=${minAlt})`);
  }
}
console.log(`[bands] frames=${files.length} bandas=${bands} flagged=${flagged}`);
console.log(flagged === 0 ? '[bands] OK: sin bandas alternas' : '[bands] FAIL: bandas alternas detectadas');
process.exit(flagged === 0 ? 0 : 1);
