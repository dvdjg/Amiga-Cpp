// Tests del decodificador de imágenes SCUMM V5 (`decode/image.mjs`): codec Major/Minor y
// zig-zag básico sobre tiras sintéticas construidas con un escritor de bits LSB-first
// (mismo orden que el lector de ventana de 16 bits del original).
// Uso: node --test tools/adventure/decode/image.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeSmappedImage } from './image.mjs';

function block(tag, payload) {
	const head = Buffer.alloc(8);
	head.write(tag, 0, 4, 'latin1');
	head.writeUInt32BE(8 + payload.length, 4);
	return Buffer.concat([head, payload]);
}

// Escritor de bits LSB-first (el lector lee `bits & 1` y desplaza a la derecha).
function bitWriter() {
	const bits = [];
	return {
		push(v, n) {
			for (let i = 0; i < n; i++) bits.push((v >> i) & 1);
		},
		zeros(n) {
			for (let i = 0; i < n; i++) bits.push(0);
		},
		bytes(minLen = 0) {
			while (bits.length % 8 !== 0 || bits.length < minLen * 8) bits.push(0);
			const out = Buffer.alloc(bits.length / 8);
			for (let i = 0; i < bits.length; i += 8) {
				let b = 0;
				for (let j = 0; j < 8; j++) b |= bits[i + j] << j;
				out[i / 8] = b;
			}
			return out;
		},
	};
}

// Construye un payload de imagen con 1 tira (8 px) y la tabla SMAP correcta:
// el payload de SMAP contiene la tabla de offsets (relativos al tag) y las tiras.
function singleStripImage(strip) {
	const tableBytes = 4; // 1 offset
	const rel = 8 + tableBytes; // tag + cabecera(8) + tabla
	const smapPayload = Buffer.alloc(tableBytes + strip.length);
	smapPayload.writeUInt32LE(rel, 0);
	strip.copy(smapPayload, tableBytes);
	return block('SMAP', smapPayload);
}

test('majmin: cambios de color, valor nuevo y repetidos', () => {
	const w = bitWriter();
	// Cada píxel, tras emitir su color, consume control (también el primero).
	w.zeros(1); // pixel 1: sin cambio
	w.push(3, 2); // pixel 2: "cambio" (bits 1,1)
	w.push(6, 3); // read(3) = 6 -> diff = 6 - 4 = +2
	w.push(1, 2); // pixel 3: "nuevo color" (bits 1,0)
	w.push(20, 8); // read(shift=8)
	w.zeros(5 + 8); // resto de la fila 1 (5 px) y fila 2 entera (8 px): sin cambios
	const body = w.bytes(2); // ventana inicial de 16 bits (src[1]|src[2]<<8)
	const strip = Buffer.concat([Buffer.from([68, 10]), body]); // code=68 = MAJMIN_H8
	const img = decodeSmappedImage(singleStripImage(strip), 8, 2);
	// El control leído tras emitir un píxel se aplica al SIGUIENTE (semántica de `decodeLine`):
	// p1=10, p2=10 (el diff aún no aplica), p3=12 (diff +2), p4=20 (nuevo color), resto 20.
	assert.deepEqual([...img.indices.subarray(0, 8)], [10, 10, 12, 20, 20, 20, 20, 20]);
	assert.deepEqual([...img.indices.subarray(8, 16)], Array(8).fill(20));
	assert.deepEqual(img.codes, [68]);
});

test('majmin: repetido largo (read(8) - 1)', () => {
	const w = bitWriter();
	w.zeros(1); // pixel 1
	w.push(3, 2); // pixel 2: "cambio" (bits 1,1)
	w.push(4, 3); // read(3) = 4 -> diff 0 -> repeatMode, repeatCount = read(8) - 1
	w.push(8, 8); // read(8) = 8 -> p2..p8 repiten el color (fila completa)
	const strip = Buffer.concat([Buffer.from([68, 77]), w.bytes(2)]);
	const img = decodeSmappedImage(singleStripImage(strip), 8, 1);
	assert.deepEqual([...img.indices], [77, 77, 77, 77, 77, 77, 77, 77]);
});

test('zigzag básico: color sólido (código 28)', () => {
	const strip = Buffer.from([28, 7, 0, 0, 0, 0]); // ZIGZAG_H8: bits=0 -> nunca cambia de color
	const img = decodeSmappedImage(singleStripImage(strip), 8, 2);
	assert.deepEqual([...img.indices], Array(16).fill(7));
});

test('imagen: sin SMAP lanza error claro', () => {
	assert.throws(() => decodeSmappedImage(block('IM00', Buffer.alloc(4)), 8, 1), /SMAP/);
});
