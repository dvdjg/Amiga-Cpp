// Tests del decodificador SCUMM V5 y del SIM v2 (fixture sintético -> extractor -> decode -> golden).
// Uso: node --test tools/adventure/decode/decode.test.mjs
// Para regenerar el golden (cambio intencionado del decodificador o del esquema):
//   SIM_UPDATE_GOLDEN=1 node --test tools/adventure/decode/decode.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { validateSim2 } from '../sim/sim.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const TMP = path.join(REPO, 'out', 'tmp', 'decode-fixture');
const GOLDEN = path.join(HERE, 'test', 'golden-sim2.json');

function run(cmd, args) {
	const r = spawnSync(cmd, args, { encoding: 'utf8' });
	assert.equal(r.status, 0, `fallo: ${cmd} ${args.join(' ')}\n${r.stderr}`);
	return r.stdout;
}

test('fixture: el decodificador produce un SIM v2 válido y estable (golden)', () => {
	fs.rmSync(TMP, { recursive: true, force: true });
	fs.mkdirSync(TMP, { recursive: true });
	run(process.execPath, [path.join(HERE, '..', 'sim', 'make-fixture.mjs'), '--out', TMP]);
	run(process.execPath, [path.join(HERE, '..', 'frontends', 'scumm_v5', 'extract.mjs'), 'dump-game', path.join(TMP, 'fixture.000'), '--out', TMP, '--no-descumm']);
	run(process.execPath, [path.join(HERE, 'scumm_v5-decode.mjs'), TMP]);

	const sim = JSON.parse(fs.readFileSync(path.join(TMP, 'decoded', 'sim.json'), 'utf8'));
	assert.deepEqual(validateSim2(sim), [], 'el SIM v2 del fixture debe validar sin errores');

	// Comprobaciones semánticas concretas del fixture (no solo el golden).
	const room = sim.rooms[0];
	assert.equal(room.width, 320);
	assert.equal(room.height, 200);
	assert.equal(room.walkboxes.length, 1);
	assert.deepEqual(room.walkboxes[0].ul, [0, 0]);
	assert.deepEqual(room.adjacency, [[0]]);
	assert.equal(room.palette.kind, 'clut');
	assert.equal(room.objects.length, 1);
	assert.equal(room.objects[0].id, 1);
	assert.equal(room.objects[0].x, 40);
	assert.equal(room.objects[0].y, 48);
	assert.equal(room.objects[0].image.imageCount, 1);
	assert.deepEqual(room.objects[0].image.hotspots, []);
	assert.equal(room.scripts.length, 2);

	if (process.env.SIM_UPDATE_GOLDEN) {
		fs.mkdirSync(path.dirname(GOLDEN), { recursive: true });
		fs.writeFileSync(GOLDEN, JSON.stringify(sim, null, 2) + '\n');
		return;
	}
	const golden = JSON.parse(fs.readFileSync(GOLDEN, 'utf8'));
	assert.deepEqual(sim, golden, 'el SIM v2 difiere del golden (regenera con SIM_UPDATE_GOLDEN=1 si es intencionado)');
});

test('decoders: rechazan payloads cortos o sin sub-bloque', async () => {
	const { decodeObject, decodeObjectImage, decodePalette, decodeRoomHeader, decodeWalkboxes, findBlock } = await import('./decoders.mjs');
	assert.equal(decodeWalkboxes(Buffer.from([0x01]), null), null);
	assert.equal(decodeObject(Buffer.from('nada')), null);
	assert.equal(decodeObjectImage(Buffer.from('nada')), null);
	assert.equal(decodeRoomHeader(Buffer.from([1, 2])), null);
	assert.equal(decodePalette(null, null), null);
	assert.equal(findBlock(Buffer.from('no-block'), 'CDHD'), null);
});
