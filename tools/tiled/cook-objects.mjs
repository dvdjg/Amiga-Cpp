#!/usr/bin/env node
// Cocina los objetos de primer plano/occluder de un mapa Tiled como arte
// planar + máscara 1bpp y emite la tabla `MapObjects` para el runtime.
//
// Salida en `outDir`:
//   - `objects.h`: `kMapObjects`, `kMapObjectImages` y contadores.
//   - `object_sheet.bin`: por objeto, planos contiguos; por plano, filas de
//     `rowWords = width/16 + 1` palabras (la última es guarda a cero para el
//     shift fino del Blitter).
//   - `object_masks.bin`: un plano 1bpp con el mismo layout de fila.
//   - `object_pair.bin`: hoja interleaved pair con guarda por mitad
//     (`[imagen k][guarda k][máscara k][guarda k]` por fila y plano); la consume
//     `eng::graphics::Bob` con `BobMaskPack::InterleavedPair` en **un solo blit**.
//   - `objects.json`: informe de roles, imágenes y bytes.
//
// Uso:
//   node tools/tiled/cook-objects.mjs --ir ir.json --plan plan.json \
//        --palette palette.json --planes 6 --out selected/

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeGid, parseTmxFile } from './parse-tmx.mjs';
import { collectMapObjects } from './map-objects.mjs';
import { nearest, paletteColors } from './palette.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const TILE_EPS = 1e-6;

const argValue = (args, name) => {
	const at = args.indexOf(name);
	return at >= 0 ? args[at + 1] : null;
};

const asNumber = (value, fallback = 0) => {
	const n = Number(value);
	return Number.isFinite(n) ? n : fallback;
};

/// Añade el tileoffset del tileset al rectángulo del objeto (misma regla que
/// `compose-map`; sin él, un objeto de colección con offset quedaría desplazado
/// respecto a su versión horneada).
function applyTileOffset(rect, image) {
	const offset = image.tileoffset ?? { x: 0, y: 0 };
	rect.x += asNumber(offset.x, 0);
	rect.y += asNumber(offset.y, 0);
	return rect;
}

/// Empaqueta una imagen a planos Amiga con guarda por fila.
/// \param {object} image entrada de `collectMapObjects().images`.
/// \param {number[][]} colors paleta del perfil.
/// \param {number} planes planos destino.
/// \returns {{sheet:Buffer, mask:Buffer, rowWords:number, width:number, height:number}}
function packObject(image, colors, planes) {
	const png = image.image.png;
	const sx = image.image.sx;
	const sy = image.image.sy;
	const nativeW = image.width;
	const nativeH = image.height;
	const width = Math.ceil(nativeW / 16) * 16;
	const rowWords = width / 16 + 1;
	const flags = image.flags ?? decodeGid(image.gid);
	const indices = new Uint8Array(width * nativeH);
	const mask = new Uint8Array(rowWords * 2 * nativeH);
	for (let y = 0; y < nativeH; ++y) {
		const rowBase = y * rowWords * 2;
		for (let x = 0; x < width; ++x) {
			if (x >= nativeW) continue;
			let u = flags.flipH ? nativeW - 1 - x : x;
			let v = flags.flipV ? nativeH - 1 - y : y;
			if (flags.flipD) {
				const t = u;
				u = v;
				v = t;
			}
			const so = ((sy + v) * png.width + (sx + u)) * 4;
			if (png.data[so + 3] < 128) continue;
			const index = nearest(colors, png.data[so], png.data[so + 1], png.data[so + 2]);
			indices[y * width + x] = index;
			const bit = 0x80 >> (x & 7);
			mask[rowBase + (x >> 3)] |= bit;
		}
	}
	const sheet = Buffer.alloc(planes * nativeH * rowWords * 2);
	for (let p = 0; p < planes; ++p) {
		for (let y = 0; y < nativeH; ++y) {
			let word = 0;
			for (let x = 0; x < width; ++x) {
				if ((indices[y * width + x] & (1 << p)) !== 0) word |= 0x8000 >> (x & 15);
				if ((x & 15) === 15) {
					const at = ((p * nativeH + y) * rowWords + (x >> 4)) * 2;
					sheet[at] = (word >> 8) & 0xff;
					sheet[at + 1] = word & 0xff;
					word = 0;
				}
			}
		}
	}
	// Hoja par interleaved para el cookie-cut de un solo blit: por cada fila y
	// plano del destino, [imagen k][guarda k][máscara k][guarda k].
	const k = width / 16;
	const pair = Buffer.alloc(nativeH * planes * 4 * k * 2);
	for (let y = 0; y < nativeH; ++y) {
		for (let p = 0; p < planes; ++p) {
			const rowBase = (y * planes + p) * 4 * k * 2;
			for (let w = 0; w < k; ++w) {
				let img = 0;
				let msk = 0;
				for (let b = 0; b < 16; ++b) {
					const x = w * 16 + b;
					const color = indices[y * width + x];
					if ((color & (1 << p)) !== 0) img |= 0x8000 >> b;
					if ((mask[y * rowWords * 2 + (x >> 3)] & (0x80 >> (x & 7))) !== 0) {
						msk |= 0x8000 >> b;
					}
				}
				pair[rowBase + w * 2] = (img >> 8) & 0xff;
				pair[rowBase + w * 2 + 1] = img & 0xff;
				pair[rowBase + (k + w) * 2] = 0;
				pair[rowBase + (k + w) * 2 + 1] = 0;
				pair[rowBase + (2 * k + w) * 2] = (msk >> 8) & 0xff;
				pair[rowBase + (2 * k + w) * 2 + 1] = msk & 0xff;
				pair[rowBase + (3 * k + w) * 2] = 0;
				pair[rowBase + (3 * k + w) * 2 + 1] = 0;
			}
		}
	}
	return { sheet, mask: Buffer.from(mask), pair, rowWords, width, height: nativeH };
}

/// Cocina objetos y escribe los assets en `outDir`.
/// \param {object} opts `{ ir, plan, paletteFile, planes, outDir }`.
/// \returns {object|null} resumen o null si no hay objetos de runtime.
export function cookObjects({ ir, plan, paletteFile, planes, outDir }) {
	const collected = collectMapObjects(ir, plan);
	if (collected.errors.length > 0) {
		throw new Error(`objetos: ${collected.errors.join(' | ')}`);
	}
	if (collected.objects.length === 0) return null;
	const palette = JSON.parse(fs.readFileSync(paletteFile, 'utf8'));
	const p = planes ?? palette.planes ?? 6;
	if (p < 1 || p > 6) throw new Error(`planos inválidos: ${p}`);
	const colors = paletteColors(palette, p);
	const sheetParts = [];
	const maskParts = [];
	const pairParts = [];
	let sheetWords = 0;
	let maskWords = 0;
	let pairWords = 0;
	const imageMeta = [];
	for (const image of collected.images) {
		const packed = packObject(image, colors, p);
		const imageIndex = imageMeta.length;
		imageMeta.push({
			index: imageIndex,
			gid: image.gid,
			width: packed.width,
			height: packed.height,
			row_words: packed.rowWords,
			sheet_word_offset: sheetWords,
			mask_word_offset: maskWords,
			pair_word_offset: pairWords,
			sheet_bytes: packed.sheet.length,
			mask_bytes: packed.mask.length,
			pair_bytes: packed.pair.length,
		});
		sheetParts.push(packed.sheet);
		maskParts.push(packed.mask);
		pairParts.push(packed.pair);
		sheetWords += packed.sheet.length / 2;
		maskWords += packed.mask.length / 2;
		pairWords += packed.pair.length / 2;
	}
	const sheet = Buffer.concat(sheetParts);
	const masks = Buffer.concat(maskParts);
	const pair = Buffer.concat(pairParts);
	fs.mkdirSync(outDir, { recursive: true });
	fs.writeFileSync(path.join(outDir, 'object_sheet.bin'), sheet);
	fs.writeFileSync(path.join(outDir, 'object_masks.bin'), masks);
	fs.writeFileSync(path.join(outDir, 'object_pair.bin'), pair);

	const lines = [];
	lines.push('// Objetos Tiled (foreground/occluders): arte planar+máscara 1bpp y hoja par interleaved.');
	lines.push(`// ${collected.objects.length} objetos, ${imageMeta.length} imágenes, ${p} planos.`);
	lines.push('#pragma once');
	lines.push('#include <eng/map/objects.hpp>');
	lines.push('');
	lines.push(`#define TILED_MAP_OBJECT_FOREGROUND_COUNT ${collected.foreground_count}u`);
	lines.push(`#define TILED_MAP_OBJECT_OCCLUDER_COUNT ${collected.occluder_count}u`);
	lines.push(`#define TILED_MAP_OBJECT_COUNT ${collected.objects.length}u`);
	lines.push(`#define TILED_MAP_OBJECT_IMAGE_COUNT ${imageMeta.length}u`);
	lines.push(`#define TILED_MAP_OBJECT_PLANES ${p}u`);
	lines.push(`#define TILED_MAP_OBJECT_SHEET_WORDS ${sheetWords}u`);
	lines.push(`#define TILED_MAP_OBJECT_MASK_WORDS ${maskWords}u`);
	lines.push(`#define TILED_MAP_OBJECT_PAIR_WORDS ${pairWords}u`);
	lines.push(`#define TILED_MAP_OBJECT_SHEET_BYTES ${sheet.length}u`);
	lines.push(`#define TILED_MAP_OBJECT_MASK_BYTES ${masks.length}u`);
	lines.push(`#define TILED_MAP_OBJECT_PAIR_BYTES ${pair.length}u`);
	lines.push('');
	lines.push(`static const eng::map::MapObjectImageView kMapObjectImages[TILED_MAP_OBJECT_IMAGE_COUNT] = {`);
	for (const image of imageMeta) {
		lines.push(`  {${image.sheet_word_offset}u, ${image.mask_word_offset}u, ${image.width}u, ${image.height}u, ${image.row_words}u},`);
	}
	lines.push('};');
	lines.push('');
	lines.push('// Desplazamiento en palabras de cada imagen en object_pair.bin.');
	lines.push(`static const eng::u32 kMapObjectPairWordOffset[TILED_MAP_OBJECT_IMAGE_COUNT] = {`);
	for (const image of imageMeta) lines.push(`  ${image.pair_word_offset}u,`);
	lines.push('};');
	lines.push('');
	lines.push(`static const eng::map::MapObjectView kMapObjects[TILED_MAP_OBJECT_COUNT] = {`);
	for (const object of collected.objects) {
		const kind = object.kind === 'occluder' ? 'eng::map::MapObjectKind::Occluder' : 'eng::map::MapObjectKind::Foreground';
		const sort = object.sort_y === 'top' ? 'eng::map::MapObjectSort::Top' : 'eng::map::MapObjectSort::Base';
		lines.push(
			`  {${Math.round(object.x)}, ${Math.round(object.y)}, ${Math.round(object.foot_y)}, ` +
			`${object.w}u, ${object.h}u, ${object.id}u, ${object.image}u, ${kind}, ${sort}},`);
	}
	lines.push('};');
	lines.push('');
	fs.writeFileSync(path.join(outDir, 'objects.h'), lines.join('\n'), 'utf8');
	const report = {
		planes: p,
		foreground: collected.foreground_count,
		occluders: collected.occluder_count,
		images: imageMeta,
		objects: collected.objects.map((object) => ({
			id: object.id,
			layer: object.layer,
			gid: object.gid,
			image: object.image,
			x: object.x,
			y: object.y,
			w: object.w,
			h: object.h,
			foot_y: object.foot_y,
			sort_y: object.sort_y,
			sort_key: object.sort_key,
			kind: object.kind,
			occluder: object.occluder,
			source: object.source,
		})),
		stats: collected.stats,
		bytes: { sheet: sheet.length, masks: masks.length, pair: pair.length },
	};
	fs.writeFileSync(path.join(outDir, 'objects.json'), JSON.stringify(report, null, 2) + '\n', 'utf8');
	return {
		foreground: collected.foreground_count,
		occluders: collected.occluder_count,
		images: imageMeta.length,
		sheetBytes: sheet.length,
		maskBytes: masks.length,
		pairBytes: pair.length,
	};
}

function main() {
	const args = process.argv.slice(2);
	let file = null;
	let irFile = null;
	const planFile = argValue(args, '--plan');
	const paletteFile = argValue(args, '--palette');
	const planes = argValue(args, '--planes');
	const outDir = argValue(args, '--out');
	for (let i = 0; i < args.length; ++i) {
		if (args[i] === '--ir') { irFile = args[++i] ?? null; continue; }
		if (args[i].startsWith('--')) { ++i; continue; }
		if (file == null) file = args[i];
	}
	if ((irFile == null && file == null) || planFile == null || paletteFile == null || outDir == null) {
		console.error('Uso: node tools/tiled/cook-objects.mjs <mapa.tmx|--ir ir.json> --plan plan.json --palette palette.json --planes 6 --out selected/');
		process.exit(2);
	}
	const ir = irFile != null ? JSON.parse(fs.readFileSync(path.resolve(irFile), 'utf8')) : parseTmxFile(file);
	const plan = JSON.parse(fs.readFileSync(path.resolve(planFile), 'utf8'));
	const result = cookObjects({
		ir,
		plan,
		paletteFile: path.resolve(paletteFile),
		planes: planes == null ? undefined : Number(planes),
		outDir: path.resolve(outDir),
	});
	if (result == null) {
		console.log('[objects] sin objetos de runtime');
		return;
	}
	console.log(`[objects] ${result.foreground} foreground + ${result.occluders} occluders, ${result.images} imágenes, sheet=${(result.sheetBytes / 1024).toFixed(1)}KB masks=${(result.maskBytes / 1024).toFixed(1)}KB pair=${(result.pairBytes / 1024).toFixed(1)}KB`);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
