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

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const FIXTURE = path.join(ROOT, 'tools/tiled/fixtures/ortho.tmx');
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
		assert.ok(fs.existsSync(path.join(out, 'selected', 'tiled_map.h')));
		const header = fs.readFileSync(path.join(out, 'selected', 'tiled_map.h'), 'utf8');
		assert.match(header, /TILED_MAP_W 4u/);
		assert.match(header, /TILED_MAP_PLANES 5u/);
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
} finally {
	fs.rmSync(temp, { recursive: true, force: true });
}

console.log(`\n[tiled-cook] ${failures === 0 ? 'OK' : 'FALLO'}: ${failures} fallo(s).`);
process.exit(failures === 0 ? 0 : 1);
