#!/usr/bin/env node
// Frame-diff determinista: cuenta píxeles cambiados y su bbox entre frames CONSECUTIVOS de una
// secuencia. Es la referencia directa para separar **movimiento** (cambian las zonas que se
// desplazan) de **glitch** (cambia una zona que debería ser estable). Complementa la detección
// por optical flow (temporal-detect.py) y es lo que prevalece ante una respuesta dudosa del VLM.
//
// Uso: node tools/vision-review/frame-diff.mjs --sequence <dir> [--thresh 40] [--json]
// Salida: imprime una línea por par de frames; con --json, objeto con `pairs`.

import * as fs from 'node:fs';
import * as path from 'node:path';
import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

// Punto de entrada unificado: si hay Python + OpenCV, delega en `frame-diff.py` (NumPy/OpenCV, SIMD
// y SSIM). Si no, usa esta implementación Node (pngjs). Así hay una sola verdad para el usuario.
if (!process.argv.includes('--node') && !process.env.FRAME_DIFF_FORCE_NODE) {
  try {
    execFileSync(process.env.PYTHON || 'python', ['-c', 'import cv2, numpy'], { stdio: 'ignore' });
    const py = path.join(ROOT, 'tools/vision-review/frame-diff.py');
    const args = process.argv.slice(2).filter((a) => a !== '--node');
    execFileSync(process.env.PYTHON || 'python', [py, ...args], { stdio: 'inherit' });
    process.exit(0);
  } catch { /* sin Python: sigue con la implementación Node */ }
}

const arg = (n, fb) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fb; };
const has = (n) => process.argv.includes(n);

const seqDir = arg('--sequence', '');
if (!seqDir) { console.error('Uso: node tools/vision-review/frame-diff.mjs --sequence <dir> [--thresh 40] [--json]'); process.exit(2); }
const thresh = parseInt(arg('--thresh', '40'), 10);

let PNG;
try { ({ PNG } = require('pngjs')); } catch { console.error('[frame-diff] pngjs no disponible.'); process.exit(2); }
const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d{3,}\.png$/.test(f)).sort();
if (files.length < 2) { console.log(`[frame-diff] ${seqDir}: <2 frames (se omite).`); process.exit(3); }
const imgs = files.map((f) => PNG.sync.read(fs.readFileSync(path.join(seqDir, f))));
const W = imgs[0].width, H = imgs[0].height;

const pairs = [];
for (let f = 1; f < imgs.length; f++) {
  const a = imgs[f - 1], b = imgs[f];
  let minx = W, miny = H, maxx = -1, maxy = -1, n = 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    const d = Math.abs(a.data[i] - b.data[i]) + Math.abs(a.data[i + 1] - b.data[i + 1]) + Math.abs(a.data[i + 2] - b.data[i + 2]);
    if (d > thresh) {
      n++;
      if (x < minx) minx = x; if (x > maxx) maxx = x;
      if (y < miny) miny = y; if (y > maxy) maxy = y;
    }
  }
  pairs.push({ from: f - 1, to: f, changed: n, bbox: n ? [minx, miny, maxx, maxy] : null });
}

if (has('--json')) {
  console.log(JSON.stringify({ sequence: seqDir.replace(/\\/g, '/'), size: [W, H], thresh, pairs }, null, 2));
} else {
  console.log(`[frame-diff] ${seqDir.replace(/\\/g, '/')} · ${imgs.length} frames · ${W}×${H} · umbral ${thresh}`);
  for (const p of pairs) {
    const bbox = p.bbox ? `[${p.bbox[0]},${p.bbox[1]}]-[${p.bbox[2]},${p.bbox[3]}]` : '(sin cambio)';
    console.log(`  f${String(p.from).padStart(2, '0')}->f${String(p.to).padStart(2, '0')}: px=${String(p.changed).padStart(6)} ${bbox}`);
  }
}
process.exit(0);
