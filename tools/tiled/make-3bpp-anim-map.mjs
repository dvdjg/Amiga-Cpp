#!/usr/bin/env node
// Convierte un mapa completo indexado (salida `.idx` de png2amiga, 3bpp) en un
// mundo planar interleaved para `FlatScrollPlayfield` y reserva dos índices de
// paleta para animar fuego/flores por **paleta** (sin blits por frame).
//
// Uso:
//   node tools/tiled/make-3bpp-anim-map.mjs --idx map.idx --palette palette.json
//        --animations animations.h --width 640 --height 640 --out out/assets/tiled/<x>
//
// - Las celdas de las capas 0 (Flowers) y 2 (Object Layer 1 = fuego) se pintan
//   con los índices reservados 6 y 7; el resto de píxeles que usaban 6/7 se
//   remapea al color más cercano de 0..5 (así la animación de paleta no cambia
//   tejados/vegetación).
// - Emite `world_3bpp.bin` (layout Amiga `[fila][plano][40 B]`), `world_3bpp.h`
//   (paleta y constantes), `map_anim_preview.png` (simulación hardware para
//   visión) y `map_anim.json` (informe).
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const arg = (name, fallback = null) => {
  const at = process.argv.indexOf(name);
  return at >= 0 && at + 1 < process.argv.length ? process.argv[at + 1] : fallback;
};
const idxPath = arg('--idx');
const palettePath = arg('--palette');
const animPath = arg('--animations');
const basePngPath = arg('--base');
const livePngPath = arg('--live');
const width = parseInt(arg('--width', '640'), 10);
const height = parseInt(arg('--height', '640'), 10);
const outDir = arg('--out', 'out/assets/tiled/beginning-fields-3bpp');
if (!idxPath || !palettePath) {
  console.error('uso: make-3bpp-anim-map.mjs --idx map.idx --palette palette.json [--animations animations.h] --out dir');
  process.exit(2);
}

const paletteJson = JSON.parse(fs.readFileSync(palettePath, 'utf8'));
const basePalette = paletteJson.palette.map((p) => ({
  idx: p.idx,
  r: parseInt(p.rgb.slice(0, 2), 16),
  g: parseInt(p.rgb.slice(2, 4), 16),
  b: parseInt(p.rgb.slice(4, 6), 16),
}));
if (basePalette.length > 8) {
  console.error(`[3bpp] la paleta tiene ${basePalette.length} colores; se esperaban <=8`);
  process.exit(1);
}
// Índices reservados de la animación (paleta de 8 colores).
const kFlowerIndex = 6;
const kFireIndex = 7;
const palette = basePalette.slice(0, 6).map((p) => ({ ...p }));
palette.push({ idx: kFlowerIndex, r: 0xe0, g: 0x60, b: 0xa0 }); // flor
palette.push({ idx: kFireIndex, r: 0xff, g: 0x88, b: 0x00 });   // fuego

function nearestMapColor(r, g, b) {
  let best = 0;
  let bestD = Infinity;
  for (let i = 0; i < 6; ++i) {
    const p = palette[i];
    const dr = r - p.r;
    const dg = g - p.g;
    const db = b - p.b;
    const d = dr * dr + dg * dg + db * db;
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

// Celdas vivas (capa 0 flores, capa 2 fuego) desde el header de animaciones.
const liveLayer = new Uint8Array(width * height);
let cellCount = 0;
if (animPath && fs.existsSync(animPath)) {
  const text = fs.readFileSync(animPath, 'utf8');
  const block = text.match(/kTiledAnimCells\[[^\]]*\]\s*=\s*\{([\s\S]*?)\n\};/);
  if (block) {
    const re = /\{\s*(\d+)u?\s*,\s*(\d+)u?\s*,\s*(\d+)u?\s*,\s*0u?\s*\}/g;
    for (const m of block[1].matchAll(re)) {
      const cx = parseInt(m[1], 10);
      const cy = parseInt(m[2], 10);
      const layer = parseInt(m[3], 10);
      if (layer !== 0 && layer !== 2) continue;
      const px0 = cx * 16;
      const py0 = cy * 16;
      for (let y = py0; y < py0 + 16 && y < height; ++y) {
        for (let x = px0; x < px0 + 16 && x < width; ++x) {
          liveLayer[y * width + x] = layer === 0 ? kFlowerIndex : kFireIndex;
        }
      }
      ++cellCount;
    }
  }
}

const idx = fs.readFileSync(idxPath);
if (idx.length !== width * height) {
  console.error(`[3bpp] ${idxPath} tiene ${idx.length} B; se esperaban ${width * height}`);
  process.exit(1);
}
// Máscara real del arte vivo: píxeles que cambian entre el bake base y el
// compuesto con la primera pose de las capas vivas. Sin ella, la celda entera
// se pintaría del color reservado (bloques rosa/naranja).
const liveMask = new Uint8Array(width * height);
if (basePngPath && livePngPath && fs.existsSync(basePngPath) && fs.existsSync(livePngPath)) {
  const base = PNG.sync.read(fs.readFileSync(basePngPath));
  const live = PNG.sync.read(fs.readFileSync(livePngPath));
  if (base.width !== width || base.height !== height || live.width !== width || live.height !== height) {
    console.error('[3bpp] base/live no coinciden con --width/--height');
    process.exit(1);
  }
  for (let i = 0; i < width * height; ++i) {
    const o = i * 4;
    if (base.data[o] !== live.data[o] || base.data[o + 1] !== live.data[o + 1] ||
        base.data[o + 2] !== live.data[o + 2]) liveMask[i] = 1;
  }
} else {
  // Sin base/live, la máscara es toda la celda (fallback histórico).
  for (let i = 0; i < liveLayer.length; ++i) if (liveLayer[i] !== 0) liveMask[i] = 1;
}
const outIdx = Buffer.alloc(idx.length);
for (let i = 0; i < idx.length; ++i) {
  const live = liveMask[i] !== 0 ? liveLayer[i] : 0;
  if (live !== 0) {
    // Solo los píxeles con tinta del arte vivo; el fondo (0) se conserva.
    outIdx[i] = idx[i] === 0 ? 0 : live;
  } else if (idx[i] >= 6) {
    const c = palette[idx[i]];
    outIdx[i] = nearestMapColor(c.r, c.g, c.b);
  } else {
    outIdx[i] = idx[i];
  }
}

// --- Mundo planar interleaved: [fila][plano][40 B] (3 planos, 640 px) ---
const rowBytes = width / 8;
const world = Buffer.alloc(rowBytes * height * 3, 0);
for (let y = 0; y < height; ++y) {
  for (let xByte = 0; xByte < rowBytes; ++xByte) {
    let b0 = 0, b1 = 0, b2 = 0;
    for (let bit = 0; bit < 8; ++bit) {
      const v = outIdx[y * width + xByte * 8 + bit];
      const mask = 0x80 >> bit;
      if (v & 1) b0 |= mask;
      if (v & 2) b1 |= mask;
      if (v & 4) b2 |= mask;
    }
    const base = (y * 3) * rowBytes + xByte;
    world[base] = b0;
    world[base + rowBytes] = b1;
    world[base + rowBytes * 2] = b2;
  }
}

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'world_3bpp.bin'), world);
fs.writeFileSync(path.join(outDir, 'map_anim.idx'), outIdx);

// --- Preview 24-bit (simulación hardware) para visión ---
const preview = new PNG({ width, height });
for (let i = 0; i < width * height; ++i) {
  const c = palette[outIdx[i]];
  const o = i * 4;
  preview.data[o] = c.r;
  preview.data[o + 1] = c.g;
  preview.data[o + 2] = c.b;
  preview.data[o + 3] = 255;
}
fs.writeFileSync(path.join(outDir, 'map_anim_preview.png'), PNG.sync.write(preview));

const words = palette.map((c) => ((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4));
const paletteJsonOut = { depth: 3, chipset: 'ocs', mode: 'lores', palette };
fs.writeFileSync(path.join(outDir, 'palette_anim.json'), `${JSON.stringify(paletteJsonOut, null, 2)}\n`);
const header = `// Mundo 3bpp Beginning Fields generado por tools/tiled/make-3bpp-anim-map.mjs.
// Fuente: ${path.basename(idxPath)} (png2amiga --mode lores --depth 3 --dither floyd-steinberg).
#pragma once
#include <eng/core/types/types.hpp>

#define TILED3_MAP_W ${width}u
#define TILED3_MAP_H ${height}u
#define TILED3_PLANES 3u
#define TILED3_ROW_BYTES ${rowBytes}u
#define TILED3_WORLD_BYTES ${world.length}u

static const eng::u16 kPalette3[8] = {
  ${words.map((w) => `0x${w.toString(16).padStart(4, '0')}u`).join(', ')}
};
static constexpr eng::u8 kFlowerColorIndex = ${kFlowerIndex}u;
static constexpr eng::u8 kFireColorIndex = ${kFireIndex}u;
`;
fs.writeFileSync(path.join(outDir, 'world_3bpp.h'), header);
fs.writeFileSync(path.join(outDir, 'map_anim.json'), `${JSON.stringify({
  idx: idxPath,
  palette: palettePath,
  animations: animPath,
  width,
  height,
  planes: 3,
  liveCells: cellCount,
  flowerIndex: kFlowerIndex,
  fireIndex: kFireIndex,
  worldBytes: world.length,
  palette: palette
}, null, 2)}\n`);
console.log(`[3bpp] ${width}x${height}, ${cellCount} celdas vivas, mundo ${world.length} B, paleta 6+2`);
