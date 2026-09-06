#!/usr/bin/env node
// verify-parallax.mjs — verifica el parallax 2:1 de la demo 202 (DPF 3+3):
// entre frame_000 y frame_002 de la secuencia, el BG (mapa, PF2) debe moverse
// ~2× lo que el FG (plaquettes, PF1). Máscaras de color estrictas (paleta DPF).
// Uso: node demos/202_xlimited_dpf/verify-parallax.mjs [--config A500_debug]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const config = process.argv.includes('--config') ? process.argv[process.argv.indexOf('--config') + 1] : 'A500_debug';
const dir = path.join(ROOT, 'out/run/202_xlimited_dpf', config, 'sequence');

const fgW = [0xe44, 0xf86, 0xfe0, 0x4c8, 0x48e, 0xc5f, 0xfdf];
const bgW = [0x664, 0x5a6, 0xb95, 0xa98, 0x8cd, 0x9d8, 0xeb5, 0xdc9];
const fg = fgW.map(w => [(w >> 8 & 15) * 17, (w >> 4 & 15) * 17, (w & 15) * 17]);
const bg = bgW.map(w => [(w >> 8 & 15) * 17, (w >> 4 & 15) * 17, (w & 15) * 17]);
const load = n => PNG.sync.read(fs.readFileSync(path.join(dir, n)));
const near = (p, arr) => Math.min(...arr.map(q => Math.abs(p[0] - q[0]) + Math.abs(p[1] - q[1]) + Math.abs(p[2] - q[2])));
function mask(img) {
  const W = img.width, H = img.height;
  const f = new Uint8Array(W * H), b = new Uint8Array(W * H);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const o = (y * W + x) * 4, p = [img.data[o], img.data[o + 1], img.data[o + 2]];
    if (near(p, fg) < 55) f[y * W + x] = 1;
    else if (near(p, bg) < 55) b[y * W + x] = 1;
  }
  return { W, H, f, b };
}
function shiftX(mA, mB, y0, y1, kind, maxs, step) {
  // Correla la máscara de una clase (FG o BG): para un desplazamiento s busca
  // minimizar el desacuerdo (a[x+s]!=b[x]) NORMALIZADO por la unión de la clase
  // en ambas imágenes (solo píxeles que pertenecen a esa clase en alguna).
  const a = kind === 'f' ? mA.f : mA.b, b = kind === 'f' ? mB.f : mB.b;
  let best = 0, bestScore = 1e18;
  for (let s = -maxs; s <= maxs; s++) {
    let mis = 0, uni = 0;
    for (let y = y0; y < y1; y += 1) for (let x = Math.max(0, -s); x + s < mA.W && x < mB.W; x += step) {
      const va = a[y * mA.W + x + s], vb = b[y * mB.W + x];
      if (va || vb) { uni++; if (va !== vb) mis++; }
    }
    if (uni && mis / uni < bestScore) { bestScore = mis / uni; best = s; }
  }
  return best;
}
const A = mask(load('frame_000.png'));
const C = mask(load('frame_002.png'));
const yTop = 40, yBot = Math.min(A.H, C.H) - 40;
// FG: filas que contienen alguna placa en ambos frames (dispersas). BG: todas
// las filas de contenido (las placas son ~10% del área y no dominan la
// correlación del mapa que hay detrás).
const fgRows = [];
for (let y = yTop; y < yBot; y++) {
  let f = 0;
  for (let x = 0; x < A.W; x += 3) f += A.f[y * A.W + x] + C.f[y * C.W + x];
  if (f > 0) fgRows.push(y);
}
if (fgRows.length < 30) {
  console.error('[verify-202] no hay suficientes filas con placas (FG) en la secuencia'); process.exit(2);
}
const pick = (r, k) => r[Math.min(r.length - 1, Math.floor(r.length * k))];
const sFg = shiftX(A, C, pick(fgRows, 0.1), pick(fgRows, 0.9), 'f', 160, 1);
const sBg = shiftX(A, C, yTop, yBot, 'b', 160, 2);
const aFg = Math.abs(sFg), aBg = Math.abs(sBg);
const ratio = aFg ? aBg / aFg : NaN;
console.log(`[verify-202] desplaz. FG (plaquettes) = ${sFg} px · BG (mapa) = ${sBg} px · ratio |BG|/|FG| = ${ratio.toFixed(2)}`);
const ok = aFg >= 8 && aBg >= 12 && ratio >= 1.4 && ratio <= 2.8;
console.log(ok ? '[verify-202] PASS: parallax 2:1 (movimiento independiente por campo)' : '[verify-202] FAIL');
process.exit(ok ? 0 : 1);
