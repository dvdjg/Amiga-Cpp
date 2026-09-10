#!/usr/bin/env node
/**
 * Test host del generador/analizador de ondas (`tools/audio/wave.ts`).
 *
 * Verifica que las ondas generadas tienen la amplitud y frecuencia esperadas, y
 * que el analizador las recupera (cruces por cero -> frecuencia).
 */
import { analyzeWave, generateWave } from './wave.js';

let failures = 0;
function check(cond: boolean, msg: string) {
	if (!cond) {
		console.error(`  [FAIL] ${msg}`);
		failures++;
	}
}

const SR = 11025;

function testSine() {
	console.log('wave: seno 440 Hz, amplitud ±127');
	const w = generateWave('sine', 440, SR, 1.0, 127);
	const r = analyzeWave(w, SR);
	check(r.count === SR, `count debe ser ${SR}, fue ${r.count}`);
	check(r.min <= -120 && r.min >= -127, `min ~ -127, fue ${r.min}`);
	check(r.max >= 120 && r.max <= 127, `max ~ +127, fue ${r.max}`);
	check(Math.abs(r.dc) < 4, `DC ~ 0, fue ${r.dc}`);
	check(r.rms > 60 && r.rms < 110, `RMS ~ 90 (127/sqrt2), fue ${r.rms}`);
	check(Math.abs(r.dominantHz - 440) < 8, `frecuencia ~ 440 Hz, fue ${r.dominantHz}`);
}

function testSquare() {
	console.log('wave: cuadrada 220 Hz');
	const w = generateWave('square', 220, SR, 1.0, 64);
	const r = analyzeWave(w, SR);
	check(r.min === -64 && r.max === 64, `cuadrada ±64, fue ${r.min}/${r.max}`);
	check(Math.abs(r.dominantHz - 220) < 5, `frecuencia ~ 220 Hz, fue ${r.dominantHz}`);
}

function testZeroCrossingsMatchesFrequency() {
	console.log('wave: la frecuencia por cruces coincide con la pedida');
	for (const f of [110, 261, 440, 880]) {
		const w = generateWave('sine', f, SR, 1.0, 100);
		const r = analyzeWave(w, SR);
		check(Math.abs(r.dominantHz - f) < Math.max(4, f * 0.03), `${f} Hz, midio ${r.dominantHz}`);
	}
}

testSine();
testSquare();
testZeroCrossingsMatchesFrequency();

if (failures === 0) {
	console.log('OK: generador/analizador de ondas validado.');
	process.exit(0);
}
console.error(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
