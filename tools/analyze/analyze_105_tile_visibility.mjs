#!/usr/bin/env node
// Comprueba que las cargas de tiles no cambian bloques interiores del viewport.
// El movimiento normal se elimina probando desplazamientos pequenos; un tile que
// se redibuja mientras esta visible queda como un residuo grande dentro de un
// bloque de 32x32 (la captura del runner escala los 16x16 del Amiga x2).
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const sequenceDir = path.resolve(process.argv[2] ?? '');
if (!sequenceDir || !fs.existsSync(sequenceDir)) {
  console.error('Uso: analyze_105_tile_visibility.mjs <sequence-dir>');
  process.exit(2);
}

const files = fs.readdirSync(sequenceDir)
  .filter((name) => /^frame_\d+.*\.png$/.test(name))
  .sort()
  .map((name) => path.join(sequenceDir, name));
if (files.length < 2) {
  console.error('Se necesitan al menos dos frames PNG.');
  process.exit(2);
}

function read(file) {
  return PNG.sync.read(fs.readFileSync(file));
}

const first = read(files[0]);
// El runner entrega 320x256 ampliado x2 y centrado en 756x576. Detectamos el
// rectangulo activo para no convertir el borde negro en un falso tile cambiado.
function activeRect(image) {
  let left = image.width; let top = image.height; let right = -1; let bottom = -1;
  for (let y = 0; y < image.height; ++y) for (let x = 0; x < image.width; ++x) {
    const i = (y * image.width + x) * 4;
    if (image.data[i] || image.data[i + 1] || image.data[i + 2]) {
      left = Math.min(left, x); top = Math.min(top, y);
      right = Math.max(right, x); bottom = Math.max(bottom, y);
    }
  }
  return { left, top, width: right - left + 1, height: bottom - top + 1 };
}

const viewport = activeRect(first);
const tilePixels = 32;
const cols = Math.floor(viewport.width / tilePixels);
const rows = Math.floor(viewport.height / tilePixels);

function difference(a, b, dx, dy) {
  let total = 0; let changed = 0;
  const left = viewport.left + 2; const top = viewport.top + 2;
  const right = viewport.left + viewport.width - 2;
  const bottom = viewport.top + viewport.height - 2;
  for (let y = top; y < bottom; y += 4) for (let x = left; x < right; x += 4) {
    const ax = x - dx; const ay = y - dy;
    if (ax < 0 || ay < 0 || ax >= a.width || ay >= a.height) continue;
    const ai = (ay * a.width + ax) * 4; const bi = (y * b.width + x) * 4;
    const d = (Math.abs(a.data[ai] - b.data[bi]) +
      Math.abs(a.data[ai + 1] - b.data[bi + 1]) +
      Math.abs(a.data[ai + 2] - b.data[bi + 2])) / 3;
    total += d; if (d > 8) ++changed;
  }
  const samples = Math.max(1, Math.ceil((right - left) / 4) * Math.ceil((bottom - top) / 4));
  return { mean: total / samples, ratio: changed / samples };
}

function blockRatios(a, b, dx, dy) {
  const blocks = [];
  for (let row = 0; row < rows; ++row) for (let col = 0; col < cols; ++col) {
    let changed = 0; let samples = 0;
    const x0 = viewport.left + col * tilePixels + 6;
    const y0 = viewport.top + row * tilePixels + 6;
    for (let y = y0; y < y0 + tilePixels - 12; y += 4) for (let x = x0; x < x0 + tilePixels - 12; x += 4) {
      const ax = x - dx; const ay = y - dy;
      const ai = (ay * a.width + ax) * 4; const bi = (y * b.width + x) * 4;
      const d = (Math.abs(a.data[ai] - b.data[bi]) + Math.abs(a.data[ai + 1] - b.data[bi + 1]) + Math.abs(a.data[ai + 2] - b.data[bi + 2])) / 3;
      if (d > 8) ++changed; ++samples;
    }
    blocks.push({ col, row, ratio: changed / samples });
  }
  return blocks;
}

const sampledFiles = files.filter((_file, index) => index % 4 === 0 || index === files.length - 1);
const pairs = [];
for (let i = 1; i < sampledFiles.length; ++i) {
  const current = read(sampledFiles[i]);
  // Compare the actual pair with the same local translation search. The first
  // image above is replaced here to keep the code independent of frame timing.
  const previous = read(sampledFiles[i - 1]);
  let best = null;
  for (let dy = -4; dy <= 4; ++dy) for (let dx = -4; dx <= 4; ++dx) {
    const result = difference(previous, current, dx, dy);
    if (best === null || result.mean < best.mean) best = { dx, dy, ...result };
  }
  const blocks = blockRatios(previous, current, best.dx, best.dy);
  const suspicious = blocks.filter((block) => block.ratio > 0.65);
  pairs.push({ from: path.basename(sampledFiles[i - 1]), to: path.basename(sampledFiles[i]), best, suspicious });
}

const worst = pairs.reduce((a, b) => b.suspicious.length > a.suspicious.length ? b : a, pairs[0]);
const result = { status: worst.suspicious.length === 0 ? 'ok' : 'visible_tile_redraw_detected', frames: files.length, viewport, tilePixels, worst, pairs };
fs.writeFileSync(path.join(sequenceDir, 'tile-visibility-analysis.json'), JSON.stringify(result, null, 2));
console.log(`Status=${result.status} Frames=${result.frames} Viewport=${viewport.width}x${viewport.height} WorstSuspiciousBlocks=${worst.suspicious.length}`);
if (result.status !== 'ok') process.exit(1);
