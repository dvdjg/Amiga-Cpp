import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

/// Verifica el microtest MI09 ampliado (`demos/techniques/amiga/playfield/114_mode_switch_bands`):
/// campo de 5 planos + franjas apiladas de 4, 3 y 2 planos con `ModeSwitchZone`.
/// Cada franja pinta barras que recorren todos los indices de su profundidad, de
/// modo que su region solo puede mostrar <= 2^planos colores distintos. Si una
/// conmutacion fallara, la franja seguiria leyendo mas planos y mostraria mas
/// colores (o basura).
///
/// Uso: node tools/analyze/verify-114-mode-switch-bands.mjs <screenshot.png>

const DEFAULT = 'out/run/114_mode_switch_bands/A500_debug/screenshot.png';
const file = process.argv[2] || DEFAULT;
if (!fs.existsSync(file)) { console.error('[verify-114] no existe la captura:', file); process.exit(2); }

const img = PNG.sync.read(fs.readFileSync(file));
const W = img.width, H = img.height;
const rgb = (x, y) => { const i = (y * W + x) * 4; return (img.data[i] << 16) | (img.data[i + 1] << 8) | img.data[i + 2]; };

let minX = W, minY = H, maxX = -1, maxY = -1;
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  if (rgb(x, y) !== 0) { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
}
if (maxX < 0) { console.error('[verify-114] captura en negro'); process.exit(1); }
const boxH = maxY - minY + 1;
const sy = boxH / 256;
const mapY = (line) => Math.round(minY + line * sy);

function distinctIn(l0, l1) {
  const set = new Set();
  for (let y = mapY(l0); y < mapY(l1); y++) for (let x = minX; x <= maxX; x++) set.add(rgb(x, y));
  return set.size;
}

// Franjas (lineas de ventana) y su profundidad.
const bands = [
  { name: 'campo 5 planos', l0: 4, l1: 76, planes: 5 },
  { name: 'zona 4 planos', l0: 82, l1: 126, planes: 4 },
  { name: 'zona 3 planos', l0: 130, l1: 174, planes: 3 },
  { name: 'zona 2 planos', l0: 179, l1: 253, planes: 2 },
];

let fail = 0;
const check = (ok, what) => { if (!ok) { console.error('[FAIL] ' + what); fail++; } else { console.log('[ok] ' + what); } };

for (const b of bands) {
  const limit = 1 << b.planes;
  const n = distinctIn(b.l0, b.l1);
  check(n <= limit, `${b.name}: ${n} colores <= ${limit}`);
  const low = b.planes >= 5 ? limit - 4 : limit - 2;
  check(n >= low, `${b.name}: ${n} colores cubre su profundidad (>= ${low})`);
}

if (fail) { console.error(`[verify-114] FAIL (${fail})`); process.exit(1); }
console.log('[verify-114] PASS: campo 5 + franjas 4/3/2 planos con geometria conmutada.');
