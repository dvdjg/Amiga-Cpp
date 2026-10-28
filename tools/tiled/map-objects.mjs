#!/usr/bin/env node
// Resolución de roles de objeto (`cook`/`sort_y`/`occluder`) y emisión de la
// lista `MapObjects` cocinada.
//
// Fuente de verdad, en este orden:
//   1. propiedades del objeto en la copia de cocina (`cook`, `sort_y`, `occluder`);
//   2. reglas del cook-plan para su capa de objetos (`objects.<capa>.rules`);
//   3. `objects.<capa>.default`.
//
// Solo los objetos `foreground`/`occluder` se emiten al runtime. Los `baked`
// se funden en la imagen base (compose-map) y los `collision`/`skip` no se
// dibujan. Un objeto de colección escalado o rotado se rechaza: el runtime
// dibuja su arte nativo a escala 1:1.

import { decodeGid } from './parse-tmx.mjs';
import { ImageCache, objectRect, resolveTileImage, tileNode, tileSetFor } from './compose-map.mjs';

export const OBJECT_COOK_MODES = Object.freeze(['baked', 'foreground', 'occluder', 'collision', 'skip']);
export const OBJECT_SORT_Y = Object.freeze(['base', 'top']);
export const OBJECT_OCCLUDER = Object.freeze(['none', 'canopy']);

const layerName = (layer) => layer.name || `#${layer.id}`;

/// Normaliza un modo de objeto declarado en el plan o en una propiedad.
/// \param {string} value valor (`bake`, `foreground`, `overlay`, ...).
/// \returns {string|null} `baked|foreground|occluder|collision|skip|animated`, o null si no se reconoce.
export function normalizeObjectMode(value) {
	if (typeof value !== 'string') return null;
	switch (value.trim()) {
		case 'bake':
		case 'baked':
		case 'base':
		case 'parallax':
			return 'baked';
		case 'foreground':
			return 'foreground';
		case 'overlay':
		case 'live':
		case 'animated':
			return 'animated';
		case 'occluder':
			return 'occluder';
		case 'collision':
		case 'collisiononly':
			return 'collision';
		case 'skip':
		case 'actors':
			return 'skip';
		default:
			return null;
	}
}

/// Configuración de una capa de objetos: una cadena (modo de capa legacy) o el
/// objeto `{ default, sort_y, occluder, rules[] }`.
/// \param {object|null} plan cook-plan.
/// \param {string} name nombre de la capa (`Object Layer 1`).
/// \returns {{mode:string|null, defaultMode:string, sortY:string, occluder:string, rules:object[]}}
export function objectLayerConfig(plan, name) {
	const raw = plan?.objects?.[name];
	if (typeof raw === 'string') {
		const mode = normalizeObjectMode(raw);
		return {
			mode,
			defaultMode: mode === 'animated' ? 'animated' : (mode ?? 'baked'),
			sortY: 'base',
			occluder: 'none',
			rules: [],
		};
	}
	if (raw != null && typeof raw === 'object') {
		const defaultMode = normalizeObjectMode(raw.default ?? raw.mode ?? 'baked') ?? 'baked';
		return {
			mode: defaultMode,
			defaultMode,
			sortY: OBJECT_SORT_Y.includes(raw.sort_y) ? raw.sort_y : 'base',
			occluder: OBJECT_OCCLUDER.includes(raw.occluder) ? raw.occluder : 'none',
			rules: Array.isArray(raw.rules) ? raw.rules : [],
		};
	}
	return { mode: null, defaultMode: 'baked', sortY: 'base', occluder: 'none', rules: [] };
}

function propertyValue(object, name) {
	const entry = (object.properties ?? []).find((property) => property.name === name);
	return entry == null ? null : entry.value;
}

function gidMatches(rule, object) {
	if (rule.gid == null || object.gid == null) return false;
	const values = Array.isArray(rule.gid) ? rule.gid : [rule.gid];
	const id = decodeGid(object.gid).id;
	return values.some((value) => Number(value) === id || Number(value) === object.gid);
}

/// ¿La regla del plan coincide con el objeto? Los criterios declarados se
/// combinan con AND; al menos uno debe existir.
/// \param {object} rule entrada de `objects.<capa>.rules`.
/// \param {object} object objeto del IR.
/// \returns {boolean}
export function objectMatchesRule(rule, object) {
	if (rule == null || typeof rule !== 'object') return false;
	let any = false;
	if (rule.id != null) {
		any = true;
		const ids = Array.isArray(rule.id) ? rule.id : [rule.id];
		if (!ids.some((value) => Number(value) === object.id)) return false;
	}
	if (rule.gid != null) {
		any = true;
		if (!gidMatches(rule, object)) return false;
	}
	if (rule.class != null) {
		any = true;
		if (rule.class !== object.class) return false;
	}
	if (rule.type != null) {
		any = true;
		if (rule.type !== object.type) return false;
	}
	if (rule.name != null) {
		any = true;
		if (rule.name !== object.name) return false;
	}
	return any;
}

/// Resuelve el rol de un objeto. Las propiedades mandan sobre las reglas.
/// \param {object} object objeto del IR (`gid`, `class`, `properties`, ...).
/// \param {object} config salida de `objectLayerConfig`.
/// \returns {{mode:string, sortY:string, occluder:string, source:string}}
export function objectRole(object, config) {
	let mode = config.defaultMode;
	let sortY = config.sortY;
	let occluder = config.occluder;
	let source = 'default';
	if (config.mode === 'animated') {
		return { mode: 'animated', sortY: 'base', occluder: 'none', source: 'layer' };
	}
	for (const rule of config.rules) {
		if (!objectMatchesRule(rule, object)) continue;
		const declared = normalizeObjectMode(rule.cook ?? rule.mode);
		if (declared != null) mode = declared;
		if (OBJECT_SORT_Y.includes(rule.sort_y)) sortY = rule.sort_y;
		if (OBJECT_OCCLUDER.includes(rule.occluder)) occluder = rule.occluder;
		source = 'rule';
		break;
	}
	const propertyMode = normalizeObjectMode(propertyValue(object, 'cook'));
	if (propertyMode != null) {
		mode = propertyMode;
		source = 'property';
	}
	const propertySortY = propertyValue(object, 'sort_y');
	if (OBJECT_SORT_Y.includes(propertySortY)) sortY = propertySortY;
	const propertyOccluder = propertyValue(object, 'occluder');
	if (propertyOccluder === true) occluder = 'canopy';
	else if (OBJECT_OCCLUDER.includes(propertyOccluder)) occluder = propertyOccluder;
	if (occluder === 'canopy' && mode === 'foreground') mode = 'occluder';
	return { mode, sortY, occluder, source };
}

/// Recorre capas/grupos de objetos visibles.
function flattenObjectLayers(layers, out = []) {
	for (const layer of layers ?? []) {
		if (layer.visible === false) continue;
		if (layer.kind === 'group') {
			flattenObjectLayers(layer.layers, out);
			continue;
		}
		if (layer.kind === 'object') out.push(layer);
	}
	return out;
}

/// Frames de la animación de un tile object (`null` si es estático).
/// \param {object} ir IR de Tiled.
/// \param {number} gid GID del objeto (los flips se decodifican aparte).
/// \returns {Array<{gid:number,ticks:number}>|null}
function tileAnimationFrames(ir, gid) {
	const match = tileSetFor(ir, gid);
	if (match == null) return null;
	const tile = tileNode(match.tileset, match.local);
	const frames = tile?.animation?.frames;
	if (!frames || frames.length === 0) return null;
	return frames.map((frame) => ({
		gid: match.tileset.firstgid + frame.tileid,
		ticks: Math.max(1, Math.round(Number(frame.duration) / 20)),
	}));
}

/// Genera los `MapObject` de runtime (solo `foreground`/`occluder`) y las
/// imágenes únicas que necesitan. No escribe ficheros: `cook-objects.mjs` hace
/// la emisión binaria.
/// \param {object} ir IR de `parse-tmx`.
/// \param {object} plan cook-plan.
/// \param {object} [options] `{ cache }` para reutilizar imágenes ya cargadas.
/// \returns {{objects:object[], images:object[], stats:object, errors:string[]}}
export function collectMapObjects(ir, plan = {}, { cache = new ImageCache() } = {}) {
	const objects = [];
	const images = new Map();
	const errors = [];
	const stats = { layers: 0, baked: 0, foreground: 0, occluder: 0, collision: 0, skip: 0, animated: 0 };
	for (const layer of flattenObjectLayers(ir.layers)) {
		++stats.layers;
		const config = objectLayerConfig(plan, layerName(layer));
		for (const object of layer.objects ?? []) {
			if (object.visible === false) continue;
			const role = objectRole(object, config);
			if (Object.hasOwn(stats, role.mode)) ++stats[role.mode];
			if (role.mode !== 'foreground' && role.mode !== 'occluder') continue;
			if (object.gid == null) {
				errors.push(`${layerName(layer)}: objeto ${object.id} ${role.mode} sin gid; no se puede dibujar`);
				continue;
			}
			const flags = decodeGid(object.gid);
			if (Math.abs(object.rotation) > 1e-6) {
				errors.push(`${layerName(layer)}: objeto ${object.id} rotado ${object.rotation}°; el runtime no rota objetos`);
				continue;
			}
			const image = resolveTileImage(ir, object.gid, cache, { useFirstFrame: true });
			if (image.error != null) {
				errors.push(`${layerName(layer)}: objeto ${object.id}: ${image.error}`);
				continue;
			}
			const animation = tileAnimationFrames(ir, object.gid);
			const frames = animation == null ? null : animation.map((frame) => {
				const resolved = resolveTileImage(ir, frame.gid, cache, { useFirstFrame: true });
				if (resolved.error != null) {
					throw new Error(`${layerName(layer)}: objeto ${object.id}: ${resolved.error}`);
				}
				return { ...frame, image: resolved };
			});
			const nativeW = image.sw;
			const nativeH = image.sh;
			if ((object.width > 0 && Math.abs(object.width - nativeW) > 1e-6) ||
			    (object.height > 0 && Math.abs(object.height - nativeH) > 1e-6)) {
				errors.push(
					`${layerName(layer)}: objeto ${object.id} escala ${object.width}x${object.height}; ` +
					`el runtime solo dibuja el arte nativo ${nativeW}x${nativeH}`);
				continue;
			}
			const rect = objectRect(ir, object, nativeW, nativeH);
			// El offset del tileset forma parte del anclaje del objeto; sin él, el
			// runtime dibujaría la imagen desplazada respecto al bake.
			const offset = image.tileoffset ?? { x: 0, y: 0 };
			rect.x += Number(offset.x) || 0;
			rect.y += Number(offset.y) || 0;
			const key = `${object.gid}:${nativeW}x${nativeH}`;
			let imageIndex = images.get(key);
			if (imageIndex == null) {
				imageIndex = images.size;
				images.set(key, {
					index: imageIndex,
					key,
					gid: object.gid,
					flags,
					width: nativeW,
					height: nativeH,
					image,
					frames,
				});
			}
			if (frames != null) ++stats.animated;
			const footY = rect.y + rect.h;
			objects.push({
				id: object.id,
				layer: layerName(layer),
				gid: object.gid,
				image: imageIndex,
				x: rect.x,
				y: rect.y,
				w: rect.w,
				h: rect.h,
				foot_y: footY,
				sort_y: role.sortY,
				sort_key: role.sortY === 'top' ? rect.y : footY,
				kind: role.mode,
				occluder: role.occluder,
				source: role.source,
			});
		}
	}
	// Orden estable del runtime: primero foreground, luego occluders; dentro de
	// cada grupo por clave de orden y, a igualdad, por id de Tiled.
	objects.sort((a, b) => {
		const ka = a.kind === 'foreground' ? 0 : 1;
		const kb = b.kind === 'foreground' ? 0 : 1;
		return (ka - kb) || (a.sort_key - b.sort_key) || (a.id - b.id);
	});
	for (let i = 0; i < objects.length; ++i) objects[i].runtime_index = i;
	const foregroundCount = objects.filter((object) => object.kind === 'foreground').length;
	const imageList = [...images.values()].sort((a, b) => a.index - b.index);
	return {
		objects,
		images: imageList,
		foreground_count: foregroundCount,
		occluder_count: objects.length - foregroundCount,
		stats: { ...stats, runtime_objects: objects.length, unique_images: imageList.length, tile: ir.map.tilewidth || 16 },
		errors,
	};
}
