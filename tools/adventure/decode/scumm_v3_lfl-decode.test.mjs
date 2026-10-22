// Pruebas deterministas de los códecs 256 colores usados por SCUMM V3 FM-Towns.
import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeTownsBitmap, decodeTownsStrip } from './scumm_v3_lfl-decode.mjs';

function bitmap(strips) {
	const tableSize = strips.length * 4;
	const payloadSize = strips.reduce((n, s) => n + s.length, 0);
	const out = Buffer.alloc(4 + tableSize + payloadSize);
	out.writeUInt32LE(out.length - 4, 0);
	let offset = 4 + tableSize;
	strips.forEach((s, i) => {
		out.writeUInt32LE(offset, 4 + i * 4);
		Buffer.from(s).copy(out, offset);
		offset += s.length;
	});
	return out;
}

test('TOWNS_2 decodifica runs de color en orden column-major', () => {
	const { indices, code } = decodeTownsStrip(Buffer.from([2, 0xff, 7]), 8, 32, 0);
	assert.equal(code, 2);
	assert.equal(indices.length, 256);
	assert.deepEqual([...new Set(indices)], [7]);
});

test('TOWNS_4 aplica la paleta local y runs largos', () => {
	const { indices } = decodeTownsStrip(Buffer.from([4, 2, 5, 9, 0, 1, 127, 7]), 8, 16, 0);
	assert.equal(indices[0], 5);
	assert.equal(indices[8], 9);
	assert.equal(indices[16], 7);
	assert.equal(indices[127], 7);
});

test('TOWNS_7 decodifica el color inicial y completa el bloque', () => {
	const { indices, code } = decodeTownsStrip(Buffer.from([7, 23, 0, 0, 0, 0, 0]), 8, 4, 0);
	assert.equal(code, 7);
	assert.equal(indices[0], 23);
	assert.equal(indices.length, 32);
});

test('BM usa la tabla de offsets y conserva las columnas de cada tira', () => {
	const raw = new Array(32).fill(3).map((_, i) => i);
	const decoded = decodeTownsBitmap(bitmap([[1, ...raw], [1, ...raw.map((x) => x + 32)]]), 16, 4);
	assert.equal(decoded.strips, 2);
	assert.deepEqual(decoded.codes, [1]);
	assert.equal(decoded.indices[0], 0);
	assert.equal(decoded.indices[8], 32);
	assert.equal(decoded.indices[15], 60);
	assert.equal(decoded.indices[2], 8);
	assert.equal(decoded.indices[16], 1);
});

test('rechaza offsets BM fuera del bloque y códigos de tira desconocidos', () => {
	const bad = bitmap([[1, ...new Array(32).fill(0)]]);
	bad.writeUInt32LE(0xffff, 4);
	assert.throws(() => decodeTownsBitmap(bad, 8, 4), /offset inválido/);
	assert.throws(() => decodeTownsStrip(Buffer.from([99]), 8, 1, 0), /no soportado/);
});
