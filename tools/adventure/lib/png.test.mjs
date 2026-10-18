// Tests del codificador PNG indexado (`lib/png.mjs`): estructura, CRCs, contenido y determinismo.
// Uso: node --test tools/adventure/lib/png.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import zlib from 'node:zlib';
import { encodeIndexedPng, paletteTo8Bit } from './png.mjs';

function parseChunks(buf) {
	assert.ok(buf.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])), 'firma PNG');
	const chunks = [];
	let off = 8;
	while (off + 12 <= buf.length) {
		const len = buf.readUInt32BE(off);
		const type = buf.toString('latin1', off + 4, off + 8);
		const data = buf.subarray(off + 8, off + 8 + len);
		const crc = buf.readUInt32BE(off + 8 + len);
		assert.equal(crc, zlib.crc32(Buffer.concat([Buffer.from(type, 'latin1'), data])) >>> 0, `CRC de ${type}`);
		chunks.push({ type, data });
		off += 12 + len;
	}
	assert.equal(off, buf.length, 'sin bytes sobrantes');
	return chunks;
}

test('png: indexado 8bpp válido, con tRNS y determinista', () => {
	const pal = [[255, 0, 0], [0, 255, 0], [0, 0, 255]];
	const idx = Uint8Array.from([0, 1, 1, 2]);
	const buf = encodeIndexedPng(2, 2, idx, pal, { transparentIndex: 0 });
	const chunks = parseChunks(buf);
	assert.deepEqual(chunks.map((c) => c.type), ['IHDR', 'PLTE', 'tRNS', 'IDAT', 'IEND']);
	const ihdr = chunks[0].data;
	assert.equal(ihdr.readUInt32BE(0), 2);
	assert.equal(ihdr.readUInt32BE(4), 2);
	assert.equal(ihdr[8], 8);
	assert.equal(ihdr[9], 3);
	assert.deepEqual([...chunks[1].data], [255, 0, 0, 0, 255, 0, 0, 0, 255]);
	assert.deepEqual([...chunks[2].data], [0]); // tRNS: el índice 0 transparente (el resto de la paleta es opaca)
	const raw = zlib.inflateSync(chunks[3].data);
	assert.deepEqual([...raw], [0, 0, 1, 0, 1, 2]); // filtro 0 por fila + índices
	assert.ok(encodeIndexedPng(2, 2, idx, pal, { transparentIndex: 0 }).equals(buf), 'determinista');
});

test('png: dimensions inválidas no cuadran con los índices', () => {
	assert.throws(() => encodeIndexedPng(2, 2, Uint8Array.from([1, 2, 3]), [[0, 0, 0]]));
});

test('png: paletteTo8Bit escala paletas de 6 bits (VGA)', () => {
	assert.deepEqual(paletteTo8Bit([[63, 32, 0], [1, 2, 3]]), [[252, 128, 0], [4, 8, 12]]);
	assert.deepEqual(paletteTo8Bit([[200, 100, 50]]), [[200, 100, 50]]); // ya en 8 bits: sin cambios
});
