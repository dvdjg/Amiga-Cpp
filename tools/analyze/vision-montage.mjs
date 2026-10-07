#!/usr/bin/env node
// Hoja de contacto (rejilla etiquetada) de un rango de frames de secuencia, para
// pasarla al modelo de visión como UNA imagen (convención DEMO_VISUAL_DEBUG §6.5).
//
// Uso:
//   node tools/analyze/vision-montage.mjs <demoId> <dirSeq> <idxFrom> <idxTo>
//        [--cols 8] [--thumb 160x117] [--demo-dir ruta]
//
// Salida: <demoDir>/vision/<demoId>_sheet_f<primero>-f<último>.png
import * as fs from 'node:fs';
import * as path from 'node:path';
import { readPng, createImage, savePng, pixel, setPixel, drawText } from '../../dist/tools/lib/image.js';

const argv = process.argv.slice(2);
const positional = [];
let cols = 8, tw = 160, th = 117, demoDirArg = '';
for (let i = 0; i < argv.length; ++i) {
  const a = argv[i];
  if (a === '--cols') cols = parseInt(argv[++i], 10);
  else if (a === '--thumb') { const m = /(\d+)x(\d+)/.exec(argv[++i]); tw = +m[1]; th = +m[2]; }
  else if (a === '--demo-dir') demoDirArg = argv[++i];
  else positional.push(a);
}
const [demoId, seqDir, fromRaw, toRaw] = positional;
const from = parseInt(fromRaw, 10), to = parseInt(toRaw, 10);
if (!demoId || !seqDir || !Number.isFinite(from) || !Number.isFinite(to)) {
  console.error('uso: vision-montage.mjs <demoId> <dirSeq> <idxFrom> <idxTo> [--cols N] [--thumb WxH] [--demo-dir ruta]');
  process.exit(2);
}

let demoDir = demoDirArg;
if (!demoDir) {
  const hit = fs.readdirSync('demos', { recursive: true, withFileTypes: true })
    .find((e) => e.isDirectory() && e.name === demoId);
  if (!hit) { console.error(`no encuentro demos/**/${demoId}; pasa --demo-dir`); process.exit(3); }
  demoDir = path.join(hit.parentPath ?? hit.path, hit.name);
}

const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d+_f\d+\.png$/.test(f));
const picked = [];
for (let idx = from; idx <= to; ++idx) {
  const m = files.map((f) => /^frame_(\d+)_f(\d+)\.png$/.exec(f)).find((x) => x && parseInt(x[1], 10) === idx);
  if (m) picked.push({ idx, frame: m[2], file: path.join(seqDir, m[0]) });
}
if (picked.length === 0) { console.error('sin frames en el rango'); process.exit(3); }

const rows = Math.ceil(picked.length / cols);
const cellH = th + 14;
const sheet = createImage(tw * cols, cellH * rows, [16, 16, 16]);
for (let i = 0; i < picked.length; ++i) {
  const img = readPng(picked[i].file);
  const cx = (i % cols) * tw, cy = Math.floor(i / cols) * cellH;
  // Escalado nearest-neighbor al tamaño de celda.
  for (let y = 0; y < th; ++y) {
    const sy = Math.min(img.height - 1, Math.floor((y * img.height) / th));
    for (let x = 0; x < tw; ++x) {
      const sx = Math.min(img.width - 1, Math.floor((x * img.width) / tw));
      const p = pixel(img, sx, sy);
      setPixel(sheet, cx + x, cy + 14 + y, p[0], p[1], p[2]);
    }
  }
  drawText(sheet, cx + 4, cy + 2, `f${picked[i].frame}`, [255, 255, 255]);
}

const outDir = path.join(demoDir, 'vision');
fs.mkdirSync(outDir, { recursive: true });
const outPath = path.join(outDir, `${demoId}_sheet_f${picked[0].frame}-f${picked[picked.length - 1].frame}.png`);
savePng(sheet, outPath);
console.log(`[montage] ${picked.length} frames -> ${outPath}`);
