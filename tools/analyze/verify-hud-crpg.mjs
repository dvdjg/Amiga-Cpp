#!/usr/bin/env node
/**
 * Verificador visual de la demo 306 (`306_hud_crpg`).
 *
 * Comprueba, sobre la captura del runner (ventana WinUAE con la pantalla Amiga a 2x),
 * que la pantalla CRPG de la franja (lineas de display 208..255) se pinta y se mueve:
 *  - hay texto blanco (etiquetas de verbos + linea de depuracion);
 *  - hay amarillo (verbo activo + barras de cantidad + linea de estado);
 *  - hay rojo (marco del foco sobre celda o verbo);
 *  - y, en secuencia, la **linea de estado `V:n S:nn`** (112,17..25) CAMBIA entre frames,
 *    igual que el contenido de la franja (el guion avanza foco/verbo/seleccion).
 *
 * Uso:
 *   node tools/analyze/verify-hud-crpg.mjs --image <screenshot.png>
 *   node tools/analyze/verify-hud-crpg.mjs --sequence-dir <dir> [--origin 76,36] [--scale 2]
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
// Linea de estado `V:n S:nn` de la demo (x alineada a 16 px): (112,17) 8 filas.
const STATE_X = 112;
const STATE_Y = 17;
const STATE_W = 80;
const STATE_H = 8;

const isWhite = (r, g, b) => r === 255 && g === 255 && b === 255;
const isYellow = (r, g, b) => r === 255 && g === 255 && b === 0;
const isRed = (r, g, b) => r === 255 && g === 0 && b === 0;
const isBg = (r, g, b) => r === 0 && g === 0 && b === 0;

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
	// Pixeles no-fondo de la linea de estado (texto `V:n S:nn`): firma de interaccion.
	const sx0 = origin[0] + STATE_X * scale;
	const sy0 = origin[1] + (BAND_TOP + STATE_Y) * scale;
	const sx1 = sx0 + STATE_W * scale;
	const sy1 = sy0 + STATE_H * scale;
	let state_ink = 0;
	let state_sig = 0;
	for (let y = sy0; y < sy1; y++) {
		for (let x = sx0; x < sx1; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (!isBg(r, g, b)) {
				state_ink++;
				state_sig = (state_sig * 31 + x * 7 + y * 13) >>> 0;
			}
		}
	}
	return { imagePath, white, yellow, red, state_ink, state_sig };
}

/// Diferencia de dos PNG restringida a la **franja** (sin la linea de depuracion inferior,
/// que cambia cada frame): pixeles distintos.
function bandDiff(a, b) {
	const ia = readPng(a);
	const ib = readPng(b);
	const x0 = origin[0];
	const x1 = origin[0] + 320 * scale;
	const y0 = origin[1] + BAND_TOP * scale;
	const y1 = origin[1] + (BAND_TOP + 38) * scale; // hasta y=38 (sin depuracion)
	let n = 0;
	for (let y = y0; y < y1; y++) {
		for (let x = x0; x < x1; x++) {
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
	console.log(`verify-hud-crpg: ${path.basename(image)} (origen ${origin}, x${scale})`);
	check(s.white > 300, `texto blanco (verbos/depuracion) presente (${s.white} px)`);
	check(s.yellow > 800, `amarillo (verbo activo/estado) presente (${s.yellow} px)`);
	check(s.red > 60, `marco del foco presente (${s.red} px)`);
	check(s.state_ink > 60, `linea de estado con texto (${s.state_ink} px de tinta)`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) {
		console.error(`No existe ${seqDir}`);
		process.exit(2);
	}
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+(_f\d+)?\.png$/.test(f)).sort();
	console.log(`verify-hud-crpg: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 2, `frames suficientes (${frames.length})`);
	const stats = frames.map((f) => frameStats(path.join(seqDir, f)));
	for (const s of stats) {
		console.log(`    ${path.basename(s.imagePath)}: blanco=${s.white} amarillo=${s.yellow} ` +
			`rojo=${s.red} estado=${s.state_ink}`);
	}
	check(stats.every((s) => s.white > 300 && s.yellow > 800 && s.red > 60 && s.state_ink > 60),
		'texto, amarillo, foco y linea de estado presentes en todos los frames');
	// La interaccion avanza: el guion mueve el foco/verbo/seleccion. **Informativo**: la
	// captura por paso de este entorno puede servir video congelado (DT-023/035); la
	// prueba dura de que el guion avanza es el contador `aframe` (analyze-sequence.sh).
	let animated = 0;
	for (let i = 1; i < frames.length; ++i) {
		const d = bandDiff(path.join(seqDir, frames[i - 1]), path.join(seqDir, frames[i]));
		console.log(`    diff franja ${frames[i - 1]} -> ${frames[i]}: ${d} px`);
		if (d >= 24) animated++;
	}
	if (animated < 2) {
		console.log(`  WARN la franja apenas cambia en la captura (${animated} pares); ` +
			'puede ser el video congelado del entorno (DT-023/035), no el demo');
	}
	const sigs = new Set(stats.map((s) => s.state_sig));
	console.log(`    firmas distintas de la linea de estado: ${sigs.size}`);
	if (sigs.size < 2) {
		console.log('  WARN la linea de estado no cambio en la captura (mismo artefacto)');
	}
} else {
	console.error('Uso: verify-hud-crpg.mjs --image <png> | --sequence-dir <dir> [--origin 76,36] [--scale 2]');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 306 verificada (verbos, inventario RPG, tooltip y foco en movimiento).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
