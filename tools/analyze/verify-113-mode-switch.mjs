import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

/// Verifica el microtest MI09 (`demos/amiga/113_mode_switch`).
///
/// La demo parte la pantalla en un campo de 5 planos (arriba) y un HUD de 2 planos
/// (abajo) con `ModeSwitchZone`. El chequeo es determinista sobre la captura:
///   - el HUD (ultimas 96 lineas de la ventana) solo puede mostrar los 4 colores de
///     su paleta de 2 planos; si la conmutacion de geometria no se aplicara, los
///     planos "veneno" (0xFF) pintarian indices altos con otros colores;
///   - el campo (arriba) muestra muchos mas colores (>= 5 planos).
///
/// Uso: node tools/analyze/verify-113-mode-switch.mjs <screenshot.png>

const DEFAULT = 'out/run/113_mode_switch/A500_debug/screenshot.png';
const file = process.argv[2] || DEFAULT;
if (!fs.existsSync(file)) {
  console.error('[verify-113] no existe la captura:', file);
  process.exit(2);
}

const img = PNG.sync.read(fs.readFileSync(file));
const W = img.width, H = img.height;
const rgb = (x, y) => { const i = (y * W + x) * 4; return (img.data[i] << 16) | (img.data[i + 1] << 8) | img.data[i + 2]; };

// Caja de la ventana = bounding box de pixeles no negros (fondo 0 = negro).
let minX = W, minY = H, maxX = -1, maxY = -1;
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  if (rgb(x, y) !== 0) { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
}
if (maxX < 0) { console.error('[verify-113] captura en negro'); process.exit(1); }

const boxW = maxX - minX + 1, boxH = maxY - minY + 1;
const sx = boxW / 320, sy = boxH / 256;
const mapY = (line) => Math.round(minY + line * sy);

function distinctIn(y0, y1) {
  const set = new Set();
  for (let y = y0; y < y1; y++) for (let x = minX; x <= maxX; x++) set.add(rgb(x, y));
  return set;
}

// Paleta del HUD (0xe00, 0x0e0, 0x00e expandidos a 8 bits: nibble*17).
const hudSet = new Set([0x000000, 0xee0000, 0x00ee00, 0x0000ee]);

// HUD: lineas de ventana 160..256, recortando 4 lineas de borde.
const hud = distinctIn(mapY(164), mapY(252));
// Campo: lineas de ventana 8..152.
const field = distinctIn(mapY(8), mapY(152));

let fail = 0;
const check = (ok, what) => { if (!ok) { console.error('[FAIL] ' + what); fail++; } else { console.log('[ok] ' + what); } };

check(boxH >= 500 && boxW >= 620, `ventana detectada ${boxW}x${boxH}`);
check(field.size > 8, `campo con muchos colores (${field.size} > 8)`);
check(hud.size <= 4, `HUD con <= 4 colores (${hud.size})`);
for (const c of hud) {
  check(hudSet.has(c), 'HUD color #' + c.toString(16).padStart(6, '0') + ' pertenece a la paleta de 2 planos');
}

if (fail) { console.error(`[verify-113] FAIL (${fail})`); process.exit(1); }
console.log(`[verify-113] PASS: campo=${field.size} colores, HUD=${hud.size} colores (geometria conmutada).`);
