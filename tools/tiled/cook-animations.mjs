#!/usr/bin/env node
// Cocina las celdas animadas de un mapa Tiled como overlays X-Limited:
//   - mapas dispersos de grupo por celda (u16, 0 = vacío),
//   - frames planares 16x16 + máscara 1bpp para cookie-cut,
//   - duraciones en ticks de juego (50 Hz).
//
// Entrada: IR de parse-tmx, cook-plan y palette.json del perfil seleccionado.
// Salida: animations.h + anim_sheet.bin + anim_masks.bin + animations.json.
//
// Uso:
//   node tools/tiled/cook-animations.mjs --ir ir.json --plan plan.json \
//        --palette palette.json --planes 6 --out selected/

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeGid, parseTmxFile } from './parse-tmx.mjs';
import {
	ImageCache,
	objectRect,
	resolveTileImage,
	tileHasAnimation,
	tileNode,
	tileSetFor,
} from './compose-map.mjs';
import { ditherCovers, nearest, paletteColors, shadowLevel } from './palette.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const TILE = 16;
const MAX_FRAMES = 8;
const MASK_ROW_BYTES = TILE / 8;

const argValue = (args, name) => {
	const at = args.indexOf(name);
	return at >= 0 ? args[at + 1] : null;
};

const asNumber = (value, fallback = 0) => {
	const n = Number(value);
	return Number.isFinite(n) ? n : fallback;
};

const layerName = (layer) => layer.name || `#${layer.id}`;

/// Color RGB de sombra del plan (`#rrggbb`); negro si no se declara.
/// \param {string|null|undefined} value valor de `render.shadow_color`.
/// \returns {number[]} `[r,g,b]` 0..255.
function parseShadowColor(value) {
	if (typeof value === 'string' && /^#[0-9a-fA-F]{6}$/.test(value)) {
		return [
			Number.parseInt(value.slice(1, 3), 16),
			Number.parseInt(value.slice(3, 5), 16),
			Number.parseInt(value.slice(5, 7), 16),
		];
	}
	return [0, 0, 0];
}

function flattenLayers(layers, out = []) {
	for (const layer of layers) {
		if (layer.visible === false) continue;
		out.push(layer);
		if (layer.kind === 'group') flattenLayers(layer.layers ?? [], out);
	}
	return out;
}

function animationFrames(ir, gid) {
	const match = tileSetFor(ir, gid);
	if (match == null) return null;
	const tile = tileNode(match.tileset, match.local);
	const frames = tile?.animation?.frames;
	if (!frames || frames.length === 0) return null;
	if (frames.length > MAX_FRAMES) {
		throw new Error(`animación de ${frames.length} frames supera el máximo ${MAX_FRAMES}`);
	}
	return frames.map((frame) => ({
		gid: match.tileset.firstgid + frame.tileid,
		ticks: Math.max(1, Math.round(asNumber(frame.duration, 0) / 20)),
	}));
}

function packFrame(ir, frameGid, flags, cache, colors, planes, dither = false, shadowIndex = -1) {
	const image = resolveTileImage(ir, frameGid, cache, { useFirstFrame: false });
	if (image.error != null) throw new Error(image.error);
	if (image.sw !== TILE || image.sh !== TILE) {
		throw new Error(`frame ${frameGid} mide ${image.sw}x${image.sh}; se esperaba ${TILE}x${TILE}`);
	}
	const indices = new Uint8Array(TILE * TILE);
	const mask = new Uint8Array(TILE * MASK_ROW_BYTES);
	for (let y = 0; y < TILE; ++y) {
		for (let x = 0; x < TILE; ++x) {
			let u = flags.flipH ? TILE - 1 - x : x;
			let v = flags.flipV ? TILE - 1 - y : y;
			if (flags.flipD) {
				const t = u;
				u = v;
				v = t;
			}
			const so = ((image.sy + v) * image.png.width + (image.sx + u)) * 4;
			const a = image.png.data[so + 3];
			let color = 0;
			if (dither && shadowIndex >= 0) {
				// Sombra viva: el alfa de origen decide la cobertura del bloque 2x2.
				// El color es el índice oscuro elegido para el perfil; una sombra con
				// alfa 64 ocupa 1 de cada 4 píxeles (no se descarta ni se pinta entera).
				const level = shadowLevel(a);
				if (level > 0 && ditherCovers(x, y, level)) color = shadowIndex;
			} else if (a >= 128) {
				color = nearest(colors, image.png.data[so], image.png.data[so + 1], image.png.data[so + 2]);
			}
			indices[y * TILE + x] = color;
			if (color !== 0) mask[y * MASK_ROW_BYTES + (x >> 3)] |= 0x80 >> (x & 7);
		}
	}
	const frame = Buffer.alloc(planes * 32);
	for (let p = 0; p < planes; ++p) {
		const bit = 1 << p;
		for (let y = 0; y < TILE; ++y) {
			let word = 0;
			for (let x = 0; x < TILE; ++x) {
				if ((indices[y * TILE + x] & bit) !== 0) word |= 0x8000 >> x;
			}
			const at = (p * TILE + y) * 2;
			frame[at] = (word >> 8) & 0xff;
			frame[at + 1] = word & 0xff;
		}
	}
	return { frame, mask: Buffer.from(mask) };
}

function objectCell(ir, object, cache) {
	const image = resolveTileImage(ir, object.gid, cache, { useFirstFrame: false });
	if (image.error != null) throw new Error(image.error);
	if (image.sw !== TILE || image.sh !== TILE) return null;
	const rect = objectRect(ir, object, image.sw, image.sh);
	const x = Math.round(rect.x / TILE);
	const y = Math.round(rect.y / TILE);
	if (Math.abs(rect.x - x * TILE) > 1e-6 || Math.abs(rect.y - y * TILE) > 1e-6) {
		throw new Error(`objeto ${object.id}: posición ${rect.x},${rect.y} no alineada a ${TILE}px`);
	}
	return { x, y, gid: object.gid };
}

/// Cocina animaciones y escribe `animations.h`/bins en `outDir`.
/// \param {object} opts `{ ir, plan, paletteFile, planes, outDir }`
export function cookAnimations({ ir, plan, paletteFile, planes, outDir }) {
	const layerPolicy = plan?.layers ?? {};
	const animLayers = new Set(plan?.animations?.layers ?? []);
	const animObjects = new Set(plan?.animations?.objects ?? []);
	const objectPolicy = plan?.objects ?? {};
	const shadowDither = plan?.render?.shadow_mode === 'dither';
	const palette = JSON.parse(fs.readFileSync(paletteFile, 'utf8'));
	const p = planes ?? palette.planes ?? 6;
	if (p < 1 || p > 6) throw new Error(`planos inválidos: ${p}`);
	const colors = paletteColors(palette, p);
	const shadowRgb = parseShadowColor(plan?.render?.shadow_color);
	const shadowIndex = shadowDither ? nearest(colors, shadowRgb[0], shadowRgb[1], shadowRgb[2]) : -1;
	const cache = new ImageCache();
	const width = ir.map.width;
	const height = ir.map.height;
	if (!(width > 0 && height > 0)) throw new Error('el cooker de animaciones exige un mapa finito con width/height');

	const layers = [];
	const groups = new Map(); // gid -> { frames, cells:[{layer,x,y}] }
	const addCell = (layer, cell, gid, live = false, shadow = false) => {
		const frames = animationFrames(ir, gid) ?? (live ? [{ gid, ticks: 0 }] : null);
		if (frames == null) return;
		const key = shadow ? `${gid}@shadow` : gid;
		let group = groups.get(key);
		if (group == null) {
			group = { gid, key, dither: shadow, frames, cells: [] };
			groups.set(key, group);
		}
		layer.cells[cell.y * width + cell.x] = { gid: key };
		group.cells.push({ layer, x: cell.x, y: cell.y });
	};
	const flat = flattenLayers(ir.layers);

	for (const layer of flat) {
		if (layer.kind === 'tile') {
			const policy = layerPolicy[layerName(layer)];
			if (policy !== 'overlay' && policy !== 'animated' && policy !== 'live' && !animLayers.has(layerName(layer))) continue;
			if (layer.width !== width || layer.height !== height) {
				throw new Error(`${layerName(layer)}: capa ${layer.width}x${layer.height}, mapa ${width}x${height}`);
			}
			const out = { name: layerName(layer), cells: new Array(width * height).fill(null) };
			for (let y = 0; y < height; ++y) {
				for (let x = 0; x < width; ++x) {
					const gid = layer.data?.cells?.[y * width + x] ?? 0;
					if (gid === 0) continue;
					if (policy !== 'live' && !tileHasAnimation(ir, gid) && !animLayers.has(layerName(layer))) continue;
					addCell(out, { x, y }, gid, policy === 'live', shadowDither && /shadow|sombra/i.test(layerName(layer)));
				}
			}
			if (out.cells.some((c) => c != null)) layers.push(out);
		} else if (layer.kind === 'object') {
			const policy = objectPolicy[layerName(layer)];
			if (policy !== 'overlay' && policy !== 'animated' && policy !== 'live' && !animObjects.has(layerName(layer))) continue;
			const out = { name: layerName(layer), cells: new Array(width * height).fill(null) };
			for (const object of layer.objects ?? []) {
				if (object.gid == null) continue;
				if (policy !== 'live' && !tileHasAnimation(ir, object.gid) && !animObjects.has(layerName(layer))) continue;
				const cell = objectCell(ir, object, cache);

				if (cell == null) continue;

				addCell(out, cell, object.gid, policy === 'live', shadowDither && /shadow|sombra/i.test(layerName(layer)));
			}
			if (out.cells.some((c) => c != null)) layers.push(out);
		}
	}

	if (layers.length === 0) {
		return null;
	}

	// Agrupa como máximo 4 celdas por grupo: un grupo de agua puede tener 18
	// celdas y 108 jobs (6 planos), lo que bloquea el parche entero. Varios
	// grupos pequeños comparten los mismos frames y el FramePlan reparte el
	// trabajo sin congelar las demás capas.
	const kCellsPerGroup = 4;
	const groupList = [];
	for (const group of groups.values()) {
		for (let i = 0; i < group.cells.length; i += kCellsPerGroup) {
			groupList.push({ ...group, cells: group.cells.slice(i, i + kCellsPerGroup) });
		}
	}
	if (groupList.length > 255) throw new Error(`demasiados grupos de animación (${groupList.length} > 255)`);
	// Reparte los grupos por capas en round-robin: con un presupuesto pequeño,
	// un orden por capas dejaría la fogata sin parchear durante segundos.
	const byLayer = new Map();
	for (const group of groupList) {
		const layerIndex = layers.indexOf(group.cells[0]?.layer);
		const key = layerIndex >= 0 ? layerIndex : 0;
		if (!byLayer.has(key)) byLayer.set(key, []);
		byLayer.get(key).push(group);
	}
	const layerGroups = [...byLayer.entries()]
		.sort((a, b) => b[0] - a[0])
		.map((entry) => entry[1]);
	const roundRobin = [];
	const maxPerLayer = Math.max(0, ...layerGroups.map((v) => v.length));
	for (let i = 0; i < maxPerLayer; ++i) {
		for (const groupsOfLayer of layerGroups) {
			if (i < groupsOfLayer.length) roundRobin.push(groupsOfLayer[i]);
		}
	}
	groupList.length = 0;
	groupList.push(...roundRobin);
	const sheetParts = [];
	const maskParts = [];
	const groupMeta = [];
	const frameFirst = new Map();
	let frameCursor = 0;
	for (const group of groupList) {
		let first = frameFirst.get(group.key);
		if (first == null) {
			const flags = decodeGid(group.gid);
			const frames = group.frames.map((frame) => packFrame(ir, frame.gid, flags, cache, colors, p, group.dither === true, shadowIndex));
			first = frameCursor;
			for (const frame of frames) {
				sheetParts.push(frame.frame);
				maskParts.push(frame.mask);
				++frameCursor;
			}
			frameFirst.set(group.key, first);
		}
		groupMeta.push({
			gid: group.gid,
			first_frame: first,
			frame_count: group.frames.length,
			durations: group.frames.map((f) => f.ticks),
			cells: group.cells.length,
			first_cell: 0,
			cell_count: 0,
			dither: group.dither === true,
		});
	}

	for (let g = 0; g < groupList.length; ++g) {
		for (const cell of groupList[g].cells) {
			cell.layer.cells[cell.y * width + cell.x].group = g;
		}
	}
	// Lista dispersa global ordenada por grupo: el parche recorre solo las celdas
	// de los grupos que cambiaron, no las 1600 celdas de cada capa.
	const sparse = [];
	for (let li = 0; li < layers.length; ++li) {
		const layer = layers[li];
		for (let y = 0; y < height; ++y) {
			for (let x = 0; x < width; ++x) {
				const cell = layer.cells[y * width + x];
				if (cell != null) sparse.push({ x, y, layer: li, group: cell.group });
			}
		}
	}
	sparse.sort((a, b) => (a.group - b.group) || (a.layer - b.layer) || (a.y - b.y) || (a.x - b.x));
	for (const group of groupMeta) { group.first_cell = 0; group.cell_count = 0; }
	for (let i = 0; i < sparse.length; ++i) {
		const meta = groupMeta[sparse[i].group];
		if (meta.cell_count === 0) meta.first_cell = i;
		meta.cell_count++;
	}
	for (const group of groupMeta) {
		if (group.cell_count == null) { group.first_cell = 0; group.cell_count = 0; }
	}

	const sheet = Buffer.concat(sheetParts);
	const masks = Buffer.concat(maskParts);
	fs.mkdirSync(outDir, { recursive: true });
	fs.writeFileSync(path.join(outDir, 'anim_sheet.bin'), sheet);
	fs.writeFileSync(path.join(outDir, 'anim_masks.bin'), masks);

	const frameStride = p * 32;
	const maskStride = 32;
	const lines = [];
	lines.push('// Animaciones Tiled cocinadas (T5): overlays planares + máscara 1bpp.');
	lines.push(`// ${groupList.length} grupos, ${frameCursor} frames, ${layers.length} capas, ${p} planos.`);
	lines.push('#pragma once');
	lines.push('#include <eng/field/tile_animation.hpp>');
	lines.push('');
	lines.push(`#define TILED_ANIM_GROUP_COUNT ${groupList.length}u`);
	lines.push(`#define TILED_ANIM_LAYER_COUNT ${layers.length}u`);
	lines.push(`#define TILED_ANIM_FRAME_COUNT ${frameCursor}u`);
	lines.push(`#define TILED_ANIM_FRAME_STRIDE ${frameStride}u`);
	lines.push(`#define TILED_ANIM_MASK_STRIDE ${maskStride}u`);
	lines.push(`#define TILED_ANIM_TILE ${TILE}u`);
	lines.push(`#define TILED_ANIM_PLANES ${p}u`);
	lines.push(`#define TILED_ANIM_SHEET_BYTES ${sheet.length}u`);
	lines.push(`#define TILED_ANIM_CELL_COUNT ${sparse.length}u`);
	lines.push(`#define TILED_ANIM_MASK_BYTES ${masks.length}u`);
	lines.push('');
	lines.push(`static const eng::playfield::TileAnimationGroupView kTiledAnimGroups[TILED_ANIM_GROUP_COUNT] = {`);
	for (const group of groupMeta) {
		const durations = group.durations;
		const padded = [...durations, ...new Array(MAX_FRAMES - durations.length).fill(0)];
		lines.push(`  {${group.first_frame}u, ${group.frame_count}u, 1u, ${group.first_cell}u, ${group.cell_count}u, {${padded.map((v) => `${v}u`).join(',')}}},`);
	}
	lines.push('};');
	lines.push('');
	lines.push(`static const eng::playfield::TileAnimationCellView kTiledAnimCells[TILED_ANIM_CELL_COUNT] = {`);
	for (const cell of sparse) {
		lines.push(`  {${cell.x}u, ${cell.y}u, ${cell.layer}u, 0u},`);
	}
	lines.push('};');
	lines.push('');
	for (let i = 0; i < layers.length; ++i) {
		const cells = layers[i].cells.map((c) => (c == null ? 0 : c.group + 1));
		lines.push(`static const unsigned short kTiledAnimLayer${i}Cells[${width * height}] = {`);
		for (let y = 0; y < height; ++y) {
			lines.push(`  ${cells.slice(y * width, (y + 1) * width).map((v) => `${v}u`).join(',')},`);
		}
		lines.push('};');
	}
	lines.push('');
	lines.push(`static const eng::playfield::TileAnimationLayerView kTiledAnimLayers[TILED_ANIM_LAYER_COUNT] = {`);
	for (let i = 0; i < layers.length; ++i) {
		lines.push(`  {kTiledAnimLayer${i}Cells, ${width}u, ${height}u, ${width}u, ${height}u},`);
	}
	lines.push('};');
	lines.push('');
	fs.writeFileSync(path.join(outDir, 'animations.h'), lines.join('\n'), 'utf8');
	fs.writeFileSync(path.join(outDir, 'animations.json'), JSON.stringify({
		planes: p,
		frameStride,
		maskStride,
		frameCount: frameCursor,
		sheetBytes: sheet.length,
		maskBytes: masks.length,
		layers: layers.map((l) => ({
			name: l.name,
			cells: l.cells.reduce((n, c) => n + (c == null ? 0 : 1), 0),
			groups: [...new Set(l.cells.filter((c) => c != null).map((c) => c.group))].length,
		})),
		groups: groupMeta,
	}, null, 2) + '\n', 'utf8');
	return {
		groups: groupList.length,
		frames: frameCursor,
		layers: layers.map((l) => l.name),
		sheetBytes: sheet.length,
		maskBytes: masks.length,
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
		console.error('Uso: node tools/tiled/cook-animations.mjs <mapa.tmx|--ir ir.json> --plan plan.json --palette palette.json --planes 6 --out selected/');
		process.exit(2);
	}
	const ir = irFile != null ? JSON.parse(fs.readFileSync(path.resolve(irFile), 'utf8')) : parseTmxFile(file);
	const plan = JSON.parse(fs.readFileSync(path.resolve(planFile), 'utf8'));
	const result = cookAnimations({
		ir,
		plan,
		paletteFile: path.resolve(paletteFile),
		planes: planes == null ? undefined : Number(planes),
		outDir: path.resolve(outDir),
	});
	console.log(`[anim] ${result.groups} grupos, ${result.frames} frames, ${result.layers.join('/')}, sheet=${(result.sheetBytes / 1024).toFixed(1)}KB masks=${(result.maskBytes / 1024).toFixed(1)}KB`);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
