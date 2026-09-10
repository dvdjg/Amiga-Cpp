#!/usr/bin/env node
/**
 * Genera una onda de prueba y la analiza, para depurar el pipeline de sonido.
 *
 * Uso:
 *   node dist/tools/audio/gen-wave.js sine 440 11025 1.0 salida
 *
 * Escribe:
 *   - <salida>.raw  (PCM 8-bit con signo, para incrustar en el Amiga)
 *   - <salida>.wav  (para escucharla/inspeccionarla en una herramienta externa)
 * Y por stdout imprime el informe del análisis (amplitud, DC, RMS, frecuencia).
 */
import * as fs from 'fs';
import { analyzeWave, generateWave, toWav, WaveKind } from './wave.js';

const args = process.argv.slice(2);
const kind = (args[0] ?? 'sine') as WaveKind;
const freq = parseFloat(args[1] ?? '440');
const rate = parseInt(args[2] ?? '11025', 10);
const seconds = parseFloat(args[3] ?? '1.0');
const outBase = args[4] ?? 'out';
const amp = args[5] !== undefined ? parseInt(args[5], 10) : 127;

if (!['sine', 'square', 'triangle', 'saw'].includes(kind) || isNaN(freq) || isNaN(rate)) {
	console.error('Uso: gen-wave.js <sine|square|triangle|saw> <Hz> <sampleRate> <segundos> [salida] [amplitud]');
	process.exit(2);
}

const data = generateWave(kind, freq, rate, seconds, amp);
const report = analyzeWave(data, rate);

fs.writeFileSync(`${outBase}.raw`, data);
fs.writeFileSync(`${outBase}.wav`, toWav(data, rate));

console.log(
	`${kind} ${freq}Hz @${rate}Hz x${seconds}s -> ${outBase}.raw/.wav ` +
	`(count=${report.count} min=${report.min} max=${report.max} dc=${report.dc.toFixed(2)} ` +
	`rms=${report.rms.toFixed(1)} dominant=${report.dominantHz.toFixed(1)}Hz)`,
);
