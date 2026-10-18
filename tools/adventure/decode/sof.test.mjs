// Tests del extractor `.sof` (`decode/sof.mjs`): contenedor sintético propio (formato descrito
// en el módulo, sound.cpp:1180-1200) -> índice correcto + blobs sin pérdidas + manifiesto.
// Uso: node --test tools/adventure/decode/sof.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readSofIndex, extractSof } from './sof.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const TMP = path.join(REPO, 'out', 'tmp', 'sof-test');

function buildSof(entries) {
	// entries: [{org, tags:Buffer, data:Buffer}] -> fichero .sof completo.
	const dataParts = [];
	let off = 0;
	const idx = Buffer.alloc(entries.length * 16);
	entries.forEach((e, i) => {
		idx.writeUInt32BE(e.org, i * 16);
		idx.writeUInt32BE(off, i * 16 + 4);
		idx.writeUInt32BE(e.tags.length, i * 16 + 8);
		idx.writeUInt32BE(e.data.length, i * 16 + 12);
		dataParts.push(e.tags, e.data);
		off += e.tags.length + e.data.length;
	});
	const head = Buffer.alloc(4);
	head.writeUInt32BE(idx.length, 0);
	return Buffer.concat([head, idx, ...dataParts]);
}

test('sof: índice, offsets absolutos y extracción sin pérdidas', () => {
	fs.rmSync(TMP, { recursive: true, force: true });
	fs.mkdirSync(TMP, { recursive: true });
	const flac = Buffer.concat([Buffer.from('fLaC'), Buffer.alloc(20, 0x11)]);
	const ogg = Buffer.concat([Buffer.from('OggS'), Buffer.alloc(12, 0x22)]);
	const sof = buildSof([
		{ org: 8, tags: Buffer.from([0xaa, 0xbb]), data: flac },
		{ org: 1000, tags: Buffer.alloc(0), data: ogg },
	]);
	const file = path.join(TMP, 'test.sof');
	fs.writeFileSync(file, sof);

	const { entries, idxSize } = readSofIndex(file);
	assert.equal(idxSize, 32);
	assert.equal(entries.length, 2);
	assert.equal(entries[0].abs, 4 + 32 + 0 + 2); // tras idx + tags de la entrada 0
	assert.equal(entries[1].abs, 4 + 32 + (2 + flac.length) + 0);
	assert.equal(entries[1].size, ogg.length);

	const manifest = extractSof(file, path.join(TMP, 'audio'));
	assert.equal(manifest.format, 'adv-sof/1');
	assert.deepEqual(manifest.entries.map((e) => e.ext), ['flac', 'ogg']);
	assert.ok(fs.readFileSync(path.join(TMP, 'audio', '00000.flac')).equals(flac), 'FLAC sin pérdidas');
	assert.ok(fs.readFileSync(path.join(TMP, 'audio', '00001.ogg')).equals(ogg), 'Ogg sin pérdidas');
});

test('sof: índice no múltiplo de 16 se rechaza', () => {
	fs.mkdirSync(TMP, { recursive: true });
	const bad = path.join(TMP, 'bad.sof');
	const head = Buffer.alloc(4);
	head.writeUInt32BE(10, 0);
	fs.writeFileSync(bad, Buffer.concat([head, Buffer.alloc(10)]));
	assert.throws(() => readSofIndex(bad), /múltiplo de 16/);
});
