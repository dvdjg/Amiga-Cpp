import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

/// Verifica el microtest MI09 EHB (`demos/amiga/115_mode_switch_ehb_hud`): campo
/// EHB (6 planos) arriba + HUD de 4 planos SIN EHB (BPLCON4=0 por zona) abajo. Es
/// el uso real de 201. La franja del HUD debe mostrar 2^4 = 16 colores (sin
/// half-brite), y el campo > 32 (EHB).
///
/// Uso: node tools/analyze/verify-115-mode-switch-ehb.mjs <screenshot.png>

const DEFAULT = 'out/run/115_mode_switch_ehb_hud/A500_debug/screenshot.png';
const file = process.argv[2] || DEFAULT;
if (!fs.existsSync(file)) { console.error('[verify-115] no existe la captura:', file); process.exit(2); }

const img = PNG.sync.read(fs.readFileSync(file));
const W = img.width, H = img.height;
const rgb = (x, y) => { const i = (y * W + x) * 4; return (img.data[i] << 16) | (img.data[i + 1] << 8) | img.data[i + 2]; };

let minX = W, minY = H, maxX = -1, maxY = -1;
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  if (rgb(x, y) !== 0) { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
}
if (maxX < 0) { console.error('[verify-115] captura en negro'); process.exit(1); }
const sy = (maxY - minY + 1) / 256;
const mapY = (line) => Math.round(minY + line * sy);
function distinctIn(l0, l1) {
  const set = new Set();
  for (let y = mapY(l0); y < mapY(l1); y++) for (let x = minX; x <= maxX; x++) set.add(rgb(x, y));
  return set.size;
}

let fail = 0;
const check = (ok, what) => { if (!ok) { console.error('[FAIL] ' + what); fail++; } else { console.log('[ok] ' + what); } };

const field = distinctIn(8, 152);
const hud = distinctIn(168, 252);
check(field > 32, `campo EHB con mas de 32 colores (${field})`);
check(hud <= 16, `HUD sin EHB con <= 16 colores (${hud})`);
check(hud >= 14, `HUD cubre sus 4 planos (${hud} >= 14)`);

if (fail) { console.error(`[verify-115] FAIL (${fail})`); process.exit(1); }
console.log(`[verify-115] PASS: campo EHB=${field} colores, HUD 4 planos sin EHB=${hud}.`);
