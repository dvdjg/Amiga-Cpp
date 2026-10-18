// Tests del generador de contenido (`emit-content.mjs`): fixture -> SIM v2 -> header C++ -> golden.
// Uso: node --test tools/adventure/codegen/codegen.test.mjs
// Para regenerar el golden: SIM_UPDATE_GOLDEN=1 node --test tools/adventure/codegen/codegen.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const TMP = path.join(REPO, 'out', 'tmp', 'codegen-fixture');
const GOLDEN = path.join(HERE, 'test', 'golden-content.hpp');

function run(cmd, args) {
	const r = spawnSync(cmd, args, { encoding: 'utf8' });
	assert.equal(r.status, 0, `fallo: ${cmd} ${args.join(' ')}\n${r.stderr}`);
	return r.stdout;
}

test('fixture: el generador emite un header C++ estable (golden)', () => {
	fs.rmSync(TMP, { recursive: true, force: true });
	fs.mkdirSync(TMP, { recursive: true });
	run(process.execPath, [path.join(HERE, '..', 'sim', 'make-fixture.mjs'), '--out', TMP]);
	run(process.execPath, [path.join(HERE, '..', 'frontends', 'scumm_v5', 'extract.mjs'), 'dump-game', path.join(TMP, 'fixture.000'), '--out', TMP, '--no-descumm']);
	run(process.execPath, [path.join(HERE, '..', 'decode', 'scumm_v5-decode.mjs'), TMP]);
	const headerFile = path.join(TMP, 'content.hpp');
	run(process.execPath, [path.join(HERE, 'emit-content.mjs'), TMP, '--out', headerFile, '--namespace', 'fixture']);

	const header = fs.readFileSync(headerFile, 'utf8');
	// Propiedades concretas del fixture (no solo el golden).
	assert.ok(header.includes('kRoom1Walkboxes[]'));
	assert.ok(header.includes('kRoom1Hotspots[]'));
	assert.ok(header.includes('{{0, 0}}, {{64, 0}}')); // primeras esquinas de la caja
	assert.ok(header.includes('{{40, 48}}, {{55, 63}}, 1u')); // hotspot inclusivo 40..55 x 48..63 del objeto 1
	assert.ok(header.includes('inline constexpr eng::adv::RoomContent<eng::s16> kRooms[]'));
	assert.ok(header.includes('find_room(eng::adv::RoomId id)'));

	if (process.env.SIM_UPDATE_GOLDEN) {
		fs.mkdirSync(path.dirname(GOLDEN), { recursive: true });
		fs.writeFileSync(GOLDEN, header);
		return;
	}
	assert.equal(header, fs.readFileSync(GOLDEN, 'utf8'), 'el header difiere del golden (regenera con SIM_UPDATE_GOLDEN=1 si es intencionado)');
});

test('fixture: rechaza un SIM que no sea v2', () => {
	const bad = path.join(TMP, 'bad.json');
	fs.mkdirSync(TMP, { recursive: true });
	fs.writeFileSync(bad, JSON.stringify({ format: 'sim/1' }));
	const r = spawnSync(process.execPath, [path.join(HERE, 'emit-content.mjs'), bad], { encoding: 'utf8' });
	assert.notEqual(r.status, 0);
	assert.ok(r.stderr.includes('SIM v2'));
});
