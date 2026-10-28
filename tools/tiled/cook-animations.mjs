#!/usr/bin/env node
// Cocina las celdas animadas de un mapa Tiled como overlays X-Limited:
//   - mapas dispersos de grupo por celda (u16, 0 = vacío),
//   - frames planares cuadrados de 16 o 32 px + máscara 1bpp para cookie-cut,
//   - duraciones en ticks de juego (50 Hz).
//
// Entrada: IR de parse-tmx, cook-plan y palette.json del perfil seleccionado.
// Salida: animations.h + anim_sheet.bin + anim_masks.bin + animations.json.
//
// Uso:
//   node tools/tiled/cook-animations.mjs --ir ir.json --plan plan.json \
//        --palette palette.json --planes 6 --tile 16 --out selected/
//
// `--tile` es opcional: si se omite se usa `map.tilewidth`; sólo se aceptan
// tiles cuadrados de 16×16 o 32×32.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeGid, parseTmxFile } from './parse-tmx.mjs';
import {
	ImageCache,
	objectHasAnimation,
	objectRect,
	resolveTileImage,
	tileHasAnimation,
	tileNode,
	tileSetFor,
} from './compose-map.mjs';
import { ditherCovers, nearest, paletteColors, shadowLevel } from './palette.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SUPPORTED_TILES = new Set([16, 32]);
const DEFAULT_TILE = 16;
const MAX_FRAMES = 8;

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

function packFrame(ir, frameGid, flags, cache, colors, planes, tile, dither = false, shadowIndex = -1) {
	const image = resolveTileImage(ir, frameGid, cache, { useFirstFrame: false });
	if (image.error != null) throw new Error(image.error);
	if (image.sw !== tile || image.sh !== tile) {
		throw new Error(`frame ${frameGid} mide ${image.sw}x${image.sh}; se esperaba ${tile}x${tile}`);
	}
	const rowBytes = tile / 8;
	const planeStride = rowBytes * tile;
	const wordsPerRow = tile / 16;
	const indices = new Uint8Array(tile * tile);
	const mask = new Uint8Array(tile * rowBytes);
	for (let y = 0; y < tile; ++y) {
		for (let x = 0; x < tile; ++x) {
			let u = flags.flipH ? tile - 1 - x : x;
			let v = flags.flipV ? tile - 1 - y : y;
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
			indices[y * tile + x] = color;
			if (color !== 0) mask[y * rowBytes + (x >> 3)] |= 0x80 >> (x & 7);
		}
	}
	const frame = Buffer.alloc(planes * planeStride);
	for (let p = 0; p < planes; ++p) {
		const bit = 1 << p;
		for (let y = 0; y < tile; ++y) {
			for (let wordIndex = 0; wordIndex < wordsPerRow; ++wordIndex) {
				let word = 0;
				for (let b = 0; b < 16; ++b) {
					const x = wordIndex * 16 + b;
					if ((indices[y * tile + x] & bit) !== 0) word |= 0x8000 >> b;
				}
				const at = p * planeStride + y * rowBytes + wordIndex * 2;
				frame[at] = (word >> 8) & 0xff;
				frame[at + 1] = word & 0xff;
			}
		}
	}
	return { frame, mask: Buffer.from(mask) };
}

function objectCell(ir, object, cache, tile) {
	const image = resolveTileImage(ir, object.gid, cache, { useFirstFrame: false });
	if (image.error != null) throw new Error(image.error);
	if (image.sw !== tile || image.sh !== tile) return null;
	const rect = objectRect(ir, object, image.sw, image.sh);
	const x = Math.round(rect.x / tile);
	const y = Math.round(rect.y / tile);
	if (Math.abs(rect.x - x * tile) > 1e-6 || Math.abs(rect.y - y * tile) > 1e-6) {
		throw new Error(`objeto ${object.id}: posición ${rect.x},${rect.y} no alineada a ${tile}px`);
	}
	return { x, y, gid: object.gid };
}

/// ¿Dos grupos comparten exactamente la misma línea temporal de frames?
/// La fusión solo es válida si todos los componentes avanzan al unísono; si no,
/// un sprite compuesto mostraría frames de instantes distintos.
function sameTimeline(a, b) {
	if (a.length !== b.length) return false;
	for (let i = 0; i < a.length; ++i) {
		if (a[i].ticks !== b[i].ticks) return false;
	}
	return true;
}

/// Busca **clusters rectangulares de celdas animadas contiguas** que puedan
/// cocinarse como un único sprite de mapa. Reglas generales (no dependen de un
/// mapa concreto):
/// - misma capa y misma línea temporal de frames;
/// - el bounding box está completamente ocupado por el cluster (sin huecos);
/// - al menos 2 celdas y 2 grupos distintos;
/// - si algún gid se repite en el mapa, el sprite fusionado duplicaría frames:
///   solo se acepta si `duplicate_bytes` (sheet completa × frames) cabe en
///   `maxDuplicateBytes`; con 0 se exige que cada gid se use una sola vez;
/// - tamaño acotado (`maxCells`) para que el save-under no crezca sin control.
/// Devuelve los clusters sin mutar los grupos; el llamador decide y aplica.
/// \param {object} opts `{ layers, groups, maxCells, tile, planes, maxDuplicateBytes }`
/// \returns {Array<object>} clusters `{layer,minx,miny,maxx,maxy,cells,duplicate_bytes,jobs_saved}`
export function findFusedClusters({ layers, groups, maxCells = 8, tile = 16, planes = 6, maxDuplicateBytes = 0 }) {
	const byLayer = layers.map(() => new Map());
	const keyUses = new Map();
	for (const group of groups.values()) {
		for (const cell of group.cells) {
			keyUses.set(cell.key, (keyUses.get(cell.key) ?? 0) + 1);
			const li = layers.indexOf(cell.layer);
			if (li < 0) continue;
			byLayer[li].set(`${cell.x},${cell.y}`, { cell, group });
		}
	}
	const clusters = [];
	const dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
	for (let li = 0; li < layers.length; ++li) {
		const positions = byLayer[li];
		const seen = new Set();
		for (const [startKey, start] of positions) {
			if (seen.has(startKey)) continue;
			const comp = [];
			const queue = [start];
			seen.add(startKey);
			while (queue.length > 0) {
				const cur = queue.pop();
				comp.push(cur);
				for (const [dx, dy] of dirs) {
					const nk = `${cur.cell.x + dx},${cur.cell.y + dy}`;
					if (seen.has(nk)) continue;
					const next = positions.get(nk);
					if (next == null) continue;
					if (cur.cell.dither !== next.cell.dither) continue;
					if (!sameTimeline(cur.cell.frames, next.cell.frames)) continue;
					seen.add(nk);
					queue.push(next);
				}
			}
			if (comp.length < 2 || comp.length > maxCells) continue;
			let minx = comp[0].cell.x, maxx = minx;
			let miny = comp[0].cell.y, maxy = miny;
			for (const item of comp) {
				minx = Math.min(minx, item.cell.x);
				maxx = Math.max(maxx, item.cell.x);
				miny = Math.min(miny, item.cell.y);
				maxy = Math.max(maxy, item.cell.y);
			}
			const area = (maxx - minx + 1) * (maxy - miny + 1);
			if (area !== comp.length) continue; // con huecos no es un sprite rectangular
			const keys = new Set(comp.map((item) => item.cell.key));
			if (keys.size < 2) continue; // mismo gid: ya comparte frames; no fusionar
			if (comp.some((item) => item.cell.frames.length < 2)) continue;
			const duplicate = comp.some((item) => (keyUses.get(item.cell.key) ?? 0) !== 1);
			const width = (maxx - minx + 1) * tile;
			const height = (maxy - miny + 1) * tile;
			const duplicateBytes = duplicate
				? (width / 8) * height * planes * comp[0].cell.frames.length
				: 0;
			if (duplicateBytes > maxDuplicateBytes) continue;
			clusters.push({
				layer: li, layerRef: start.cell.layer, minx, miny, maxx, maxy,
				cells: comp, duplicate_bytes: duplicateBytes,
				jobs_saved: 2 * (comp.length - 1),
			});
		}
	}
	return clusters;
}

/// Compone los frames de un cluster fusionado en una sola caja planar. Cada
/// frame del sprite final es la unión de los tiles de sus celdas en la misma
/// posición relativa; la máscara también se compone. El banco resultante usa el
/// mismo layout denso que `TileAnimator` espera (`[plano][fila][palabra]`).
/// \param {object} opts `{ ir, cache, colors, planes, tile, group, shadowIndex }`
/// \returns {Array<{frame:Buffer,mask:Buffer}>}
function composeFusedFrames({ ir, cache, colors, planes, tile, group, shadowIndex }) {
	const f = group.fused;
	const width = (f.maxx - f.minx + 1) * tile;
	const height = (f.maxy - f.miny + 1) * tile;
	const tileRow = tile / 8;
	const rowBytes = width / 8;
	const planeStride = rowBytes * height;
	const out = [];
	for (let fi = 0; fi < group.frames.length; ++fi) {
		const frame = Buffer.alloc(planes * planeStride);
		const mask = Buffer.alloc(rowBytes * height);
		for (const cell of f.cells) {
			const flags = decodeGid(cell.gid);
			const packed = packFrame(ir, cell.frames[fi].gid, flags, cache, colors, planes,
				tile, cell.dither === true, shadowIndex);
			const dx = (cell.x - f.minx) * tile / 8;
			const dy = (cell.y - f.miny) * tile;
			for (let p = 0; p < planes; ++p) {
				const srcPlane = p * tileRow * tile;
				const dstPlane = p * planeStride;
				for (let r = 0; r < tile; ++r) {
					const src = srcPlane + r * tileRow;
					const dst = dstPlane + (dy + r) * rowBytes + dx;
					packed.frame.copy(frame, dst, src, src + tileRow);
				}
			}
			for (let r = 0; r < tile; ++r) {
				const src = r * tileRow;
				const dst = (dy + r) * rowBytes + dx;
				packed.mask.copy(mask, dst, src, src + tileRow);
			}
		}
		out.push({ frame, mask });
	}
	return out;
}

/// Cocina animaciones y escribe `animations.h`/bins en `outDir`.
/// \param {object} opts `{ ir, plan, paletteFile, planes, tile, outDir }`
export function cookAnimations({ ir, plan, paletteFile, planes, tile: requestedTile, outDir }) {
	const mapTile = Number(ir?.map?.tilewidth ?? 0);
	const tile = requestedTile ?? (mapTile || DEFAULT_TILE);
	if (!SUPPORTED_TILES.has(tile)) {
		throw new Error(`tile ${tile} no soportado; usa 16 o 32`);
	}
	if (mapTile > 0 && mapTile !== tile) {
		throw new Error(`tile ${tile} no coincide con map.tilewidth ${mapTile}`);
	}
	if (Number(ir?.map?.tileheight ?? tile) !== tile) {
		throw new Error(`tile ${tile} no es cuadrado (map.tileheight=${ir?.map?.tileheight})`);
	}
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
		group.cells.push({ layer, x: cell.x, y: cell.y, gid, key, frames, dither: shadow });
	};
	const flat = flattenLayers(ir.layers);
	// Celdas animadas bajo un objeto/capa estatica posterior: no se animan para
	// que el frame del agua no borre el sprite horneado que va encima.
	const staticOverlapCells = (fromIndex) => {
		const covered = new Set();
		for (let i = fromIndex + 1; i < flat.length; ++i) {
			const above = flat[i];
			if (above.kind === 'object') {
				for (const object of above.objects ?? []) {
					if (object.gid == null || objectHasAnimation(ir, object, cache)) continue;
					const image = resolveTileImage(ir, object.gid, cache, { useFirstFrame: true });
					if (image.error != null) continue;
					const rect = objectRect(ir, object, image.sw, image.sh);
					const x0 = Math.max(0, Math.floor(rect.x / tile));
					const y0 = Math.max(0, Math.floor(rect.y / tile));
					const x1 = Math.min(width - 1, Math.floor((rect.x + rect.w - 0.001) / tile));
					const y1 = Math.min(height - 1, Math.floor((rect.y + rect.h - 0.001) / tile));
					for (let y = y0; y <= y1; ++y) {
						for (let x = x0; x <= x1; ++x) covered.add(`${x},${y}`);
					}
				}
			}
			// Las capas de tiles estaticas pueden tener pixeles transparentes; no
			// se suprimen celdas enteras por ellas. El solape que rompe el agua
			// es el de los objetos/sprites (p. ej. la copa del arbol).
		}
		return covered;
	};

	for (let li = 0; li < flat.length; ++li) {
		const layer = flat[li];
		if (layer.kind === 'tile') {
			const policy = layerPolicy[layerName(layer)];
			if (policy !== 'overlay' && policy !== 'animated' && policy !== 'live' && !animLayers.has(layerName(layer))) continue;
			if (layer.width !== width || layer.height !== height) {
				throw new Error(`${layerName(layer)}: capa ${layer.width}x${layer.height}, mapa ${width}x${height}`);
			}
			const suppressed = staticOverlapCells(li);
			const out = { name: layerName(layer), cells: new Array(width * height).fill(null) };
			for (let y = 0; y < height; ++y) {
				for (let x = 0; x < width; ++x) {
					const gid = layer.data?.cells?.[y * width + x] ?? 0;
					if (gid === 0) continue;
					if (policy !== 'live' && !tileHasAnimation(ir, gid) && !animLayers.has(layerName(layer))) continue;
					if (suppressed.has(`${x},${y}`)) continue;
					addCell(out, { x, y }, gid, policy === 'live', shadowDither && /shadow|sombra/i.test(layerName(layer)));
				}
			}
			if (out.cells.some((c) => c != null)) layers.push(out);
		} else if (layer.kind === 'object') {
			const policy = objectPolicy[layerName(layer)];
			if (policy !== 'overlay' && policy !== 'animated' && policy !== 'live' && !animObjects.has(layerName(layer))) continue;
			const suppressed = staticOverlapCells(li);
			const out = { name: layerName(layer), cells: new Array(width * height).fill(null) };
			for (const object of layer.objects ?? []) {
				if (object.gid == null) continue;
				if (policy !== 'live' && !tileHasAnimation(ir, object.gid) && !animObjects.has(layerName(layer))) continue;
				const cell = objectCell(ir, object, cache, tile);

				if (cell == null || suppressed.has(`${cell.x},${cell.y}`)) continue;

				addCell(out, cell, object.gid, policy === 'live', shadowDither && /shadow|sombra/i.test(layerName(layer)));
			}
			if (out.cells.some((c) => c != null)) layers.push(out);
		}
	}

	if (layers.length === 0) {
		return null;
	}

	// Fusión automática de objetos compuestos: un cluster rectangular de celdas
	// con la misma cadencia se cocina como **un grupo de un solo sprite** (un
	// save-under y un blit por segmento) en vez de una plaqueta por celda. No
	// depende de la fogata: cualquier mapa que produzca un cluster válido entra.
	const fusionPolicy = plan?.animations?.fusion ?? {};
	const fusedClusters = findFusedClusters({
		layers, groups, tile, planes: p,
		maxCells: asNumber(fusionPolicy.max_cells, 8),
		maxDuplicateBytes: asNumber(fusionPolicy.max_duplicate_bytes, 0),
	});
	for (const cluster of fusedClusters) {
		for (const item of cluster.cells) {
			item.group.cells = item.group.cells.filter((c) =>
				!(c.layer === item.cell.layer && c.x === item.cell.x && c.y === item.cell.y));
		}
		const key = `fused:${cluster.layer}:${cluster.minx}:${cluster.miny}`;
		const layer = cluster.layerRef;
		for (const item of cluster.cells) layer.cells[item.cell.y * width + item.cell.x] = null;
		layer.cells[cluster.miny * width + cluster.minx] = { gid: key };
		const frames = cluster.cells[0].cell.frames.map((frame) => ({ ticks: frame.ticks }));
		groups.set(key, {
			gid: 0,
			key,
			dither: cluster.cells[0].cell.dither,
			frames,
			cells: [{
				layer,
				x: cluster.minx,
				y: cluster.miny,
				gid: 0,
				key,
				frames,
				dither: cluster.cells[0].cell.dither,
			}],
			fused: {
				minx: cluster.minx,
				miny: cluster.miny,
				maxx: cluster.maxx,
				maxy: cluster.maxy,
				cells: cluster.cells.map((item) => item.cell),
				duplicate_bytes: cluster.duplicate_bytes,
				jobs_saved: cluster.jobs_saved,
			},
		});
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
	const groupMeta = new Array(groupList.length);
	const frameFirst = new Map();
	const planeStride = (tile / 8) * tile;
	const frameStride = p * planeStride;
	const maskStride = (tile / 8) * tile;
	let frameCursor = 0;
	let sheetCursor = 0;
	let maskCursor = 0;
	// Primero los frames de los grupos normales, para que `first_frame ×
	// frame_stride` siga siendo válido. Los grupos fusionados se añaden después
	// con su propio `sheet_offset`/`frame_stride` (frames más grandes).
	for (let i = 0; i < groupList.length; ++i) {
		const group = groupList[i];
		if (group.fused != null) continue;
		let first = frameFirst.get(group.key);
		if (first == null) {
			const flags = decodeGid(group.gid);
			const frames = group.frames.map((frame) => packFrame(ir, frame.gid, flags, cache, colors, p, tile, group.dither === true, shadowIndex));
			first = frameCursor;
			for (const frame of frames) {
				sheetParts.push(frame.frame);
				maskParts.push(frame.mask);
				sheetCursor += frame.frame.length;
				maskCursor += frame.mask.length;
				++frameCursor;
			}
			frameFirst.set(group.key, first);
		}
		groupMeta[i] = {
			gid: group.gid,
			first_frame: first,
			frame_count: group.frames.length,
			durations: group.frames.map((f) => f.ticks),
			cells: group.cells.length,
			first_cell: 0,
			cell_count: 0,
			dither: group.dither === true,
			pixel_width: 0,
			pixel_height: 0,
			sheet_offset: 0,
			frame_stride: 0,
			mask_offset: 0,
			mask_stride: 0,
		};
	}
	for (let i = 0; i < groupList.length; ++i) {
		const group = groupList[i];
		if (group.fused == null) continue;
		const frames = composeFusedFrames({ ir, cache, colors, planes: p, tile, group, shadowIndex });
		const sheetOffset = sheetCursor;
		const maskOffset = maskCursor;
		for (const frame of frames) {
			sheetParts.push(frame.frame);
			maskParts.push(frame.mask);
			sheetCursor += frame.frame.length;
			maskCursor += frame.mask.length;
			++frameCursor;
		}
		const widthPx = (group.fused.maxx - group.fused.minx + 1) * tile;
		const heightPx = (group.fused.maxy - group.fused.miny + 1) * tile;
		groupMeta[i] = {
			gid: group.gid,
			first_frame: 0,
			frame_count: group.frames.length,
			durations: group.frames.map((f) => f.ticks),
			cells: 1,
			first_cell: 0,
			cell_count: 0,
			dither: group.dither === true,
			pixel_width: widthPx,
			pixel_height: heightPx,
			sheet_offset: sheetOffset,
			frame_stride: frames[0].frame.length,
			mask_offset: maskOffset,
			mask_stride: frames[0].mask.length,
			fused: `${group.fused.minx},${group.fused.miny} ${widthPx}x${heightPx}`,
			duplicate_bytes: group.fused.duplicate_bytes,
			jobs_saved: group.fused.jobs_saved,
		};
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
	lines.push(`#define TILED_ANIM_TILE ${tile}u`);
	lines.push(`#define TILED_ANIM_PLANES ${p}u`);
	lines.push(`#define TILED_ANIM_SHEET_BYTES ${sheet.length}u`);
	lines.push(`#define TILED_ANIM_CELL_COUNT ${sparse.length}u`);
	lines.push(`#define TILED_ANIM_MASK_BYTES ${masks.length}u`);
	lines.push('');
	lines.push(`static const eng::playfield::TileAnimationGroupView kTiledAnimGroups[TILED_ANIM_GROUP_COUNT] = {`);
	for (const group of groupMeta) {
		const durations = group.durations;
		const padded = [...durations, ...new Array(MAX_FRAMES - durations.length).fill(0)];
		lines.push(`  {${group.first_frame}u, ${group.frame_count}u, 1u, ${group.first_cell}u, ${group.cell_count}u, {${padded.map((v) => `${v}u`).join(',')}}, ${group.pixel_width}u, ${group.pixel_height}u, ${group.sheet_offset}u, ${group.frame_stride}u, ${group.mask_offset}u, ${group.mask_stride}u},`);
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
		tile,
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
		fused: groupMeta
			.filter((group) => group.pixel_width != null && group.pixel_width !== 0)
			.map((group) => ({
				origin: group.fused,
				width: group.pixel_width,
				height: group.pixel_height,
				frames: group.frame_count,
				bytes: group.frame_stride * group.frame_count,
			})),
	}, null, 2) + '\n', 'utf8');
	return {
		groups: groupList.length,
		frames: frameCursor,
		layers: layers.map((l) => l.name),
		sheetBytes: sheet.length,
		maskBytes: masks.length,
		fused: groupMeta.filter((group) => group.pixel_width !== 0).length,
	};
}

function main() {
	const args = process.argv.slice(2);
	let file = null;
	let irFile = null;
	const planFile = argValue(args, '--plan');
	const paletteFile = argValue(args, '--palette');
	const planes = argValue(args, '--planes');
	const tile = argValue(args, '--tile');
	const outDir = argValue(args, '--out');
	for (let i = 0; i < args.length; ++i) {
		if (args[i] === '--ir') { irFile = args[++i] ?? null; continue; }
		if (args[i].startsWith('--')) { ++i; continue; }
		if (file == null) file = args[i];
	}
	if ((irFile == null && file == null) || planFile == null || paletteFile == null || outDir == null) {
		console.error('Uso: node tools/tiled/cook-animations.mjs <mapa.tmx|--ir ir.json> --plan plan.json --palette palette.json --planes 6 [--tile 16|32] --out selected/');
		process.exit(2);
	}
	const ir = irFile != null ? JSON.parse(fs.readFileSync(path.resolve(irFile), 'utf8')) : parseTmxFile(file);
	const plan = JSON.parse(fs.readFileSync(path.resolve(planFile), 'utf8'));
	const result = cookAnimations({
		ir,
		plan,
		paletteFile: path.resolve(paletteFile),
		planes: planes == null ? undefined : Number(planes),
		tile: tile == null ? undefined : Number(tile),
		outDir: path.resolve(outDir),
	});
	console.log(`[anim] ${result.groups} grupos, ${result.frames} frames, ${result.layers.join('/')}, ${result.fused} fusionados, sheet=${(result.sheetBytes / 1024).toFixed(1)}KB masks=${(result.maskBytes / 1024).toFixed(1)}KB`);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
