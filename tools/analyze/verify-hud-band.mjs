#!/usr/bin/env node
/**
 * Verificador visual de la demo 302 (`302_hud_band`).
 *
 * Comprueba, sobre la captura del runner (ventana WinUAE con la pantalla Amiga a 2x),
 * que la franja del HUD (lineas de display 208..255) se pinta de verdad:
 *  - hay texto blanco (etiquetas, marco y linea de depuracion);
 *  - hay relleno amarillo (digitos del contador, energia, pips);
 *  - y, en secuencia, que la FRANJA cambia entre frames consecutivos (contador/energia),
 *    con el cambio concentrado dentro de la banda (no es solo el juego de arriba).
 *
 * Uso:
 *   node tools/analyze/verify-hud-band.mjs --image <screenshot.png>
 *   node tools/analyze/verify-hud-band.mjs --sequence-dir <dir> [--origin 76,36] [--scale 2]
 * Salida: informe por stdout; exit 0 = OK, 1 = fallo, 2 = uso.
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const arg = (name, def = '') => {
	const i = process.argv.indexOf(name);
	return i >= 0 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : def;
};

const origin = arg('--origin', '76,36').split(',').map(Number);
const scale = Number(arg('--scale', '2'));
const BAND_TOP = 208;
const BAND_H = 48;

const isWhite = (r, g, b) => r === 255 && g === 255 && b === 255;
const isYellow = (r, g, b) => r === 255 && g === 255 && b === 0;
const isRed = (r, g, b) => r === 255 && g === 0 && b === 0;

/// Cuenta colores del HUD dentro de la franja (ROI de la banda) y devuelve su caja.
function bandStats(imagePath) {
	const img = readPng(imagePath);
	const W = img.width;
	const x0 = origin[0];
	const x1 = origin[0] + 320 * scale;
	const y0 = origin[1] + BAND_TOP * scale;
	const y1 = origin[1] + (BAND_TOP + BAND_H) * scale;
	let white = 0;
	let yellow = 0;
	let red = 0;
	for (let y = y0; y < y1; y++) {
		for (let x = x0; x < x1; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (isWhite(r, g, b)) white++;
			else if (isYellow(r, g, b)) yellow++;
			else if (isRed(r, g, b)) red++;
		}
	}
	return { imagePath, W, white, yellow, red };
}

/// Diferencia de dos PNG restringida a la franja; devuelve nº de pixeles y su caja.
function bandDiff(a, b) {
	const ia = readPng(a);
	const ib = readPng(b);
	const x0 = origin[0];
	const x1 = origin[0] + 320 * scale;
	const y0 = origin[1] + BAND_TOP * scale;
	const y1 = origin[1] + (BAND_TOP + BAND_H) * scale;
	let n = 0;
	let minx = x1;
	let maxx = -1;
	let miny = y1;
	let maxy = -1;
	for (let y = y0; y < y1; y++) {
		for (let x = x0; x < x1; x++) {
			const [r0, g0, b0] = pixel(ia, x, y);
			const [r1, g1, b1] = pixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) {
				n++;
				if (x < minx) minx = x;
				if (x > maxx) maxx = x;
				if (y < miny) miny = y;
				if (y > maxy) maxy = y;
			}
		}
	}
	return { n, w: maxx - minx + 1, h: maxy - miny + 1 };
}

let failures = 0;
const check = (ok, msg) => {
	console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`);
	if (!ok) failures++;
};

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = bandStats(image);
	console.log(`verify-hud-band: ${path.basename(image)} (${s.W}px, origen ${origin}, x${scale})`);
	check(s.white > 400, `texto/marco blanco presente (${s.white} px)`);
	check(s.yellow > 300, `relleno amarillo presente (${s.yellow} px)`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) {
		console.error(`No existe ${seqDir}`);
		process.exit(2);
	}
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+(_f\d+)?\.png$/.test(f)).sort();
	console.log(`verify-hud-band: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 2, `frames suficientes (${frames.length})`);
	const stats = frames.map((f) => bandStats(path.join(seqDir, f)));
	for (const s of stats) {
		console.log(`    ${path.basename(s.imagePath)}: blanco=${s.white} amarillo=${s.yellow} rojo=${s.red}`);
	}
	check(stats.every((s) => s.white > 400 && s.yellow > 300),
		'texto y amarillo presentes en todos los frames');
	let animated = 0;
	for (let i = 1; i < frames.length; ++i) {
		const d = bandDiff(path.join(seqDir, frames[i - 1]), path.join(seqDir, frames[i]));
		console.log(`    diff franja ${frames[i - 1]} -> ${frames[i]}: ${d.n} px, caja ${d.w}x${d.h}`);
		// En captura frame a frame el delta tipico es la barra de energia (2 px x 6 filas
		// x 2 de escala = 48 px); los cambios de digito o de color suman mas.
		if (d.n >= 32) animated++;
	}
	check(animated >= 2, `la franja anima entre frames consecutivos (${animated} pares con cambio)`);
} else {
	console.error('Uso: verify-hud-band.mjs --image <png> | --sequence-dir <dir> [--origin 76,36] [--scale 2]');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 302 verificada (franja del HUD con contenido y animacion).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
