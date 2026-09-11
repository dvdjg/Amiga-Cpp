#!/usr/bin/env node
/**
 * Test host del pipeline de muestras (`tools/audio/prep-sample.ts`).
 *
 * Valida parseWav (8-bit sin signo -> con signo), resampleBox, normalizeForMixer
 * (pico objetivo / voces) y toRaw (relleno a múltiplo de 4).
 */
import { parseWav, resampleBox, normalizeForMixer, toRaw } from './prep-sample.js';

let failures = 0;
function check(cond: boolean, msg: string) {
	if (!cond) {
		console.error(`  [FAIL] ${msg}`);
		failures++;
	}
}

/** Construye un WAV PCM 8-bit mono a partir de samples sin signo. */
function buildWav8(data: number[], sampleRate: number, channels = 1): Buffer {
	const n = data.length;
	const buf = Buffer.alloc(44 + n);
	buf.write('RIFF', 0, 'ascii');
	buf.writeUInt32LE(36 + n, 4);
	buf.write('WAVE', 8, 'ascii');
	buf.write('fmt ', 12, 'ascii');
	buf.writeUInt32LE(16, 16);
	buf.writeUInt16LE(1, 20);
	buf.writeUInt16LE(channels, 22);
	buf.writeUInt32LE(sampleRate, 24);
	buf.writeUInt32LE(sampleRate * channels, 28);
	buf.writeUInt16LE(channels, 32);
	buf.writeUInt16LE(8, 34);
	buf.write('data', 36, 'ascii');
	buf.writeUInt32LE(n, 40);
	for (let i = 0; i < n; i++) buf[44 + i] = data[i] & 0xff;
	return buf;
}

function testParseWav() {
	console.log('prep: parseWav convierte 8-bit sin signo a con signo');
	const wav = parseWav(buildWav8([0x80, 0xff, 0x00], 44100));
	check(wav.samples[0] === 0, `0x80 debe ser 0, fue ${wav.samples[0]}`);
	check(wav.samples[1] === 127, `0xff debe ser 127, fue ${wav.samples[1]}`);
	check(wav.samples[2] === -128, `0x00 debe ser -128, fue ${wav.samples[2]}`);
	check(wav.sampleRate === 44100 && wav.bits === 8 && wav.channels === 1, 'cabecera mal leída');
}

function testResample() {
	console.log('prep: resampleBox decima promediando');
	// 4 muestras a 4 Hz -> 2 muestras a 2 Hz: pares (0,4)->2 y (0,4)->2.
	const out = resampleBox(Int8Array.from([0, 4, 0, 4]), 4, 2);
	check(out.length === 2, `deben quedar 2 muestras, quedaron ${out.length}`);
	check(out[0] === 2 && out[1] === 2, `promedios deben ser 2, fueron ${out[0]},${out[1]}`);
}

function testNormalize() {
	console.log('prep: normalizeForMixer lleva el pico a target/voces');
	const out = normalizeForMixer(Int8Array.from([100, -100, 50]), 2, 120);
	// gain = 120/(100*2) = 0.6 -> 60, -60, 30
	check(out[0] === 60 && out[1] === -60 && out[2] === 30, `esperado 60,-60,30, fue ${out[0]},${out[1]},${out[2]}`);
}

function testToRaw() {
	console.log('prep: toRaw rellena a múltiplo de 4');
	const raw = toRaw(Int8Array.from([1, 2, 3, 4, 5]));
	check(raw.length === 8, `5 bytes deben rellenarse a 8, fue ${raw.length}`);
	check(raw[5] === 0 && raw[6] === 0 && raw[7] === 0, 'el relleno debe ser ceros');
}

testParseWav();
testResample();
testNormalize();
testToRaw();

if (failures === 0) {
	console.log('OK: pipeline de muestras validado (parse + resample + normalize + raw).');
	process.exit(0);
}
console.error(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
