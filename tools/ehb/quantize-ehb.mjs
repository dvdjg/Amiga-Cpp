#!/usr/bin/env node
// F3-tools · Cuantizador EHB (k-means modificado, half-aware).
//
// Elige 32 colores base de forma que {base} ∪ {half(base)} (half = c>>1 por
// componente, half de $FFF = $777) se acerque lo máximo a la imagen fuente.
// Reserva el color base 0 para transparencia. Emite:
//   - la paleta como `constexpr u16 kEhbPalette[32]` (palabras Amiga 0x0RGB)
//   - una preview PNG (bases + half + reconstrucción de una franja)
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
  if (a < 128) continue; // transparente: fuera de la estadística (reservamos base 0)
  const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2];
  counts.set(k, (counts.get(k) || 0) + 1);
}
const entries = [...counts.entries()].sort((a, b) => b[1] - a[1]);
const uid = entries.length;
console.log(`[ehb] ${uid} colores únicos (sin transparencia)`);

// --- Decisión de modo según colores únicos del ORIGINAL -------------------
//   <=16 -> 4 planes (sin EHB) · <=32 -> 5 planes (sin EHB) · >32 -> 6 planes EHB.
// Con pocos colores NO se gastan 6 bits por píxel; se reserva color 0 siempre.
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

const total = entries.reduce((s, e) => s + e[1], 0);

// --- seed: farthest-first ponderado (color, no solo luminancia) ---
//
// La luminancia reparte semillas por bandas de brillo, pero deja fuera tonos
// minoritarios y muy saturados (agua, flores, fuego) que sí son importantes en
// el mapa. Farthest-first recorre el espacio RGB y el peso `count^0.35` evita
// que un color raro pero aislado desplace a una masa de color dominante. El
// resultado es una cobertura de tonos mucho más fiel para cualquier mapa, sin
// conocer su contenido.
function seed(k) {
  if (entries.length === 0) return [];
  const sorted = entries.slice().sort((a, b) => b[1] - a[1]);
  const out = [sorted[0][0]];
  const chosen = new Set(out);
  while (out.length < k) {
    let best = null, bestScore = -1;
    for (const [c, n] of sorted) {
      if (chosen.has(c)) continue;
      let minD = Infinity;
      for (const q of out) {
        const d = Math.min(dist2(c, q), useEHB ? dist2(c, half(q)) : Infinity);
        if (d < minD) minD = d;
      }
      const score = Math.sqrt(minD) * Math.pow(n / total, 0.35);
      if (score > bestScore) { bestScore = score; best = c; }
    }
    if (best == null) break;
    out.push(best);
    chosen.add(best);
  }
  while (out.length < k) out.push(out[out.length - 1] ?? 0);
  return out;
}

// --- k-means modificado (half-aware, mediana ponderada) ---
let centers = seed(K);
for (let it = 0; it < 60; it++) {
  const chans = centers.map(() => [[], [], []]);
  for (const [c, n] of entries) {
    let best = 0, bestDU = Infinity;
    for (let i = 0; i < K; i++) {
      const dF = dist2(c, centers[i]);
      const dH = useEHB ? dist2(c, half(centers[i])) : Infinity;
      const d = dF < dH ? dF : dH;
      if (d < bestDU) { bestDU = d; best = i; }
    }
    const isHalf = useEHB && dist2(c, half(centers[best])) < dist2(c, centers[best]);
    // Subida ×2 POR COMPONENTE con clamp (un carry cruzaría el canal superior).
    chans[best][0].push([isHalf ? Math.min(255, ((c >> 16) & 255) << 1) : (c >> 16) & 255, n]);
    chans[best][1].push([isHalf ? Math.min(255, ((c >> 8) & 255) << 1) : (c >> 8) & 255, n]);
    chans[best][2].push([isHalf ? Math.min(255, (c & 255) << 1) : c & 255, n]);
  }
  let moved = 0;
  const wmed = (a) => {
    if (!a.length) return -1;
    a.sort((x, y) => x[0] - y[0]);
    const halfW = a.reduce((s, x) => s + x[1], 0) / 2;
    let acc = 0;
    for (const x of a) { acc += x[1]; if (acc >= halfW) return x[0]; }
    return a[a.length - 1][0];
  };
  for (let i = 0; i < K; i++) {
    const r = wmed(chans[i][0]);
    if (r < 0) {
      // Cluster vacío: recolocarlo en el color peor representado (distancia ×
      // frecuencia) en vez de dejar una semilla muerta que ocupa una base.
      let worst = null, worstScore = -1;
      for (const [c, n] of entries) {
        let minD = Infinity;
        for (const q of centers) {
          const d = Math.min(dist2(c, q), useEHB ? dist2(c, half(q)) : Infinity);
          if (d < minD) minD = d;
        }
        const score = minD * Math.sqrt(n);
        if (score > worstScore) { worstScore = score; worst = c; }
      }
      if (worst != null) { centers[i] = worst; moved++; }
      continue;
    }
    const g = wmed(chans[i][1]), b = wmed(chans[i][2]);
    const next = (r << 16) | (g << 8) | b;
    if (next !== centers[i]) { centers[i] = next; moved++; }
  }
  if (moved === 0) break;
}

// --- ordenar por luminosidad y pasar a RGB444 (máscara 0xF0 para no colisionar con half) ---
const order = centers.map((c, i) => ({ c, i })).sort((a, b) => lum(a.c) - lum(b.c));
const pal = order.map((o) => o.c);
function to444(c) { return ((((c >> 16) & 255) >> 4) << 8) | ((((c >> 8) & 255) >> 4) << 4) | ((c & 255) >> 4); }
const pal444 = pal.map(to444);

// Emitir también la paleta en JSON (32 bases RGB) para slice-tiles con --palette.
const palJson = pal.map((c) => [(c >> 16) & 255, (c >> 8) & 255, c & 255]);
const outDirP = argV('--out', 'out/assets/ehb');
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
const outDir = argV('--out', 'out/assets/ehb');
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