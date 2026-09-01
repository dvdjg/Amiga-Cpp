#!/usr/bin/env node
// F3-tools Â· Slicer EHB-first: convierte el ORIGINAL a la paleta EHB, extrae los
// tiles Ãºnicos (dedupe exacto + fusiÃ³n por similitud opcional), reconstruye y
// compara la versiÃ³n cuantizada del ORIGINAL con la reconstruida (debe dar 100%
// sin fusiÃ³n). Emite PNG indexados (encoder propio PLTE+IDAT) y el .h para Amiga.
//
// Uso: node tools/ehb/slice-tiles.mjs <png> --palette out/ehb/palette.json
//      [--ehb-merge 0.0..1.0] [--sheet-scale 1|2] [--classify] [--out dir]
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { deflateSync } from 'node:zlib';
import { PNG } from 'pngjs';

const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const img = process.argv[2];
if (!img) { console.error('Uso: slice-tiles.mjs <png> --palette palette.json [--ehb-merge F]'); process.exit(2); }
const tile = parseInt(argV('--tile', '16'), 10);
const outDir = argV('--out', 'out/ehb');
const ehbMerge = parseFloat(argV('--ehb-merge', '1'));
const sheetScale = parseInt(argV('--sheet-scale', '1'), 10);
const classify = process.argv.includes('--classify');

const png = PNG.sync.read(fs.readFileSync(img));
const cols = png.width / tile, rows = png.height / tile;
if (png.width % tile || png.height % tile) { console.error('[slice] dimensiones no mÃºltiplo de tile'); process.exit(1); }
fs.mkdirSync(outDir, { recursive: true });
console.log(`[slice] ${png.width}x${png.height} -> ${cols}x${rows} tiles de ${tile}`);

// --- 1) PALETA: bases (y half si EHB) DESDE quantize-ehb ---------------------
// Solo se reserva el índice 0 para transparente si la fuente tiene píxeles
// transparentes. Si es opaca (p. ej. un tileset de suelo): 16 colores = 4 bits/
// píxel sin desperdiciar slots.
let bases = [], planes = 6;
try { const j = JSON.parse(fs.readFileSync(argV('--palette', ''), 'utf8')); bases = Array.isArray(j.bases) ? j.bases : []; if (typeof j.planes === 'number') planes = j.planes; } catch { }
if (!bases.length) { console.error('[slice] requiere --palette palette.json (corre quantize-ehb primero)'); process.exit(1); }
const wantAlpha = !process.argv.includes('--no-alpha');
const alphaN = (() => { let n = 0; for (let i = 0; i < png.width * png.height; i++) if (png.data[i * 4 + 3] < 128) n++; return n; })();
// Solo reservar índice 0 para transparente si es significativo y deseado; si el
// atlas tiene huecos pero los TILES del mapa son opacos, con --no-alpha se usan
// 16 colores reales y 4 bits/px (los px transparentes van al base más cercano).
const hasAlphaSrc = wantAlpha && (alphaN * 100) / (png.width * png.height) >= 0.5;
const paletteI = [];
if (hasAlphaSrc) paletteI.push([0, 0, 0]); // índice 0 = transparente (si hace falta)
for (const b of bases) { paletteI.push([b[0] & 255, b[1] & 255, b[2] & 255]); if (planes >= 6) paletteI.push([b[0] >> 1, b[1] >> 1, b[2] >> 1]); }
const palSize = paletteI.length;
console.log(`[slice] paleta ${palSize} colores (${palSize <= 16 ? '4 bits/px' : palSize <= 32 ? '5 bits/px' : 'EHB 64'}${hasAlphaSrc ? ', índice 0 transparente' : ' sin transparencia'})`);
const nearest = (r, g, bl) => { let bi = 0, dmin = Infinity; for (let q = 0; q < palSize; q++) { const p = paletteI[q]; const dr = r - p[0], dg = g - p[1], db = bl - p[2]; const d = dr * dr + dg * dg + db * db; if (d < dmin) { dmin = d; bi = q; } } return bi; };

// --- 2) CUANTIZAR EL ORIGINAL A LA PALETA PRIMERO (origEhb) -------------------
const rW = png.width, rH = png.height;
const origEhb = new Uint8Array(rW * rH);
for (let y = 0; y < rH; y++) for (let x = 0; x < rW; x++) {
  const o = (y * rW + x) * 4;
  const a = png.data[o + 3];
  // Transparencia→slot 0 solo si reservamos slot; con --no-alpha los px del
  // atlas (huecos) van también al base más próximo (16 colores / 4 bits/px).
  origEhb[y * rW + x] = (hasAlphaSrc && a < 128) ? 0 : nearest(png.data[o], png.data[o + 1], png.data[o + 2]);
}
console.log(`[slice] original cuantizado a EHB (${palSize} colores) listo`);

// --- 3) EXTRAER tiles Ãºnicos EXACTOS desde origEhb ----------------------------
const bank = [];          // {x,y,pix:Uint8Array(256)}
const map = new Array(cols * rows);
const seen = new Map();
for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < cols; tx++) {
  const pix = new Uint8Array(tile * tile);
  for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++) pix[yy * tile + xx] = origEhb[(ty * tile + yy) * rW + (tx * tile + xx)];
  const key = Buffer.from(pix).toString('base64');
  if (seen.has(key)) { map[ty * cols + tx] = seen.get(key); }
  else { seen.set(key, bank.length); map[ty * cols + tx] = bank.length; bank.push({ x: tx, y: ty, pix }); }
}
console.log(`[slice] tiles Ãºnicos (EHB exacto): ${bank.length} de ${cols * rows}`);

// --- 4) FUSIÃ“N por similitud en Ã­ndices EHB (--ehb-merge) ----------------------
if (ehbMerge < 1) {
  const repsA = []; const rem = new Map(); let absorbed = 0;
  const eqFrac = (a, b) => { let eq = 0; for (let i = 0; i < a.pix.length; i++) if (a.pix[i] === b.pix[i]) eq++; return eq / a.pix.length; };
  for (let i = 0; i < bank.length; i++) {
    let take = -1, best = 0;
    for (let r = 0; r < repsA.length; r++) { const f = eqFrac(bank[i], repsA[r]); if (f >= ehbMerge && f > best) { best = f; take = r; } }
    if (take >= 0) { rem.set(i, take); absorbed++; } else { rem.set(i, repsA.length); repsA.push(bank[i]); }
  }
  bank.length = 0; bank.push(...repsA);
  for (let t = 0; t < map.length; t++) map[t] = rem.get(map[t]);
  console.log(`[slice] fusiÃ³n EHB (>=${ehbMerge} Ã­ndices iguales): ${bank.length} Ãºnicos (${absorbed} absorbidos)`);
}

// --- 5) RECONSTRUIR y COMPARAR con el original cuantizado (origEhb) ------------
const reconIdx = new Uint8Array(rW * rH);
for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < cols; tx++) {
  const b = bank[map[ty * cols + tx]];
  for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++) reconIdx[(ty * tile + yy) * rW + (tx * tile + xx)] = b.pix[yy * tile + xx];
}
let eq = 0;
for (let i = 0; i < reconIdx.length; i++) if (origEhb[i] === reconIdx[i]) eq++;
const pct = (eq / reconIdx.length) * 100;
console.log(`[slice] COMPARAR: original(cuantizado EHB) vs reconstruido = ${pct.toFixed(2)}% Ã­ndices iguales (${reconIdx.length})`);
if (ehbMerge === 1 && pct < 100) { console.error('[slice] FALLO: sin fusiÃ³n la reconstrucciÃ³n debe cuadrar 100%'); process.exit(1); }

// --- 6) Exports: .h + tiles.json + PNG indexados ------------------------------
const bits = palSize <= 16 ? 4 : palSize <= 32 ? 5 : 6;
const perByte = bits === 6 ? 1 : 8 / bits; // 16col->2 px/byte, 32col->1 px/byte (5b) ó 6b->1
const bankBytes = Math.ceil((bank.length * tile * tile) / perByte);
const bankPacked = new Uint8Array(bankBytes);
for (let i = 0; i < bank.length; i++) {
  for (let p = 0; p < tile * tile; p++) {
    const v = bank[i].pix[p];
    const idx = i * tile * tile + p;
    if (perByte === 2) { const b = idx >> 1; bankPacked[b] = (idx & 1) ? ((bankPacked[b] & 0xf0) | (v & 0x0f)) : ((bankPacked[b] & 0x0f) | (((v & 0x0f) << 4))); }
    else bankPacked[idx] = v;
  }
}
const bankInd = bank.map((b) => ({ x: b.x, y: b.y }));
// RLE del banco (píxel por píxel): tiles cuantizados tienen zonas planas, así
// `(count,value)` comprime drásticamente. `kTileBankRleOffset[i]` = comienzo del
// tile i en `kTileBankRle`; decodificar: pares (len, idx) hasta completar 256 px.
const rleOff = new Uint16Array(bank.length);
const rleData = [];
for (let i = 0; i < bank.length; i++) {
  rleOff[i] = rleData.length;
  const p = bank[i].pix;
  let cur = p[0], cnt = 0;
  for (let q = 0; q < p.length; q++) { if (p[q] === cur) cnt++; else { rleData.push(cnt, cur); cur = p[q]; cnt = 1; } }
  rleData.push(cnt, cur);
}
const hLines = [];
hLines.push('// Tiles EHB slice (original cuantizado primero), para carga en el Amiga.');
hLines.push(`// ${bits} bits/píxel (${palSize} colores${hasAlphaSrc ? ', índice0 transparente' : ''}); ${bank.length} tiles de ${tile}x${tile}; mapa ${cols}x${rows}.`);
hLines.push(`// BANCO en RLE (${rleData.length} bytes) + offsets (${bank.length}u16). Decodificar por tile: pares (len,índice) hasta 256 px.`);
hLines.push(`static const unsigned char kTileIndexedPalette[${palSize * 3}] = {`);
for (let r = 0; r < palSize; r += 12) hLines.push('  ' + paletteI.slice(r, r + 12).map((c) => `${c[0]},${c[1]},${c[2]}`).join(',') + ',');
hLines.push('};');
hLines.push(`static const unsigned short kTileBankRleOffset[${bank.length}] = {`);
for (let r = 0; r < bank.length; r += 20) hLines.push('  ' + [...rleOff.slice(r, r + 20)].join(',') + ',');
hLines.push('};');
hLines.push(`static const unsigned char kTileBankRle[${rleData.length}] = {`);
for (let r = 0; r < rleData.length; r += 32) hLines.push('  ' + rleData.slice(r, r + 32).join(',') + ',');
hLines.push('};');
hLines.push(`static const unsigned short kTileIndexedMap[${map.length}] = {`);
for (let i = 0; i < map.length; i += 24) hLines.push('  ' + map.slice(i, i + 24).join(',') + ',');
hLines.push('};');
const hPath = path.join(outDir, 'tilebank_indexed.h');
fs.writeFileSync(hPath, hLines.join('\n') + '\n', 'utf8');
fs.writeFileSync(path.join(outDir, 'tiles.json'), JSON.stringify({ tile, cols, rows, palette: paletteI, bank: bankInd.map((b, i) => ({ x: b.x, y: b.y, pix: [...bank[i].pix] })), map, stats: { unique: bank.length, cells: cols * rows } }, null, 2), 'utf8');
console.log(`[slice] export indexado -> ${hPath} (${palSize} colores, ${bank.length} tiles)`);

// --- 7) PNG indexados (encoder propio) + verify round-trip ---------------------
function crc32(buf) { let c, t = crc32.table || (crc32.table = new Int32Array(256).map((_, n) => { c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; })); let crc = -1; for (let i = 0; i < buf.length; i++) crc = (crc >>> 8) ^ t[(crc ^ buf[i]) & 0xff]; return (crc ^ -1) >>> 0; }
function pngChunk(type, data) { const t = Buffer.from(type, 'ascii'), len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([t, data])), 0); return Buffer.concat([len, t, data, crc]); }
function writeIndexedPng(filePath, palette, indices, w, h) {
  const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 3;
  const plte = Buffer.alloc(palette.length * 3);
  palette.forEach((c, i) => { plte[i * 3] = c[0]; plte[i * 3 + 1] = c[1]; plte[i * 3 + 2] = c[2]; });
  const rows = [];
  for (let y = 0; y < h; y++) { const r = Buffer.alloc(w + 1); r[0] = 0; for (let x = 0; x < w; x++) r[x + 1] = indices[y * w + x]; rows.push(r); }
  const idat = deflateSync(Buffer.concat(rows));
  fs.writeFileSync(filePath, Buffer.concat([sig, pngChunk('IHDR', ihdr), pngChunk('PLTE', plte), pngChunk('IDAT', idat), pngChunk('IEND', Buffer.alloc(0))]));
}
function verifyPng(filePath, palette, indices, w, h) {
  const d = PNG.sync.read(fs.readFileSync(filePath));
  let dif = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const o = (y * w + x) * 4, p = palette[indices[y * w + x]] || palette[0] || [0, 0, 0];
    if (d.data[o] !== p[0] || d.data[o + 1] !== p[1] || d.data[o + 2] !== p[2]) dif++;
  }
  const bytes = fs.statSync(filePath).size, raw = w * h;
  const ok = d.colorType === 3 && dif === 0;
  console.log(`[verify] ${path.basename(filePath)}: colorType=${d.colorType} ${w}x${h} raw=${raw} png=${bytes} (${((bytes / raw) * 100).toFixed(1)}%) roundtrip=${dif} -> ${ok ? 'OK' : 'FALLO'}`);
  if (!ok) process.exit(1);
}
const reconPng = path.join(outDir, 'reconstruct.png');
writeIndexedPng(reconPng, paletteI, reconIdx, rW, rH);
verifyPng(reconPng, paletteI, reconIdx, rW, rH);
console.log(`[slice] reconstruct (del banco) -> ${reconPng} (${palSize} colores)`);

{
  const perRow = Math.max(1, Math.floor((png.width / tile) / sheetScale));
  const sw = tile * sheetScale;
  const cW = perRow * sw, cH = sw * Math.ceil(bank.length / perRow);
  const inds = new Uint8Array(cW * cH);
  for (let i = 0; i < bank.length; i++) {
    const b = bank[i];
    const ox = (i % perRow) * sw, oy = Math.floor(i / perRow) * sw;
    for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++) {
      const v = b.pix[yy * tile + xx];
      for (let dy = 0; dy < sheetScale; dy++) for (let dx = 0; dx < sheetScale; dx++) inds[(oy + yy * sheetScale + dy) * cW + (ox + xx * sheetScale + dx)] = v;
    }
  }
  const sheetPath = path.join(outDir, 'tilebank.png');
  writeIndexedPng(sheetPath, paletteI, inds, cW, cH);
  verifyPng(sheetPath, paletteI, inds, cW, cH);
  console.log(`[slice] tilebank (${sheetScale}x) -> ${sheetPath} (${bank.length} tiles, ${cW}x${cH})`);
}

if (classify) {
  const d = path.join(outDir, '_vk'); fs.mkdirSync(d, { recursive: true });
  fs.copyFileSync(path.join(outDir, 'tilebank.png'), path.join(d, 'frame_000.png'));
  const r = spawn('node', [path.join(process.cwd(), 'tools', 'analyze', 'ollama-desc.mjs'), d, '0', 'Banco de tiles de un juego: describe QUÃ‰ materiales se ven (hierba, tierra, agua, muro, camino, roca, decoraciÃ³n). MÃ¡x 50 palabras.']);
  r.stdout.pipe(process.stdout); r.stderr.pipe(process.stderr);
}