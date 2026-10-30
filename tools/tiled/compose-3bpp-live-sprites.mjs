#!/usr/bin/env node
// Compone las capas vivas (flores y fuego) en un mapa 3bpp ya cuantizado.
//
// `make-3bpp-anim-map.mjs` reserva índices de paleta por celda, pero el arte
// vivo no está horneado en `composed.png` (fuego/flores son overlays de la 207).
// Este paso pega **frame 0 con su máscara** en el índice reservado:
//   - flores: pétalos -> índice 6; tallos/hojas -> color base más cercano.
//   - fuego: 32×32 fusionado -> anillo en colores base, llama -> índice 7.
// Sin este paso el fuego queda como una mancha naranja sin anillo.
//
// Uso:
//   node tools/tiled/compose-3bpp-live-sprites.mjs \
//     --idx out/assets/tiled/beginning-fields-3bpp/map.idx \
//     --palette out/assets/tiled/beginning-fields-3bpp/palette_anim.json \
//     --animations out/assets/tiled/beginning-fields-live-flowers/selected/animations.h \
//     --anim-json out/assets/tiled/beginning-fields-live-flowers/selected/animations.json \
//     --sheet out/assets/tiled/beginning-fields-live-flowers/selected/anim_sheet.bin \
//     --masks out/assets/tiled/beginning-fields-live-flowers/selected/anim_masks.bin \
//     --bundle out/assets/tiled/beginning-fields-live-flowers/selected/map.bundle \
//     --out out/assets/tiled/beginning-fields-3bpp
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const arg = (name, fallback = null) => {
  const at = process.argv.indexOf(name);
  return at >= 0 && at + 1 < process.argv.length ? process.argv[at + 1] : fallback;
};
const width = parseInt(arg('--width', '640'), 10);
const height = parseInt(arg('--height', '640'), 10);
const idxPath = arg('--idx');
const palettePath = arg('--palette');
const animationsPath = arg('--animations');
const animJsonPath = arg('--anim-json');
const sheetPath = arg('--sheet');
const masksPath = arg('--masks');
const bundlePath = arg('--bundle');
const outDir = arg('--out', path.dirname(idxPath ?? 'out/assets/tiled/beginning-fields-3bpp'));
if (!idxPath || !palettePath || !animationsPath || !animJsonPath || !sheetPath || !masksPath || !bundlePath) {
  console.error('faltan argumentos: --idx --palette --animations --anim-json --sheet --masks --bundle');
  process.exit(2);
}

const palette = JSON.parse(fs.readFileSync(palettePath, 'utf8')).palette;
const basePalette = palette.slice(0, 6);
const idx = new Uint8Array(fs.readFileSync(idxPath));
if (idx.length !== width * height) {
  console.error(`[3bpp-live] ${idxPath} tiene ${idx.length} B; se esperaban ${width * height}`);
  process.exit(1);
}
const sheet = new Uint8Array(fs.readFileSync(sheetPath));
const masks = new Uint8Array(fs.readFileSync(masksPath));
const groups = JSON.parse(fs.readFileSync(animJsonPath, 'utf8')).groups;
const bundle = new Uint8Array(fs.readFileSync(bundlePath));
const be16 = (o) => (bundle[o] << 8) | bundle[o + 1];
const be32 = (o) => (bundle[o] << 24) | (bundle[o + 1] << 16) | (bundle[o + 2] << 8) | bundle[o + 3];
const mapPaletteCount = be16(14);
const mapPaletteOffset = be32(28);
const mapPalette = [];
for (let i = 0; i < mapPaletteCount; ++i) {
  const w = be16(mapPaletteOffset + i * 2);
  mapPalette.push([((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17]);
}

const text = fs.readFileSync(animationsPath, 'utf8');
const block = text.match(/kTiledAnimCells\[[^\]]*\]\s*=\s*\{([\s\S]*?)\n\};/);
if (!block) {
  console.error('[3bpp-live] no se encontró kTiledAnimCells');
  process.exit(1);
}
const cells = [];
for (const m of block[1].matchAll(/\{\s*(\d+)u?\s*,\s*(\d+)u?\s*,\s*(\d+)u?/g)) {
  cells.push([parseInt(m[1], 10), parseInt(m[2], 10), parseInt(m[3], 10)]);
}

const nearestBase = (r, g, b) => {
  let best = 0;
  let bestD = Infinity;
  for (const p of basePalette) {
    const d = (r - p.r) ** 2 + (g - p.g) ** 2 + (b - p.b) ** 2;
    if (d < bestD) { bestD = d; best = p.idx; }
  }
  return best;
};
// Quita todos los índices reservados: las celdas vivas se rellenan después
// con la máscara real, nunca con la celda completa.
for (let i = 0; i < idx.length; ++i) {
  if (idx[i] >= 6) {
    const c = palette[idx[i]];
    idx[i] = nearestBase(c.r, c.g, c.b);
  }
}

function pasteGroup(group) {
  const w = group.pixel_width || 16;
  const h = group.pixel_height || 16;
  const row = w / 8;
  const planeBytes = row * h;
  const frameBase = group.frame_stride ? group.sheet_offset : group.first_frame * 192;
  const maskBase = group.frame_stride ? group.mask_offset : group.first_frame * 32;
  if (!w || w % 16 !== 0 || !h) return;
  for (let k = 0; k < group.cell_count; ++k) {
    const ci = group.first_cell + k;
    if (ci >= cells.length) break;
    const [cx, cy, layer] = cells[ci];
    if (w > 16 && k !== 0) continue;
    const x0 = cx * 16;
    const y0 = cy * 16;
    for (let y = 0; y < h && y0 + y < height; ++y) {
      for (let x = 0; x < w && x0 + x < width; ++x) {
        const mByte = masks[maskBase + y * row + (x >> 3)];
        if ((mByte & (0x80 >> (x & 7))) === 0) continue;
        let v = 0;
        for (let p = 0; p < 6; ++p) {
          const b = sheet[frameBase + p * planeBytes + y * row + (x >> 3)];
          if ((b & (0x80 >> (x & 7))) !== 0) v |= 1 << p;
        }
        const [r, g, b] = mapPalette[v] ?? [0, 0, 0];
        let out;
        if (layer === 0 && !(g > r && g > b)) out = 6;
        else if (layer === 2 && r >= 190 && b < 100) out = 7;
        else out = nearestBase(r, g, b);
        idx[(y0 + y) * width + (x0 + x)] = out;
      }
    }
  }
}
for (const group of groups) {
  if (!group.cell_count || group.first_cell >= cells.length) continue;
  const layer = cells[group.first_cell][2];
  if (layer === 0 || layer === 2) pasteGroup(group);
}

const rowBytes = width / 8;
const world = Buffer.alloc(rowBytes * height * 3, 0);
for (let y = 0; y < height; ++y) {
  for (let xb = 0; xb < rowBytes; ++xb) {
    let b0 = 0, b1 = 0, b2 = 0;
    for (let bit = 0; bit < 8; ++bit) {
      const v = idx[y * width + xb * 8 + bit];
      const mask = 0x80 >> bit;
      if (v & 1) b0 |= mask;
      if (v & 2) b1 |= mask;
      if (v & 4) b2 |= mask;
    }
    const base = (y * 3) * rowBytes + xb;
    world[base] = b0;
    world[base + rowBytes] = b1;
    world[base + rowBytes * 2] = b2;
  }
}
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'map_anim.idx'), idx);
fs.writeFileSync(path.join(outDir, 'world_3bpp.bin'), world);

const preview = new PNG({ width, height });
for (let i = 0; i < width * height; ++i) {
  const c = palette[idx[i]];
  const o = i * 4;
  preview.data[o] = c.r;
  preview.data[o + 1] = c.g;
  preview.data[o + 2] = c.b;
  preview.data[o + 3] = 255;
}
fs.writeFileSync(path.join(outDir, 'map_anim_preview.png'), PNG.sync.write(preview));
console.log(`[3bpp-live] ${width}x${height}: flores/fuego compuestos con máscara`);
