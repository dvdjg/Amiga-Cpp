#!/usr/bin/env node
// F3-tools · pack-world: empaqueta un mapa de tiles en el formato de MUNDO
// INCRUSTABLE (chunk `WorldMap`, ver docs/engine/architecture/WORLD_FORMAT.md).
//
// Entradas:
//   --tmx   JSON de parse-tmx.mjs (finito con `gids`, o infinito con `chunks`)
//   --bank  JSON de slice-tiles.mjs (banco: `map` sheet-local -> indice de banco)
// Salidas:
//   --out-bin  payload binario del chunk WorldMap (big-endian)
//   --out-h    cabecera C++ para incbin (g_world[] / g_world_size)
//
// El gid de Tiled se convierte a indice de BANCO en el host; el runtime no
// conoce Tiled. Los chunks totalmente vacios se descartan. Se auto-valida el
// round-trip (reconstruir el mapa denso desde los chunks = 100%).
//
// Uso: node tools/ehb/pack-world.mjs --tmx tmx.json --bank tiles.json \
//        [--layer Ground] [--tileset-gid 1] [--chunk 16] [--wrap-x 0] [--wrap-y 0] \
//        [--out-bin out/assets/world/world.bin] [--out-h out/assets/world/world.h]
import fs from 'node:fs';
import path from 'node:path';

const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const intV = (n, d) => { const v = parseInt(argV(n, String(d)), 10); return Number.isFinite(v) ? v : d; };

const tmxPath = argV('--tmx', process.argv[2]);
const bankPath = argV('--bank', '');
if (!tmxPath || !bankPath) {
	console.error('Uso: pack-world.mjs --tmx tmx.json --bank tiles.json [--layer L] [--chunk 16] [--out-bin f] [--out-h f]');
	process.exit(2);
}
const chunk = intV('--chunk', 16);
if (chunk < 1 || (chunk & (chunk - 1)) !== 0) { console.error('[pack-world] --chunk debe ser potencia de dos'); process.exit(2); }
const chunkLog2 = Math.log2(chunk);
const EMPTY = 0xffff;

const tmx = JSON.parse(fs.readFileSync(tmxPath, 'utf8'));
const bank = JSON.parse(fs.readFileSync(bankPath, 'utf8'));
const sheetMap = bank.map || [];
const tsGid = intV('--tileset-gid', 0) || (tmx.tilesets.find((t) => t.name && /ground/i.test(t.name)) || {}).firstgid || tmx.tilesets[0]?.firstgid || 1;
const layerName = argV('--layer', '');
const layer = layerName ? tmx.layers.find((l) => l.name === layerName) : tmx.layers[0];
if (!layer) { console.error('[pack-world] capa no encontrada'); process.exit(2); }

// gid -> indice de banco (0/ausente/fora de rango -> EMPTY).
let missing = 0;
const mapGid = (g) => {
	if (g === 0) return EMPTY;
	const local = g - tsGid;
	if (local < 0 || local >= sheetMap.length || sheetMap[local] == null) { ++missing; return EMPTY; }
	return sheetMap[local] & 0xffff;
};

// Recolecta celdas (x,y)->bank, con bounds del mundo.
let minX = 0, minY = 0, maxX = -1, maxY = -1;
const cells = new Map(); // key "x,y" -> bank
const put = (x, y, g) => {
	cells.set(`${x},${y}`, mapGid(g));
	if (maxX < 0) { minX = maxX = x; minY = maxY = y; }
	if (x < minX) minX = x; if (x > maxX) maxX = x;
	if (y < minY) minY = y; if (y > maxY) maxY = y;
};
if (layer.chunks) {
	for (const c of layer.chunks) {
		for (let ly = 0; ly < c.height; ++ly) for (let lx = 0; lx < c.width; ++lx) {
			put(c.x + lx, c.y + ly, c.gids[ly * c.width + lx] || 0);
		}
	}
} else {
	const W = layer.width, H = layer.height;
	for (let y = 0; y < H; ++y) for (let x = 0; x < W; ++x) put(x, y, layer.gids[y * W + x] || 0);
}
if (maxX < 0) { console.error('[pack-world] capa vacía'); process.exit(1); }

// Normaliza a chunk-grid y agrupa en chunks (cx,cy) -> celdas ordenadas.
const ox = Math.floor(minX / chunk), oy = Math.floor(minY / chunk);
const ncX = Math.floor(maxX / chunk) - ox + 1;
const ncY = Math.floor(maxY / chunk) - oy + 1;
const width = ncX * chunk, height = ncY * chunk;
const chunks = new Map(); // "cx,cy" -> Uint16Array(chunk*chunk) (EMPTY)
for (const [key, v] of cells) {
	const [x, y] = key.split(',').map(Number);
	const cx = Math.floor(x / chunk) - ox, cy = Math.floor(y / chunk) - oy;
	const k = `${cx},${cy}`;
	let arr = chunks.get(k);
	if (!arr) { arr = new Uint16Array(chunk * chunk).fill(EMPTY); chunks.set(k, arr); }
	arr[(y - (cy + oy) * chunk) * chunk + (x - (cx + ox) * chunk)] = v;
}
// Descarta chunks vacíos y ordena por (cy,cx).
const dir = [...chunks.entries()]
	.filter(([, arr]) => arr.some((v) => v !== EMPTY))
	.map(([k, arr]) => { const [cx, cy] = k.split(',').map(Number); return { cx, cy, arr }; })
	.sort((a, b) => (a.cy - b.cy) || (a.cx - b.cx));

// Round-trip: reconstruir y comparar con la fuente.
const rt = new Uint16Array(width * height).fill(EMPTY);
for (const c of dir) for (let ly = 0; ly < chunk; ++ly) for (let lx = 0; lx < chunk; ++lx) {
	const v = c.arr[ly * chunk + lx];
	if (v !== EMPTY) rt[(c.cy * chunk + ly) * width + (c.cx * chunk + lx)] = v;
}
let mism = 0;
for (const [key, v] of cells) {
	const [x, y] = key.split(',').map(Number);
	const ex = rt[(y - oy * chunk) * width + (x - ox * chunk)];
	if (ex !== v) ++mism;
}
if (mism !== 0) { console.error(`[pack-world] round-trip FALLO: ${mism} celdas`); process.exit(1); }

// Payload WorldMap (ver WORLD_FORMAT.md).
const wrapX = intV('--wrap-x', 0), wrapY = intV('--wrap-y', 0);
const HDR = 16, LD = 32, CE = 4;
const dirOff = HDR + LD;
const cellsOff = dirOff + dir.length * CE;
const total = cellsOff + dir.length * chunk * chunk * 2;
const buf = Buffer.alloc(total, 0);
buf.writeUInt16BE(1, 0);                 // version
buf.writeUInt16BE(0, 2);                 // flags
buf[4] = chunkLog2;                      // chunk_log2
buf[5] = 1;                              // layer_count
buf.writeUInt16BE(0, 6);                 // tiles_chunk (lo fija el packer UAF-R)
buf.writeUInt16BE(0, 8);                 // palette_chunk
buf.writeUInt32BE(0, 10);                // reservado
buf.writeUInt16BE(0, 14);                // reservado2
// LayerDesc
buf.writeUInt16BE(0, HDR + 0);           // id
buf.writeUInt16BE(0, HDR + 2);           // kind = tiles
buf.writeUInt16BE(width & 0xffff, HDR + 4);
buf.writeUInt16BE(height & 0xffff, HDR + 6);
buf.writeUInt16BE(wrapX & 0xffff, HDR + 8);
buf.writeUInt16BE(wrapY & 0xffff, HDR + 10);
buf.writeUInt16BE(EMPTY, HDR + 12);
buf.writeUInt16BE(0, HDR + 14);          // meta_count
buf.writeUInt32BE(dirOff, HDR + 16);
buf.writeUInt32BE(dir.length, HDR + 20);
buf.writeUInt32BE(cellsOff, HDR + 24);
buf.writeUInt32BE(0, HDR + 28);          // meta_off
// Directorio + celdas
dir.forEach((c, i) => {
	buf.writeInt16BE(c.cx, dirOff + i * CE);
	buf.writeInt16BE(c.cy, dirOff + i * CE + 2);
	const base = cellsOff + i * chunk * chunk * 2;
	for (let k = 0; k < chunk * chunk; ++k) buf.writeUInt16BE(c.arr[k], base + k * 2);
});

const outBin = path.resolve(argV('--out-bin', 'out/assets/world/world.bin'));
const outH = path.resolve(argV('--out-h', 'out/assets/world/world.h'));
fs.mkdirSync(path.dirname(outBin), { recursive: true });
fs.writeFileSync(outBin, buf);
fs.writeFileSync(outH, [
	'// WorldMap (chunk de tiles) generado por tools/ehb/pack-world.mjs.',
	`// Capa '${layer.name}' ${width}x${height} celdas, chunk ${chunk}x${chunk}, ${dir.length} chunks.`,
	'// Incrustar en .MEMF_CHIP o servir por offset (ver WORLD_FORMAT.md / STREAMING_LOADER.md).',
	'extern "C" const unsigned char g_world[];',
	'extern "C" const unsigned int g_world_size;',
	'',
].join('\n'), 'utf8');

console.log(`[pack-world] capa '${layer.name}' ${width}x${height} (mundo ${minX}..${maxX} x ${minY}..${maxY})`);
console.log(`[pack-world] chunks=${dir.length} celdas/chunk=${chunk * chunk} gid fuera de rango=${missing} round-trip=OK`);
console.log(`[pack-world] -> ${outBin} (${total} B) | ${outH}`);
