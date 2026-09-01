#!/usr/bin/env node
// F3-tools · gid→bank: convierte los gids de un .tmx (parse-tmx) en índices del
// banco EHB de su tileset (slice-tiles), generando el mapa virtual para la demo.
//
// Uso: node tools/ehb/gid-to-bank.mjs <tmx.json> --bank <tiles.json> \
//        [--tileset-gid <firstgid del tileset del banco>] [--layer Ground] \
//        [--out mapa_ehb.h] [--layers]
import fs from 'node:fs';
import path from 'node:path';
const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const tmxJson = argV('--tmx', process.argv[2]);
const bankJson = argV('--bank', '');
if (!tmxJson || !bankJson) { console.error('Uso: gid-to-bank.mjs <tmx.json> --bank tiles.json [--tileset-gid N] [--layer L] [--out mapa.h]'); process.exit(2); }
const tmx = JSON.parse(fs.readFileSync(tmxJson, 'utf8'));
const bank = JSON.parse(fs.readFileSync(bankJson, 'utf8'));
const layerName = argV('--layer', 'Ground');
const tsGid = parseInt(argV('--tileset-gid', '0'), 10) || (tmx.tilesets.find((t) => t.name && t.name.includes('Ground')) || {}).firstgid || tmx.tilesets[0]?.firstgid || 1;
const out = argV('--out', 'mapa_ehb.h');

// mapa del sheet (célula fila-primaria del tileset → índice de banco)
const sheetMap = bank.map || [];
const bankCount = bank.stats?.unique || bank.bank?.length || 0;
const layer = tmx.layers.find((l) => l.name === layerName) || tmx.layers[0];
if (!layer) { console.error('[gid2bank] capa no encontrada'); process.exit(2); }
const W = layer.width, H = layer.height;

const outIdx = new Array(layer.gids.length).fill(0);
let used = new Set(), missing = 0, cellsN = 0;
for (let i = 0; i < layer.gids.length; i++) {
  const g = layer.gids[i];
  if (g === 0) continue;
  cellsN++;
  const local = g - tsGid;
  if (local < 0 || local >= sheetMap.length) { missing++; continue; }
  const b = sheetMap[local];
  outIdx[i] = b; used.add(b);
}
console.log(`[gid2bank] capa '${layer.name}': celdas llenas=${cellsN} tiles usados=${used.size} (banco ${bankCount}) fueraDeRango=${missing} tsGid=${tsGid}`);

const h = [];
h.push('// Mapa EHB (gid→banco) generado por gid-to-bank.');
h.push(`// Capa '${layer.name}' ${W}x${H}; banco de ${bankCount} tiles (índice 0 = vacío).`);
h.push(`static const unsigned char kMapaEhB[${W * H}] = {`);
for (let r = 0; r < H; r++) h.push('  ' + outIdx.slice(r * W, (r + 1) * W).join(',') + ',');
h.push('};');
if (out) { fs.writeFileSync(path.resolve(argV('--out-dir', '.'), out), h.join('\n') + '\n', 'utf8'); }
if (process.argv.includes('--print')) console.log(h.join('\n'));

// --png <f>: render del mapa (preview host) desde banco+mapa+paleta.
const pngOut = argV('--png', '');
if (pngOut) {
  const { createRequire } = await import('node:module');
  const require = createRequire(import.meta.url);
  const { PNG } = require('pngjs');
  const pal = bank.palette || [];
  const ts = parseInt(argV('--tile', '16'), 10);
  const tw = W * ts, th = H * ts;
  const pix = new PNG({ width: tw, height: th });
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const ti = outIdx[y * W + x];
    const tp = (bank.bank[ti]?.pix || []);
    for (let yy = 0; yy < ts; yy++) for (let xx = 0; xx < ts; xx++) {
      const o = ((y * ts + yy) * tw + (x * ts + xx)) * 4;
      const c = pal[tp[yy * ts + xx]] || [0, 0, 0];
      pix.data[o] = c[0]; pix.data[o + 1] = c[1]; pix.data[o + 2] = c[2]; pix.data[o + 3] = 255;
    }
  }
  fs.writeFileSync(pngOut, PNG.sync.write(pix));
  console.log(`[gid2bank] preview mapa -> ${pngOut} (${tw}x${th})`);
}