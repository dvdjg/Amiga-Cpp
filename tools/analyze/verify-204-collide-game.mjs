#!/usr/bin/env node
/**
 * Verificador visual de la demo 204 (`204_collide_game`).
 *
 * Comprueba que la captura tiene jugador (0x0cf) y obstáculo (0xf33) y, en secuencia,
 * que el jugador se mueve y que la **barra de flash** (0xff0) aparece en algún frame:
 * evidencia de que `blitter_collide` detecta el solape en el bucle de juego.
 *
 * Uso:
 *   node tools/analyze/verify-204-collide-game.mjs --image <screenshot.png>
 *   node tools/analyze/verify-204-collide-game.mjs --sequence-dir <dir>
 * Salida: informe por stdout; exit 0 = OK, 1 = fallo, 2 = uso.
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const arg = (name, def = '') => {
	const i = process.argv.indexOf(name);
	return i >= 0 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : def;
};

const c444 = (v) => [((v >> 8) & 15) * 0x11, ((v >> 4) & 15) * 0x11, (v & 15) * 0x11];
const eq = (r, g, b, v) => { const [R, G, B] = c444(v); return r === R && g === G && b === B; };
const PLAYER = 0x0cf, HAZARD = 0xf33, FLASH = 0xff0;

function analyze(imagePath) {
	const img = readPng(imagePath);
	let player = 0, hazard = 0, flash = 0, px0 = img.width, px1 = 0;
	for (let y = 0; y < img.height; y++)
		for (let x = 0; x < img.width; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (eq(r, g, b, PLAYER)) { player++; if (x < px0) px0 = x; if (x > px1) px1 = x; }
			else if (eq(r, g, b, HAZARD)) hazard++;
			else if (eq(r, g, b, FLASH)) flash++;
		}
	return { player, hazard, flash, px0: player ? px0 : -1, px1 };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = analyze(image);
	console.log(`verify-204-collide-game: ${path.basename(image)}`);
	check(s.player > 100, `jugador presente (${s.player} px)`);
	check(s.hazard > 100, `obstaculo presente (${s.hazard} px)`);
} else if (seqDir) {
	const files = fs.readdirSync(seqDir).filter((f) => f.endsWith('.png')).sort();
	if (files.length < 2) {
		console.log('verify-204-collide-game: se necesitan >=2 frames');
		process.exit(2);
	}
	let flashFrames = 0;
	const xs = new Set();
	for (const f of files) {
		const s = analyze(path.join(seqDir, f));
		if (s.flash > 0) flashFrames++;
		if (s.px0 >= 0) xs.add(s.px0);
	}
	console.log(`verify-204-collide-game: secuencia ${files.length} frames`);
	check(xs.size >= 2, `el jugador se mueve (${xs.size} posiciones)`);
	check(flashFrames > 0, `colision detectada (flash en ${flashFrames} frames)`);
} else {
	console.log('Uso: --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

process.exit(failures === 0 ? 0 : 1);
