#!/usr/bin/env node
// pack-auzx.mjs: PCM mono 8-bit con signo -> contenedor AUZX con codificador **Fibonacci Delta**
// (IFF 8SVX). Port fiel de `host-tools/pack-pcm` (mismo layout AUZX de `eng/audio/auzx.hpp` y
// mismo encoder que `eng/audio/fib_delta.hpp`), para que el pipeline de assets no necesite
// compilar C++ ni depender de Python.
//
// Uso:
//   node tools/audio/pack-auzx.mjs <in.raw> <out.auzx> [chunk_samples=1024] [rate=8000]

import fs from 'node:fs';

const KCODE = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21];

const args = process.argv.slice(2);
const inPath = args[0];
const outPath = args[1];
const chunk = Number(args[2]) || 1024;
const rate = Number(args[3]) || 8000;
if (!inPath || !outPath) {
	console.error('uso: node tools/audio/pack-auzx.mjs <in.raw> <out.auzx> [chunk=1024] [rate=8000]');
	process.exit(2);
}

const toS8 = (v) => (v << 24) >> 24;
function nearest(target) {
	let best = 0;
	let bestDiff = 1 << 20;
	for (let k = 0; k < 16; ++k) {
		const d = Math.abs(KCODE[k] - target);
		if (d < bestDiff) {
			bestDiff = d;
			best = k;
		}
	}
	return best;
}

// Fibonacci Delta con semilla encadenada entre chunks (continuidad, sin clic en la frontera).
// Devuelve `{ bytes, seed }`; `pcm` es un Int8Array.
function fibEncode(pcm, seed) {
	const out = [0, seed & 0xff];
	let x = toS8(seed & 0xff);
	let i = 0;
	while (i < pcm.length) {
		const hi = nearest(pcm[i] - x);
		x = toS8((x + KCODE[hi]) & 0xff);
		++i;
		let lo = 8; // delta 0: relleno si el numero de muestras es impar
		if (i < pcm.length) {
			lo = nearest(pcm[i] - x);
			x = toS8((x + KCODE[lo]) & 0xff);
			++i;
		}
		out.push(((hi << 4) | lo) & 0xff);
	}
	return { bytes: out, seed: x };
}

const raw = fs.readFileSync(inPath);
const pcm = new Int8Array(raw.buffer, raw.byteOffset, raw.length);
// Rellena el ultimo chunk para que TODOS descompriman exactamente `chunk` muestras (contrato de
// `PcmStream::provide`). El relleno (muestra 0) va al final y `total_samples` lo incluye.
let total = pcm.length;
if (total === 0) {
	console.error('entrada vacia');
	process.exit(1);
}
const padded = new Int8Array(Math.ceil(total / chunk) * chunk);
padded.set(pcm);
total = padded.length;

const numChunks = total / chunk;
const header = 32;
const entry = 8;
const bodies = [];
const offsets = [];
const sizes = [];
let at = header + numChunks * entry;
let seed = 0;
for (let c = 0; c < numChunks; ++c) {
	const sub = padded.subarray(c * chunk, (c + 1) * chunk);
	const enc = fibEncode(sub, seed);
	seed = enc.seed;
	bodies.push(Buffer.from(enc.bytes));
	offsets.push(at);
	sizes.push(enc.bytes.length);
	at += enc.bytes.length;
}

const file = Buffer.alloc(at);
file.write('AUZX', 0, 'ascii');
const w8 = (o, v) => (file[o] = v & 0xff);
const wr16 = (o, v) => file.writeUInt16LE(v & 0xffff, o);
const wr32 = (o, v) => file.writeUInt32LE(v >>> 0, o);
w8(4, 1); // version
w8(5, 4); // compression = Codec::FibDelta
wr16(6, rate);
wr16(8, 1); // mono
w8(10, 8); // bits
w8(11, 0);
wr32(12, total);
wr16(16, chunk);
wr16(18, numChunks);
wr32(20, header); // table_offset
wr32(24, header + numChunks * entry); // data_offset
wr32(28, 0); // checksum
for (let c = 0; c < numChunks; ++c) {
	const e = header + c * entry;
	wr32(e, offsets[c]);
	wr32(e + 4, sizes[c]);
	bodies[c].copy(file, offsets[c]);
}

fs.writeFileSync(outPath, file);
console.log(`pack-auzx: ${inPath} -> ${outPath} | fib rate=${rate} chunk=${chunk} | ` +
	`${total} muestras, ${numChunks} chunks, ${file.length} bytes`);
