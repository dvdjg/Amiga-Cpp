#!/usr/bin/env node
// F3-tools Â· Cuantizador EHB (k-means modificado, half-aware).
//
// Elige 32 colores base de forma que {base} âˆª {half(base)} (half = c>>1 por
// componente, half de $FFF = $777) se acerque lo mÃ¡ximo a la imagen fuente.
// Reserva el color base 0 para transparencia. Emite:
//   - la paleta como `constexpr u16 kEhbPalette[32]` (palabras Amiga 0x0RGB)
//   - una preview PNG (bases + half + reconstrucciÃ³n de una franja)
//   - el error MSE del remapeo {base,half}
//
// Uso: node tools/ehb/quantize-ehb.mjs <imagen.png> [--tile 16] [--out direc]
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const imgPath = argV('--img', process.argv[2]) || null;
if (!imgPath) { console.error('Uso: quantize-ehb.mjs <png> [--tile 16]'); process.exit(2); }
const tile = parseInt(argV('--tile', '16'), 10);

const png = PNG.sync.read(fs.readFileSync(imgPath));
const W = png.width, H = png.height;
console.log(`[ehb] ${imgPath} ${W}x${H}, tile ${tile}`);

// --- muestreo de histograma (unique colors) ---
const counts = new Map();
for (let i = 0; i < W * H; i++) {
  const o = i * 4, a = png.data[o + 3];
  if (a < 128) continue; // transparente: fuera de la estadÃ­stica (reservamos base 0)
  const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2];
  counts.set(k, (counts.get(k) || 0) + 1);
}
const entries = [...counts.entries()].sort((a, b) => b[1] - a[1]);
const uid = entries.length;
console.log(`[ehb] ${uid} colores Ãºnicos (sin transparencia)`);

// --- DecisiÃ³n de modo segÃºn colores Ãºnicos del ORIGINAL -------------------
//   <=16 -> 4 planes (sin EHB) Â· <=32 -> 5 planes (sin EHB) Â· >32 -> 6 planes EHB.
// Con pocos colores NO se gastan 6 bits por pÃ­xel; se reserva color 0 siempre.
const force = argV('--force', '');
let planes = 6, K = 32, useEHB = true, modeNote = '';
try {
  const mi = Number.parseInt(force, 10);
  if (force !== '') {
    if (mi === 4) { planes = 4; K = 16; useEHB = false; modeNote = `(forzado 4 planes / 16 colores)`; }
    else if (mi === 5) { planes = 5; K = 32; useEHB = false; modeNote = `(forzado 5 planes / 32 colores)`; }
    else throw 0;
  }
} catch {
  if (force === 'ehb') { planes = 6; K = 32; useEHB = true; modeNote = '(forzado EHB)'; }
}
if (force === '') {
  if (uid <= 16) { planes = 4; K = 16; useEHB = false; modeNote = '(<=16 colores: 4 planes, sin EHB)'; }
  else if (uid <= 32) { planes = 5; K = 32; useEHB = false; modeNote = '(<=32 colores: 5 planes, sin EHB)'; }
  else { planes = 6; K = 32; useEHB = true; modeNote = '; EHB (32 base + half)'; }
}
console.log(`[ehb] modo detectado: ${planes} planes / ${K} colores base ${useEHB ? 'con EHB' : 'sin EHB'} ${modeNote}`);

const eps = 1e-9;
const lum = (c) => 0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255);
const dist2 = (a, b) => {
  const dr = ((a >> 16) & 255) - ((b >> 16) & 255), dg = ((a >> 8) & 255) - ((b >> 8) & 255), db = (a & 255) - (b & 255);
  return dr * dr + dg * dg + db * db;
};
const half = (c) => ((((c >> 16) & 255) >> 1) << 16) | ((((c >> 8) & 255) >> 1) << 8) | ((c & 255) >> 1);

// --- seed: 32 colores diversificados por luminosidad ---
function seed(k) {
  const sorted = entries.map((e) => e[0]).sort((a, b) => lum(a) - lum(b));
  const out = [];
  for (let i = 0; i < k; i++) out.push(sorted[Math.min(sorted.length - 1, Math.floor(((i + 0.5) / k) * sorted.length))]);
  return out;
}

// --- k-means modificado (half-aware) ---
let centers = seed(K);
const total = entries.reduce((s, e) => s + e[1], 0);
for (let it = 0; it < 60; it++) {
  const chans = centers.map(() => [[], [], []]);
  for (const [c, ] of entries) {
    let best = 0, bestDU = Infinity;
    for (let i = 0; i < K; i++) {
      const dF = dist2(c, centers[i]);
      const dH = useEHB ? dist2(c, half(centers[i])) : Infinity;
      const d = dF < dH ? dF : dH;
      if (d < bestDU) { bestDU = d; best = i; }
    }
    const isHalf = useEHB && dist2(c, half(centers[best])) < dist2(c, centers[best]);
    // Subida Ã—2 POR COMPONENTE con clamp (un carry cruzarÃ­a el canal superior).
    chans[best][0].push(isHalf ? Math.min(255, ((c >> 16) & 255) << 1) : (c >> 16) & 255);
    chans[best][1].push(isHalf ? Math.min(255, ((c >> 8) & 255) << 1) : (c >> 8) & 255);
    chans[best][2].push(isHalf ? Math.min(255, (c & 255) << 1) : c & 255);
  }
  let moved = 0;
  const med = (a) => { if (!a.length) return -1; a.sort((x, y) => x - y); return a[Math.floor(a.length / 2)]; };
  for (let i = 0; i < K; i++) {
    const r = med(chans[i][0]);
    if (r < 0) continue;
    const g = med(chans[i][1]), b = med(chans[i][2]);
    const next = (r << 16) | (g << 8) | b;
    if (next !== centers[i]) { centers[i] = next; moved++; }
  }
  if (moved === 0) break;
}

// --- ordenar por luminosidad y pasar a RGB444 (mÃ¡scara 0xF0 para no colisionar con half) ---
const order = centers.map((c, i) => ({ c, i })).sort((a, b) => lum(a.c) - lum(b.c));
const pal = order.map((o) => o.c);
function to444(c) { return ((((c >> 16) & 255) >> 4) << 8) | ((((c >> 8) & 255) >> 4) << 4) | ((c & 255) >> 4); }
const pal444 = pal.map(to444);

// Emitir también la paleta en JSON (32 bases RGB) para slice-tiles con --palette.
const palJson = pal.map((c) => [(c >> 16) & 255, (c >> 8) & 255, c & 255]);
const outDirP = argV('--out', 'out/ehb');
fs.mkdirSync(outDirP, { recursive: true });
fs.writeFileSync(path.join(outDirP, 'palette.json'), JSON.stringify({ planes, bases: palJson }, null, 2), 'utf8');
console.log(`[ehb] palette.json (${K} bases) -> ${path.join(outDirP, 'palette.json')}`);

// --- MSE del remapeo {base, half} ---
let mse = 0;
for (const [c, n] of entries) {
  let min = Infinity;
  for (let i = 0; i < K; i++) { const d = Math.min(dist2(c, pal[i]), dist2(c, half(pal[i]))); if (d < min) min = d; }
  mse += min * n;
}
mse /= total;
const psnr = 10 * Math.log10(255 * 255 * 3 / (mse + eps));

// --- emitir C y preview ---
const outDir = argV('--out', 'out/ehb');
fs.mkdirSync(outDir, { recursive: true });
const lines = ['// Paleta EHB generada (32 bases; half = c>>1 en el hardware).', '// base 0 = transparencia. Ordenada por luminosidad.', 'constexpr eng::u16 kEhbPalette[32] {'];
for (let r = 0; r < Math.ceil(K / 8); r++) {
  const row = [];
  for (let c = 0; c < 8; c++) row.push(`0x${pal444[r * 8 + c].toString(16).padStart(3, '0')}`);
  lines.push('    ' + row.join(', ') + (r < 3 ? ',' : ''));
}
lines.push('};');
const cFile = path.join(outDir, 'ehb_palette.h');
fs.writeFileSync(cFile, lines.join('\n') + '\n', 'utf8');

// Preview PNG: fila bases + fila half + franja reconstruida.
const sw = 24, swH = 24;
const prev = new PNG({ width: sw * 32, height: swH * 3 });
function put(x, y, rgb) { const o = (y * prev.width + x) * 4; prev.data[o] = rgb[0]; prev.data[o + 1] = rgb[1]; prev.data[o + 2] = rgb[2]; prev.data[o + 3] = 255; }
for (let i = 0; i < K; i++) {
  const c = pal[i];
  for (let yy = 0; yy < swH; yy++) for (let xx = 0; xx < sw; xx++) put(i * sw + xx, yy, [(c >> 16) & 255, (c >> 8) & 255, c & 255]);
  const h = useEHB ? half(c) : c;
  for (let yy = 0; yy < swH; yy++) for (let xx = 0; xx < sw; xx++) put(i * sw + xx, swH + yy, [(h >> 16) & 255, (h >> 8) & 255, h & 255]);
}
// franja: remap de las primeras `sw*32` columnas sobre H (muestras cada 2px)
for (let y = 0; y < Math.min(swH, H); y++) {
  for (let x = 0; x < sw * 32; x++) {
    const sx = x, sy = y; const o = (sy * W + sx) * 4, a = png.data[o + 3];
    if (a < 128) { put(x, swH * 2 + y, [0, 0, 255]); continue; }
    const c = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2];
    let bi = 0, dmin = Infinity, isH = false;
    for (let i = 0; i < K; i++) { const d = Math.min(dist2(c, pal[i]), dist2(c, half(pal[i]))); if (d < dmin) { dmin = d; bi = i; isH = dist2(c, half(pal[i])) < dist2(c, pal[i]); } }
    const r = (useEHB && isH) ? half(pal[bi]) : pal[bi];
    put(x, swH * 2 + y, [(r >> 16) & 255, (r >> 8) & 255, r & 255]);
  }
}
const prevOut = path.join(outDir, 'ehb_preview.png');
fs.writeFileSync(prevOut, PNG.sync.write(prev));

console.log(`[ehb] paleta -> ${cFile}`);
console.log(`[ehb] preview -> ${prevOut}`);
console.log(`[ehb] MSE=${mse.toFixed(1)} PSNR=${psnr.toFixed(1)} dB`);
console.log(lines.join('\n'));