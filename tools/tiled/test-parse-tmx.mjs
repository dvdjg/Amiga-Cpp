#!/usr/bin/env node
// Autotest de `tools/tiled/xml.mjs` + `tools/tiled/parse-tmx.mjs`.
//
// Cubre: XML estructural, CSV/base64 (zlib/gzip), chunks infinitos con coordenadas
// negativas, GID con flips, TSX externos, atlas/colección, animaciones, objectgroups,
// propiedades tipadas/clase, grupos con opacidad/paralaje, rechazo de zstd y
// diagnósticos de conteo/GID/orientación. Si existe el proyecto local de
// `Beginning Fields`, lo parsea como integración (si no, lo omite).
//
// Uso: node tools/tiled/test-parse-tmx.mjs

import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { parseXml, XmlSyntaxError } from './xml.mjs';
import { decodeGid, parseTmxFile, parseTmxText, GID_FLIP_H } from './parse-tmx.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const FIXTURES = path.join(ROOT, 'tools/tiled/fixtures');
const BEGINNING_FIELDS = 'C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx';

let failures = 0;
let skipped = 0;
function test(name, fn) {
	try {
		fn();
		console.log(`OK   ${name}`);
	} catch (error) {
		++failures;
		console.error(`FAIL ${name}`);
		console.error(error?.stack ?? error);
	}
}

function fixture(name) {
	return path.join(FIXTURES, name);
}

function tempDir() {
	return fs.mkdtempSync(path.join(os.tmpdir(), 'eng-tiled-'));
}

function withTemp(fn) {
	const dir = tempDir();
	try {
		fn(dir);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}

function writeMap(dir, body) {
	const file = path.join(dir, 'map.tmx');
	fs.writeFileSync(file, body, 'utf8');
	return file;
}

function copyAtlas(dir) {
	fs.copyFileSync(fixture('ortho.tsx'), path.join(dir, 'ortho.tsx'));
}

function encodeBase64Cells(cells, compression) {
	const buffer = Buffer.alloc(cells.length * 4);
	for (let k = 0; k < cells.length; ++k) buffer.writeUInt32LE(cells[k] >>> 0, k * 4);
	let payload = buffer;
	if (compression === 'zlib') payload = zlib.deflateSync(buffer);
	else if (compression === 'gzip') payload = zlib.gzipSync(buffer);
	return payload.toString('base64');
}

function mapWithData(encoding, compression, data, width = 2, height = 2) {
	return `<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" renderorder="right-down" width="${width}" height="${height}" tilewidth="16" tileheight="16" infinite="0" nextlayerid="2" nextobjectid="1">
 <tileset firstgid="1" source="ortho.tsx"/>
 <layer id="1" name="Ground" width="${width}" height="${height}">
  <data encoding="${encoding}"${compression ? ` compression="${compression}"` : ''}>${data}</data>
 </layer>
</map>`;
}

function diagnosticsWith(ir, code) {
	return ir.diagnostics.filter((item) => item.code === code);
}

// --- XML --------------------------------------------------------------------

test('xml: atributos, entidades, CDATA, comentarios y anidamiento', () => {
	const root = parseXml('<?xml version="1.0"?><!-- c --><root a="x &amp; y"><child b=\'1\'/><text>Hola &lt;mundo&gt;</text><![CDATA[<crudo>]]></root>', { source: 'inline.xml' });
	assert.equal(root.name, 'root');
	assert.equal(root.attrs.a, 'x & y');
	assert.equal(root.children.length, 2);
	assert.equal(root.children[0].name, 'child');
	assert.equal(root.children[0].attrs.b, '1');
	assert.equal(root.children[1].name, 'text');
	assert.equal(root.children[1].text, 'Hola <mundo>');
	assert.equal(root.text, '<crudo>');
});

test('xml: cierre incorrecto lanza XmlSyntaxError', () => {
	assert.throws(() => parseXml('<a><b></a></b>', { source: 'bad.xml' }), XmlSyntaxError);
});

test('xml: atributo duplicado lanza XmlSyntaxError', () => {
	assert.throws(() => parseXml('<a x="1" x="2"/>', { source: 'dup.xml' }), XmlSyntaxError);
});

// --- Fixtures principales ---------------------------------------------------

test('ortho: mapa, capas agrupadas, TSX externos, propiedades y objetos', () => {
	const ir = parseTmxFile(fixture('ortho.tmx'));
	assert.equal(ir.format, 'eng.tiled-ir');
	assert.equal(ir.version, 1);
	assert.equal(ir.map.width, 4);
	assert.equal(ir.map.height, 3);
	assert.equal(ir.map.orientation, 'orthogonal');
	assert.equal(ir.map.renderorder, 'right-down');
	assert.equal(ir.stats.tilesets, 2);
	assert.equal(ir.stats.tileLayers, 3);
	assert.equal(ir.stats.objectLayers, 1);
	assert.equal(ir.stats.imageLayers, 1);
	assert.equal(ir.stats.groupLayers, 1);
	assert.equal(ir.stats.objects, 5);
	assert.equal(ir.stats.errors, 0);
	assert.equal(ir.stats.warnings, 0);

	const atlas = ir.tilesets.find((tileset) => tileset.name === 'OrthoAtlas');
	assert.ok(atlas);
	assert.equal(atlas.firstgid, 1);
	assert.equal(atlas.lastgid, 4);
	assert.equal(atlas.columns, 2);
	assert.deepEqual(atlas.tileoffset, { x: 1, y: -2 });
	assert.equal(atlas.properties[0].name, 'material');
	assert.equal(atlas.tiles.length, 2);
	const animated = atlas.tiles.find((tile) => tile.id === 1);
	assert.equal(animated.class, 'Animated');
	assert.deepEqual(animated.animation.frames, [
		{ tileid: 1, duration: 100 },
		{ tileid: 3, duration: 150 },
	]);
	assert.equal(animated.objectgroup.objects[0].shape, 'polygon');
	assert.deepEqual(animated.objectgroup.objects[0].points, [0, 0, 16, 0, 16, 16]);
	assert.equal(animated.properties[0].name, 'solid');
	assert.equal(animated.properties[0].value, true);

	const collection = ir.tilesets.find((tileset) => tileset.name === 'Collection');
	assert.ok(collection);
	assert.equal(collection.columns, 0);
	assert.equal(collection.firstgid, 5);
	assert.equal(collection.lastgid, 7);
	assert.equal(collection.maxTileId, 2);
	assert.deepEqual(collection.tileIds, [0, 2]);
	assert.equal(collection.tiles[1].animation.frames[0].tileid, 0);

	const ground = ir.layers.find((layer) => layer.name === 'Ground');
	assert.deepEqual(ground.data.cells, [1, 2, 3, 4, 0, 2147483649, 5, 0, 4, 4, 4, 4]);
	const flipped = decodeGid(ground.data.cells[5]);
	assert.equal(flipped.id, 1);
	assert.equal(flipped.flipH, true);
	assert.equal(flipped.flipV, false);
	assert.equal(decodeGid(GID_FLIP_H | 2).id, 2);

	const group = ir.layers.find((layer) => layer.kind === 'group');
	assert.ok(group);
	assert.equal(group.name, 'Decor');
	assert.equal(group.opacity, 0.5);
	assert.equal(group.parallaxx, 0.5);
	assert.equal(group.parallaxy, 0.75);
	assert.equal(group.layers.length, 1);
	assert.equal(group.layers[0].name, 'Overlay');

	const hidden = ir.layers.find((layer) => layer.name === 'Hidden');
	assert.equal(hidden.visible, false);

	const objects = ir.layers.find((layer) => layer.kind === 'object');
	assert.equal(objects.objects.length, 5);
	const spawn = objects.objects.find((object) => object.name === 'hero');
	assert.equal(spawn.type, 'spawn');
	assert.equal(spawn.shape, 'rectangle');
	const trigger = objects.objects.find((object) => object.name === 'zone');
	assert.equal(trigger.class, 'trigger');
	assert.equal(trigger.shape, 'polygon');
	assert.deepEqual(trigger.points, [0, 0, 20, 0, 20, 10]);
	const tileObject = objects.objects.find((object) => object.name === 'icon');
	assert.equal(tileObject.gid, 5);
	const marker = objects.objects.find((object) => object.name === 'marker');
	assert.equal(marker.shape, 'point');
	const label = objects.objects.find((object) => object.name === 'label');
	assert.equal(label.shape, 'text');
	assert.equal(label.text.text, 'Hello & Tiled');
	assert.equal(label.text.wrap, true);

	const imageLayer = ir.layers.find((layer) => layer.kind === 'image');
	assert.equal(imageLayer.name, 'Sky');
	assert.equal(imageLayer.repeatx, true);
	assert.equal(imageLayer.image.width, 32);
	assert.ok(imageLayer.image.path.endsWith('test_icons.png'));

	const config = ir.map.properties.find((property) => property.name === 'config');
	assert.equal(config.type, 'class');
	assert.equal(config.propertytype, 'MapConfig');
	assert.equal(config.properties[0].value, 3);
});

test('infinite: chunks con coordenadas negativas y GID con flip', () => {
	const ir = parseTmxFile(fixture('infinite.tmx'));
	assert.equal(ir.map.infinite, true);
	assert.equal(ir.stats.errors, 0);
	const layer = ir.layers[0];
	assert.equal(layer.data.chunks.length, 2);
	assert.deepEqual(layer.data.chunks[0], {
		x: -1,
		y: -1,
		width: 2,
		height: 2,
		cells: [1, 2, 3, 4],
	});
	assert.equal(layer.data.chunks[1].x, 1);
	assert.equal(layer.data.chunks[1].cells[1], 2147483650);
	assert.equal(decodeGid(layer.data.chunks[1].cells[1]).flipH, true);
});

test('iso: orientación y geometría de rejilla preservadas', () => {
	const ir = parseTmxFile(fixture('iso.tmx'));
	assert.equal(ir.map.orientation, 'isometric');
	assert.equal(ir.map.tilewidth, 32);
	assert.equal(ir.map.tileheight, 16);
	assert.equal(ir.layers[0].data.cells.length, 4);
	assert.equal(ir.tilesets[0].tileoffset.x, 1);
});

// --- Codificaciones ---------------------------------------------------------

for (const compression of ['', 'zlib', 'gzip']) {
	test(`data: base64${compression ? `+${compression}` : ''}`, () => {
		withTemp((dir) => {
			copyAtlas(dir);
			const payload = encodeBase64Cells([1, 2, 3, 4], compression);
			const file = writeMap(dir, mapWithData('base64', compression, payload));
			const ir = parseTmxFile(file);
			assert.equal(ir.stats.errors, 0);
			assert.deepEqual(ir.layers[0].data.cells, [1, 2, 3, 4]);
		});
	});
}

test('data: zstd se rechaza con diagnóstico', () => {
	withTemp((dir) => {
		copyAtlas(dir);
		const file = writeMap(dir, mapWithData('base64', 'zstd', 'AAAA'));
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'zstd-unsupported').length, 1);
		assert.ok(ir.stats.errors >= 1);
	});
});

test('data: conteo de celdas incorrecto se diagnostica', () => {
	withTemp((dir) => {
		copyAtlas(dir);
		const file = writeMap(dir, mapWithData('csv', '', '1,2,3'));
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'layer-cells').length, 1);
	});
});

// --- Validación de GID ------------------------------------------------------

test('gid: fuera de rango se diagnostica', () => {
	withTemp((dir) => {
		copyAtlas(dir);
		const file = writeMap(dir, mapWithData('csv', '', '1,2,3,99'));
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'gid-out-of-range').length, 1);
	});
});

test('gid: id no definido en colección se diagnostica', () => {
	withTemp((dir) => {
		copyAtlas(dir);
		fs.copyFileSync(fixture('collection.tsx'), path.join(dir, 'collection.tsx'));
		const body = `<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" width="2" height="2" tilewidth="16" tileheight="16" infinite="0">
 <tileset firstgid="1" source="collection.tsx"/>
 <layer id="1" name="Ground" width="2" height="2"><data encoding="csv">1,2,3,4</data></layer>
</map>`;
		const file = writeMap(dir, body);
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'gid-tile-undefined').length, 1);
	});
});

test('orientación hex/staggered se rechaza', () => {
	withTemp((dir) => {
		copyAtlas(dir);
		const body = mapWithData('csv', '', '1,2,3,4').replace('orientation="orthogonal"', 'orientation="hexagonal"');
		const file = writeMap(dir, body);
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'orientation-unsupported').length, 1);
	});
});

test('TSX inexistente se diagnostica sin abortar el IR', () => {
	withTemp((dir) => {
		const body = `<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" width="1" height="1" tilewidth="16" tileheight="16" infinite="0">
 <tileset firstgid="1" source="missing.tsx"/>
 <layer id="1" name="Ground" width="1" height="1"><data encoding="csv">1</data></layer>
</map>`;
		const file = writeMap(dir, body);
		const ir = parseTmxFile(file);
		assert.equal(diagnosticsWith(ir, 'tsx-not-found').length, 1);
		assert.equal(ir.tilesets.length, 1);
	});
});

// --- Integración opcional con Beginning Fields ------------------------------

test('Beginning Fields (integración local)', () => {
	if (!fs.existsSync(BEGINNING_FIELDS)) {
		++skipped;
		console.log('SKIP Beginning Fields no está en la ruta local');
		return;
	}
	const ir = parseTmxFile(BEGINNING_FIELDS);
	assert.equal(ir.map.width, 40);
	assert.equal(ir.map.height, 40);
	assert.equal(ir.map.orientation, 'orthogonal');
	assert.equal(ir.stats.tilesets, 18);
	assert.equal(ir.stats.tileLayers, 7);
	assert.equal(ir.stats.objectLayers, 2);
	assert.equal(ir.stats.objects, 169);
	assert.equal(ir.stats.uniqueGids, 256);
	assert.equal(ir.stats.errors, 0);
	assert.equal(ir.stats.warnings, 0);
	const water = ir.tilesets.find((tileset) => tileset.name === 'Tileset_Water');
	assert.ok(water);
	assert.ok(water.tiles.filter((tile) => tile.animation).length >= 50);
	const shadows = ir.tilesets.find((tileset) => tileset.name === 'Objects_Shadows');
	assert.equal(shadows.columns, 0);
	assert.equal(shadows.maxTileId, 101);
	assert.equal(shadows.lastgid, 5951);
	const shadowsLayer = ir.layers.find((layer) => layer.kind === 'group');
	assert.equal(shadowsLayer.name, 'Shadows');
	assert.equal(shadowsLayer.opacity, 0.25);
});

console.log(`\n[tiled-parser] ${failures === 0 ? 'OK' : 'FALLO'}: ${failures} fallo(s), ${skipped} omitido(s).`);
process.exit(failures === 0 ? 0 : 1);
