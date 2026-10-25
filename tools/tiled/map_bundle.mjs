#!/usr/bin/env node
// Formato `MapBundle` v1 (host) y emisión de la cabecera C para el runtime.
//
// Formato big-endian (m68k), pensado para leerse sin heap en `eng::map::MapAssetView`.
// Es la primera versión **baked**: un banco de tiles compartido + una o más capas de
// celdas u16 (índice de banco) + una paleta de palabras Amiga. Los IDs estables,
// catálogo de páginas y objetos llegarán en versiones posteriores; el header ya
// reserva flags y versión para evolucionar.
//
// Cabecera (40 B):
//   0  'EMAP'
//   4  u16 version
//   6  u16 flags
//   8  u8  tile_w
//   9  u8  tile_h
//   10 u8  planes
//   11 u8  chunk_log2 (0 = baked)
//   12 u16 layer_count
//   14 u16 palette_count
//   16 u16 tile_count
//   18 u16 reserved
//   20 u32 bank_bytes (banco externo)
//   24 u32 layers_offset
//   28 u32 palette_offset
//   32 u32 total_size
//   36 u32 reserved
// LayerDesc (32 B): id, mode, width, height, origin_x, origin_y, wrap_x, wrap_y,
//   empty_tile, reserved, cells_offset, cells_bytes, reserved.
// Paleta: palette_count palabras u16 0x0RGB. Celdas: u16 BE row-major.

export const MAP_BUNDLE_MAGIC = 'EMAP';
export const MAP_BUNDLE_VERSION = 1;
export const MAP_LAYER_DESC_SIZE = 32;
export const MAP_BUNDLE_HEADER_SIZE = 40;

export const LAYER_MODES = Object.freeze({
	Baked: 0,
	Base: 1,
	Overlay: 2,
	Occluder: 3,
	Parallax: 4,
	CollisionOnly: 5,
});

const toWord = (rgb) => ((((rgb[0] ?? 0) & 0xf0) << 4) | (((rgb[1] ?? 0) & 0xf0) << 0) | (((rgb[2] ?? 0) & 0xf0) >> 4));

export function packMapBundle({ tileWidth, tileHeight, planes, tileCount, bankBytes, palette, layers }) {
	if (tileWidth <= 0 || tileHeight <= 0 || planes < 1 || planes > 6) throw new Error('MapBundle: geometría inválida');
	if (!Array.isArray(palette) || palette.length !== 1 << planes) throw new Error('MapBundle: palette_count debe ser 2^planes');
	if (!Array.isArray(layers) || layers.length === 0 || layers.length > 0xffff) throw new Error('MapBundle: layer_count inválido');
	if (tileCount > 0xffff) throw new Error('MapBundle: tile_count > 65535 (v1)');
	const paletteOffset = MAP_BUNDLE_HEADER_SIZE + layers.length * MAP_LAYER_DESC_SIZE;
	const cellsStart = paletteOffset + palette.length * 2;
	let total = cellsStart;
	for (const layer of layers) {
		if (layer.width <= 0 || layer.height <= 0) throw new Error('MapBundle: capa sin dimensiones');
		if (!Array.isArray(layer.cells) || layer.cells.length !== layer.width * layer.height) throw new Error('MapBundle: celdas no cuadran');
		total += layer.cells.length * 2;
	}
	const buf = Buffer.alloc(total, 0);
	buf.write(MAP_BUNDLE_MAGIC, 0, 'ascii');
	buf.writeUInt16BE(MAP_BUNDLE_VERSION, 4);
	buf.writeUInt16BE(0, 6);
	buf[8] = tileWidth;
	buf[9] = tileHeight;
	buf[10] = planes;
	buf[11] = 0;
	buf.writeUInt16BE(layers.length, 12);
	buf.writeUInt16BE(palette.length, 14);
	buf.writeUInt16BE(tileCount, 16);
	buf.writeUInt16BE(0, 18);
	buf.writeUInt32BE(bankBytes >>> 0, 20);
	buf.writeUInt32BE(MAP_BUNDLE_HEADER_SIZE, 24);
	buf.writeUInt32BE(paletteOffset, 28);
	buf.writeUInt32BE(total, 32);
	buf.writeUInt32BE(0, 36);
	let cellsOffset = cellsStart;
	for (let i = 0; i < layers.length; ++i) {
		const layer = layers[i];
		const at = MAP_BUNDLE_HEADER_SIZE + i * MAP_LAYER_DESC_SIZE;
		buf.writeUInt16BE(layer.id ?? i, at + 0);
		buf.writeUInt16BE(layer.mode ?? 0, at + 2);
		buf.writeUInt16BE(layer.width, at + 4);
		buf.writeUInt16BE(layer.height, at + 6);
		buf.writeInt16BE(layer.originX ?? 0, at + 8);
		buf.writeInt16BE(layer.originY ?? 0, at + 10);
		buf.writeUInt16BE(layer.wrapX ?? 0, at + 12);
		buf.writeUInt16BE(layer.wrapY ?? 0, at + 14);
		buf.writeUInt16BE(layer.emptyTile ?? 0xffff, at + 16);
		buf.writeUInt16BE(0, at + 18);
		buf.writeUInt32BE(cellsOffset, at + 20);
		buf.writeUInt32BE(layer.cells.length * 2, at + 24);
		buf.writeUInt32BE(0, at + 28);
		for (let k = 0; k < layer.cells.length; ++k) buf.writeUInt16BE(layer.cells[k] & 0xffff, cellsOffset + k * 2);
		cellsOffset += layer.cells.length * 2;
	}
	for (let i = 0; i < palette.length; ++i) buf.writeUInt16BE(toWord(palette[i]), paletteOffset + i * 2);
	return buf;
}

export function bundleHeader(name, buffer) {
	const lines = [
		`// MapBundle v1 (${buffer.length} B) generado por tools/tiled/cook-map.mjs.`,
		`// Formato big-endian; consumir con eng::map::MapAssetView.`,
		'#pragma once',
		`#define TILED_MAP_PLANES ${buffer[10]}u`,
		`static const unsigned char ${name}[] = {`,
	];
	for (let i = 0; i < buffer.length; i += 16) {
		const row = [...buffer.subarray(i, i + 16)].map((value) => `0x${value.toString(16).padStart(2, '0')}`).join(', ');
		lines.push(`\t${row},`);
	}
	lines.push('};');
	lines.push(`static const unsigned int ${name}_size = ${buffer.length}u;`);
	lines.push('');
	return lines.join('\n');
}
