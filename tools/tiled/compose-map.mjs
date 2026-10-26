#!/usr/bin/env node
// Compone una imagen RGBA "baked" a partir de un TMX/IR de Tiled: capas de tiles,
// grupos con opacidad, objectgroups con tile objects e image layers. Es la entrada
// del cooker de paleta/tiles (T2); no genera todavía el MapBundle runtime.
//
// Uso:
//   node tools/tiled/compose-map.mjs <mapa.tmx> --plan cook-plan.json --out out/assets/tiled/<mapa> [--layer-preview]
//   node tools/tiled/compose-map.mjs --ir ir.json --plan cook-plan.json --out ...
//
// Salida: composed.png + compose.json (bounds, capas, objetos, avisos y estadísticas).
// Las capas vivas/animadas se hornean con su **primer frame** en este paso; el overlay
// y el parcheo por Blitter llegan en T5. La opacidad de capa/grupo se aplica al componer.

import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
import { fileURLToPath } from 'node:url';
import { decodeGid, parseTmxFile } from './parse-tmx.mjs';
import { objectLayerConfig, objectRole } from './map-objects.mjs';

const TILE_EPS = 1e-6;
const MAX_PIXELS = 16 * 1024 * 1024;

const asNumber = (value, fallback = 0) => {
	const n = Number(value);
	return Number.isFinite(n) ? n : fallback;
};

function parseBackground(value) {
	if (typeof value !== 'string' || !value.startsWith('#')) return [0, 0, 0, 255];
	const hex = value.slice(1);
	if (hex.length === 8) return [parseInt(hex.slice(2, 4), 16), parseInt(hex.slice(4, 6), 16), parseInt(hex.slice(6, 8), 16), parseInt(hex.slice(0, 2), 16)];
	if (hex.length === 6) return [parseInt(hex.slice(0, 2), 16), parseInt(hex.slice(2, 4), 16), parseInt(hex.slice(4, 6), 16), 255];
	return [0, 0, 0, 255];
}

function layerName(layer) {
	return layer.name || `#${layer.id}`;
}

function flattenLayers(layers, inherited, out) {
	for (const layer of layers) {
		const visible = inherited.visible && layer.visible !== false;
		if (!visible) continue;
		const opacity = Math.max(0, Math.min(1, inherited.opacity * asNumber(layer.opacity, 1)));
		const offsetx = inherited.offsetx + asNumber(layer.offsetx, 0);
		const offsety = inherited.offsety + asNumber(layer.offsety, 0);
		if (layer.kind === 'group') {
			flattenLayers(layer.layers ?? [], { visible, opacity, offsetx, offsety }, out);
		} else {
			out.push({ layer, opacity, offsetx, offsety });
		}
	}
}

export class ImageCache {
	constructor() {
		this.images = new Map();
	}
	get(file) {
		if (!file) return null;
		let png = this.images.get(file);
		if (png !== undefined) return png;
		if (!fs.existsSync(file)) {
			this.images.set(file, null);
			return null;
		}
		png = PNG.sync.read(fs.readFileSync(file));
		this.images.set(file, png);
		return png;
	}
}

export function tileSetFor(ir, gid) {
	const id = decodeGid(gid).id;
	const sorted = [...ir.tilesets].sort((a, b) => a.firstgid - b.firstgid);
	let found = null;
	for (const tileset of sorted) {
		if (id >= tileset.firstgid && (tileset.lastgid == null || id <= tileset.lastgid)) found = tileset;
	}
	return found == null ? null : { tileset: found, local: id - found.firstgid };
}

export function tileNode(tileset, local) {
	return tileset.tiles?.find((tile) => tile.id === local) ?? null;
}

export function tileHasAnimation(ir, gid) {
	const match = tileSetFor(ir, gid);
	if (match == null) return false;
	const tile = tileNode(match.tileset, match.local);
	return tile?.animation?.frames?.length > 0;
}

export function objectHasAnimation(ir, object, cache) {
	if (object?.gid == null) return false;
	return tileHasAnimation(ir, object.gid);
}

export function resolveTileImage(ir, gid, cache, { useFirstFrame = true, stack = new Set() } = {}) {
	const match = tileSetFor(ir, gid);
	if (match == null) return { error: `gid ${gid} sin tileset` };
	const { tileset, local } = match;
	const tile = tileNode(tileset, local);
	if (tile?.animation != null && useFirstFrame && tile.animation.frames.length > 0) {
		if (stack.has(local)) return { error: `ciclo de animación en ${tileset.name}` };
		stack.add(local);
		const resolved = resolveTileImage(ir, tileset.firstgid + tile.animation.frames[0].tileid, cache, { useFirstFrame: false, stack });
		stack.delete(local);
		return resolved;
	}
	if (tileset.image?.path != null && tileset.columns > 0) {
		const png = cache.get(tileset.image.path);
		if (png == null) return { error: `imagen no encontrada: ${tileset.image.path}` };
		const col = local % tileset.columns;
		const row = Math.floor(local / tileset.columns);
		const sx = asNumber(tileset.margin, 0) + col * (tileset.tilewidth + asNumber(tileset.spacing, 0));
		const sy = asNumber(tileset.margin, 0) + row * (tileset.tileheight + asNumber(tileset.spacing, 0));
		return {
			png,
			sx,
			sy,
			sw: tileset.tilewidth,
			sh: tileset.tileheight,
			tileoffset: tileset.tileoffset ?? { x: 0, y: 0 },
			tileset,
			local,
		};
	}
	if (tile == null) return { error: `tile local ${local} no definido en ${tileset.name || tileset.firstgid}` };
	if (tile.image?.path == null) return { error: `tile local ${local} sin imagen` };
	const png = cache.get(tile.image.path);
	if (png == null) return { error: `imagen no encontrada: ${tile.image.path}` };
	return {
		png,
		sx: 0,
		sy: 0,
		// En una colección, `tile.image` puede recortar una región menor que el
		// PNG completo; usar sus dimensiones declaradas, no el tamaño del fichero.
		sw: tile.image.width > 0 ? tile.image.width : png.width,
		sh: tile.image.height > 0 ? tile.image.height : png.height,
		tileoffset: tileset.tileoffset ?? { x: 0, y: 0 },
		tileset,
		local,
	};
}

function blendPixel(dst, offset, r, g, b, a) {
	if (a <= 0) return;
	if (a >= 1) {
		dst[offset] = r;
		dst[offset + 1] = g;
		dst[offset + 2] = b;
		dst[offset + 3] = 255;
		return;
	}
	const da = dst[offset + 3] / 255;
	const oa = a + da * (1 - a);
	if (oa <= 0) {
		dst[offset] = 0;
		dst[offset + 1] = 0;
		dst[offset + 2] = 0;
		dst[offset + 3] = 0;
		return;
	}
	dst[offset] = Math.round((r * a + dst[offset] * da * (1 - a)) / oa);
	dst[offset + 1] = Math.round((g * a + dst[offset + 1] * da * (1 - a)) / oa);
	dst[offset + 2] = Math.round((b * a + dst[offset + 2] * da * (1 - a)) / oa);
	dst[offset + 3] = Math.round(oa * 255);
}

function blit(dst, dstWidth, dstHeight, image, sx, sy, sw, sh, dx, dy, dw, dh, opacity, flipH, flipV, flipD) {
	if (image == null || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
	const left = Math.max(0, dx);
	const top = Math.max(0, dy);
	const right = Math.min(dstWidth, dx + dw);
	const bottom = Math.min(dstHeight, dy + dh);
	if (right <= left || bottom <= top) return;
	for (let y = top; y < bottom; ++y) {
		const v = Math.floor(((y - dy) * sh) / dh);
		for (let x = left; x < right; ++x) {
			const u = Math.floor(((x - dx) * sw) / dw);
			const su = flipH ? sw - 1 - u : u;
			const sv = flipV ? sh - 1 - v : v;
			let tx = su;
			let ty = sv;
			if (flipD) {
				tx = sv;
				ty = su;
			}
			const px = Math.min(image.width - 1, Math.max(0, sx + tx));
			const py = Math.min(image.height - 1, Math.max(0, sy + ty));
			const so = (py * image.width + px) * 4;
			const a = (image.data[so + 3] / 255) * opacity;
			blendPixel(dst, (y * dstWidth + x) * 4, image.data[so], image.data[so + 1], image.data[so + 2], a);
		}
	}
}

export function objectRect(ir, object, iw, ih) {
	const w = object.width > 0 ? object.width : iw;
	const h = object.height > 0 ? object.height : ih;
	const align = ir.map.objectalignment || 'unspecified';
	const horizontal = align === 'unspecified' ? 'left' : align.includes('left') ? 'left' : align.includes('right') ? 'right' : 'center';
	const vertical = align.includes('top') ? 'top' : align.includes('bottom') || align === 'unspecified' ? 'bottom' : 'center';
	const x = horizontal === 'left' ? object.x : horizontal === 'right' ? object.x - w : object.x - w / 2;
	const y = vertical === 'top' ? object.y : vertical === 'bottom' ? object.y - h : object.y - h / 2;
	return { x, y, w, h };
}

function drawObject(ir, object, layerOpacity, offsetx, offsety, ctx, policy = null) {
	if (object.gid == null) return;
	const image = resolveTileImage(ir, object.gid, ctx.cache, { useFirstFrame: true });
	if (image.error != null) {
		ctx.errors.push(`${layerName(object.__layer ?? {})}: ${image.error}`);
		return;
	}
	if ((policy === 'live' || policy === 'overlay' || policy === 'animated') &&
	    (policy === 'live' || objectHasAnimation(ir, object, ctx.cache))) {
		if (image.sw === 16 && image.sh === 16) {
			ctx.skippedAnimated++;
			return;
		}
	}
	const flags = decodeGid(object.gid);
	const rect = objectRect(ir, object, image.sw, image.sh);
	const tileoffset = image.tileoffset ?? { x: 0, y: 0 };
	if (Math.abs(object.rotation) > TILE_EPS) ctx.warnings.add(`rotación de objeto no horneada (id ${object.id}, ${object.rotation}°)`);
	if (flags.flipD && image.sw !== image.sh) ctx.warnings.add(`flip diagonal en imagen no cuadrada (id ${object.id})`);
	blit(ctx.buffer, ctx.width, ctx.height, image.png, image.sx, image.sy, image.sw, image.sh,
		Math.round(rect.x + offsetx + tileoffset.x), Math.round(rect.y + offsety + tileoffset.y),
		Math.round(rect.w), Math.round(rect.h), layerOpacity, flags.flipH, flags.flipV, flags.flipD);
	ctx.objectCount++;
}

function collectPixelBounds(ir, flat) {
	let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
	const add = (x, y) => { if (x < minX) minX = x; if (y < minY) minY = y; if (x > maxX) maxX = x; if (y > maxY) maxY = y; };
	const tw = ir.map.tilewidth || 16;
	const th = ir.map.tileheight || 16;
	// En mapas finitos el lienzo es el rectángulo lógico del mapa; los objetos que
	// sobresalen se recortan como en el export de Tiled. En infinitos se calcula por
	// chunks/objetos porque no hay un rectángulo declarado.
	if (ir.map.width > 0 && ir.map.height > 0) {
		return { minX: 0, minY: 0, maxX: ir.map.width * tw, maxY: ir.map.height * th };
	}
	const addLayer = (layer) => {
		if (layer.kind === 'tile') {
			const w = layer.width || 0;
			const h = layer.height || 0;
			if (layer.data?.chunks != null) {
				for (const chunk of layer.data.chunks) {
					add(chunk.x * tw, chunk.y * th);
					add((chunk.x + chunk.width) * tw, (chunk.y + chunk.height) * th);
				}
			} else if (w > 0 && h > 0) {
				add(0, 0);
				add(w * tw, h * th);
			}
		} else if (layer.kind === 'object') {
			for (const object of layer.objects ?? []) {
				if (object.gid == null) continue;
				const image = resolveTileImage(ir, object.gid, new ImageCache(), { useFirstFrame: true });
				const iw = image.error == null ? image.sw : tw;
				const ih = image.error == null ? image.sh : th;
				const rect = objectRect(ir, object, iw, ih);
				add(rect.x, rect.y);
				add(rect.x + rect.w, rect.y + rect.h);
			}
		} else if (layer.kind === 'image' && layer.image?.width > 0 && layer.image?.height > 0) {
			add(asNumber(layer.offsetx, 0), asNumber(layer.offsety, 0));
			add(asNumber(layer.offsetx, 0) + layer.image.width, asNumber(layer.offsety, 0) + layer.image.height);
		}
	};
	for (const item of flat) addLayer(item.layer);
	if (!Number.isFinite(minX)) { minX = 0; minY = 0; maxX = tw; maxY = th; }
	return { minX, minY, maxX, maxY };
}

function applyPlan(ir, plan) {
	const layerPolicy = plan?.layers ?? {};
	const objectPolicy = plan?.objects ?? {};
	const flat = [];
	flattenLayers(ir.layers, { visible: true, opacity: 1, offsetx: 0, offsety: 0 }, flat);
	const usable = [];
	for (const item of flat) {
		const name = layerName(item.layer);
		const policy = item.layer.kind === 'object' ? objectPolicy[name] : layerPolicy[name];
		if (policy === 'skip' || policy === 'collision' || policy === 'collisiononly' || policy === 'actors' || policy === 'live') continue;
		if (policy === 'parallax' && item.layer.kind !== 'image') {
			// Se hornea en su posición base; el parallax real necesita un campo propio.
			usable.push(item);
			continue;
		}
		usable.push({ ...item, policy });
	}
	return usable;
}

/// Compone la imagen baked. Devuelve `{png, bounds, warnings, errors, stats}`.
export function composeMap(ir, plan = null, { layerPreview = false } = {}) {
	const flat = applyPlan(ir, plan);
	const cache = new ImageCache();
	const bounds = collectPixelBounds(ir, flat);
	const align16 = (v) => Math.floor(v / 16) * 16;
	const align16Up = (v) => Math.ceil(v / 16) * 16;
	const minX = align16(Math.min(0, bounds.minX));
	const minY = align16(Math.min(0, bounds.minY));
	const maxX = align16Up(Math.max(bounds.maxX, minX + 16));
	const maxY = align16Up(Math.max(bounds.maxY, minY + 16));
	const width = maxX - minX;
	const height = maxY - minY;
	if (width <= 0 || height <= 0 || width * height > MAX_PIXELS) throw new Error(`bounds baked inválidos: ${width}x${height}`);
	const background = parseBackground(ir.map.backgroundcolor);
	const buffer = new Uint8Array(width * height * 4);
	for (let i = 0; i < width * height; ++i) {
		buffer[i * 4] = background[0];
		buffer[i * 4 + 1] = background[1];
		buffer[i * 4 + 2] = background[2];
		buffer[i * 4 + 3] = background[3];
	}
	const ctx = { buffer, width, height, cache, warnings: new Set(), errors: [], objectCount: 0, skippedAnimated: 0, skippedRuntimeObjects: 0 };
	const tileCount = { cells: 0, drawn: 0, animated: 0, skippedAnimated: 0, missing: 0 };
	const layerReports = [];
	for (const item of flat) {
		const layer = item.layer;
		const name = layerName(layer);
		let drawn = 0;
		if (layer.kind === 'tile') {
			const tw = ir.map.tilewidth || 16;
			const th = ir.map.tileheight || 16;
			const opacity = item.opacity;
			const visit = (cx, cy, gid) => {
				++tileCount.cells;
				if (gid === 0) return;
				const animatedTile = tileHasAnimation(ir, gid);
				if ((item.policy === 'overlay' || item.policy === 'animated') && animatedTile) {
					++tileCount.animated;
					++tileCount.skippedAnimated;
					return;
				}
				const image = resolveTileImage(ir, gid, cache, { useFirstFrame: true });
				if (image.error != null) {
					++tileCount.missing;
					ctx.errors.push(`${name}[${cx},${cy}]: ${image.error}`);
					return;
				}
				const flags = decodeGid(gid);
				if (flags.flipD && image.sw !== image.sh) ctx.warnings.add(`flip diagonal en tile no cuadrado (${name})`);
				const tileoffset = image.tileoffset ?? { x: 0, y: 0 };
				blit(buffer, width, height, image.png, image.sx, image.sy, image.sw, image.sh,
					Math.round(cx * tw - minX + item.offsetx + tileoffset.x),
					Math.round(cy * th - minY + item.offsety + tileoffset.y),
					tw, th, opacity, flags.flipH, flags.flipV, flags.flipD);
				++drawn;
				++tileCount.drawn;
				if (image.tileset?.tiles?.some((tile) => tile.id === image.local && tile.animation != null)) ++tileCount.animated;
			};
			if (layer.data?.chunks != null) {
				for (const chunk of layer.data.chunks) {
					for (let ly = 0; ly < chunk.height; ++ly) for (let lx = 0; lx < chunk.width; ++lx) visit(chunk.x + lx, chunk.y + ly, chunk.cells[ly * chunk.width + lx] ?? 0);
				}
			} else {
				const w = layer.width || 0;
				for (let y = 0; y < layer.height; ++y) for (let x = 0; x < w; ++x) visit(x, y, layer.data?.cells?.[y * w + x] ?? 0);
			}
		} else if (layer.kind === 'object') {
			const objects = [...(layer.objects ?? [])];
			if (layer.draworder === 'topdown') objects.sort((a, b) => (a.y - b.y) || (a.id - b.id));
			const config = objectLayerConfig(plan, name);
			for (const object of objects) {
				object.__layer = layer;
				const role = objectRole(object, config);
				if (role.mode === 'foreground' || role.mode === 'occluder') {
					// El objeto vive en runtime (MapObjects): no debe quedar una copia
					// horneada que revele su posición al taparlo.
					++ctx.skippedRuntimeObjects;
					continue;
				}
				if (role.mode === 'collision' || role.mode === 'skip') continue;
				drawObject(ir, object, item.opacity, item.offsetx, item.offsety, ctx, role.mode);
			}
			drawn = objects.length;
		} else if (layer.kind === 'image') {
			if (layer.image?.path != null) {
				const png = cache.get(layer.image.path);
				if (png == null) ctx.errors.push(`${name}: imagen no encontrada ${layer.image.path}`);
				else {
					const w = layer.image.width || png.width;
					const h = layer.image.height || png.height;
					if (layer.repeatx || layer.repeaty) ctx.warnings.add(`${name}: repeatx/repeaty se ignoran en el bake`);
					blit(buffer, width, height, png, 0, 0, png.width, png.height,
						Math.round(asNumber(layer.offsetx, 0) - minX), Math.round(asNumber(layer.offsety, 0) - minY), w, h, item.opacity, false, false, false);
					drawn = 1;
				}
			} else {
				ctx.errors.push(`${name}: capa de imagen sin fuente`);
			}
		}
		layerReports.push({ name, kind: layer.kind, visible: layer.visible !== false, opacity: item.opacity, drawn });
		if (layerPreview) {
			// La preview por capa se deja al cooker; aquí basta el informe.
		}
	}
	const png = new PNG({ width, height });
	for (let i = 0; i < buffer.length; ++i) png.data[i] = buffer[i];
	return {
		png,
		bounds: { minX, minY, maxX, maxY, width, height },
		warnings: [...ctx.warnings],
		errors: ctx.errors,
		stats: { ...tileCount, objects: ctx.objectCount, skippedAnimatedObjects: ctx.skippedAnimated, skippedRuntimeObjects: ctx.skippedRuntimeObjects, layers: layerReports },
	};
}

function argValue(args, name) {
	const at = args.indexOf(name);
	return at >= 0 ? args[at + 1] : null;
}

function main() {
	const args = process.argv.slice(2);
	let file = null, irFile = null, planFile = null, outDir = null;
	for (let k = 0; k < args.length; ++k) {
		const arg = args[k];
		if (arg === '--ir') { irFile = args[++k] ?? null; continue; }
		if (arg === '--plan') { planFile = args[++k] ?? null; continue; }
		if (arg === '--out') { outDir = args[++k] ?? null; continue; }
		if (arg.startsWith('--')) continue;
		if (file == null) file = arg;
	}
	if (file == null && irFile == null) {
		console.error('Uso: node tools/tiled/compose-map.mjs <mapa.tmx> [--plan cook-plan.json] [--out dir]');
		process.exit(2);
	}
	const ir = irFile != null ? JSON.parse(fs.readFileSync(path.resolve(irFile), 'utf8')) : parseTmxFile(file);
	const plan = planFile != null ? JSON.parse(fs.readFileSync(path.resolve(planFile), 'utf8')) : null;
	const result = composeMap(ir, plan);
	const out = path.resolve(outDir ?? `out/assets/tiled/${path.basename(file ?? 'map', path.extname(file ?? 'map'))}`);
	fs.mkdirSync(out, { recursive: true });
	const pngPath = path.join(out, 'composed.png');
	fs.writeFileSync(pngPath, PNG.sync.write(result.png));
	const report = {
		source: ir.source,
		bounds: result.bounds,
		stats: result.stats,
		warnings: result.warnings,
		errors: result.errors,
	};
	fs.writeFileSync(path.join(out, 'compose.json'), JSON.stringify(report, null, 2) + '\n', 'utf8');
	console.log(`[compose] ${result.bounds.width}x${result.bounds.height} -> ${pngPath}`);
	console.log(`[compose] tiles=${result.stats.drawn}/${result.stats.cells} animated=${result.stats.animated} objects=${result.stats.objects} errors=${result.errors.length} warnings=${result.warnings.length}`);
	for (const error of result.errors) console.error(`[compose] error: ${error}`);
	for (const warning of result.warnings) console.error(`[compose] warning: ${warning}`);
	process.exit(result.errors.length === 0 ? 0 : 1);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
