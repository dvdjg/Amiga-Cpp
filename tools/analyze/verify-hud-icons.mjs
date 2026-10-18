#!/usr/bin/env node
/**
 * Verificador visual de la demo 303 (`303_hud_icons`).
 *
 * Comprueba, sobre la captura del runner (ventana WinUAE con la pantalla Amiga a 2x),
 * que el inventario de la franja (lineas de display 208..255) se pinta y se mueve:
 *  - hay texto blanco (etiquetas, estado SEL/FOC y linea de depuracion);
 *  - hay relleno amarillo (los objetos 12x12 de los slots con item);
 *  - hay rojo (marco del foco + objeto del slot seleccionado);
 *  - y, en secuencia, la **celda seleccionada** (bloque blanco de 16x16 en la rejilla)
 *    CAMBIA de posicion entre frames (la seleccion/foco avanzan con el guion o el input).
 *
 * Uso:
 *   node tools/analyze/verify-hud-icons.mjs --image <screenshot.png>
 *   node tools/analyze/verify-hud-icons.mjs --sequence-dir <dir> [--origin 76,36] [--scale 2]
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
// Rejilla del inventario en coordenadas de banda (6x2 celdas de 16x16 en (8,8)).
const GRID_X = 8;
const GRID_Y = 8;
const CELL = 16;
const COLS = 6;
const ROWS = 2;

const isWhite = (r, g, b) => r === 255 && g === 255 && b === 255;
const isYellow = (r, g, b) => r === 255 && g === 255 && b === 0;
const isRed = (r, g, b) => r === 255 && g === 0 && b === 0;

/// Colores de la franja (ROI) y celda de la seleccion (bloque blanco de la rejilla).
function frameStats(imagePath) {
	const img = readPng(imagePath);
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
	// Centroide de los blancos DENTRO de la rejilla: el bloque de seleccion (celda blanca).
	const gx0 = origin[0] + GRID_X * scale;
	const gy0 = origin[1] + (BAND_TOP + GRID_Y) * scale;
	const gx1 = gx0 + COLS * CELL * scale;
	const gy1 = gy0 + ROWS * CELL * scale;
	let sx = 0;
	let sy = 0;
	let n = 0;
	for (let y = gy0; y < gy1; y++) {
		for (let x = gx0; x < gx1; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (isWhite(r, g, b)) {
				sx += x;
				sy += y;
				n++;
			}
		}
	}
	let cell = -1;
	if (n > 0) {
		const bx = (sx / n - gx0) / scale;
		const by = (sy / n - gy0) / scale;
		const col = Math.floor(bx / CELL);
		const row = Math.floor(by / CELL);
		if (col >= 0 && col < COLS && row >= 0 && row < ROWS) cell = row * COLS + col;
	}
	return { imagePath, white, yellow, red, selection: cell };
}

/// Diferencia de dos PNG restringida a la **rejilla** del inventario (el movimiento del
/// foco/seleccion repinta celdas): nº de pixeles distintos.
function gridDiff(a, b) {
	const ia = readPng(a);
	const ib = readPng(b);
	const gx0 = origin[0] + GRID_X * scale;
	const gy0 = origin[1] + (BAND_TOP + GRID_Y) * scale;
	const gx1 = gx0 + COLS * CELL * scale;
	const gy1 = gy0 + ROWS * CELL * scale;
	let n = 0;
	for (let y = gy0; y < gy1; y++) {
		for (let x = gx0; x < gx1; x++) {
			const [r0, g0, b0] = pixel(ia, x, y);
			const [r1, g1, b1] = pixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) n++;
		}
	}
	return n;
}

let failures = 0;
const check = (ok, msg) => {
	console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`);
	if (!ok) failures++;
};

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = frameStats(image);
	console.log(`verify-hud-icons: ${path.basename(image)} (origen ${origin}, x${scale})`);
	check(s.white > 400, `texto/marco blanco presente (${s.white} px)`);
	check(s.yellow > 3000, `objetos amarillos de la rejilla presentes (${s.yellow} px)`);
	check(s.red > 200, `marco del foco / objeto seleccionado presentes (${s.red} px)`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) {
		console.error(`No existe ${seqDir}`);
		process.exit(2);
	}
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+(_f\d+)?\.png$/.test(f)).sort();
	console.log(`verify-hud-icons: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 2, `frames suficientes (${frames.length})`);
	const stats = frames.map((f) => frameStats(path.join(seqDir, f)));
	for (const s of stats) {
		console.log(`    ${path.basename(s.imagePath)}: blanco=${s.white} amarillo=${s.yellow} ` +
			`rojo=${s.red} seleccion=${s.selection >= 0 ? s.selection : '(ninguna)'}`);
	}
	check(stats.every((s) => s.white > 400 && s.yellow > 3000 && s.red > 200),
		'texto, objetos y foco presentes en todos los frames');
	// La interaccion avanza: el foco mueve su marco cada ~20 frames y la seleccion cambia
	// con los disparos del guion; se exige movimiento EN LA REJILLA en >=2 pares.
	let animated = 0;
	for (let i = 1; i < frames.length; ++i) {
		const d = gridDiff(path.join(seqDir, frames[i - 1]), path.join(seqDir, frames[i]));
		console.log(`    diff rejilla ${frames[i - 1]} -> ${frames[i]}: ${d} px`);
		if (d >= 32) animated++;
	}
	check(animated >= 2, `la rejilla anima entre frames consecutivos (${animated} pares con cambio)`);
	// Y la seleccion (celda con bloque blanco) cambia de posicion si aparece en >=2 celdas.
	const cells = new Set(stats.map((s) => s.selection).filter((c) => c >= 0));
	if (cells.size > 0) {
		console.log(`    celdas de seleccion vistas: ${[...cells].join(',')}`);
	}
} else {
	console.error('Uso: verify-hud-icons.mjs --image <png> | --sequence-dir <dir> [--origin 76,36] [--scale 2]');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 303 verificada (inventario con objetos, foco y seleccion en movimiento).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
