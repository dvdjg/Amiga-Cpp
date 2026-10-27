#!/usr/bin/env node
// Regresión del cuantizador: una imagen con tonos dominantes y acentos
// minoritarios debe conservar los acentos en la paleta EHB generada. No importa
// el mapa concreto; comprueba el algoritmo general de selección de bases.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const out = path.join(ROOT, 'out/tmp/quantize-ehb-test');
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });

const colors = [
  [[149, 217, 130], 70],
  [[169, 149, 138], 18],
  [[238, 102, 85], 3],
  [[135, 199, 223], 3],
  [[238, 238, 102], 3],
  [[170, 153, 204], 3],
];
const pixels = [];
for (const [rgb, weight] of colors) {
  for (let i = 0; i < weight * 100; ++i) pixels.push(rgb);
}
const png = new PNG({ width: 200, height: 200 });
for (let i = 0; i < png.width * png.height; ++i) {
  const rgb = pixels[i % pixels.length];
  const o = i * 4;
  png.data[o] = rgb[0];
  png.data[o + 1] = rgb[1];
  png.data[o + 2] = rgb[2];
  png.data[o + 3] = 255;
}
const input = path.join(out, 'source.png');
fs.writeFileSync(input, PNG.sync.write(png));

const quant = spawnSync(process.execPath, [
  path.join(ROOT, 'tools/ehb/quantize-ehb.mjs'), input, '--force', 'ehb', '--out', out,
], { encoding: 'utf8' });
if (quant.status !== 0) {
  console.error(quant.stdout ?? '');
  console.error(quant.stderr ?? '');
  process.exit(1);
}
const palette = JSON.parse(fs.readFileSync(path.join(out, 'palette.json'), 'utf8'));
if (!Array.isArray(palette.bases) || palette.bases.length !== 32) {
  console.error(`[quantize-test] esperaba 32 bases, hay ${palette.bases?.length}`);
  process.exit(1);
}
const half = (c) => [c[0] >> 1, c[1] >> 1, c[2] >> 1];
const distance = (a, b) => (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2;
let failure = 0;
for (const [rgb] of colors.slice(2)) {
  const best = Math.min(...palette.bases.flatMap((b) => [distance(rgb, b), distance(rgb, half(b))]));
  const d = Math.sqrt(best);
  if (d > 24) {
    console.error(`[quantize-test] color ${rgb.join(',')} no cubierto (dist ${d.toFixed(1)})`);
    ++failure;
  }
}
if (failure !== 0) process.exit(1);
console.log('OK: paleta EHB generada; 32 bases y acentos minoritarios cubiertos');
