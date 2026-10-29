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

// Bits de `TileAnimationGroupView::flags` (contrato en tile_animation.hpp).
const kFlagMasked = 1;
const kFlagStableMask = 2;
const kFlagSharedMask = 4;
/// El frame incluye la máscara expandida por plano: layout
/// `[fila][plano] = [imagen w][máscara w]`. El runtime lo dibuja con un solo
/// blit cuando el destino es interleaved (`make_interleaved_masked_bob`).
const kFlagInterleaved = 8;

const maskAllOpaque = (mask) => {
	for (let i = 0; i < mask.length; ++i) if (mask[i] !== 0xff) return false;
	return true;
};

/// Calcula los flags de un grupo a partir de los frames ya empaquetados:
/// enmascarado si algún frame tiene transparencia, silueta fija si todos
/// comparten máscara (y entonces se emite una sola, con el bit de compartición).
/// \param {Array<{frame:Buffer,mask:Buffer}>} frames
/// \returns {number} bits `kFlag*`
function packedFlags(frames) {
	const masked = frames.some((f) => !maskAllOpaque(f.mask));
	const stable = frames.length > 1 && frames.every((f) => f.mask.equals(frames[0].mask));
	return (masked ? kFlagMasked : 0) | (stable ? kFlagStableMask | kFlagSharedMask : 0);
}

/// Bit `f` = el frame `f` es opaco (máscara toda a 1). El runtime lo dibuja
/// con copia y omite el restore: el frame cubre la celda entera.
/// \param {Array<{mask:Buffer}>} frames frames empaquetados
/// \returns {number} máscara de bits de frames opacos (8 frames máx.)
function opaqueMask(frames) {
	let mask = 0;
	for (let i = 0; i < frames.length && i < 8; ++i) {
		if (maskAllOpaque(frames[i].mask)) mask |= 1 << i;
	}
	return mask;
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
	return packIndices(indices, mask, planes, tile);
}

/// Empaqueta índices de color + máscara 1bpp en el layout planar de un frame.
/// \param {Uint8Array} indices `tile×tile` índices del perfil (0 = transparente)
/// \param {Uint8Array} mask `tile × (tile/8)` máscara 1bpp
/// \param {number} planes planos del perfil
/// \param {number} tile lado del tile en píxeles (múltiplo de 16)
/// \returns {{frame:Buffer,mask:Buffer}}
function packIndices(indices, mask, planes, tile) {
	const rowBytes = tile / 8;
	const planeStride = rowBytes * tile;
	const wordsPerRow = tile / 16;
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

/// Convierte un frame planar `[plano][fila][palabra]` al layout interleaved
/// `[fila][plano] = [imagen w][máscara w]`. La máscara se repite en cada plano:
/// así `A` (máscara) y `B` (imagen) avanzan con el mismo par y el blit único
/// recorre `filas × planos` sin punteros de plano (`make_interleaved_masked_bob`).
/// \param {{frame:Buffer,mask:Buffer}} packed frame planar y su máscara 1bpp
/// \param {number} words palabras por fila (`ancho/16`)
/// \param {number} rows filas del frame
/// \param {number} planes planos del perfil
/// \returns {{frame:Buffer,mask:Buffer}} frame interleaved y la máscara original
function interleaveFrame({ frame, mask }, words, rows, planes) {
	const rowBytes = words * 2;
	const planeStride = rowBytes * rows;
	const pairBytes = rowBytes * 2;
	const out = Buffer.alloc(rows * planes * pairBytes);
	for (let r = 0; r < rows; ++r) {
		for (let p = 0; p < planes; ++p) {
			const src = p * planeStride + r * rowBytes;
			const dst = (r * planes + p) * pairBytes;
			frame.copy(out, dst, src, src + rowBytes);
			mask.copy(out, dst + rowBytes, r * rowBytes, r * rowBytes + rowBytes);
		}
	}
	return { frame: out, mask };
}

/// Cosecha en un frame de 16×16 los píxeles de los objetos estáticos que van
/// **encima** de una celda viva. Es la capa de cobertura que el runtime
/// reestampa después del parche para que el objeto (una banca, una roca) tape
/// al overlay animado sin congelar la celda. Sólo se emiten píxeles con alfa;
/// los objetos con animación propia no cuentan (son otra capa viva).
/// \param {object} opts `{ occluders, cellX, cellY, colors, planes, tile, shadowIndex }`
/// \returns {{frame:Buffer,mask:Buffer}}
function packCoverFrame({ occluders, cellX, cellY, colors, planes, tile, shadowIndex }) {
	const rowBytes = tile / 8;
	const indices = new Uint8Array(tile * tile);
	const mask = new Uint8Array(tile * rowBytes);
	const px0 = cellX * tile;
	const py0 = cellY * tile;
	for (const occluder of occluders) {
		const r = occluder.rect;
		if (r.x >= px0 + tile || r.y >= py0 + tile || r.x + r.w <= px0 || r.y + r.h <= py0) continue;
		const ix0 = Math.max(px0, Math.ceil(r.x));
		const ix1 = Math.min(px0 + tile, Math.floor(r.x + r.w));
		const iy0 = Math.max(py0, Math.ceil(r.y));
		const iy1 = Math.min(py0 + tile, Math.floor(r.y + r.h));
		for (let py = iy0; py < iy1; ++py) {
			for (let px = ix0; px < ix1; ++px) {
				let u = Math.floor(px - r.x);
				let v = Math.floor(py - r.y);
				if (u < 0 || v < 0 || u >= occluder.image.sw || v >= occluder.image.sh) continue;
				if (occluder.flip.flipH) u = occluder.image.sw - 1 - u;
				if (occluder.flip.flipV) v = occluder.image.sh - 1 - v;
				if (occluder.flip.flipD) { const t = u; u = v; v = t; }
				if (u < 0 || v < 0 || u >= occluder.image.sw || v >= occluder.image.sh) continue;
				const so = ((occluder.image.sy + v) * occluder.image.png.width +
					occluder.image.sx + u) * 4;
				const alpha = occluder.image.png.data[so + 3];
				let color = 0;
				if (alpha >= 128) {
					color = nearest(colors, occluder.image.png.data[so],
						occluder.image.png.data[so + 1], occluder.image.png.data[so + 2]);
				} else if (alpha > 0 && shadowIndex >= 0) {
					const level = shadowLevel(alpha);
					if (level > 0 && ditherCovers(px, py, level)) color = shadowIndex;
				}
				if (color === 0) continue;
				const di = (py - py0) * tile + (px - px0);
				indices[di] = color;
				mask[(py - py0) * rowBytes + ((px - px0) >> 3)] |= 0x80 >> ((px - px0) & 7);
			}
		}
	}
	return packIndices(indices, mask, planes, tile);
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
		if (frames == null) return false;
		const key = shadow ? `${gid}@shadow` : gid;
		let group = groups.get(key);
		if (group == null) {
			group = { gid, key, dither: shadow, frames, cells: [] };
			groups.set(key, group);
		}
		layer.cells[cell.y * width + cell.x] = { gid: key };
		group.cells.push({ layer, x: cell.x, y: cell.y, gid, key, frames, dither: shadow });
		return true;
	};
	const flat = flattenLayers(ir.layers);
	// Celdas vivas con objeto estático encima: se animan igualmente y se emite
	// una **capa de cobertura** con los píxeles del objeto para reestamparlos
	// después del parche (el objeto queda por encima del overlay, como en Tiled).
	const coverCells = [];
	// Celdas animadas bajo un objeto/capa estatica posterior: no se animan para
	// que el frame del agua no borre el sprite horneado que va encima. Para las
	// capas de tiles el solape se decide **pixel a pixel**: el bbox de un arbol
	// roza celdas enteras sin cubrir ni un pixel del overlay y congelaba flores
	// visibles (el borde de la copa caia en su celda). Se tolera un solape de
	// borde minimo: cubrir 1-2 px de un sprite es menos grave que dejar una
	// flor/ola sin animacion.
	const kMaxOccluderOverlapPx = 2;
	// Si un objeto tapa casi toda la silueta animada, mantener la celda viva
	// (aunque sea con cobertura) no aporta nada visible y gasta frame, máscara
	// y jobs. Se cuece con el primer frame (bake_animated_first_frame) y se
	// ahorra la celda viva y su cobertura. Porcentaje de píxeles opacos que
	// deben quedar visibles para animar la celda.
	const kMinVisibleOpaquePercent = 25;
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
	// Oclusores por objeto para las capas de tiles vivas (misma regla que
	// `staticOverlapCells`, pero con la cobertura real de cada sprite).
	const occludersAbove = (fromIndex) => {
		const list = [];
		for (let i = fromIndex + 1; i < flat.length; ++i) {
			const above = flat[i];
			if (above.kind !== 'object') continue;
			// Las sombras cuentan para **decidir si la celda se cuece** (si tapan
			// la mayor parte de la silueta, se conserva la sombra horneada), pero
			// NUNCA se reestampan en la cobertura: su color horneado es una
			// mezcla suave y reestampar el sprite la convertía en un bloque
			// sólido (la cuña negra junto al tocón). La cobertura es solo para
			// objetos opacos.
			const shadow = /shadow|sombra/i.test(layerName(above));
			for (const object of above.objects ?? []) {
				if (object.gid == null || objectHasAnimation(ir, object, cache)) continue;
				const image = resolveTileImage(ir, object.gid, cache, { useFirstFrame: true });
				if (image.error != null) continue;
				list.push({
					rect: objectRect(ir, object, image.sw, image.sh),
					image,
					flip: decodeGid(object.gid),
					shadow,
				});
			}
		}
		return list;
	};
	// Unión de píxeles opacos de todos los frames de una celda. Un píxel vale
	// como opaco si el alfa de origen es >0: cualquier píxel que el overlay
	// escriba puede tapar al objeto de encima.
	const opaqueUnionCache = new Map();
	const animatedOpaqueUnion = (gid) => {
		const key = gid >>> 0;
		let union = opaqueUnionCache.get(key);
		if (union != null) return union;
		union = new Uint8Array(tile * tile);
		const flip = decodeGid(gid);
		const frames = animationFrames(ir, gid) ?? [{ gid }];
		for (const frame of frames) {
			const image = resolveTileImage(ir, frame.gid, cache, { useFirstFrame: false });
			if (image.error != null) continue;
			for (let y = 0; y < tile; ++y) {
				for (let x = 0; x < tile; ++x) {
					let u = flip.flipH ? image.sw - 1 - x : x;
					let v = flip.flipV ? image.sh - 1 - y : y;
					if (flip.flipD) { const t = u; u = v; v = t; }
					if (u < 0 || v < 0 || u >= image.sw || v >= image.sh) continue;
					const alpha = image.png.data[((image.sy + v) * image.png.width + image.sx + u) * 4 + 3];
					if (alpha > 0) union[y * tile + x] = 1;
				}
			}
		}
		opaqueUnionCache.set(key, union);
		return union;
	};
	/// Cobertura (alfa > 0) de los occluders dentro de una celda. Devuelve un
	/// bitmap `tile×tile` con 1 en los píxeles que algún objeto pinta.
	const cellOccluderCoverage = (x, y, occluders) => {
		const coverage = new Uint8Array(tile * tile);
		const px0 = x * tile;
		const py0 = y * tile;
		let any = false;
		for (const occluder of occluders) {
			const r = occluder.rect;
			if (r.x >= px0 + tile || r.y >= py0 + tile || r.x + r.w <= px0 || r.y + r.h <= py0) continue;
			const ix0 = Math.max(px0, Math.ceil(r.x));
			const ix1 = Math.min(px0 + tile, Math.floor(r.x + r.w));
			const iy0 = Math.max(py0, Math.ceil(r.y));
			const iy1 = Math.min(py0 + tile, Math.floor(r.y + r.h));
			for (let py = iy0; py < iy1; ++py) {
				for (let px = ix0; px < ix1; ++px) {
					let u = Math.floor(px - r.x);
					let v = Math.floor(py - r.y);
					if (u < 0 || v < 0 || u >= occluder.image.sw || v >= occluder.image.sh) continue;
					if (occluder.flip.flipH) u = occluder.image.sw - 1 - u;
					if (occluder.flip.flipV) v = occluder.image.sh - 1 - v;
					if (occluder.flip.flipD) { const t = u; u = v; v = t; }
					if (u < 0 || v < 0 || u >= occluder.image.sw || v >= occluder.image.sh) continue;
					const alpha = occluder.image.png.data[
						((occluder.image.sy + v) * occluder.image.png.width + occluder.image.sx + u) * 4 + 3];
					if (alpha > 0) {
						coverage[(py - py0) * tile + (px - px0)] = 1;
						any = true;
					}
				}
			}
		}
		return any ? coverage : null;
	};
	/// Cobertura y píxeles opacos de la celda: `{ overlap, opaque }`.
	const occluderStats = (coverage, gid) => {
		const union = animatedOpaqueUnion(gid);
		let overlap = 0;
		let opaque = 0;
		for (let i = 0; i < coverage.length; ++i) {
			if (union[i] === 0) continue;
			++opaque;
			if (coverage[i] !== 0) ++overlap;
		}
		return { overlap, opaque };
	};

	for (let li = 0; li < flat.length; ++li) {
		const layer = flat[li];
		if (layer.kind === 'tile') {
			const policy = layerPolicy[layerName(layer)];
			if (policy !== 'overlay' && policy !== 'animated' && policy !== 'live' && !animLayers.has(layerName(layer))) continue;
			if (layer.width !== width || layer.height !== height) {
				throw new Error(`${layerName(layer)}: capa ${layer.width}x${layer.height}, mapa ${width}x${height}`);
			}
			const occluders = occludersAbove(li);
			const out = { name: layerName(layer), cells: new Array(width * height).fill(null) };
			for (let y = 0; y < height; ++y) {
				for (let x = 0; x < width; ++x) {
					const gid = layer.data?.cells?.[y * width + x] ?? 0;
					if (gid === 0) continue;
					if (policy !== 'live' && !tileHasAnimation(ir, gid) && !animLayers.has(layerName(layer))) continue;
					const coverage = occluders.length !== 0 ? cellOccluderCoverage(x, y, occluders) : null;
					const stats = coverage != null ? occluderStats(coverage, gid) : null;
					if (stats != null && stats.opaque > 0 &&
					    (stats.opaque - stats.overlap) * 100 < stats.opaque * kMinVisibleOpaquePercent) {
						continue; // mayormente tapada: se cuece y no gasta celda viva ni cobertura
					}
					if (!addCell(out, { x, y }, gid, policy === 'live', shadowDither && /shadow|sombra/i.test(layerName(layer)))) continue;
					// Cobertura solo con objetos opacos: las sombras se quedan en el
					// bake (arriba se usaron para decidir si la celda merece animarse).
					const propOccluders = occluders.filter((occluder) => !occluder.shadow);
					const propCoverage = propOccluders.length !== 0
						? cellOccluderCoverage(x, y, propOccluders) : null;
					const propStats = propCoverage != null ? occluderStats(propCoverage, gid) : null;
					if (propStats != null && propStats.overlap > kMaxOccluderOverlapPx) {
						coverCells.push({
							x, y,
							frame: packCoverFrame({ occluders: propOccluders, cellX: x, cellY: y, colors, planes: p, tile, shadowIndex }),
						});
					}
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

	// Capa de cobertura: un grupo de 1 frame por celda viva tapada por un objeto
	// estático. El runtime la reestampa después del parche para que el objeto
	// siga por encima del overlay. Se añade después de la fusión para que ésta
	// no la trate como un cluster de animación.
	let coverLayerIndex = 0xffff;
	if (coverCells.length !== 0) {
		const coverLayer = { name: 'Covers', cells: new Array(width * height).fill(null) };
		for (const cover of coverCells) {
			const key = `cover:${cover.x},${cover.y}`;
			const frames = [{ gid: 0, ticks: 0 }];
			const cell = { layer: coverLayer, x: cover.x, y: cover.y, gid: 0, key, frames, dither: false };
			groups.set(key, { gid: 0, key, dither: false, frames, cells: [cell], cover: cover.frame });
			coverLayer.cells[cover.y * width + cover.x] = { gid: key };
		}
		layers.push(coverLayer);
		coverLayerIndex = layers.length - 1;
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
	// Frames interleaved opt-in: `animations.interleaved: true` en el plan.
	// Duplica la hoja (máscara por plano) y no siempre compensa; el camino
	// planar es el defecto (ver OPTIMIZACION_TILED_ANIMADO.md).
	const interleaved = plan.animations?.interleaved === true;
	// Frame normal: interleaved `[fila][plano] = imagen+máscara` o planar.
	const frameStride = interleaved ? 2 * (tile / 8) * tile * p : (tile / 8) * tile * p;
	const maskStride = (tile / 8) * tile;
	let frameCursor = 0;
	let sheetCursor = 0;
	let maskCursor = 0;
	// Layout por clave de grupo: las celdas de un mismo gid comparten frames,
	// máscara y flags; se emite una sola copia aunque el grupo se parta en
	// subgrupos de 4 celdas. Si la máscara es fija entre frames se emite UNA vez
	// (`kFlagSharedMask`) en vez de una por frame.
	const layoutByKey = new Map();
	// Primero los frames de los grupos normales, para que `first_frame ×
	// frame_stride` siga siendo válido. Los grupos fusionados se añaden después
	// con su propio `sheet_offset`/`frame_stride` (frames más grandes).
	for (let i = 0; i < groupList.length; ++i) {
		const group = groupList[i];
		if (group.fused != null || group.cover != null) continue;
		let layout = layoutByKey.get(group.key);
		if (layout == null) {
			const flip = decodeGid(group.gid);
			const packed = group.frames.map((frame) => packFrame(ir, frame.gid, flip, cache, colors, p, tile, group.dither === true, shadowIndex));
			const frames = interleaved ? packed.map((frame) => interleaveFrame(frame, tile / 16, tile, p)) : packed;
			const flags = packedFlags(packed) | (interleaved ? kFlagInterleaved : 0);
			const shared = (flags & kFlagSharedMask) !== 0;
			const first = frameCursor;
			const maskOffset = maskCursor;
			for (const frame of frames) {
				sheetParts.push(frame.frame);
				sheetCursor += frame.frame.length;
				++frameCursor;
			}
			if (shared) {
				maskParts.push(packed[0].mask);
				maskCursor += packed[0].mask.length;
			} else {
				for (const frame of packed) {
					maskParts.push(frame.mask);
					maskCursor += frame.mask.length;
				}
			}
			layout = { first, maskOffset, maskStride: packed[0].mask.length, flags };
			layoutByKey.set(group.key, layout);
		}
		groupMeta[i] = {
			gid: group.gid,
			first_frame: layout.first,
			frame_count: group.frames.length,
			flags: layout.flags,
			durations: group.frames.map((f) => f.ticks),
			cells: group.cells.length,
			first_cell: 0,
			cell_count: 0,
			dither: group.dither === true,
			pixel_width: 0,
			pixel_height: 0,
			sheet_offset: 0,
			frame_stride: 0,
			mask_offset: layout.maskOffset,
			mask_stride: layout.maskStride,
			opaque_mask: opaqueMask(packed),
		};
	}
	for (let i = 0; i < groupList.length; ++i) {
		const group = groupList[i];
		if (group.fused == null) continue;
		const packed = composeFusedFrames({ ir, cache, colors, planes: p, tile, group, shadowIndex });
		const widthPx = (group.fused.maxx - group.fused.minx + 1) * tile;
		const heightPx = (group.fused.maxy - group.fused.miny + 1) * tile;
		const frames = interleaved
			? packed.map((frame) => interleaveFrame(frame, widthPx / 16, heightPx, p))
			: packed;
		const flags = packedFlags(packed) | (interleaved ? kFlagInterleaved : 0);
		const shared = (flags & kFlagSharedMask) !== 0;
		const sheetOffset = sheetCursor;
		const maskOffset = maskCursor;
		for (const frame of frames) {
			sheetParts.push(frame.frame);
			sheetCursor += frame.frame.length;
			++frameCursor;
		}
		if (shared) {
			maskParts.push(packed[0].mask);
			maskCursor += packed[0].mask.length;
		} else {
			for (const frame of packed) {
				maskParts.push(frame.mask);
				maskCursor += frame.mask.length;
			}
		}
		groupMeta[i] = {
			gid: group.gid,
			first_frame: 0,
			frame_count: group.frames.length,
			flags,
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
			opaque_mask: opaqueMask(packed),
			fused: `${group.fused.minx},${group.fused.miny} ${widthPx}x${heightPx}`,
			duplicate_bytes: group.fused.duplicate_bytes,
			jobs_saved: group.fused.jobs_saved,
		};
	}

	for (let i = 0; i < groupList.length; ++i) {
		const group = groupList[i];
		if (group.cover == null) continue;
		const packed = group.cover;
		const frame = interleaved ? interleaveFrame(packed, tile / 16, tile, p) : packed;
		const sheetOffset = sheetCursor;
		const maskOffset = maskCursor;
		sheetParts.push(frame.frame);
		maskParts.push(packed.mask);
		sheetCursor += frame.frame.length;
		maskCursor += packed.mask.length;
		++frameCursor;
		groupMeta[i] = {
			gid: 0,
			first_frame: 0,
			frame_count: 1,
			flags: packedFlags([packed]) | (interleaved ? kFlagInterleaved : 0),
			durations: [1],
			cells: 1,
			first_cell: 0,
			cell_count: 0,
			dither: false,
			pixel_width: 0,
			pixel_height: 0,
			sheet_offset: sheetOffset,
			frame_stride: frame.frame.length,
			mask_offset: maskOffset,
			mask_stride: packed.mask.length,
			opaque_mask: opaqueMask([packed]),
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
	// `groupMeta[i].flags` se calculó al empaquetar cada grupo con `packedFlags`:
	// enmascarado si algún frame tiene transparencia y silueta fija si todos
	// comparten máscara (entonces se emitió una sola, `kFlagSharedMask`).
	fs.mkdirSync(outDir, { recursive: true });
	fs.writeFileSync(path.join(outDir, 'anim_sheet.bin'), sheet);
	fs.writeFileSync(path.join(outDir, 'anim_masks.bin'), masks);

	const lines = [];
	lines.push(`// Animaciones Tiled cocinadas (T5): overlays ${interleaved ? 'interleaved' : 'planares'} + máscara 1bpp.`);
	lines.push(`// ${groupList.length} grupos, ${frameCursor} frames, ${layers.length} capas, ${p} planos.`);
	lines.push('#pragma once');
	lines.push('#include <eng/field/tile_animation.hpp>');
	lines.push('');
	lines.push(`#define TILED_ANIM_GROUP_COUNT ${groupList.length}u`);
	lines.push(`#define TILED_ANIM_LAYER_COUNT ${layers.length}u`);
	// Índice de la capa sintética de cobertura (objetos estáticos sobre celdas
	// vivas), o 0xffff si el mapa no tiene ninguna. El runtime la reestampa
	// después del parche y la excluye del pase normal.
	lines.push(`#define TILED_ANIM_COVER_LAYER ${coverLayerIndex === 0xffff ? '0xffffu' : `${coverLayerIndex}u`}`);
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
		lines.push(`  {${group.first_frame}u, ${group.frame_count}u, ${group.flags}u, ${group.first_cell}u, ${group.cell_count}u, {${padded.map((v) => `${v}u`).join(',')}}, ${group.pixel_width}u, ${group.pixel_height}u, ${group.sheet_offset}u, ${group.frame_stride}u, ${group.mask_offset}u, ${group.mask_stride}u, ${group.opaque_mask ?? 0}u},`);
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
