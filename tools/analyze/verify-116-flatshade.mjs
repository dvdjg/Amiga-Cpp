import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

/// Verifica la demo 116 (`flatshade-convex`): un objeto CONVEXO girando, relleno
/// por cara con su color de luz. Comprueba que hay un "balon" relleno (blob de
/// pixeles no-fondo, mas o menos redondo y centrado) con varios tonos de luz.
///
/// Uso: node tools/analyze/verify-116-flatshade.mjs <screenshot.png>

const DEFAULT = 'out/run/116_flatshade_convex/A500_debug/screenshot.png';
const file = process.argv[2] || DEFAULT;
if (!fs.existsSync(file)) { console.error('[verify-116] no existe la captura:', file); process.exit(2); }

const img = PNG.sync.read(fs.readFileSync(file));
const W = img.width, H = img.height;
const rgb = (x, y) => { const i = (y * W + x) * 4; return (img.data[i] << 16) | (img.data[i + 1] << 8) | img.data[i + 2]; };

// Color de fondo: el mas frecuente.
const freq = new Map();
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const c = rgb(x, y); freq.set(c, (freq.get(c) || 0) + 1); }
let bg = 0, best = -1;
for (const [c, n] of freq) if (n > best) { best = n; bg = c; }

let count = 0, minX = W, minY = H, maxX = -1, maxY = -1, sx = 0, sy = 0;
const colors = new Set();
// Silueta por fila (primer/ultimo pixel del blob): un objeto CONVEXO tiene un contorno
// suave, asi que el borde izquierdo/derecho no puede saltar mas de unos pocos px entre
// filas consecutivas. Con el area fill XOR mal (paridad rota) el relleno se desmadra en
// bandas y aparecen saltos enormes aunque cobertura/tonos/bbox sigan OK.
const rowL = new Int32Array(H).fill(-1), rowR = new Int32Array(H).fill(-1);
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const c = rgb(x, y);
  if (c === bg || c === 0) continue;
  count++; colors.add(c); sx += x; sy += y;
  if (rowL[y] < 0) rowL[y] = x;
  rowR[y] = x;
  if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y;
}
let maxEdgeJump = 0;
for (let y = 1; y < H; y++) {
  if (rowL[y] < 0 || rowL[y - 1] < 0) continue;
  maxEdgeJump = Math.max(maxEdgeJump, Math.abs(rowL[y] - rowL[y - 1]), Math.abs(rowR[y] - rowR[y - 1]));
}

let fail = 0;
const check = (ok, what) => { if (!ok) { console.error('[FAIL] ' + what); fail++; } else { console.log('[ok] ' + what); } };

if (count === 0) { console.error('[verify-116] no hay balon (todo fondo)'); process.exit(1); }
const bw = maxX - minX + 1, bh = maxY - minY + 1;
const fracPct = (100 * count) / (W * H);
const cx = sx / count, cy = sy / count;

check(colors.size >= 4, `gradiente de luz con >= 4 tonos (${colors.size})`);
check(bw > 0.25 * W && bh > 0.25 * H, `balon de tamano apreciable (${bw}x${bh})`);
check(bw / bh > 0.65 && bw / bh < 1.55, `balon aproximadamente redondo (ratio ${(bw / bh).toFixed(2)})`);
check(Math.abs(cx - W / 2) < 0.20 * W && Math.abs(cy - H / 2) < 0.20 * H,
      `balon centrado (centroide ${cx.toFixed(0)},${cy.toFixed(0)} de ${W}x${H})`);
check(fracPct > 5 && fracPct < 80, `cobertura razonable (${fracPct.toFixed(1)} %)`);
// Silueta convexa: descarta el relleno desmadrado (bandas/triangulos del area fill XOR
// roto). Referencias medidas: balon correcto 14-70 px segun la fase (esquinas del
// poligono proyectado); render con el fill roto ~378 px.
check(maxEdgeJump < 0.30 * W, `silueta convexa (salto maximo de borde ${maxEdgeJump} px)`);

if (fail) { console.error(`[verify-116] FAIL (${fail})`); process.exit(1); }
console.log(`[verify-116] PASS: balon convexo flat-shaded (${colors.size} tonos, ${bw}x${bh}).`);
