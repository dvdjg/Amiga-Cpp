#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const [screenFile, expectedFile] = process.argv.slice(2);
if (!screenFile || !expectedFile) {
  console.error('Uso: node tools/analyze/compare-201-capture.mjs <captura.png> <esperado.png>');
  process.exit(2);
}

const screen = PNG.sync.read(fs.readFileSync(screenFile));
const expected = PNG.sync.read(fs.readFileSync(expectedFile));
const paletteJson = JSON.parse(fs.readFileSync('out/ehb/palette.json', 'utf8'));
const toAmiga = ([r, g, b]) => [(r >> 4) * 17, (g >> 4) * 17, (b >> 4) * 17];
const hardwareBases = paletteJson.bases.map(toAmiga);
const palette = [...hardwareBases, ...hardwareBases.map(([r, g, b]) => [r >> 1, g >> 1, b >> 1])];
const scale = 2;
const fieldX = 76, fieldY = 30;
const w = expected.width, h = expected.height;
if (w !== 320 || (h !== 208 && h !== 256) || screen.width < fieldX + w * scale || screen.height < fieldY + h * scale) {
  throw new Error(`geometría inesperada: captura ${screen.width}x${screen.height}, esperado ${w}x${h}`);
}

const actual = new PNG({ width: w, height: h });
let total = 0;
let indexedMismatch = 0;
const nearest = (r, g, b) => {
  let best = 0, bestD = Number.MAX_SAFE_INTEGER;
  for (let i = 0; i < palette.length; i++) {
    const p = palette[i];
    const d = (r - p[0]) ** 2 + (g - p[1]) ** 2 + (b - p[2]) ** 2;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
};
const expectedIndex = new Uint8Array(w * h);
const actualIndex = new Uint8Array(w * h);
for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
  const si = ((fieldY + y * scale) * screen.width + fieldX + x * scale) * 4;
  const di = (y * w + x) * 4;
  actual.data[di] = screen.data[si];
  actual.data[di + 1] = screen.data[si + 1];
  actual.data[di + 2] = screen.data[si + 2];
  actual.data[di + 3] = 255;
  total += Math.abs(actual.data[di] - expected.data[di]);
  total += Math.abs(actual.data[di + 1] - expected.data[di + 1]);
  total += Math.abs(actual.data[di + 2] - expected.data[di + 2]);
  expectedIndex[y * w + x] = nearest(expected.data[di], expected.data[di + 1], expected.data[di + 2]);
  actualIndex[y * w + x] = nearest(actual.data[di], actual.data[di + 1], actual.data[di + 2]);
  if (expectedIndex[y * w + x] !== actualIndex[y * w + x]) indexedMismatch++;
}

const out = path.join(path.dirname(expectedFile), 'actual-normalized.png');
fs.writeFileSync(out, PNG.sync.write(actual));
console.log(`diferencia absoluta RGB total: ${total}`);
console.log(`media por píxel/canal: ${(total / (w * h * 3)).toFixed(2)}`);
console.log(`mismatches de índice EHB: ${indexedMismatch}/${w * h}`);
for (let ty = 0; ty < h / 16; ty++) {
  const row = [];
  for (let tx = 0; tx < w / 16; tx++) {
    let d = 0;
    let mi = 0;
    for (let y = ty * 16; y < (ty + 1) * 16; y++) for (let x = tx * 16; x < (tx + 1) * 16; x++) {
      const i = (y * w + x) * 4;
      d += Math.abs(actual.data[i] - expected.data[i]);
      d += Math.abs(actual.data[i + 1] - expected.data[i + 1]);
      d += Math.abs(actual.data[i + 2] - expected.data[i + 2]);
      if (actualIndex[y * w + x] !== expectedIndex[y * w + x]) mi++;
    }
    row.push(`${String(d).padStart(6)}/${String(mi).padStart(3)}`);
  }
  console.log(row.join(' '));
}
console.log(`normalizada: ${out}`);
