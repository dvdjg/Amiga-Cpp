#!/usr/bin/env node
// ---------------------------------------------------------------------------
// emit-202.mjs — Assets de la demo 202 (DPF 3+3):
//   BG : mapa real "Beginning Fields" a 8 colores (producido por amiga-tiles.mjs
//        --colors 8 --xlimited en out/demo202/bg) -> se reusa su banco 3 planos.
//   FG : plaquettes sintéticas (7 colores + transparencia, layout X-Limited 3
//        planos) + un mapa decorativo disperso.
// Emite `out/demo202/const_202.h` (paletas 12-bit, mapas y conteos) y el banco
// del FG (`out/demo202/fg/tilebank_xlimited.bin`). El BG incrusta su propio
// banco ya generado (ver README).
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BG_DIR = path.join(ROOT, 'out/demo202/bg');
const OUT_DIR = path.join(ROOT, 'out/demo202');
const FG_DIR = path.join(OUT_DIR, 'fg');
fs.mkdirSync(FG_DIR, { recursive: true });

function to444(c) { return ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4); }

// --------------------------- BG (leído de amiga-tiles) ----------------------
const bgJsonPath = fs.readdirSync(BG_DIR).find((f) => /^palette_.*\.json$/.test(f));
if (!bgJsonPath) { console.error('No hay palette_*.json en ' + BG_DIR); process.exit(1); }
const bg = JSON.parse(fs.readFileSync(path.join(BG_DIR, bgJsonPath), 'utf8'));
if (bg.colors !== 8) { console.error('BG debe ser 8 colores, es ' + bg.colors); process.exit(1); }
const kBgPaletteWords = bg.palette.map(to444); // 8 palabras (regs PF2 8..15)
const kBgCols = bg.cols, kBgRows = bg.rows;    // 40 x 40
const kBgTiles = bg.stats.unique;
const kBgMap = bg.map;                          // flat u16 ids (row-major)
console.log(`BG: ${kBgCols}x${kBgRows} mapa, ${kBgTiles} tiles, paleta ${kBgPaletteWords.map(w=>w.toString(16)).join(',')}`);

// --------------------------- FG plaquettes sintéticas -----------------------
// Paleta del FG (PF1, registros 0..7). Índice 0 = transparente (deja ver el BG).
const FG_WORDS = [0x000, 0xe44, 0xf86, 0xfe0, 0x4c8, 0x48e, 0xc5f, 0xfdf];
// Variante de placa por tile: base 4..6 y acento 1..3/7 (todas dentro de la paleta).
const FG_BASE = [4, 5, 6, 4, 5, 6, 4, 5, 6, 7, 7, 7];
const FG_ACC  = [7, 2, 3, 1, 2, 3, 3, 1, 7, 2, 3, 1];
const T = 16;
function plaqueTile(v) {
  // v en 0..11. Devuelve Uint8Array[T*T] con índices 0..7.
  const base = FG_BASE[v], acc = FG_ACC[v];
  const out = new Uint8Array(T * T);
  for (let y = 0; y < T; y++) for (let x = 0; x < T; x++) {
    // Perímetro = marco (color 1), bisel interior (color 2), interior (base).
    const ring = Math.min(x, y, T - 1 - x, T - 1 - y);
    const corner = (x < 3 || x >= T - 3) && (y < 3 || y >= T - 3) && ring >= 2;
    let c = 0;
    if (ring === 0) c = 1;            // marco exterior
    else if (ring === 1) c = 2;       // bisel
    else if (corner) c = 2;           // esquinas interiores redondeadas (bisel)
    else c = base;                    // cuerpo de la placa
    out[y * T + x] = c;
  }
  // Símbolo central (varía con v) en color acento.
  const cx = 7, cy = 7; // 8x8 zona central (x,y = cx..cx+7)
  const s = v % 4;
  for (let yy = 0; yy < 8; yy++) for (let xx = 0; xx < 8; xx++) {
    const px = cx + xx, py = cy + yy;
    let on = false;
    if (s === 0) on = Math.abs(xx - 3.5) + Math.abs(yy - 3.5) <= 3;         // rombo
    else if (s === 1) on = (Math.abs(xx - 3) <= 0.5 && yy <= 6) || (Math.abs(yy - 3) <= 0.5 && xx <= 6); // cruz
    else if (s === 2) on = Math.abs(xx - 3.5) === Math.abs(yy - 3.5);        // aspa
    else on = Math.max(Math.abs(xx - 3.5), Math.abs(yy - 3.5)) <= 2;         // anillo relleno
    if (on) out[py * T + px] = acc;
  }
  return out;
}
// tile 0 = vacío transparente (todo a 0). Plaquetas = ids 1..12.
const fgBankPix = [new Uint8Array(T * T)];
for (let v = 0; v < 12; v++) fgBankPix.push(plaqueTile(v));
const FG_TILES = fgBankPix.length; // 13

// Mapa del FG: rejilla propia (ancho distinto al BG, wrap toroidal) con placas
// muy dispersas (~1/41 celdas, ~2 %) sobre el tile 0 transparente: el BG (mapa
// real) se ve a través casi en toda la pantalla y quedan filas libres para
// medir el parallax de cada capa por separado.
const FG_COLS = 48, FG_ROWS = 40;
const fgMap = new Uint16Array(FG_COLS * FG_ROWS);
for (let y = 0; y < FG_ROWS; y++) for (let x = 0; x < FG_COLS; x++) {
  // Hash pseudoaleatorio determinista (xorshift barato) -> dispersión sin
  // alinearse en columnas/filas (rejilla visible) del mapa de placas.
  let h = (x * 31 + y * 57) | 0;
  h ^= h << 13; h ^= h >>> 17; h ^= h << 5; h |= 0;
  fgMap[y * FG_COLS + x] = (h % 41) === 0 ? (1 + ((x * 5 + y * 13) % (FG_TILES - 1))) : 0;
}
// Banco X-Limited del FG: layout 320px, planelínea = row*planes+plane (3 planos),
// tile t -> (t%20, t/20), words big-endian. Índice bit p -> plano p.
const planes = 3;
const fgOut = Buffer.alloc(40 * Math.ceil(FG_TILES / 20) * T * planes, 0);
for (let t = 0; t < FG_TILES; t++) {
  const bx = t % 20, by = Math.floor(t / 20);
  const base = by * (T * planes) * 40 + bx * (T / 8);
  for (let row = 0; row < T; row++) for (let pl = 0; pl < planes; pl++) {
    const bit = 1 << pl; let word = 0;
    for (let c = 0; c < T; c++) if (fgBankPix[t][row * T + c] & bit) word |= 0x8000 >> c;
    const off = base + (row * planes + pl) * 40;
    fgOut[off] = (word >> 8) & 0xff; fgOut[off + 1] = word & 0xff;
  }
}
fs.writeFileSync(path.join(FG_DIR, 'tilebank_xlimited.bin'), fgOut);
console.log(`FG: mapa ${FG_COLS}x${FG_ROWS}, ${FG_TILES} tiles, banco ${fgOut.length} B, paleta ${FG_WORDS.map(w=>w.toString(16)).join(',')}`);

// --------------------------- Header const ------------------------------------
const L = [];
L.push('// out/demo202/const_202.h — generado por tools/demo202/emit-202.mjs');
L.push('// No editar a mano. Paletas en formato Amiga 12-bit (0x0RGB).');
L.push('#pragma once');
L.push('#include <eng/core/types.hpp>');
L.push('');
L.push('// --- FG (PF1, primer plano): plaquettes 7 colores + transparencia ---');
L.push('static constexpr eng::u16 kFgPalette[8] {');
for (let r = 0; r < 8; r += 4) L.push('    ' + FG_WORDS.slice(r, r + 4).map((w) => '0x' + w.toString(16).padStart(3, '0')).join(', ') + ',');
L.push('};');
L.push(`static constexpr eng::u16 kFgCols = ${FG_COLS};`);
L.push(`static constexpr eng::u16 kFgRows = ${FG_ROWS};`);
L.push(`static constexpr eng::u16 kFgTiles = ${FG_TILES};`);
L.push('static constexpr eng::u16 kFgMap[' + FG_COLS * FG_ROWS + '] {');
for (let y = 0; y < FG_ROWS; y++) L.push('    ' + Array.from(fgMap.slice(y * FG_COLS, (y + 1) * FG_COLS)).join(', ') + ',');
L.push('};');
L.push('');
L.push('// --- BG (PF2, fondo): mapa real "Beginning Fields" a 8 colores ---------');
L.push('static constexpr eng::u16 kBgPalette[8] {');
for (let r = 0; r < 8; r += 4) L.push('    ' + kBgPaletteWords.slice(r, r + 4).map((w) => '0x' + w.toString(16).padStart(3, '0')).join(', ') + ',');
L.push('};');
L.push(`static constexpr eng::u16 kBgCols = ${kBgCols};`);
L.push(`static constexpr eng::u16 kBgRows = ${kBgRows};`);
L.push(`static constexpr eng::u16 kBgTiles = ${kBgTiles};`);
L.push('static constexpr eng::u16 kBgMap[' + kBgCols * kBgRows + '] {');
for (let y = 0; y < kBgRows; y++) L.push('    ' + kBgMap.slice(y * kBgCols, (y + 1) * kBgCols).join(', ') + ',');
L.push('};');
L.push('');
fs.writeFileSync(path.join(OUT_DIR, 'const_202.h'), L.join('\n') + '\n', 'utf8');
console.log('OK -> out/demo202/const_202.h');
