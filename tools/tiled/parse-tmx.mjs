#!/usr/bin/env node
// Parser estructural de Tiled (.tmx/.tsx) → IR `eng.tiled-ir` v1.
//
// Sustituye la extracción por expresiones regulares de `tools/ehb/parse-tmx.mjs` para el
// pipeline Tiled nuevo. Lee el XML con `./xml.mjs`, resuelve TSX externos, conserva los GID
// completos (id + flips), las capas agrupadas, los objetos, propiedades y animaciones, y
// emite diagnósticos con fichero/línea/columna. No lee imágenes ni cocina: eso es T2/T3.
//
// Uso:
//   node tools/tiled/parse-tmx.mjs <mapa.tmx> [--out ir.json] [--json] [--strict] [--quiet]
//
// Código: 0 OK, 1 errores de validación, 2 uso o XML inválido.
//
// IR (resumen; el esquema completo está en tools/tiled/README.md):
//   { format:'eng.tiled-ir', version:1, source, map:{...}, tilesets:[...], layers:[...],
//     diagnostics:[{severity,code,message,source,line,column,path}], stats:{...} }
//
// Los GID se guardan **en crudo** en `cells`/`chunks`/`objects[].gid` para no perder los
// bits de flip H/V/D; `decodeGid(gid)` los separa. El cooker (T2/T3) es quien materializa
// `VisualId`, páginas y variantes.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { parseXml, XmlSyntaxError, child, hasAttr } from './xml.mjs';

export const IR_FORMAT = 'eng.tiled-ir';
export const IR_VERSION = 1;

export const GID_ID_MASK = 0x1fffffff;
export const GID_FLIP_H = 0x80000000;
export const GID_FLIP_V = 0x40000000;
export const GID_FLIP_D = 0x20000000;
const LAYER_NAMES = new Set(['layer', 'objectgroup', 'imagelayer', 'group']);
const TILESET_CHILD_NAMES = new Set(['image', 'tileoffset', 'grid', 'properties', 'tile', 'wangsets', 'terraintypes']);
const TILE_CHILD_NAMES = new Set(['image', 'animation', 'objectgroup', 'properties', 'terrain', 'wangsets']);

/// Separa un GID de Tiled en id local/global y flags de transformación.
/// Los bits 29–31 son H/V/D; el id es `gid & 0x1FFFFFFF`. Un `gid` 0 es vacío.
export function decodeGid(gid) {
	const value = Number(gid) >>> 0;
	return {
		id: value & GID_ID_MASK,
		flipH: (value & GID_FLIP_H) !== 0,
		flipV: (value & GID_FLIP_V) !== 0,
		flipD: (value & GID_FLIP_D) !== 0,
	};
}

const asInt = (value, fallback = 0) => {
	const n = Number.parseInt(value ?? '', 10);
	return Number.isFinite(n) ? n : fallback;
};
const asNumber = (value, fallback = 0) => {
	const n = Number.parseFloat(value ?? '');
	return Number.isFinite(n) ? n : fallback;
};
const asBool = (value, fallback = false) => {
	if (value == null) return fallback;
	return value === '1' || value === 'true' || value === 'True';
};
const attrsOf = (node) => ({ ...(node?.attrs ?? {}) });

function addDiagnostic(ctx, severity, code, message, node = null, where = null) {
	ctx.diagnostics.push({
		severity,
		code,
		message,
		source: ctx.source,
		line: node?.line ?? null,
		column: node?.column ?? null,
		path: where,
	});
}

function parsePropertyValue(raw, type) {
	switch (type) {
		case 'int': {
			const n = Number.parseInt(raw ?? '', 10);
			return Number.isFinite(n) ? n : raw;
		}
		case 'float': {
			const n = Number.parseFloat(raw ?? '');
			return Number.isFinite(n) ? n : raw;
		}
		case 'bool':
			return raw === 'true' || raw === '1';
		default:
			return raw;
	}
}

function parseProperties(node, ctx, where) {
	if (node == null) return [];
	const out = [];
	for (const prop of node.children.filter((item) => item.name === 'property')) {
		const name = prop.attrs.name ?? '';
		const type = prop.attrs.type ?? 'string';
		const entry = { name, type };
		if (prop.attrs.propertytype != null) entry.propertytype = prop.attrs.propertytype;
		if (hasAttr(prop, 'value')) entry.value = parsePropertyValue(prop.attrs.value, type);
		const nested = child(prop, 'properties');
		if (nested != null) entry.properties = parseProperties(nested, ctx, `${where}.${name}`);
		if (!hasAttr(prop, 'value') && nested == null) entry.value = '';
		entry.attrs = attrsOf(prop);
		out.push(entry);
	}
	return out;
}

function parseImage(node, ctx, baseFile, where) {
	if (node == null) return null;
	const source = node.attrs.source ?? '';
	const image = {
		source,
		path: source === '' ? null : path.resolve(path.dirname(baseFile), source),
		width: asInt(node.attrs.width, 0),
		height: asInt(node.attrs.height, 0),
		trans: node.attrs.trans ?? null,
		attrs: attrsOf(node),
	};
	if (source === '') addDiagnostic(ctx, 'warning', 'image-source-missing', 'imagen sin atributo source', node, where);
	return image;
}

function parsePointList(value, ctx, node, where) {
	const tokens = String(value ?? '').trim().split(/\s+/).filter(Boolean);
	if (tokens.length === 0) return [];
	const points = [];
	for (const token of tokens) {
		const parts = token.split(',');
		if (parts.length !== 2) {
			addDiagnostic(ctx, 'error', 'polygon-point-invalid', `punto inválido "${token}"`, node, where);
			continue;
		}
		const x = Number.parseFloat(parts[0]);
		const y = Number.parseFloat(parts[1]);
		if (!Number.isFinite(x) || !Number.isFinite(y)) {
			addDiagnostic(ctx, 'error', 'polygon-point-invalid', `punto inválido "${token}"`, node, where);
			continue;
		}
		points.push(x, y);
	}
	return points;
}

function parseTextObject(node, ctx, where) {
	const out = {
		text: node.text.trim(),
		wrap: asBool(node.attrs.wrap, false),
		color: node.attrs.color ?? null,
		fontfamily: node.attrs.fontfamily ?? null,
		pixelsize: asInt(node.attrs.pixelsize, 0),
		bold: asBool(node.attrs.bold, false),
		italic: asBool(node.attrs.italic, false),
		underline: asBool(node.attrs.underline, false),
		strikeout: asBool(node.attrs.strikeout, false),
		kerning: asBool(node.attrs.kerning, true),
		halign: node.attrs.halign ?? 'left',
		valign: node.attrs.valign ?? 'top',
		properties: parseProperties(child(node, 'properties'), ctx, `${where}.properties`),
	};
	return out;
}

function parseObject(node, ctx, baseFile, where) {
	const object = {
		id: asInt(node.attrs.id, 0),
		name: node.attrs.name ?? '',
		type: node.attrs.type ?? '',
		class: node.attrs.class ?? '',
		x: asNumber(node.attrs.x, 0),
		y: asNumber(node.attrs.y, 0),
		width: asNumber(node.attrs.width, 0),
		height: asNumber(node.attrs.height, 0),
		rotation: asNumber(node.attrs.rotation, 0),
		visible: asBool(node.attrs.visible, true),
		gid: hasAttr(node, 'gid') ? Number(node.attrs.gid) : null,
		template: node.attrs.template ?? '',
		shape: 'rectangle',
		points: null,
		text: null,
		properties: parseProperties(child(node, 'properties'), ctx, `${where}.properties`),
		attrs: attrsOf(node),
	};
	if (object.template !== '') {
		addDiagnostic(ctx, 'error', 'template-unsupported', `plantilla de objeto no soportada: ${object.template}`, node, where);
	}
	if (object.gid != null && (!Number.isFinite(object.gid) || object.gid < 0 || object.gid > 0xffffffff)) {
		addDiagnostic(ctx, 'error', 'gid-invalid', `gid de objeto inválido "${node.attrs.gid}"`, node, where);
		object.gid = null;
	}
	if (child(node, 'ellipse') != null) object.shape = 'ellipse';
	else if (child(node, 'point') != null) object.shape = 'point';
	else if (child(node, 'polygon') != null) {
		object.shape = 'polygon';
		object.points = parsePointList(child(node, 'polygon').attrs.points, ctx, child(node, 'polygon'), `${where}.polygon`);
	} else if (child(node, 'polyline') != null) {
		object.shape = 'polyline';
		object.points = parsePointList(child(node, 'polyline').attrs.points, ctx, child(node, 'polyline'), `${where}.polyline`);
	} else if (child(node, 'text') != null) {
		object.shape = 'text';
		object.text = parseTextObject(child(node, 'text'), ctx, `${where}.text`);
	}
	return object;
}

function parseObjectGroup(node, ctx, baseFile, where) {
	const common = parseLayerCommon(node, ctx, where);
	const group = {
		...common,
		kind: 'object',
		draworder: node.attrs.draworder ?? 'topdown',
		color: node.attrs.color ?? null,
		objects: [],
	};
	for (const objectNode of node.children.filter((item) => item.name === 'object')) {
		group.objects.push(parseObject(objectNode, ctx, baseFile, `${where}.objects[${group.objects.length}]`));
	}
	return group;
}

function parseLayerCommon(node, ctx, where) {
	return {
		id: asInt(node.attrs.id, 0),
		name: node.attrs.name ?? '',
		type: node.attrs.type ?? '',
		class: node.attrs.class ?? '',
		visible: asBool(node.attrs.visible, true),
		opacity: asNumber(node.attrs.opacity, 1),
		offsetx: asNumber(node.attrs.offsetx, 0),
		offsety: asNumber(node.attrs.offsety, 0),
		parallaxx: asNumber(node.attrs.parallaxx, 1),
		parallaxy: asNumber(node.attrs.parallaxy, 1),
		tintcolor: node.attrs.tintcolor ?? null,
		x: asNumber(node.attrs.x, 0),
		y: asNumber(node.attrs.y, 0),
		width: asInt(node.attrs.width, 0),
		height: asInt(node.attrs.height, 0),
		properties: parseProperties(child(node, 'properties'), ctx, `${where}.properties`),
		attrs: attrsOf(node),
	};
}

function decodeBase64(text, compression, ctx, node, where) {
	const encoded = String(text ?? '').replace(/\s+/g, '');
	let buffer;
	try {
		buffer = Buffer.from(encoded, 'base64');
	} catch (error) {
		addDiagnostic(ctx, 'error', 'base64-invalid', `base64 inválido: ${error.message}`, node, where);
		return [];
	}
	if (compression === 'gzip') {
		try {
			buffer = zlib.gunzipSync(buffer);
		} catch (error) {
			addDiagnostic(ctx, 'error', 'gzip-invalid', `gzip inválido: ${error.message}`, node, where);
			return [];
		}
	} else if (compression === 'zlib') {
		try {
			buffer = zlib.inflateSync(buffer);
		} catch (error) {
			addDiagnostic(ctx, 'error', 'zlib-invalid', `zlib inválido: ${error.message}`, node, where);
			return [];
		}
	} else if (compression === 'zstd') {
		addDiagnostic(ctx, 'error', 'zstd-unsupported', 'compresión zstd no soportada; reconvierte el mapa a csv/gzip/zlib', node, where);
		return [];
	} else if (compression !== '') {
		addDiagnostic(ctx, 'error', 'compression-unsupported', `compresión desconocida "${compression}"`, node, where);
		return [];
	}
	if ((buffer.length & 3) !== 0) {
		addDiagnostic(ctx, 'error', 'base64-length', `base64 decodifica ${buffer.length} bytes, no múltiplo de 4`, node, where);
		return [];
	}
	const cells = new Array(buffer.length / 4);
	for (let k = 0; k < cells.length; ++k) cells[k] = buffer.readUInt32LE(k * 4);
	return cells;
}

function decodeCsv(text, ctx, node, where) {
	const body = String(text ?? '').trim();
	if (body === '') return [];
	const cells = [];
	for (const token of body.split(',')) {
		const value = token.trim();
		if (value === '') {
			cells.push(0);
			continue;
		}
		const n = Number.parseInt(value, 10);
		if (!Number.isFinite(n)) {
			addDiagnostic(ctx, 'error', 'csv-cell-invalid', `celda CSV inválida "${value}"`, node, where);
			cells.push(0);
		} else {
			cells.push(n);
		}
	}
	return cells;
}

function decodeXmlCells(node, ctx, where) {
	if (node == null) return [];
	const cells = [];
	for (const tileNode of node.children.filter((item) => item.name === 'tile')) {
		if (!hasAttr(tileNode, 'gid')) {
			cells.push(0);
			continue;
		}
		const gid = Number(tileNode.attrs.gid);
		if (!Number.isFinite(gid)) {
			addDiagnostic(ctx, 'error', 'gid-invalid', `gid inválido "${tileNode.attrs.gid}"`, tileNode, `${where}[${cells.length}]`);
			cells.push(0);
		} else {
			cells.push(gid);
		}
	}
	return cells;
}

function decodePayload(node, encoding, compression, ctx, where) {
	if (encoding === 'base64') return decodeBase64(node.text, compression, ctx, node, where);
	if (encoding === 'csv') return decodeCsv(node.text, ctx, node, where);
	if (encoding === '' || encoding === 'xml') return decodeXmlCells(node, ctx, where);
	addDiagnostic(ctx, 'error', 'encoding-unsupported', `encoding desconocido "${encoding}"`, node, where);
	return [];
}

function parseData(dataNode, layer, ctx, where) {
	if (dataNode == null) {
		addDiagnostic(ctx, 'warning', 'data-missing', 'capa de tiles sin <data>', null, where);
		return { encoding: '', compression: '', cells: [] };
	}
	const encoding = dataNode.attrs.encoding ?? '';
	const compression = dataNode.attrs.compression ?? '';
	const chunkNodes = dataNode.children.filter((item) => item.name === 'chunk');
	const data = { encoding, compression, cells: [] };
	if (chunkNodes.length > 0) {
		data.chunks = [];
		for (let k = 0; k < chunkNodes.length; ++k) {
			const chunkNode = chunkNodes[k];
			const cx = asInt(chunkNode.attrs.x, 0);
			const cy = asInt(chunkNode.attrs.y, 0);
			const width = asInt(chunkNode.attrs.width, 0);
			const height = asInt(chunkNode.attrs.height, 0);
			const chunkWhere = `${where}.chunks[${k}]`;
			const cells = decodePayload(chunkNode, encoding, compression, ctx, chunkWhere);
			if (width <= 0 || height <= 0) {
				addDiagnostic(ctx, 'error', 'chunk-size', `chunk ${width}x${height} inválido`, chunkNode, chunkWhere);
			} else if (cells.length !== width * height) {
				addDiagnostic(ctx, 'error', 'chunk-cells', `chunk ${cx},${cy} tiene ${cells.length} celdas; se esperaban ${width * height}`, chunkNode, chunkWhere);
			}
			data.chunks.push({ x: cx, y: cy, width, height, cells });
		}
		return data;
	}
	data.cells = decodePayload(dataNode, encoding, compression, ctx, where);
	const expected = layer.width * layer.height;
	if (expected > 0 && data.cells.length !== expected) {
		addDiagnostic(ctx, 'error', 'layer-cells', `capa ${layer.name || layer.id} tiene ${data.cells.length} celdas; se esperaban ${expected}`, dataNode, where);
	}
	return data;
}

function parseTileLayer(node, ctx, baseFile, where) {
	const common = parseLayerCommon(node, ctx, where);
	const layer = { ...common, kind: 'tile', data: parseData(child(node, 'data'), common, ctx, `${where}.data`) };
	return layer;
}

function parseImageLayer(node, ctx, baseFile, where) {
	const common = parseLayerCommon(node, ctx, where);
	const layer = {
		...common,
		kind: 'image',
		image: parseImage(child(node, 'image'), ctx, baseFile, `${where}.image`),
		repeatx: asBool(node.attrs.repeatx, false),
		repeaty: asBool(node.attrs.repeaty, false),
		transparentcolor: node.attrs.transparentcolor ?? null,
	};
	if (layer.image == null) addDiagnostic(ctx, 'warning', 'image-missing', 'capa de imagen sin <image>', node, where);
	return layer;
}

function parseGroup(node, ctx, baseFile, where) {
	const common = parseLayerCommon(node, ctx, where);
	const group = { ...common, kind: 'group', layers: [] };
	for (const layerNode of node.children.filter((item) => LAYER_NAMES.has(item.name))) {
		const parsed = parseLayerNode(layerNode, ctx, baseFile, `${where}.layers[${group.layers.length}]`);
		if (parsed != null) group.layers.push(parsed);
	}
	return group;
}

function parseLayerNode(node, ctx, baseFile, where) {
	switch (node.name) {
		case 'layer':
			return parseTileLayer(node, ctx, baseFile, where);
		case 'objectgroup':
			return parseObjectGroup(node, ctx, baseFile, where);
		case 'imagelayer':
			return parseImageLayer(node, ctx, baseFile, where);
		case 'group':
			return parseGroup(node, ctx, baseFile, where);
		default:
			addDiagnostic(ctx, 'warning', 'layer-unsupported', `elemento de capa no soportado <${node.name}>`, node, where);
			return null;
	}
}

function parseAnimation(node, ctx, where) {
	const frames = [];
	for (const frameNode of node.children.filter((item) => item.name === 'frame')) {
		const tileid = asInt(frameNode.attrs.tileid, -1);
		const duration = asInt(frameNode.attrs.duration, 0);
		if (tileid < 0) {
			addDiagnostic(ctx, 'error', 'animation-frame', 'frame de animación sin tileid válido', frameNode, `${where}.frames[${frames.length}]`);
			continue;
		}
		if (duration < 0) {
			addDiagnostic(ctx, 'error', 'animation-duration', 'duración de frame negativa', frameNode, `${where}.frames[${frames.length}]`);
		}
		frames.push({ tileid, duration });
	}
	if (frames.length === 0) addDiagnostic(ctx, 'warning', 'animation-empty', 'animación sin frames', node, where);
	return frames;
}

function parseTile(node, ctx, baseFile, where) {
	const tile = {
		id: asInt(node.attrs.id, 0),
		type: node.attrs.type ?? '',
		class: node.attrs.class ?? '',
		probability: node.attrs.probability != null ? asNumber(node.attrs.probability, 1) : 1,
		image: parseImage(child(node, 'image'), ctx, baseFile, `${where}.image`),
		animation: null,
		objectgroup: null,
		properties: parseProperties(child(node, 'properties'), ctx, `${where}.properties`),
		attrs: attrsOf(node),
	};
	const anim = child(node, 'animation');
	if (anim != null) tile.animation = { frames: parseAnimation(anim, ctx, `${where}.animation`) };
	const objects = child(node, 'objectgroup');
	if (objects != null) tile.objectgroup = parseObjectGroup(objects, ctx, baseFile, `${where}.objectgroup`);
	for (const item of node.children) {
		if (!TILE_CHILD_NAMES.has(item.name)) {
			addDiagnostic(ctx, 'warning', 'tile-child-unsupported', `hijo de tile no interpretado <${item.name}>`, item, `${where}.${item.name}`);
		}
	}
	return tile;
}

function parseTilesetBody(node, ctx, baseFile, firstgid, where, source, sourcePath) {
	const attrs = node.attrs;
	const tileset = {
		firstgid,
		lastgid: null,
		source,
		sourcePath,
		name: attrs.name ?? '',
		class: attrs.class ?? '',
		tilewidth: asInt(attrs.tilewidth, 0),
		tileheight: asInt(attrs.tileheight, 0),
		tilecount: hasAttr(node, 'tilecount') ? asInt(attrs.tilecount, 0) : null,
		maxTileId: null,
		tileIds: null,
		columns: hasAttr(node, 'columns') ? asInt(attrs.columns, 0) : null,
		margin: asInt(attrs.margin, 0),
		spacing: asInt(attrs.spacing, 0),
		objectalignment: attrs.objectalignment ?? 'unspecified',
		tileoffset: null,
		grid: null,
		image: parseImage(child(node, 'image'), ctx, baseFile, `${where}.image`),
		tiles: [],
		properties: parseProperties(child(node, 'properties'), ctx, `${where}.properties`),
		attrs: attrsOf(node),
	};
	const offset = child(node, 'tileoffset');
	if (offset != null) tileset.tileoffset = { x: asNumber(offset.attrs.x, 0), y: asNumber(offset.attrs.y, 0) };
	const grid = child(node, 'grid');
	if (grid != null) {
		tileset.grid = {
			orientation: grid.attrs.orientation ?? 'orthogonal',
			width: asNumber(grid.attrs.width, 1),
			height: asNumber(grid.attrs.height, 1),
		};
	}
	for (const tileNode of node.children.filter((item) => item.name === 'tile')) {
		tileset.tiles.push(parseTile(tileNode, ctx, baseFile, `${where}.tiles[${tileset.tiles.length}]`));
	}
	for (const item of node.children) {
		if (!TILESET_CHILD_NAMES.has(item.name)) {
			addDiagnostic(ctx, 'warning', 'tileset-child-unsupported', `hijo de tileset no interpretado <${item.name}>`, item, `${where}.${item.name}`);
		}
	}
	if (tileset.tilewidth <= 0 || tileset.tileheight <= 0) {
		addDiagnostic(ctx, 'error', 'tileset-tile-size', 'tileset sin tilewidth/tileheight válidos', node, where);
	}
	if (tileset.tilecount == null) {
		const image = tileset.image;
		if (image != null && tileset.columns != null && tileset.columns > 0 && tileset.tileheight > 0 && image.width > 0 && image.height > 0) {
			const rowHeight = tileset.tileheight + tileset.spacing;
			const rows = Math.floor((image.height - 2 * tileset.margin + tileset.spacing) / rowHeight);
			if (rows > 0) tileset.tilecount = tileset.columns * rows;
		} else if (tileset.tiles.length > 0) {
			tileset.tilecount = tileset.tiles.length;
		}
		if (tileset.tilecount == null) {
			addDiagnostic(ctx, 'warning', 'tileset-tilecount', 'tileset sin tilecount ni geometría suficiente para deducirlo', node, where);
		}
	}
	if (tileset.tiles.length > 0) {
		tileset.maxTileId = Math.max(...tileset.tiles.map((tile) => tile.id));
		if (tileset.columns === 0) tileset.tileIds = [...new Set(tileset.tiles.map((tile) => tile.id))].sort((a, b) => a - b);
	}
	if (tileset.columns === 0 && tileset.maxTileId != null) {
		// En una colección de imágenes los ids locales pueden tener huecos y `tilecount`
		// cuenta los tiles definidos, no el máximo id. El rango de GID llega hasta el
		// máximo id local; la pertenencia exacta se valida con `tileIds`.
		tileset.lastgid = tileset.firstgid + tileset.maxTileId;
	} else if (tileset.tilecount != null && tileset.tilecount > 0) {
		tileset.lastgid = tileset.firstgid + tileset.tilecount - 1;
	}
	return tileset;
}

function parseTilesetReference(node, ctx, baseFile, where) {
	const firstgid = asInt(node.attrs.firstgid, 0);
	const source = node.attrs.source ?? null;
	if (firstgid <= 0) addDiagnostic(ctx, 'error', 'firstgid-invalid', `firstgid inválido ${firstgid}`, node, where);
	if (source == null) return parseTilesetBody(node, ctx, baseFile, firstgid, where, null, path.resolve(baseFile));
	const sourcePath = path.resolve(path.dirname(baseFile), source);
	if (!fs.existsSync(sourcePath)) {
		addDiagnostic(ctx, 'error', 'tsx-not-found', `TSX no encontrado: ${sourcePath}`, node, where);
		return {
			firstgid,
			lastgid: null,
			source,
			sourcePath,
			name: '',
			tilewidth: 0,
			tileheight: 0,
			tilecount: null,
			columns: null,
			tiles: [],
			properties: [],
			attrs: attrsOf(node),
		};
	}
	const previousSource = ctx.source;
	try {
		ctx.source = sourcePath;
		const root = parseXml(fs.readFileSync(sourcePath, 'utf8'), { source: sourcePath });
		if (root.name !== 'tileset') {
			addDiagnostic(ctx, 'error', 'tsx-root', `el TSX no tiene raíz <tileset> (${root.name})`, root, where);
			return {
				firstgid,
				lastgid: null,
				source,
				sourcePath,
				name: '',
				tilewidth: 0,
				tileheight: 0,
				tilecount: null,
				columns: null,
				tiles: [],
				properties: [],
				attrs: attrsOf(node),
			};
		}
		return parseTilesetBody(root, ctx, sourcePath, firstgid, where, source, sourcePath);
	} catch (error) {
		if (error instanceof XmlSyntaxError) {
			addDiagnostic(ctx, 'error', 'tsx-xml', `XML inválido en TSX: ${error.message}`, node, where);
		} else {
			addDiagnostic(ctx, 'error', 'tsx-read', `no se pudo leer TSX: ${error.message}`, node, where);
		}
		return {
			firstgid,
			lastgid: null,
			source,
			sourcePath,
			name: '',
			tilewidth: 0,
			tileheight: 0,
			tilecount: null,
			columns: null,
			tiles: [],
			properties: [],
			attrs: attrsOf(node),
		};
	} finally {
		ctx.source = previousSource;
	}
}

function parseTilesets(mapNode, ctx, baseFile) {
	const tilesets = [];
	for (const node of mapNode.children.filter((item) => item.name === 'tileset')) {
		tilesets.push(parseTilesetReference(node, ctx, baseFile, `map.tilesets[${tilesets.length}]`));
	}
	const sorted = [...tilesets].sort((a, b) => a.firstgid - b.firstgid);
	for (let k = 1; k < sorted.length; ++k) {
		const previous = sorted[k - 1];
		const current = sorted[k];
		if (previous.lastgid != null && current.firstgid <= previous.lastgid) {
			addDiagnostic(ctx, 'error', 'tileset-overlap', `rango de tilesets solapado: ${previous.name || previous.firstgid} termina en ${previous.lastgid} y ${current.name || current.firstgid} empieza en ${current.firstgid}`, null, `map.tilesets[${k}]`);
		}
	}
	return tilesets;
}

function walkLayers(layers, visit) {
	for (const layer of layers) {
		visit(layer);
		if (layer.kind === 'group') walkLayers(layer.layers, visit);
	}
}

function collectGids(ir) {
	const gids = new Map();
	const add = (gid, where) => {
		if (gid == null) return;
		const value = Number(gid);
		if (!Number.isFinite(value) || value === 0) return;
		const previous = gids.get(value);
		if (previous == null) gids.set(value, { gid: value, count: 1, where });
		else previous.count += 1;
	};
	walkLayers(ir.layers, (layer) => {
		if (layer.kind === 'tile') {
			if (layer.data?.chunks != null) {
				for (const chunk of layer.data.chunks) {
					for (let k = 0; k < chunk.cells.length; ++k) add(chunk.cells[k], `${layer.name || layer.id}.chunks[${chunk.x},${chunk.y}][${k}]`);
				}
			} else if (layer.data?.cells != null) {
				for (let k = 0; k < layer.data.cells.length; ++k) add(layer.data.cells[k], `${layer.name || layer.id}.cells[${k}]`);
			}
		} else if (layer.kind === 'object') {
			for (let k = 0; k < layer.objects.length; ++k) add(layer.objects[k].gid, `${layer.name || layer.id}.objects[${k}].gid`);
		}
	});
	return gids;
}

function validateGids(ir, ctx) {
	const ranges = ir.tilesets
		.filter((tileset) => tileset.lastgid != null)
		.sort((a, b) => a.firstgid - b.firstgid);
	const gids = collectGids(ir);
	for (const entry of gids.values()) {
		if (!Number.isFinite(entry.gid) || entry.gid < 0) {
			addDiagnostic(ctx, 'error', 'gid-invalid', `gid ${entry.gid} inválido`, null, entry.where);
			continue;
		}
		if (entry.gid > 0xffffffff) {
			addDiagnostic(ctx, 'error', 'gid-too-large', `gid ${entry.gid} excede 32 bits`, null, entry.where);
			continue;
		}
		const { id } = decodeGid(entry.gid);
		if (id === 0) continue;
		const range = ranges.find((tileset) => id >= tileset.firstgid && id <= tileset.lastgid);
		if (range == null) {
			addDiagnostic(ctx, 'error', 'gid-out-of-range', `gid ${entry.gid} (tile ${id}, ${entry.count} usos) no cae en ningún tileset`, null, entry.where);
		} else if (range.columns === 0 && range.tileIds != null && !range.tileIds.includes(id - range.firstgid)) {
			addDiagnostic(ctx, 'error', 'gid-tile-undefined', `gid ${entry.gid} (tile ${id}, ${entry.count} usos) apunta a un id no definido en la colección ${range.name || range.firstgid}`, null, entry.where);
		}
	}
	return gids;
}

function countLayers(layers) {
	let tile = 0;
	let object = 0;
	let image = 0;
	let group = 0;
	let objects = 0;
	const visit = (list) => {
		for (const layer of list) {
			if (layer.kind === 'tile') ++tile;
			else if (layer.kind === 'object') {
				++object;
				objects += layer.objects.length;
			} else if (layer.kind === 'image') ++image;
			else if (layer.kind === 'group') {
				++group;
				visit(layer.layers);
			}
		}
	};
	visit(layers);
	return { total: tile + object + image + group, tile, object, image, group, objects };
}

/// Construye el IR a partir del texto de un TMX. Lanza `XmlSyntaxError` si el XML no es
/// válido; los errores semánticos se acumulan en `diagnostics`.
export function parseTmxText(text, { source = '<tmx>', ...options } = {}) {
	const root = parseXml(text, { source });
	if (root.name !== 'map') {
		throw new XmlSyntaxError(`la raíz del TMX debe ser <map>, no <${root.name}>`, root.line, root.column, source);
	}
	const baseFile = path.resolve(options.file ?? source);
	const ctx = { source, diagnostics: [] };
	const attrs = root.attrs;
	const map = {
		version: attrs.version ?? '',
		tiledversion: attrs.tiledversion ?? '',
		orientation: attrs.orientation ?? 'orthogonal',
		renderorder: attrs.renderorder ?? 'right-down',
		width: asInt(attrs.width, 0),
		height: asInt(attrs.height, 0),
		tilewidth: asInt(attrs.tilewidth, 0),
		tileheight: asInt(attrs.tileheight, 0),
		infinite: asBool(attrs.infinite, false),
		staggeraxis: attrs.staggeraxis ?? null,
		staggerindex: attrs.staggerindex ?? null,
		hexsidelength: attrs.hexsidelength != null ? asInt(attrs.hexsidelength, 0) : null,
		backgroundcolor: attrs.backgroundcolor ?? null,
		nextlayerid: attrs.nextlayerid != null ? asInt(attrs.nextlayerid, 0) : null,
		nextobjectid: attrs.nextobjectid != null ? asInt(attrs.nextobjectid, 0) : null,
		class: attrs.class ?? '',
		objectalignment: attrs.objectalignment ?? 'unspecified',
		parallaxoriginx: asNumber(attrs.parallaxoriginx, 0),
		parallaxoriginy: asNumber(attrs.parallaxoriginy, 0),
		properties: parseProperties(child(root, 'properties'), ctx, 'map.properties'),
		attrs: attrsOf(root),
	};
	if (map.orientation !== 'orthogonal' && map.orientation !== 'isometric') {
		addDiagnostic(ctx, 'error', 'orientation-unsupported', `orientación "${map.orientation}" no soportada por el pipeline Tiled`, root, 'map.orientation');
	}
	const tilesets = parseTilesets(root, ctx, baseFile);
	const layers = [];
	for (const node of root.children.filter((item) => LAYER_NAMES.has(item.name))) {
		const parsed = parseLayerNode(node, ctx, baseFile, `map.layers[${layers.length}]`);
		if (parsed != null) layers.push(parsed);
	}
	const ir = {
		format: IR_FORMAT,
		version: IR_VERSION,
		source: baseFile,
		map,
		tilesets,
		layers,
		diagnostics: ctx.diagnostics,
		stats: null,
	};
	const gids = validateGids(ir, ctx);
	const layerStats = countLayers(layers);
	const errors = ctx.diagnostics.filter((item) => item.severity === 'error').length;
	const warnings = ctx.diagnostics.filter((item) => item.severity === 'warning').length;
	ir.stats = {
		tilesets: tilesets.length,
		layers: layerStats.total,
		tileLayers: layerStats.tile,
		objectLayers: layerStats.object,
		imageLayers: layerStats.image,
		groupLayers: layerStats.group,
		objects: layerStats.objects,
		uniqueGids: gids.size,
		errors,
		warnings,
	};
	return ir;
}

export function parseTmxFile(file, options = {}) {
	const source = path.resolve(file);
	const text = fs.readFileSync(source, 'utf8');
	return parseTmxText(text, { ...options, source, file: source });
}

function usage() {
	console.log('Uso: node tools/tiled/parse-tmx.mjs <mapa.tmx> [--out ir.json] [--json] [--strict] [--quiet]');
	console.log('  --out <f>   escribe el IR JSON');
	console.log('  --json      imprime el IR JSON por stdout (no resumen)');
	console.log('  --strict    falla también con warnings');
	console.log('  --quiet     no imprime el resumen');
}

function argValue(args, name) {
	const at = args.indexOf(name);
	return at >= 0 ? args[at + 1] : null;
}

function printSummary(ir, outPath) {
	const map = ir.map;
	const stats = ir.stats;
	console.log(`[tiled] ${path.basename(ir.source)}: ${map.width}x${map.height} tile ${map.tilewidth}x${map.tileheight} ${map.orientation} ${map.renderorder} ${map.infinite ? 'infinite' : 'finite'}`);
	console.log(`[tiled] tilesets=${stats.tilesets} layers=${stats.layers} (tile=${stats.tileLayers}, object=${stats.objectLayers}, image=${stats.imageLayers}, group=${stats.groupLayers}) objects=${stats.objects} uniqueGids=${stats.uniqueGids}`);
	console.log(`[tiled] diagnostics: errors=${stats.errors} warnings=${stats.warnings}`);
	for (const diagnostic of ir.diagnostics) {
		const location = diagnostic.line != null ? ` (${diagnostic.source}:${diagnostic.line}:${diagnostic.column})` : ` (${diagnostic.source})`;
		const where = diagnostic.path != null ? ` ${diagnostic.path}` : '';
		console.log(`[tiled] ${diagnostic.severity} ${diagnostic.code}${where}: ${diagnostic.message}${location}`);
	}
	if (outPath != null) console.log(`[tiled] -> ${outPath}`);
}

function main() {
	const args = process.argv.slice(2);
	if (args.length === 0 || args.includes('--help') || args.includes('-h')) {
		usage();
		process.exit(args.length === 0 ? 2 : 0);
	}
	let file = null;
	let outPath = null;
	for (let k = 0; k < args.length; ++k) {
		const arg = args[k];
		if (arg === '--out') {
			outPath = args[++k] ?? null;
			continue;
		}
		if (arg.startsWith('--')) continue;
		if (file == null) file = arg;
	}
	if (file == null) {
		usage();
		process.exit(2);
	}
	let ir;
	try {
		ir = parseTmxFile(file);
	} catch (error) {
		if (error instanceof XmlSyntaxError || error instanceof Error) {
			console.error(`[tiled] ${error.message}`);
			process.exit(2);
		}
		throw error;
	}
	if (outPath != null) {
		const resolved = path.resolve(outPath);
		fs.mkdirSync(path.dirname(resolved), { recursive: true });
		fs.writeFileSync(resolved, `${JSON.stringify(ir, null, 2)}\n`, 'utf8');
	}
	if (args.includes('--json')) console.log(JSON.stringify(ir, null, 2));
	else if (!args.includes('--quiet')) printSummary(ir, outPath);
	const errors = ir.stats.errors;
	const warnings = ir.stats.warnings;
	process.exit(errors > 0 || (args.includes('--strict') && warnings > 0) ? 1 : 0);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
