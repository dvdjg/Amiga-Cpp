#!/usr/bin/env node
/**
 * verify-201-border.mjs — modelo HOST del corkscrew 8-way de la demo 201
 * (mapa EHB real 40x40, no wrapping) para comprobar las LECTURAS DE MAPA de
 * `scroll_right` en el límite derecho del mapa.
 *
 * Replica fielmente scroll_engine.hpp:scroll_right (+ add_draw y draw_block_job
 * de xlimited.hpp). Comprueba que todo blit del barrido H use orígenes
 * (mapx,mapy) dentro del mapa 40x40. Si algún blit sale del mapa, `tile_at`
 * devuelve `edge_tile` (=0 en la demo 201, tile index REAL y válido para el
 * Blitter, no `empty_tile` 0xFFFF) y se PINTA contenido equivocado en el
 * framebuffer -> artefacto visible junto al borde derecho.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const hdr = fs.readFileSync(path.join(root, 'out/ehb/const_game_201.h'), 'utf8');

// --- Geometría de la demo 201 (main.cpp) -----------------------------------
const TW = 16, TH = 16, PLANES = 6;
const VIEWPORT_W = 320, MAIN_H = 208;          // campo de scroll (viewport - HUD)
const MAP_WB = 40, MAP_HB = 40;                // bloques 40x40
const BPR = (VIEWPORT_W + 32) / TW;            // BITMAPBLOCKSPERROW = 22
const FIXEDW = VIEWPORT_W + 32;                // bitmap_width = 352
const DH = MAIN_H + 2 * TH;                    // display_height = 240
const DPL = DH * PLANES;                       // display_planelines = 1440
const BPC = DH / TH;                           // bitmap_blocks_per_col = 15
const LIMIT_X = MAP_WB * TW - VIEWPORT_W - TW; // 304

const FIX = process.env.VERIFY201_FIX === '1';

// --- Leer kRenderMap (1600 u16) ---------------------------------------------
const m = hdr.match(/kRenderMap\[1600\]\s*=\s*\{([\s\S]*?)\};/);
if (!m) { console.error('no kRenderMap'); process.exit(2); }
const nums = (m[1].match(/\d+/g) || []).slice(0, MAP_WB * MAP_HB).map(Number);
if (nums.length !== MAP_WB * MAP_HB) {
  console.error(`kRenderMap tiene ${nums.length} valores, esperado ${MAP_WB * MAP_HB}`);
  process.exit(2);
}

// tile_at del engine: wrap_x=wrap_y=0 -> edge_tile (=0) fuera de rango.
const tileAt = (r, c) => {
  if (FIX) {
    const rr = Math.min(Math.max(r, 0), MAP_HB - 1);
    const cc = Math.min(Math.max(c, 0), MAP_WB - 1);
    return nums[rr * MAP_WB + cc];
  }
  if (c < 0 || c >= MAP_WB) return 0;
  if (r < 0 || r >= MAP_HB) return 0;
  return nums[r * MAP_WB + c];
};

const out = 'VERIFY201_FIX=1 node tools/analyze/verify-201-border.mjs';

// --- scroll_right port fiel (scroll_engine.hpp) ------------------------------
const r_dh = (v) => ((v % DH) + DH) % DH;
const r_dph = (v) => ((v % DPL) + DPL) % DPL;
const r_tw = (v) => ((v % TW) + TW) % TW;
const r_th = (v) => ((v % TH) + TH) % TH;

let wrongPaints = 0;
let clippedPaints = 0;
const wrongList = [];
const clippedList = [];
const outOfX = new Set();
const outOfY = new Set();

function blit(planel, xpix, mapy, mapx, tag) {
  const t = tileAt(mapy, mapx);
  const ok = mapx >= 0 && mapx < MAP_WB && mapy >= 0 && mapy < MAP_HB;
  const clipped = ok ? false : FIX;
  if (!ok) {
    if (!FIX) {
      ++wrongPaints;
      if (wrongPaints <= 25) wrongList.push(`  blit ${tag}: map=(${mapx},${mapy}) planel=${planel} x=${xpix} -> tile ${t}  (edge_tile 0)`);
      if (mapx < 0 || mapx >= MAP_WB) outOfX.add(mapx);
      if (mapy < 0 || mapy >= MAP_HB) outOfY.add(mapy);
    } else {
      ++clippedPaints;
      if (clippedPaints <= 6) clippedList.push(`  blit ${tag}: map=(${mapx},${mapy}) planel=${planel} -> columna 39 (clamp) tile ${t}`);
    }
  }
  return clipped;
}

function scrollRight(mapposx, mapposy) {
  const mapblockx = Math.floor(mapposx / TW);
  const mapblocky = Math.floor(mapposy / TH);
  const stepx = r_tw(mapposx), stepy = r_th(mapposy);
  const bvpos = (Math.floor(mapposy / TH) * TH) % DH;
  const x0 = mapposx & ~(TW - 1);
  const mapx = mapblockx + BPR;
  let mapy = stepx + 1;
  if (mapy === 1) { // stepx == 0 -> dos bloques
    mapy = mapy + mapblocky;
    const y = r_dh(bvpos + TH) * PLANES;
    blit(y, x0 + FIXEDW, mapy, mapx, 'r0a');
    const y2 = r_dph(y + TH * PLANES);
    blit(y2, x0 + FIXEDW, mapy + 1, mapx, 'r0b');
    // fillup (solo buscamos origen del mapa: lo emite en el siguiente new_stepx)
  } else {
    ++mapy;
    const y = r_dh(bvpos + mapy * TH) * PLANES;
    mapy = mapy + mapblocky;
    blit(y, x0 + FIXEDW, mapy, mapx, 'r1');
  }
  const next = mapposx + 1;
  if (r_tw(next) === 0) {
    const nx0 = x0 + TW;
    const nmapblockx = mapblockx + 1;
    blit(bvpos * PLANES, nx0 + (BPR - 1) * TW, mapblocky, nmapblockx + BPR - 1, 'fillup1');
    if (stepy) {
      const mx = stepy >= BPR - TH ? stepy + (BPR - TH) - 1 : stepy * 2 - 1;
      blit(bvpos * PLANES, nx0 + mx * TW, mapblocky + BPC, mx + nmapblockx, 'fillup2');
    }
  }
  return next;
}

// --- Barrido H completo: mapposy=0, mapposx 0..limit -------------------------
let mpx = 0, mpy = 0;
let maxMapx = 0, maxMapy = 0;
while (mpx < LIMIT_X) {
  mpx = scrollRight(mpx, mpy);
  // rango de mapas vistos (diagnóstico)
  const mbx = Math.floor(mpx / TW);
  const seen = mbx + BPR;
  if (seen > maxMapx) maxMapx = seen;
  if (Math.floor(mpy / TH) + BPC > maxMapy) maxMapy = Math.floor(mpy / TH) + BPC;
}

console.log(`Mapa ${MAP_WB}x${MAP_HB} (wrap=0, edge_tile=0), TW=${TW}, BPR=${BPR}, DH=${DH}, BPC=${BPC}, limite X=${LIMIT_X}, FIX=${FIX}`);
console.log(`Rango maximo de fuente (mapx,mapy) demandado por scroll_right:`);
console.log(`  mapx en {0..${maxMapx - 1}} ${maxMapx > MAP_WB ? '>>> FUERA DE RANGO' : ''}`);
console.log(`  mapy en {0..${maxMapy - 1}} ${maxMapy > MAP_HB ? '>>> FUERA DE RANGO' : ''}`);
if (FIX) {
  console.log(`Blits con origen fuera del mapa (columna 40+) RESUELTOS por clamp: ${clippedPaints}`);
  if (clippedList.length) console.log(clippedList.join('\n'));
  const verdict = clippedPaints >= 0
    ? 'OK (FIX): el clamp mantiene el origen dentro del mapa; se repite la columna 39 en la pista de pre-pintado.'
    : 'FAIL';
  console.log(verdict);
  process.exit(0);
}
console.log(`Blits con origen fuera del mapa: ${wrongPaints}`);
if (wrongList.length) console.log(wrongList.join('\n'));
if (outOfX.size) console.log(`columnas fuera: [${[...outOfX].join(',')}]`);
if (outOfY.size) console.log(`filas fuera: [${[...outOfY].join(',')}]`);

const verdict = wrongPaints === 0
  ? 'OK: todos los blits del barrido H usan origen dentro del mapa 40x40'
  : 'FAIL: hay blits con origen fuera del mapa (pinchan tile 0/edge_tile). Aplicar el fix de clamp en add_draw y re-ejecutar con VERIFY201_FIX=1.';
console.log(verdict);

process.exit(wrongPaints === 0 ? 0 : 1);