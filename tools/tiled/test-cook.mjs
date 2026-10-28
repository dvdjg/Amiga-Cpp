#!/usr/bin/env node
// Autotest del bake y del cooker de perfiles (T2/T3).
//
// Usa el fixture `ortho.tmx` (64x48, atlas + colección + objetos + imagen) para
// comprobar que `compose-map` produce un PNG coherente y que `cook-map` cocina
// 16c/32c/EHB, emite el informe, promueve `selected/` e impone una paleta con
// `--palette`. No valida en Amiga: eso lo hace la demo 206.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';
import { parseTmxFile } from './parse-tmx.mjs';
import { composeMap } from './compose-map.mjs';
import { findFusedClusters } from './cook-animations.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const FIXTURE = path.join(ROOT, 'tools/tiled/fixtures/ortho.tmx');
const ANIM_OBJECTS = path.join(ROOT, 'tools/tiled/fixtures/animated-objects.tmx');
const FUSED_TILES = path.join(ROOT, 'tools/tiled/fixtures/fused-tiles.tmx');
const COMPOSE = path.join(ROOT, 'tools/tiled/compose-map.mjs');
const COOK = path.join(ROOT, 'tools/tiled/cook-map.mjs');

let failures = 0;
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

function run(script, args) {
	const result = spawnSync(process.execPath, [script, ...args], { cwd: ROOT, encoding: 'utf8' });
	if (result.status !== 0) throw new Error(`${path.basename(script)} falló\n${result.stdout}\n${result.stderr}`);
	return result.stdout;
}

const temp = fs.mkdtempSync(path.join(ROOT, 'out/tmp/tiled-cook-test-'));
try {
	test('compose: capas, objeto tile y aviso de repeat', () => {
		const ir = parseTmxFile(FIXTURE);
		const result = composeMap(ir);
		assert.equal(result.bounds.width, 64);
		assert.equal(result.bounds.height, 48);
		assert.equal(result.errors.length, 0);
		assert.ok(result.stats.objects >= 1);
		assert.ok(result.warnings.some((warning) => warning.includes('repeatx')));
		const png = PNG.sync.read(PNG.sync.write(result.png));
		assert.equal(png.width, 64);
		assert.equal(png.height, 48);
	});

	test('cook: 3 perfiles, propuesta y selected', () => {
		const out = path.join(temp, 'profiles');
		run(COOK, [FIXTURE, '--out', out, '--profiles', '16c,32c,ehb', '--dither', 'checker', '--select', '32c']);
		const proposal = JSON.parse(fs.readFileSync(path.join(out, 'proposal.json'), 'utf8'));
		assert.equal(proposal.profiles.length, 3);
		assert.equal(proposal.recommended, '16c'); // 2 colores únicos en el fixture
		for (const name of ['16c', '32c', 'ehb']) {
			const dir = path.join(out, 'profiles', name);
			for (const file of ['tiles.json', 'tilebank.raw.bin', 'tilebank.xlimited.bin', 'tilebank_indexed.h', 'reconstruct.png']) {
				assert.ok(fs.existsSync(path.join(dir, file)), `${name}/${file}`);
			}
		}
		const selected = JSON.parse(fs.readFileSync(path.join(out, 'selected.json'), 'utf8'));
		assert.equal(selected.profile, '32c');
		assert.equal(selected.planes, 5);
		for (const file of ['tiled_map.h', 'map.bundle', 'map_bundle.h']) assert.ok(fs.existsSync(path.join(out, 'selected', file)), `selected/${file}`);
		const header = fs.readFileSync(path.join(out, 'selected', 'tiled_map.h'), 'utf8');
		assert.match(header, /TILED_MAP_W 4u/);
		assert.match(header, /TILED_MAP_PLANES 5u/);
		const bundleHeader = fs.readFileSync(path.join(out, 'selected', 'map_bundle.h'), 'utf8');
		assert.match(bundleHeader, /TILED_MAP_PLANES 5u/);
		assert.ok(fs.statSync(path.join(out, 'selected', 'map.bundle')).size > 88);
	});

	test('compose: objetos foreground no se hornean', () => {
		const ir = parseTmxFile(FIXTURE);
		const plan = {
			render: { modes: { Objects: 'foreground' } },
			objects: { Objects: { default: 'skip', rules: [{ name: 'icon', cook: 'foreground' }] } },
		};
		const result = composeMap(ir, plan);
		assert.equal(result.errors.length, 0);
		assert.equal(result.stats.skippedRuntimeObjects, 1);
		assert.equal(result.stats.objects, 0);
	});

	test('cook: objetos foreground emiten objects.h y arte planar', () => {
		const out = path.join(temp, 'objects');
		const planFile = path.join(temp, 'objects-plan.json');
		fs.writeFileSync(planFile, JSON.stringify({
			render: { modes: { Objects: 'foreground' } },
			objects: { Objects: { default: 'skip', rules: [{ name: 'icon', cook: 'foreground', sort_y: 'base' }] } },
		}), 'utf8');
		run(COOK, [FIXTURE, '--plan', planFile, '--out', out, '--profiles', '16c', '--select', '16c']);
		for (const file of ['objects.h', 'object_sheet.bin', 'object_masks.bin', 'object_pair.bin', 'objects.json']) {
			assert.ok(fs.existsSync(path.join(out, 'selected', file)), `selected/${file}`);
		}
		const objects = JSON.parse(fs.readFileSync(path.join(out, 'selected', 'objects.json'), 'utf8'));
		assert.equal(objects.foreground, 1);
		assert.equal(objects.occluders, 0);
		assert.equal(objects.images.length, 1);
		assert.ok(objects.bytes.sheet > 0);
		assert.equal(objects.bytes.pair, objects.images[0].pair_bytes);
		const compose = JSON.parse(fs.readFileSync(path.join(out, 'compose.json'), 'utf8'));
		assert.equal(compose.stats.skippedRuntimeObjects, 1);
		const selected = JSON.parse(fs.readFileSync(path.join(out, 'selected.json'), 'utf8'));
		assert.ok(selected.objects != null);
		assert.ok(selected.objects.header.endsWith('objects.h'));
	});

	test('cook: objeto animado no alineado conserva frames y cadencia', () => {
		const out = path.join(temp, 'animated-objects');
		const planFile = path.join(temp, 'animated-plan.json');
		fs.writeFileSync(planFile, JSON.stringify({
			render: { modes: { Objects: 'foreground' } },
			objects: { Objects: { default: 'foreground' } },
		}), 'utf8');
		run(COOK, [ANIM_OBJECTS, '--plan', planFile, '--out', out, '--profiles', '16c', '--select', '16c']);
		const objects = JSON.parse(fs.readFileSync(path.join(out, 'selected', 'objects.json'), 'utf8'));
		assert.equal(objects.foreground, 1);
		assert.equal(objects.images[0].frame_count, 2);
		assert.equal(objects.images[0].animated, true);
		assert.equal(objects.objects[0].x, 11);
		assert.equal(objects.objects[0].y, -13);
		const header = fs.readFileSync(path.join(out, 'selected', 'objects.h'), 'utf8');
		assert.match(header, /kMapObjectAnimations/);
		assert.match(header, /\{128u, 2u, 0u, \{5u,8u/);
	});

	test('cook: fusiona un 2x2 en otro mapa con presupuesto de gids repetidos', () => {
		const out = path.join(temp, 'fused-tiles');
		const planFile = path.join(temp, 'fused-plan.json');
		fs.writeFileSync(planFile, JSON.stringify({
			animations: {
				mode: 'live',
				layers: ['Animated'],
				fusion: { max_cells: 8, max_duplicate_bytes: 8192 },
			},
			layers: { Animated: 'animated' },
		}), 'utf8');
		run(COOK, [FUSED_TILES, '--plan', planFile, '--out', out, '--profiles', '16c', '--select', '16c']);
		const animations = JSON.parse(fs.readFileSync(path.join(out, 'selected', 'animations.json'), 'utf8'));
		assert.equal(animations.fused.length, 1);
		assert.equal(animations.fused[0].width, 32);
		assert.equal(animations.fused[0].height, 32);
		assert.ok(animations.fused[0].bytes > 0);
		assert.ok(animations.fused[0].origin.includes('32x32'));
	});

	test('cook: paleta impuesta (custom)', () => {
		const out = path.join(temp, 'custom');
		const source = path.join(temp, 'profiles', 'profiles', '32c', 'quant', 'palette.json');
		assert.ok(fs.existsSync(source));
		run(COOK, [FIXTURE, '--out', out, '--palette', source, '--planes', '5', '--dither', 'checker', '--select', 'custom']);
		const selected = JSON.parse(fs.readFileSync(path.join(out, 'selected.json'), 'utf8'));
		assert.equal(selected.profile, 'custom');
		assert.equal(selected.planes, 5);
		const proposal = JSON.parse(fs.readFileSync(path.join(out, 'proposal.json'), 'utf8'));
		assert.equal(proposal.profiles[0].name, 'custom');
	});

	test('fuse: clusters rectangulares con la misma cadencia', () => {
		const layer = { cells: [] };
		const frames = [{ ticks: 5 }, { ticks: 5 }];
		const make = (x, y, key, timeline = frames) => ({
			layer, x, y, gid: key, key, frames: timeline, dither: false,
		});
		const g1 = { cells: [make(0, 0, 'a')] };
		const g2 = { cells: [make(1, 0, 'b')] };
		const groups = new Map([['a', g1], ['b', g2]]);
		const fused = findFusedClusters({ layers: [layer], groups });
		assert.equal(fused.length, 1);
		assert.equal(fused[0].cells.length, 2);
		assert.equal(fused[0].maxx - fused[0].minx, 1);

		const gapLayer = { cells: [] };
		const g3 = { cells: [make(0, 0, 'c')] };
		const g4 = { cells: [make(2, 0, 'd')] };
		const gap = findFusedClusters({ layers: [gapLayer], groups: new Map([['c', g3], ['d', g4]]) });
		assert.equal(gap.length, 0, 'un hueco rompe la fusión');

		const slow = [{ ticks: 5 }, { ticks: 7 }];
		const g5 = { cells: [make(0, 0, 'e')] };
		const g6 = { cells: [make(1, 0, 'f', slow)] };
		const mismatch = findFusedClusters({ layers: [layer], groups: new Map([['e', g5], ['f', g6]]) });
		assert.equal(mismatch.length, 0, 'cadencias distintas no se fusionan');

		const g7 = { cells: [make(0, 0, 'g'), make(1, 0, 'g')] };
		const g8 = { cells: [make(0, 1, 'h'), make(1, 1, 'h')] };
		const repeated = new Map([['g', g7], ['h', g8]]);
		const noBudget = findFusedClusters({ layers: [layer], groups: repeated });
		assert.equal(noBudget.length, 0, 'gids repetidos: sin presupuesto no se fusionan');
		const budget = findFusedClusters({
			layers: [layer], groups: repeated, tile: 16, planes: 6, maxDuplicateBytes: 4096,
		});
		assert.equal(budget.length, 1, 'gids repetidos: el presupuesto permite fusionar');
		assert.equal(budget[0].duplicate_bytes, 1536);
		assert.equal(budget[0].jobs_saved, 6);
	});
} finally {
	fs.rmSync(temp, { recursive: true, force: true });
}

console.log(`\n[tiled-cook] ${failures === 0 ? 'OK' : 'FALLO'}: ${failures} fallo(s).`);
process.exit(failures === 0 ? 0 : 1);
