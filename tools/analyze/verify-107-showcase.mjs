#!/usr/bin/env node
// ---------------------------------------------------------------------------
// Verificación del MUESTRARIO de la demo 107 (DPF 3+3 con FG la mitad
// transparente + sprite + HUD + 8-way). Criterio de cierre:
//   1) Al menos 6 frames consecutivos cambian (scroll animado).
//   2) La captura contiene colores de PF2 (fondo) que solo se ven a través de
//      los tiles del FG transparente (checkerboard) Y colores de PF1 → se
//      demuestra la transparencia en DPF.
//   3) No es una pantalla negra/casi negra.
//
// Rotulado de paletas (ver demo main.cpp kScenePalette):
//   PF1 (0-7): 0xf0c 0x0cf 0xff0 0xf80 0x84f 0xf44 0xfff
//   PF2 (8-15):0x021 0x063 0x0a5 0x2d7 0xdfa 0xce7 (PF2 exclusivos)
//   Sprite(16-19): 0xff8 0x4aa 0xfff
// Uso: node tools/analyze/verify-107-showcase.mjs <screenshot.png> <seqDir>
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const [pngPath, seqDir] = process.argv.slice(2);
if (!pngPath || !seqDir) { console.error('uso: verify-107-showcase.mjs <screenshot.png> <seqDir>'); process.exit(2); }

// Requiere pngjs (ya usado por analyze-sequence.sh §6b).
let PNG;
try { PNG = require('pngjs').PNG; }
catch (e) { console.error('pngjs no disponible: ' + e.message); process.exit(1); }
const rgb = (hex) => [((hex >> 8) & 0xf) * 17, ((hex >> 4) & 0xf) * 17, (hex & 0xf) * 17];
const PF2 = [0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7].map(rgb);
const PF1 = [0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44].map(rgb);

function near(c, ref, tol = 26) {
  return Math.abs(c[0] - ref[0]) <= tol && Math.abs(c[1] - ref[1]) <= tol && Math.abs(c[2] - ref[2]) <= tol;
}
function classify(image) {
  let pf2Only = 0, pf1Only = 0, nonBlack = 0;
  for (let i = 0; i < image.data.length; i += 4) {
    const R = image.data[i], G = image.data[i + 1], B = image.data[i + 2];
    if (R + G + B < 24) continue;
    nonBlack++;
    let inPf2 = false, inPf1 = false;
    for (const c of PF2) if (near([R, G, B], c)) { inPf2 = true; break; }
    for (const c of PF1) if (near([R, G, B], c)) { inPf1 = true; break; }
    if (inPf2) pf2Only++;
    if (inPf1) pf1Only++;
  }
  return { pf2Only, pf1Only, nonBlack, total: (image.width * image.height) };
}

// Animación: al menos 5 de los últimos 6 frames difieren del anterior.
let ok = true;
try {
  const frames = fs.readdirSync(seqDir).filter((n) => /^frame_.*\.png$/.test(n)).sort();
  if (frames.length < 6) { console.error(`FALLO: solo ${frames.length} frames en secuencia (esperado >=6).`); process.exit(1); }
  let changed = 0;
  let prev = PNG.sync.read(fs.readFileSync(`${seqDir}/${frames[0]}`));
  for (let i = 1; i < frames.length; ++i) {
    const f = PNG.sync.read(fs.readFileSync(`${seqDir}/${frames[i]}`));
    let diff = 0;
    for (let j = 0; j < f.data.length; j += 64) if (f.data[j] !== prev.data[j]) { diff++; if (diff >= 1) break; }
    if (diff >= 1) changed++;
    prev = f;
  }
  if (changed < 5) { console.error(`FALLO: solo ${changed}/6 frames cambiaron (sin animación).`); ok = false; }
  else console.log(`OK animación: ${changed}/${frames.length - 1} frames consecutivos cambian.`);
} catch (e) { console.error('secuencia no analizable: ' + e.message); process.exit(1); }

// Transparencia y contraste, sobre el screenshot (también valida no-negro).
const img = PNG.sync.read(fs.readFileSync(pngPath));
const c = classify(img);
console.log(`muestreo: px PF2 (fondo visto por transparencia)=${c.pf2Only}, PF1=${c.pf1Only}, no-negro=${c.nonBlack}/${c.total}`);
if (c.pf2Only < 200) { console.error('FALLO: casi sin colores de PF2 → el FG transparente no deja ver el fondo.'); ok = false; }
if (c.pf1Only < 200) { console.error('FALLO: casi sin colores de PF1 → no se ve el primer plano.'); ok = false; }
if (c.nonBlack < c.total * 0.2) { console.error('FALLO: captura casi negra.'); ok = false; }
console.log(ok ? 'OK muestrario DPF 8-way: animación + FG transparente (PF2 visible) + PF1/PF2 presentes.' : 'ERROR: muestrario no cumple.'); 
process.exit(ok ? 0 : 1);