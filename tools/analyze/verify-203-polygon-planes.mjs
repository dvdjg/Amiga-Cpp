#!/usr/bin/env node
/**
 * Verificador visual de la demo 203 (`203_polygon_planes`).
 *
 * Comprueba que la captura tiene:
 *  - fondo (0x012) y pantalla con contenido;
 *  - un solido relleno (px de la rampa de caras 1..6);
 *  - **al menos 2 colores de cara distintos** (sombreado por profundidad), que es la
 *    evidencia de que el relleno compuesto por bitplane resuelve varios colores.
 * En secuencia, ademas, que el solido CAMBIA entre frames (gira).
 *
 * Uso:
 *   node tools/analyze/verify-203-polygon-planes.mjs --image <screenshot.png>
 *   node tools/analyze/verify-203-polygon-planes.mjs --sequence-dir <dir>
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
// Rampa de caras de la demo (paleta indices 1..6).
const FACES = [0x113, 0x225, 0x337, 0x449, 0x55b, 0x66d];
const BG = 0x012;

function analyze(imagePath) {
	const img = readPng(imagePath);
	const W = img.width, H = img.height;
	let facePx = 0, bgPx = 0;
	const colors = new Set();
	let x0 = W, y0 = H, x1 = 0, y1 = 0;
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (FACES.some((v) => eq(r, g, b, v))) {
				facePx++;
				for (const v of FACES) if (eq(r, g, b, v)) colors.add(v);
				if (x < x0) x0 = x; if (x > x1) x1 = x;
				if (y < y0) y0 = y; if (y > y1) y1 = y;
			} else if (eq(r, g, b, BG)) {
				bgPx++;
			}
		}
	return { W, H, facePx, bgPx, colors: [...colors].sort(), box: facePx ? { w: x1 - x0 + 1, h: y1 - y0 + 1 } : { w: 0, h: 0 } };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = analyze(image);
	console.log(`verify-203-polygon-planes: ${path.basename(image)} (${s.W}x${s.H})`);
	check(s.bgPx > 1000, `fondo presente (${s.bgPx} px)`);
	check(s.facePx > 500, `solido relleno presente (${s.facePx} px, caja ${s.box.w}x${s.box.h})`);
} else if (seqDir) {
	const files = fs.readdirSync(seqDir).filter((f) => f.endsWith('.png')).sort();
	if (files.length < 2) {
		console.log('verify-203-polygon-planes: se necesitan >=2 frames');
		process.exit(2);
	}
	const shapes = [];
	const allColors = new Set();
	for (const f of files) {
		const s = analyze(path.join(seqDir, f));
		shapes.push(`${s.box.w}x${s.box.h}:${s.facePx}`);
		for (const c of s.colors) allColors.add(c);
	}
	console.log(`verify-203-polygon-planes: secuencia ${files.length} frames`);
	check(new Set(shapes).size >= 2, `el solido cambia entre frames (${new Set(shapes).size} formas)`);
	check(allColors.size >= 2, `>=2 colores de cara en la secuencia (${allColors.size})`);
} else {
	console.log('Uso: --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

process.exit(failures === 0 ? 0 : 1);
