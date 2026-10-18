// Test del mapa de audio (`analyze/audio-map.mjs`): fixture sintético mínimo con un bloque
// `SOUN` de pista de CD (DSOU→SOUN), un id sin sala y un código de habla (10) en un guion,
// comprobando la resolución a los ficheros reales (cd_music_flac y manifiesto `.sof`).
// Uso: node --test tools/adventure/analyze/audio-map.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

function makeFixture() {
	const ws = fs.mkdtempSync(path.join(os.tmpdir(), 'adv-audio-map-'));
	// Índice con DSOU: id 1 → sala 10, offset 0 del payload de la sala; id 2 → sala 0 (sin recurso).
	const payload = Buffer.alloc(2 + 2 + 8);
	payload.writeUInt16LE(2, 0);
	payload[2] = 10;
	payload[3] = 0;
	payload.writeUInt32LE(0, 4); // id 1
	payload.writeUInt32LE(0, 8); // id 2
	const block = Buffer.alloc(8 + payload.length);
	block.write('DSOU', 0, 'latin1');
	block.writeUInt32BE(8 + payload.length, 4);
	payload.copy(block, 8);
	fs.writeFileSync(path.join(ws, 'test.000'), block);
	// Datos: payload de sala (off 0) con bloque SOUN de pista de CD (size 32, payload 24).
	const soun = Buffer.alloc(32);
	soun.write('SOUN', 0, 'latin1');
	soun.writeUInt32BE(32, 4);
	soun.writeUInt32LE(24, 8); // SWAP_BYTES_32 == size-8 → pista de CD
	soun.writeUInt32BE(0x02040000, 12); // relleno de cabecera interior
	soun[0x18] = 3; // pista
	soun[0x19] = 1; // loops
	fs.writeFileSync(path.join(ws, 'test.001'), soun);
	fs.writeFileSync(path.join(ws, 'game.json'), JSON.stringify({ ws: 'adv-workspace/1', id: 'fixture', source: path.join(ws, 'test.000') }));
	fs.mkdirSync(path.join(ws, 'raw', 'index'), { recursive: true });
	fs.writeFileSync(path.join(ws, 'raw', 'index', 'rooms.json'), JSON.stringify({
		format: 'sim/1', game: { source: { enc: 0 } },
		rooms: [{ room: 10, file: 'test.001', off: 0 }],
	}));
	fs.mkdirSync(path.join(ws, 'raw', 'scripts', 'room_10'), { recursive: true });
	fs.writeFileSync(path.join(ws, 'raw', 'scripts', 'room_10', 'SCRP_1.txt'),
		'[0000] (94) print(4,[Text(sound(0x1234, 0xA) + "Hola")]);\n');
	fs.mkdirSync(path.join(ws, 'decoded', 'audio'), { recursive: true });
	fs.writeFileSync(path.join(ws, 'decoded', 'audio', 'manifest.json'), JSON.stringify({
		format: 'adv-sof/1', entries: [{ i: 42, org: 0x1234, off: 0, tags: 2, size: 100, ext: 'flac', file: '00042.flac' }],
	}));
	fs.mkdirSync(path.join(ws, 'cd_music_flac'), { recursive: true });
	fs.writeFileSync(path.join(ws, 'cd_music_flac', 'track3.flac'), '');
	return ws;
}

test('audio-map: DSOU→SOUN de CD, id sin sala y código de habla→pista .sof', () => {
	const ws = makeFixture();
	const out = execFileSync(process.execPath, [path.join(HERE, 'audio-map.mjs'), ws], { encoding: 'utf8' });
	assert.match(out, /música 2 ids/);
	const report = JSON.parse(fs.readFileSync(path.join(ws, 'reports', 'audio-map.json'), 'utf8'));
	const byId = new Map(report.music.entries.map((e) => [e.id, e]));
	assert.deepEqual(byId.get(1), {
		id: 1, room: 10, kind: 'cd', track: 3, loops: 1,
		startFrame: 0, endFrame: 0, startSeconds: 0, endSeconds: 0,
		file: 'cd_music_flac/track3.flac', fileSE: null,
	});
	assert.equal(byId.get(2).kind, 'unmapped');
	assert.equal(report.speech.total, 1);
	assert.equal(report.speech.matched, 1);
	assert.equal(report.speech.entries[0].file, 'decoded/audio/00042.flac');
	assert.equal(report.speech.entries[0].vctl, 0xA);
});
