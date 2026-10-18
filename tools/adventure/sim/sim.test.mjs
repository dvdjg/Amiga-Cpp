// Tests del contrato del SIM v1 (fixture sintético -> extractor -> SIM -> golden).
// Uso: node --test tools/adventure/sim/sim.test.mjs
// Para regenerar el golden (tras un cambio intencionado del extractor o del esquema):
//   SIM_UPDATE_GOLDEN=1 node --test tools/adventure/sim/sim.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { validateSim } from './sim.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const TMP = path.join(REPO, 'out', 'tmp', 'sim-fixture');
const GOLDEN = path.join(HERE, 'test', 'golden-sim.json');

function run(cmd, args) {
	const r = spawnSync(cmd, args, { encoding: 'utf8' });
	assert.equal(r.status, 0, `fallo: ${cmd} ${args.join(' ')}\n${r.stderr}`);
	return r.stdout;
}

test('fixture: el extractor produce un SIM v1 estable (golden)', () => {
	fs.rmSync(TMP, { recursive: true, force: true });
	fs.mkdirSync(TMP, { recursive: true });
	run(process.execPath, [path.join(HERE, 'make-fixture.mjs'), '--out', TMP]);
	const simFile = path.join(TMP, 'sim.json');
	run(process.execPath, [path.join(HERE, '..', 'frontends', 'scumm_v5', 'extract.mjs'), 'rooms-json', path.join(TMP, 'fixture.000'), '--out', simFile]);

	const sim = JSON.parse(fs.readFileSync(simFile, 'utf8'));
	assert.deepEqual(validateSim(sim), [], 'el SIM del fixture debe validar sin errores');

	if (process.env.SIM_UPDATE_GOLDEN) {
		fs.mkdirSync(path.dirname(GOLDEN), { recursive: true });
		fs.writeFileSync(GOLDEN, JSON.stringify(sim, null, 2) + '\n');
		return;
	}
	const golden = JSON.parse(fs.readFileSync(GOLDEN, 'utf8'));
	assert.deepEqual(sim, golden, 'el SIM difiere del golden (regenera con SIM_UPDATE_GOLDEN=1 si es intencionado)');
});

test('validator: detecta un SIM corrupto', () => {
	const broken = {
		format: 'sim/9',
		game: { id: '', source: { type: 'scumm-v5', data: 'no-array', enc: 'x' }, counts: {} },
		roomNames: null,
		rooms: [{ room: 0, name: 3, file: '', off: -1, blocks: [{ tag: 'TOOLONG', size: 1 }], lflfBlocks: [], scripts: [{}] }],
	};
	const errors = validateSim(broken);
	assert.ok(errors.length >= 8, `se esperaban varios errores, hay ${errors.length}`);
	assert.ok(errors.some((e) => e.includes('format')));
	assert.ok(errors.some((e) => e.includes('room duplicado') || e.includes('.room')));
});
