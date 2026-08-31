#!/usr/bin/env node
/**
 * verify-scroll-direction.mjs — verifica el vector de scroll de una secuencia de frames.
 *
 * Mide el desplazamiento medio entre pares de frames consecutivos por
 * cross-correlación en un eje (x|y) y comprueba: 1) que el signo coincide con el
 * esperado (`--expect`), 2) que no hay bandas negras internas (artefacto típico
 * de staging/tearing). Para scrolls mixtos (diagonal/HV) usa `--expect 0` para
 * saltarse la comprobación de signo y solo validar negro + que hay movimiento.
 *
 * Convención de signo: el desplazamiento se mide sobre el CONTENIDO en pantalla.
 *   cámara derecha → contenido se mueve a la izquierda → shift X negativo
 *   cámara abajo   → contenido se mueve arriba     → shift Y negativo
 * (para la demo corkscrew, K_EFFECT 1/2/3/4 → expect -1/+1/-1/+1 respectivamente).
 *
 * Uso: node tools/analyze/verify-scroll-direction.mjs --seq <dir> --axis x|y --expect -1|0|+1
 * Salida: `OK shift=... black=...` o `FAIL ...` (exit 1).
 */
import fs from 'fs';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);

function arg(name, def) {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 ? process.argv[i + 1] : def;
}
const seqDir = path.resolve(arg('seq', ''));
const axis = arg('axis', 'y');
const expect = parseInt(arg('expect', '0'), 10);
if (!fs.existsSync(seqDir)) { console.error('No existe la secuencia: ' + seqDir); process.exit(1); }
let PNG; try { PNG = require('pngjs').PNG; } catch (e) { console.error('pngjs no disponible: ' + e.message); process.exit(1); }
const files = fs.readdirSync(seqDir).filter(n => /^frame_.*\.png$/.test(n)).sort();
if (files.length < 3) { console.error('menos de 3 frames'); process.exit(1); }
function readPNG(p) { return PNG.sync.read(fs.readFileSync(p)); }
function gray(img, x, y) { const i = (y * img.width + x) * 4; return (img.data[i] + img.data[i + 1] + img.data[i + 2]) / 3; }

const imgs = files.map(f => readPNG(path.join(seqDir, f)));
// rect activo del primer frame (área no negra)
let left = imgs[0].width, top = imgs[0].height, right = -1, bottom = -1;
for (let y = 0; y < imgs[0].height; ++y) for (let x = 0; x < imgs[0].width; ++x) {
  if (gray(imgs[0], x, y) > 8) {
    if (x < left) left = x; if (y < top) top = y; if (x > right) right = x; if (y > bottom) bottom = y;
  }
}
if (right < 0) { console.error('FAIL: frame vacío (sin contenido)'); process.exit(1); }
const rect = { left, top, right, bottom };

// peor ratio de negro interno (bandas de staging/tearing)
let worstBlack = 0;
for (const img of imgs) {
  for (let y = rect.top + 4; y < rect.bottom - 3; y += 3) {
    let b = 0, t = 0;
    for (let x = rect.left; x <= rect.right; x += 4) { t++; if (gray(img, x, y) < 10) b++; }
    const r = t ? b / t : 1;
    if (r > worstBlack) worstBlack = r;
  }
}

// desplazamiento medio entre pares consecutivos en el eje pedido (cross-correlación)
function shift(a, b, maxShift) {
  let best = 0, bestCost = Infinity;
  for (let d = -maxShift; d <= maxShift; ++d) {
    let cost = 0, samples = 0;
    if (axis === 'x') {
      for (let x = rect.left + 4; x <= rect.right - 4; x += 6) {
        for (let y = rect.top + 4; y < rect.bottom - 3; y += 5) {
          const x2 = x + d; if (x2 < rect.left || x2 > rect.right) continue;
          cost += Math.abs(gray(a, x, y) - gray(b, x2, y)); samples++;
        }
      }
    } else {
      for (let y = rect.top + 4; y < rect.bottom - 3; y += 5) {
        for (let x = rect.left; x <= rect.right; x += 6) {
          const y2 = y + d; if (y2 < rect.top || y2 > rect.bottom) continue;
          cost += Math.abs(gray(a, x, y) - gray(b, x, y2)); samples++;
        }
      }
    }
    if (samples && cost / samples < bestCost) { bestCost = cost / samples; best = d; }
  }
  return best;
}
const shifts = [];
for (let i = 0; i < imgs.length - 1; ++i) shifts.push(shift(imgs[i], imgs[i + 1], 12));
shifts.sort((a, b) => a - b);
const median = shifts[Math.floor(shifts.length / 2)];
const moving = shifts.filter(s => Math.abs(s) > 0).length;

let fail = false;
if (worstBlack > 0.35) { console.error(`FAIL: banda negra interna (tearing/staging) black=${(worstBlack * 100).toFixed(1)}%`); fail = true; }
if (expect !== 0) {
  if (Math.sign(median) !== Math.sign(expect)) {
    console.error(`FAIL: dirección de scroll ${axis} = ${median} (esperado signo ${expect}); frames que se mueven ${moving}/${shifts.length}`);
    fail = true;
  }
} else if (moving === 0) {
  console.error(`FAIL: sin movimiento en ${shifts.length} pares (scroll detenido)`);
  fail = true;
}
console.log(`OK verify-scroll-direction: axis=${axis} expect=${expect} shift=${median} (media), moving=${moving}/${shifts.length}, black=${(worstBlack * 100).toFixed(1)}%`);
process.exit(fail ? 1 : 0);
