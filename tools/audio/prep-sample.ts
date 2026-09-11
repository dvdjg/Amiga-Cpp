#!/usr/bin/env node
/**
 * Pipeline de muestras para el Audio Mixer 3.7: WAV (PCM 8/16-bit, mono/estéreo)
 * -> raw 8-bit CON SIGNO, remuestreado a la tasa del mixer (decimación con
 * promedio = antialias sencillo) y NORMALIZADO + escalado a la amplitud del
 * mixer (`±targetPeak / nº_voces`) para que N voces sumen sin desbordar; la
 * longitud se rellena a múltiplo de 4 (mínimo de `MixerGetSampleMinSize`).
 *
 * Uso:
 *   node dist/tools/audio/prep-sample.js <in.wav> <out.raw> [rate=11025] [voices=4] [peak=120]
 *
 * Las funciones puras de este módulo son host-testables (ver test-prep-sample.ts).
 */
import * as fs from 'fs';
import { pathToFileURL } from 'url';

export interface WavData {
	samples: Int8Array; // 8-bit con signo, mono (promediado si estéreo)
	sampleRate: number;
	channels: number;
	bits: number;
}

/** Lee un WAV PCM y devuelve las muestras como 8-bit con signo mono. */
export function parseWav(buf: Buffer): WavData {
	let off = 12;
	let dataOff = -1;
	let dataLen = 0;
	let channels = 1;
	let bits = 8;
	let sampleRate = 44100;
	let fmtFound = false;
	while (off + 8 <= buf.length) {
		const id = buf.toString('ascii', off, off + 4);
		const size = buf.readUInt32LE(off + 4);
		if (id === 'fmt ') {
			channels = buf.readUInt16LE(off + 10);
			sampleRate = buf.readUInt32LE(off + 12);
			bits = buf.readUInt16LE(off + 22);
			fmtFound = true;
		} else if (id === 'data') {
			dataOff = off + 8;
			dataLen = size;
			break;
		}
		off += 8 + size + (size & 1);
	}
	if (dataOff < 0 || !fmtFound) {
		throw new Error('WAV inválido (faltan chunks fmt/data)');
	}
	const bytes = bits >> 3;
	const frames = Math.floor(dataLen / (bytes * channels));
	const data = new Int8Array(frames);
	for (let i = 0; i < frames; i++) {
		let acc = 0;
		for (let c = 0; c < channels; c++) {
			const p = dataOff + (i * channels + c) * bytes;
			acc += bytes === 1 ? buf[p] - 128 : buf.readInt16LE(p) >> 8;
		}
		data[i] = (acc / channels) | 0;
	}
	return { samples: data, sampleRate, channels, bits };
}

/** Decima de `fromRate` a `toRate` promediando (box filter, evita aliasing grave). */
export function resampleBox(data: Int8Array, fromRate: number, toRate: number): Int8Array {
	if (toRate >= fromRate) {
		return data;
	}
	const factor = fromRate / toRate;
	const n = Math.floor(data.length / factor);
	const out = new Int8Array(n);
	for (let i = 0; i < n; i++) {
		const start = Math.floor(i * factor);
		const end = Math.min(data.length, Math.round((i + 1) * factor));
		let acc = 0;
		let count = 0;
		for (let j = start; j < end; j++) {
			acc += data[j];
			count++;
		}
		out[i] = count > 0 ? Math.round(acc / count) : 0;
	}
	return out;
}

/**
 * Normaliza al pico objetivo y divide por `voices`: cada voz queda a
 * `±targetPeak/voices`, de modo que `voices` voces suman sin desbordar.
 */
export function normalizeForMixer(data: Int8Array, voices: number, targetPeak = 120): Int8Array {
	let peak = 1;
	for (const v of data) {
		const a = Math.abs(v);
		if (a > peak) peak = a;
	}
	const gain = targetPeak / (peak * voices);
	const out = new Int8Array(data.length);
	for (let i = 0; i < data.length; i++) {
		out[i] = Math.round(data[i] * gain);
	}
	return out;
}

/** Serializa a raw (8-bit con signo) rellenando a múltiplo de 4 bytes. */
export function toRaw(data: Int8Array, multiple = 4): Buffer {
	const padded = (data.length + (multiple - 1)) & ~(multiple - 1);
	const buf = Buffer.alloc(padded);
	for (let i = 0; i < data.length; i++) {
		buf[i] = data[i] & 0xff;
	}
	return buf;
}

function main() {
	const args = process.argv.slice(2);
	const input = args[0];
	const output = args[1];
	if (!input || !output) {
		console.error('Uso: prep-sample.js <in.wav> <out.raw> [rate=11025] [voices=4] [peak=120]');
		process.exit(2);
	}
	const rate = parseInt(args[2] ?? '11025', 10);
	const voices = parseInt(args[3] ?? '4', 10);
	const peak = parseInt(args[4] ?? '120', 10);
	const wav = parseWav(fs.readFileSync(input));
	const resampled = resampleBox(wav.samples, wav.sampleRate, rate);
	const scaled = normalizeForMixer(resampled, voices, peak);
	const raw = toRaw(scaled);
	fs.writeFileSync(output, raw);
	console.log(`${input}: ${wav.channels}ch ${wav.bits}bit ${wav.sampleRate}Hz -> ${raw.length} bytes @ ${rate}Hz (voices=${voices}, pico ${Math.round(peak / voices)})`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	main();
}
