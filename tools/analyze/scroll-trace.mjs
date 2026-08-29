#!/usr/bin/env node
/**
 * Mide la continuidad del scroll de una secuencia de frames rastreando la
 * posicion de una transicion vertical distintiva en una fila. Local, sin nube.
 *
 * Uso: node tools/analyze/scroll-trace.mjs <secuencia> [--row 120] [--search 40]
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng } from '../../dist/tools/lib/image.js';

const seqDir = process.argv[2];
if (!seqDir || !fs.existsSync(seqDir)) { console.error('Uso: scroll-trace.mjs <secuencia> [--row N]'); process.exit(2); }
const rowArg = process.argv.indexOf('--row');
const ROW = rowArg >= 0 ? parseInt(process.argv[rowArg + 1], 10) : 120;

const files = fs.readdirSync(seqDir).filter((n) => n.endsWith('.png')).sort();
if (files.length < 2) { console.error('necesita 2+ frames'); process.exit(2); }

function grey(img, x, y) { const i = (y * img.width + x) * 4; return (img.data[i] + img.data[i + 1] + img.data[i + 2]) / 3; }

// Por frame: perfil de la fila y posicion de los bordes mas fuertes.
const edges = files.map((f) => {
  const img = readPng(path.join(seqDir, f));
  const x0 = Math.round((img.width - 320) / 2);
  const y0 = Math.round((img.height - 256) / 2);
  const y = y0 + ROW;
  const prof = [];
  for (let x = 0; x < 320; x++) prof.push(grey(img, x0 + x, y));
  // bordes: |prof[x]-prof[x-1]|, top 3
  const es = [];
  for (let x = 1; x < 320; x++) {
    const m = Math.abs(prof[x] - prof[x - 1]);
    if (m > 60) es.push({ x, m });
  }
  es.sort((a, b) => b.m - a.m);
  return es.slice(0, 5).map((e) => e.x).sort((a, b) => a - b);
});

console.log('posiciones de bordes fuertes en la fila ' + ROW + ' (por frame):');
for (let i = 0; i < files.length; i++) {
  console.log(files[i].slice(6, 9) + '  ' + edges[i].join(','));
}
